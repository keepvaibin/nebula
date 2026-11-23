"""check_fsr3_start_log.py: what a start check with FSR 3 chosen says about FSR 3.

    python -X utf8 -I tools/check_fsr3_start_log.py <the start check's log>

The start check (native/host/host_main.cpp run_start_check) ran with `upscaler=fsr3` in its settings file. The log must say one of
  "[fsr3] context made: ..."   FSR 3 was made on the device (vk/temporal_upscaler_vk.cpp), or
  "[vulkan] FSR 3 is not offered on this device: it lacks <what>"   the device lacks what it needs (vk/vk_context.cpp);
the check says which, and fails when the log has neither, when the log says FSR 3 is not in the program ("[vulkan] features:" with
no FSR 3 line at all is neither), or when FSR 3 started and failed ("[fsr3] ... failed"). Exit 0 told, 1 not, 2 no log.
"""
import pathlib
import re
import sys


def judge(text):
    """(exit code, the line to print)."""
    made = re.search(r"^\[fsr3\] context made: (.*)$", text, flags=re.M)
    lacks = re.search(r"^\[vulkan\] FSR 3 is not offered on this device: it lacks (.+)$", text, flags=re.M)
    failed = re.findall(r"^\[fsr3\] .*(?:failed|could not be made).*$", text, flags=re.M)
    if failed:
        return 1, "FSR 3 started and failed: " + "; ".join(failed[:3])
    if made:
        return 0, "FSR 3 was made on this device: " + made.group(1)
    if lacks:
        return 0, "FSR 3 is not offered on this device, which lacks " + lacks.group(1)
    return 1, "the start check's log says nothing about FSR 3 (not made, no device feature named): the program may be built without it"


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    try:
        text = pathlib.Path(argv[1]).read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        print("check_fsr3_start_log: %s" % error)
        return 2
    code, line = judge(text)
    print("check_fsr3_start_log: " + line)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
