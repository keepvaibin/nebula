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

android {
    namespace = "dev.nebula.galaxy"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "dev.nebula.galaxy"
        minSdk = 31
        targetSdk = 36
        versionCode = 10
        versionName = "1.0.0-test10"
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    signingConfigs {
        if (signing != null) {
            create("release") {
                storeFile = file(signing.getProperty("storeFile"))
                storePassword = signing.getProperty("storePassword")
                keyAlias = signing.getProperty("keyAlias")
                keyPassword = signing.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
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
