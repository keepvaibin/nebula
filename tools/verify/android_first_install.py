#!/usr/bin/env python3
"""A true first install on an Android device or emulator: nothing kept, setup from a disc image, Play, the title.

    python tools/verify/android_first_install.py <Nebula.apk> <disc image name in the device's Downloads> <folder for pictures>

Fails on: setup not finishing, the game ending or logging fatal/crash, the Game scene not reached, or the title's 2D drawn
black. Wipes this app's data on the device. The disc image must already be in the device's Downloads folder.
"""
import os
import re
import subprocess
import sys
import time

SDK = os.environ.get('ANDROID_HOME') or os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk')
ADB = os.path.join(SDK, 'platform-tools', 'adb')
ENV = dict(os.environ, MSYS_NO_PATHCONV='1')
PACKAGE = 'dev.nebula.galaxy'
GAME_SECONDS = 150


def adb(*args):
    return subprocess.run([ADB, *args], capture_output=True, env=ENV)


def shell(command):
    return adb('shell', command).stdout.decode('utf8', 'replace')


def screen_text():
    adb('shell', 'uiautomator', 'dump', '/sdcard/ui.xml')
    return adb('exec-out', 'cat', '/sdcard/ui.xml').stdout.decode('utf8', 'replace')


def wait_for(text, seconds, tap=True):
    pattern = r'text="%s"[^>]*?bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"' % re.escape(text)
    end = time.time() + seconds
    while time.time() < end:
        found = re.search(pattern, screen_text())
        if found:
            if tap:
                a, b, c, d = map(int, found.groups())
                shell('input tap %d %d' % ((a + c) // 2, (b + d) // 2))
            return True
        time.sleep(1)
    return False


def pick_file(name):
    """The system picker lists the newest files first: a disc image under newer files is found through its search."""
    if wait_for(name, 10):
        return True
    found = re.search(r'content-desc="Search"[^>]*?bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', screen_text())
    if not found:
        return False
    a, b, c, d = map(int, found.groups())
    shell('input tap %d %d' % ((a + c) // 2, (b + d) // 2))
    time.sleep(2)
    shell('input text %s' % name.replace(' ', '%s'))
    shell('input keyevent KEYCODE_ENTER')
    # The search box holds the name too: tap a row below it (a suggestion runs the search, a result is the file).
    row = r'text="%s" resource-id="(?!com.google.android.documentsui:id/search_src_text)[^"]*"[^>]*?bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"'
    for _ in range(2):
        end = time.time() + 15
        found = None
        while time.time() < end and not found:
            time.sleep(1)
            found = re.search(row % re.escape(name), screen_text())
        if not found:
            return False
        a, b, c, d = map(int, found.groups())
        shell('input tap %d %d' % ((a + c) // 2, (b + d) // 2))
        time.sleep(3)
        if 'search_src_text' not in screen_text():
            return True
    return True


def fail(reason):
    print('FAIL:', reason)
    log = adb('logcat', '-d', '-s', 'Nebula').stdout.decode('utf8', 'replace')
    print('\n'.join(log.splitlines()[-12:]))
    return 1


def picture(folder, name):
    path = os.path.join(folder, name + '.png')
    with open(path, 'wb') as out:
        out.write(adb('exec-out', 'screencap', '-p').stdout)
    return path


def centre_black_share(path):
    from PIL import Image
    image = Image.open(path).convert('RGB')
    w, h = image.size
    box = image.crop((w * 3 // 10, h // 10, w * 7 // 10, h * 6 // 10)).resize((64, 64))
    pixels = list(box.getdata())
    return sum(1 for p in pixels if max(p) < 8) / len(pixels)


def main():
    apk, disc, folder = sys.argv[1:4]
    os.makedirs(folder, exist_ok=True)
    installed = adb('install', '-r', apk)
    answer = (installed.stdout + installed.stderr).decode('utf8', 'replace').strip()
    print(answer.splitlines()[-1] if answer else 'adb install said nothing')
    if installed.returncode != 0 or 'Success' not in answer:
        # A signature mismatch (another key's build installed) keeps the old app: uninstall it first.
        return fail('the APK did not install')
    shell('am force-stop %s; pm clear %s' % (PACKAGE, PACKAGE))
    shell('am start -n %s/.LauncherActivity' % PACKAGE)
    if not wait_for('Choose disc image', 30):
        return fail('no setup screen on a first install')
    if not pick_file(disc):
        return fail('the file picker does not show ' + disc)
    if not wait_for('Set up', 120, tap=False):
        return fail('the disc image was not accepted')
    started = time.time()
    # The button shows before it takes a tap (the screen still settling): tapped again until setup starts, as a person
    # would, and the notification question Android asks then answered.
    begun = False
    while time.time() - started < 60:
        text = screen_text()
        if 'Setting up' in text or 'text="Play"' in text:
            begun = True
            break
        if 'text="Allow"' in text:
            wait_for('Allow', 5)
        elif 'text="Set up"' in text:
            wait_for('Set up', 1)
        time.sleep(3)
    if not begun:
        return fail('setup did not start')
    if not wait_for('Play', 1500):
        return fail('setup did not finish')
    print('setup %d s' % (time.time() - started))
    adb('logcat', '-c')
    started = time.time()
    while time.time() - started < GAME_SECONDS:
        time.sleep(10)
        log = adb('logcat', '-d', '-s', 'Nebula').stdout.decode('utf8', 'replace')
        if 'fatal:' in log or 'crash:' in log:
            return fail('the game stopped')
        if not shell('pidof %s:game' % PACKAGE).strip():
            return fail('the game process ended')
        if 'initialize Scene: Game' in log and time.time() - started > 60:
            break
    else:
        return fail('the Game scene was not reached in %d s' % GAME_SECONDS)
    time.sleep(20)
    path = picture(folder, 'title')
    share = centre_black_share(path)
    print('title black share %.2f (%s)' % (share, path))
    if share > 0.25:
        return fail('the title logo is drawn black')
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
