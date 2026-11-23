"""check_fsr3_start_log.py: what a start check with FSR 3 chosen says about FSR 3.

    python -X utf8 -I tools/check_fsr3_start_log.py <the start check's log>

The start check (native/host/host_main.cpp run_start_check) ran with `upscaler=fsr3` in its settings file. The log must say one of
  "[fsr3] context made: ..."   FSR 3 was made on the device (vk/temporal_upscaler_vk.cpp), or
  "[vulkan] FSR 3 is not offered on this device: it lacks <what>"   the device lacks what it needs (vk/vk_context.cpp), or
  "[fsr3] ... failed" with "the game goes on without it" and every frame drawn: the context could not be made on this device (the
  runner's software renderer is one) and the game went on without the upscaler. That is told, not judged: ledger P-366.
The check says which, and fails when the log has none of them (a program built without FSR 3), or when the context failed and the
start check did not draw its frames. Exit 0 told, 1 not, 2 no log.
"""
import pathlib
import re
import sys


def judge(text):
    """(exit code, the line to print)."""
    made = re.search(r"^\[fsr3\] context made: (.*)$", text, flags=re.M)
    lacks = re.search(r"^\[vulkan\] FSR 3 is not offered on this device: it lacks (.+)$", text, flags=re.M)
    failed = re.findall(r"^\[fsr3\] .*(?:failed|could not be made).*$", text, flags=re.M)
    went_on = re.search(r"^\[Renderer\] the temporal upscaler could not be made on this device: the game goes on without it$", text, flags=re.M)
    drawn = re.search(r"^start check: (\d+) frames drawn", text, flags=re.M)
    if failed:
        if went_on and drawn:
            return 0, "FSR 3 could not make its context on this device (%s); the game went on without it and drew %s frames" % (failed[0], drawn.group(1))
        return 1, "FSR 3 started and failed, and the game did not go on without it: " + "; ".join(failed[:3])
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
