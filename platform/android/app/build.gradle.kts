import java.util.Properties
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Built elsewhere and not kept here (build_apk.ps1 passes them):
//   nebula.sdlDir   SDL's source, whose Java half (org.libsdl.app) must match the libSDL3.so it built
//   nebula.jniDir   the native libraries by ABI (libmain, libSDL3, libsmg_game, libnebula_import)
//   nebula.assetsDir  assets made by the build (licenses/, the texts the About screen shows)
//   nebula.signing  a properties file naming the release key (storeFile, storePassword, keyAlias, keyPassword)
val sdlDir = providers.gradleProperty("nebula.sdlDir").orNull ?: error("pass -Pnebula.sdlDir=<SDL3 source>")
val jniDir = providers.gradleProperty("nebula.jniDir").orNull ?: rootProject.file("jniLibs").path
val assetsDir = providers.gradleProperty("nebula.assetsDir").orNull ?: rootProject.file("build/nebula-assets").path
val signing = providers.gradleProperty("nebula.signing").orNull?.let { path ->
    Properties().apply { file(path).inputStream().use { load(it) } }
}

/**
 * The launcher's texts, made at every build from the game's own table (localization/NebulaText.tsv): each row whose
 * id is "L.Android.<Name>" becomes R.string.<Name> in one values folder per language column, so the table stays the
 * only copy. A cell's {0}, {1} ... become Android's %1$s, %2$s ...; its \n and \\ (the table's escapes) a line break
 * and a backslash. Each folder also gets text_language (the column's name, for the game's own code that speaks the
 * same languages) and res/xml/locales_config.xml lists the languages for Android's per-app language setting.
 */
abstract class LauncherTexts : DefaultTask() {
    @get:InputFile
    abstract val table: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    private val folders = linkedMapOf(
        "UsEnglish" to listOf("values"),
        "EuEnglish" to listOf("values-en-rGB", "values-en-rIE", "values-en-rAU", "values-en-rNZ"),
        "UsFrench" to listOf("values-fr-rCA"),
        "EuFrench" to listOf("values-fr"),
        "UsSpanish" to listOf("values-b+es+419", "values-es-rUS", "values-es-rMX"),
        "EuSpanish" to listOf("values-es"),
        "EuGerman" to listOf("values-de"),
        "EuItalian" to listOf("values-it"),
        "EuDutch" to listOf("values-nl"),
        "UsPortuguese" to listOf("values-pt"),
        "JpJapanese" to listOf("values-ja"),
        "KrKorean" to listOf("values-ko"),
        "CnSimpChinese" to listOf("values-zh", "values-b+zh+Hans"),
        "TwTradChinese" to listOf("values-zh-rTW", "values-zh-rHK", "values-zh-rMO", "values-b+zh+Hant"),
    )
    private val locales = listOf("en-US", "en-GB", "fr", "fr-CA", "es", "es-419", "de", "it", "nl", "pt-BR", "ja", "ko", "zh-Hans", "zh-Hant")

    private val PREFIX = "L.Android."

    private fun unescape(cell: String): String =
        Regex("""\\([n\\])""").replace(cell) { if (it.groupValues[1] == "n") "\n" else "\\" }

    private fun escape(text: String, positional: Boolean): String {
        val out = StringBuilder()
        for (c in text) {
            when (c) {
                '&' -> out.append("&amp;")
                '<' -> out.append("&lt;")
                '>' -> out.append("&gt;")
                '\'' -> out.append("\\'")
                '"' -> out.append("\\\"")
                '\\' -> out.append("\\\\")
                '\n' -> out.append("\\n")
                '%' -> out.append(if (positional) "%%" else "%")
                else -> out.append(c)
            }
        }
        if (out.startsWith("@") || out.startsWith("?")) out.insert(0, '\\')
        return out.toString()
    }

    @TaskAction
    fun run() {
        val lines = table.get().asFile.readText(Charsets.UTF_8).removePrefix("﻿").split("\r\n", "\n").filter { it.isNotEmpty() }
        val columns = lines.first().split('\t')
        val rows = lines.drop(1).map { it.split('\t') }.filter { it[0].startsWith(PREFIX) }
        if (rows.isEmpty()) throw GradleException("${table.get().asFile}: no $PREFIX rows")
        val placeholder = Regex("""\{(\d)\}""")
        val out = outputDir.get().asFile
        out.deleteRecursively()
        for ((column, qualifiers) in folders) {
            val index = columns.indexOf(column)
            if (index < 0) throw GradleException("NebulaText.tsv has no column $column")
            val xml = StringBuilder("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n")
            xml.append("    <string name=\"text_language\" translatable=\"false\">$column</string>\n")
            for (row in rows) {
                val name = row[0].removePrefix(PREFIX)
                val english = unescape(row.getOrElse(1) { "" })
                val text = unescape(row.getOrElse(index) { "" })
                if (english.isEmpty() || text.isEmpty()) throw GradleException("${row[0]}: no $column text (every column is filled)")
                val wanted = placeholder.findAll(english).map { it.groupValues[1] }.toSet()
                val used = placeholder.findAll(text).map { it.groupValues[1] }.toSet()
                if (used != wanted) throw GradleException("${row[0]} $column: arguments $used, UsEnglish has $wanted")
                val body = placeholder.replace(escape(text, wanted.isNotEmpty())) { m -> "%${m.groupValues[1].toInt() + 1}\$s" }
                val attributes = if (wanted.isEmpty() && '%' in text) " formatted=\"false\"" else ""
                xml.append("    <string name=\"$name\"$attributes>$body</string>\n")
            }
            xml.append("</resources>\n")
            for (qualifier in qualifiers) {
                File(out, "$qualifier/launcher_texts.xml").apply { parentFile.mkdirs() }.writeText(xml.toString(), Charsets.UTF_8)
            }
        }
        File(out, "xml/locales_config.xml").apply { parentFile.mkdirs() }.writeText(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<locale-config xmlns:android=\"http://schemas.android.com/apk/res/android\">\n" +
                locales.joinToString("") { "    <locale android:name=\"$it\" />\n" } + "</locale-config>\n",
            Charsets.UTF_8,
        )
    }
}

val launcherTexts = tasks.register<LauncherTexts>("launcherTexts") {
    table.set(rootProject.file("../../localization/NebulaText.tsv"))
    outputDir.set(layout.buildDirectory.dir("generated/launcherTexts"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.res?.addGeneratedSourceDirectory(launcherTexts, LauncherTexts::outputDir)
    }
}

android {
    namespace = "dev.nebula.galaxy"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "dev.nebula.galaxy"
        minSdk = 31
        targetSdk = 36
        versionCode = 25
        versionName = "2.0.15-test.12"
        manifestPlaceholders["gameExported"] = "false"
        manifestPlaceholders["appLabel"] = "@string/app_name"
        ndk {
            abiFilters += ((project.findProperty("nebulaAbis") as String?) ?: "arm64-v8a").split(",")
        }
    }

    signingConfigs {
        if (signing != null) {
            create("release") {
                storeFile = file(signing.getProperty("storeFile"))
                storePassword = signing.getProperty("storePassword")
                keyAlias = signing.getProperty("keyAlias")
                keyPassword = signing.getProperty("keyPassword")
                // All of them: an installer that reads only the oldest still finds its certificates.
                enableV1Signing = true
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
            isDebuggable = project.hasProperty("nebula.debuggable")
            // A debuggable test build's game screen starts from the shell (the test harness's runs, GameActivity's
            // nebula.harness); a shipped build's only from the launcher.
            manifestPlaceholders["gameExported"] = if (project.hasProperty("nebula.debuggable")) "true" else "false"
            // A test build is its own app beside the installed game: its own id (its own data folder, file provider and
            // saves) and name, so installing it can never replace the game or reach its saves.
            if (project.hasProperty("nebula.debuggable")) {
                applicationIdSuffix = ".test"
                manifestPlaceholders["appLabel"] = "Nebula test"
            }
        }
    }

    sourceSets["main"].java.srcDir("$sdlDir/android-project/app/src/main/java")
    sourceSets["main"].jniLibs.srcDir(jniDir)
    sourceSets["main"].assets.srcDir(assetsDir)

    packaging {
        jniLibs {
            // Compressed in the download and unpacked at install: the host loads the game module by its path.
            useLegacyPackaging = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    lint {
        // SDL's Java half is checked upstream.
        checkReleaseBuilds = false
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.16.0")
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.9.4")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.9.4")
    implementation("androidx.compose.ui:ui:1.11.3")
    implementation("androidx.compose.foundation:foundation:1.11.3")
    implementation("androidx.compose.animation:animation:1.11.3")
    implementation("androidx.compose.material3:material3:1.4.0")
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}
