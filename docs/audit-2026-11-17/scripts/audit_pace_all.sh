#!/bin/sh
# audit_pace_all.sh: the audit's real-time pacing runs on the PC, one after another (each a visible window, no sound).
HERE=$(cd "$(dirname "$0")" && pwd)
export RUN_SECONDS=${RUN_SECONDS:-75}
run() { sh "$HERE/audit_pace.sh" "$@"; }
run d12-v1-match Nebula.exe    GALAXY_MAX_FPS=0   GALAXY_VSYNC_INTERVAL=1
run d12-v1-120   Nebula.exe    GALAXY_MAX_FPS=120 GALAXY_VSYNC_INTERVAL=1
run d12-v1-144   Nebula.exe    GALAXY_MAX_FPS=144 GALAXY_VSYNC_INTERVAL=1
run d12-v1-60    Nebula.exe    GALAXY_MAX_FPS=60  GALAXY_VSYNC_INTERVAL=1
run d12-v0-144   Nebula.exe    GALAXY_MAX_FPS=144 GALAXY_VSYNC_INTERVAL=0
run d12-v0-240   Nebula.exe    GALAXY_MAX_FPS=240 GALAXY_VSYNC_INTERVAL=0
run d12-v0-unl   Nebula.exe    GALAXY_MAX_FPS=unlimited GALAXY_VSYNC_INTERVAL=0
run vk-v1-match  NebulaVk.exe  GALAXY_MAX_FPS=0   GALAXY_VSYNC_INTERVAL=1
run vk-v0-240    NebulaVk.exe  GALAXY_MAX_FPS=240 GALAXY_VSYNC_INTERVAL=0
run d11-v1-match NebulaD11.exe GALAXY_MAX_FPS=0   GALAXY_VSYNC_INTERVAL=1
echo "$(date +%H:%M:%S) all done" >> D:/NebulaWork/build/audit-pace/progress.txt
