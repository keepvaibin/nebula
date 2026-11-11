"""A run's GALAXY_<KEY>= settings into its settings folder (argv: SETTINGS_FOLDER [NAME=value ...]).

The game reads its settings only from config.ini and runtime_settings.ini. A test still names a setting the way it
always has, GALAXY_MAX_FPS=30 among run.sh's assignments (or exported); this writes key=value into the file that holds
it. What a run sets lasts for that run only, as an environment value did: the line each key had before is kept in
.run_settings.json beside the files and put back first by the next run, so a later run in the same folder starts from
the folder's own settings. Assignments are taken in order after the exported environment, the last one winning.
"""
import json
import os
import pathlib
import sys

# Environment name -> (file, key). Two names for one key: the later in this table wins, as the game read them.
RENDER = [
    ("GALAXY_VSYNC_INTERVAL", "vsync_interval"), ("GALAXY_MSAA_SAMPLES", "msaa_samples"),
    ("GALAXY_TAA_ENABLED", "taa_enabled"), ("GALAXY_MAX_FPS", "max_fps"),
    ("GALAXY_FRAME_RATE_DECOUPLE", "frame_rate_decouple"), ("GALAXY_TEXTURE_LOD_BIAS", "texture_lod_bias"),
    ("GALAXY_ANISOTROPIC_FILTERING", "anisotropic_filtering"), ("GALAXY_ENHANCED_MIPMAPS", "enhanced_mipmaps"),
    ("GALAXY_PER_PIXEL_LIGHTING", "per_pixel_lighting"), ("GALAXY_CURVED_SURFACES", "curved_surfaces"),
    ("GALAXY_ENHANCED_LIGHTING", "enhanced_lighting"), ("GALAXY_AMBIENT_OCCLUSION", "ambient_occlusion"),
    ("GALAXY_BLOOM", "bloom"), ("GALAXY_WIDE_SCENE", "wide_scene"), ("GALAXY_AO_SSAO", "ambient_occlusion_ssao"),
    ("GALAXY_POST_PROCESS", "post_process"), ("GALAXY_HUD_OUTPUT_RESOLUTION", "hud_output_resolution"),
    ("GALAXY_AA", "aa"), ("GALAXY_SMAA_QUALITY", "smaa_quality"), ("GALAXY_UPSCALER", "upscaler"),
    ("GALAXY_FRAME_GENERATION", "frame_generation"), ("GALAXY_TEMPORAL_QUALITY", "temporal_quality"),
    ("GALAXY_SPATIAL_QUALITY", "spatial_quality"), ("GALAXY_SHARPNESS", "sharpness"), ("GALAXY_HDR", "hdr"),
    ("GALAXY_PAPER_WHITE", "paper_white"), ("GALAXY_RAY_TRACING", "ray_tracing_enabled"),
    ("GALAXY_RAY_TRACING_ENABLED", "ray_tracing_enabled"), ("GALAXY_RT_SHADOW_QUALITY", "rt_shadow_quality"),
    ("GALAXY_RT_QUALITY", "rt_quality"), ("GALAXY_RT_LENS_BLOOM", "rt_lens_bloom"),
    ("GALAXY_RT_SUN_RAYS", "rt_sun_rays"), ("GALAXY_RT_WATER", "rt_water"), ("GALAXY_RT_AO_ENABLED", "rt_ao_enabled"),
    ("GALAXY_RT_REFLECTIONS", "rt_reflections"), ("GALAXY_TEXTURE_PACKS", "texture_packs"),
    ("GALAXY_TEXTURE_PACK_PRELOAD", "texture_pack_preload"), ("GALAXY_TEXTURE_DUMP", "texture_dump"),
    ("GALAXY_TEXTURE_PACK_BUDGET_MB", "texture_pack_budget_mb"), ("GALAXY_WINDOW_WIDTH", "window_width"),
    ("GALAXY_WINDOW_HEIGHT", "window_height"), ("GALAXY_DISPLAY_MODE", "display_mode"),
    ("GALAXY_MONITOR", "monitor"), ("GALAXY_GPU", "gpu"), ("GALAXY_ASPECT", "aspect"),
]
RUNTIME = [
    ("GALAXY_INPUT_MODE", "input_mode"), ("GALAXY_AUDIO_MASTER_GAIN", "audio_master_gain"),
    ("GALAXY_AUDIO_MUSIC_GAIN", "audio_music_gain"), ("GALAXY_AUDIO_EFFECTS_GAIN", "audio_sfx_gain"),
    ("GALAXY_AUDIO_SFX_GAIN", "audio_sfx_gain"), ("GALAXY_AUDIO_VOICE_GAIN", "audio_voice_gain"),
    ("GALAXY_AUDIO_AMBIENCE_GAIN", "audio_ambience_gain"), ("GALAXY_AUDIO_CUTSCENE_GAIN", "audio_cutscene_gain"),
    ("GALAXY_AUDIO_WIIMOTE_SPEAKER_GAIN", "audio_wiimote_speaker_gain"), ("GALAXY_TEXT_SPEED", "text_speed"),
    ("GALAXY_LANGUAGE", "language"),
]
FILES = {"config.ini": RENDER, "runtime_settings.ini": RUNTIME}
SIDECAR = ".run_settings.json"


def read_lines(path):
    return path.read_text(encoding="utf-8").splitlines() if path.exists() else []


def key_of(line):
    head, eq, _ = line.partition("=")
    return head.strip() if eq else None


def set_key(lines, key, line):
    """lines with every line of `key` replaced by `line` (removed when None); the old line, or None."""
    old = next((l for l in lines if key_of(l) == key), None)
    kept = [l for l in lines if key_of(l) != key]
    if line is not None:
        kept.append(line)
    return kept, old


def main():
    folder = pathlib.Path(sys.argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    values = {name: value for name, value in os.environ.items() if name.startswith("GALAXY_")}
    for assignment in sys.argv[2:]:
        name, eq, value = assignment.partition("=")
        if eq:
            values[name] = value
    sidecar = folder / SIDECAR
    before = json.loads(sidecar.read_text(encoding="utf-8")) if sidecar.exists() else {}
    record = {}
    for file, table in FILES.items():
        path = folder / file
        lines = read_lines(path)
        for key, old in before.get(file, {}).items():  # the last run's values out, the folder's own back
            lines, _ = set_key(lines, key, old)
        wanted = {}
        for name, key in table:
            if values.get(name):  # an empty value was an unset one
                wanted[key] = values[name]
        originals = {}
        for key, value in wanted.items():
            lines, old = set_key(lines, key, f"{key}={value}")
            originals[key] = old
        record[file] = originals
        if lines or path.exists():
            path.write_text("".join(l + "\n" for l in lines), encoding="utf-8")
    sidecar.write_text(json.dumps(record), encoding="utf-8")


if __name__ == "__main__":
    main()
