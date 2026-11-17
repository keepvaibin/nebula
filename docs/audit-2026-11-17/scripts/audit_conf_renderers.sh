#!/bin/sh
rm -f D:/NebulaWork/build/measure.lock
RENDERER=vulkan WORK=D:/NebulaWork/build/audit-conf/work-vk OUTDIR=D:/NebulaWork/build/audit-conf/out-vk PROGRESS=D:/NebulaWork/build/audit-conf/progress-vk.txt sh 'C:/Users/<user>/AppData/Local/Temp/claude/9d73c8bc852b5521/1261821a-bc5f-4a25-91d3-ad9793865d41/scratchpad/audit_conf.sh' run
RENDERER=d3d11 WORK=D:/NebulaWork/build/audit-conf/work-d11 OUTDIR=D:/NebulaWork/build/audit-conf/out-d11 PROGRESS=D:/NebulaWork/build/audit-conf/progress-d11.txt sh 'C:/Users/<user>/AppData/Local/Temp/claude/9d73c8bc852b5521/1261821a-bc5f-4a25-91d3-ad9793865d41/scratchpad/audit_conf.sh' run
echo renderers-done >> D:/NebulaWork/build/audit-conf/progress-d11.txt

