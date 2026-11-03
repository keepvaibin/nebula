// SPDX-License-Identifier: GPL-3.0-only
// runtime_settings.cpp in a test build: a test sets settings from its environment.
void apply_env(RuntimeSettings& settings, const char* env, const char* key) {
    char value[128]{};
    std::size_t length = 0;
    if (::galaxy::env_read(&length, value, sizeof(value), env) == 0 && length > 1u) {
        apply_kv(settings, key, value);
    }
}

void apply_env_overrides(RuntimeSettings& settings) {
    apply_env(settings, nebula_switch::input_mode_name(), "input_mode");
    apply_env(settings, "GALAXY_AUDIO_MASTER_GAIN", "audio_master_gain");
    apply_env(settings, "GALAXY_AUDIO_MUSIC_GAIN", "audio_music_gain");
    apply_env(settings, "GALAXY_AUDIO_EFFECTS_GAIN", "audio_sfx_gain");
    apply_env(settings, "GALAXY_AUDIO_SFX_GAIN", "audio_sfx_gain");
    apply_env(settings, "GALAXY_AUDIO_VOICE_GAIN", "audio_voice_gain");
    apply_env(
        settings, "GALAXY_AUDIO_AMBIENCE_GAIN", "audio_ambience_gain");
    apply_env(
        settings, "GALAXY_AUDIO_CUTSCENE_GAIN", "audio_cutscene_gain");
    apply_env(
        settings, "GALAXY_AUDIO_WIIMOTE_SPEAKER_GAIN", "audio_wiimote_speaker_gain");
    apply_env(settings, "GALAXY_TEXT_SPEED", "text_speed");
    apply_env(settings, "GALAXY_LANGUAGE", "language");
}
