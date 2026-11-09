"""start_hidden.py PROGRAM [ARG...]: runs PROGRAM with its window hidden (STARTUPINFO's SW_HIDE, as Windows starts a
program a shortcut sets to hidden), its output and environment this process's, and exits with its exit code. A test's
game run starts this way (launch_args.h started_hidden)."""
import subprocess
import sys

startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
sys.exit(subprocess.call(sys.argv[1:], startupinfo=startup))
