#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compares two runs' present snapshots (GALAXY_PRESENT_SNAPSHOT_DIR), picture by picture.

  compare_snapshots.py <reference dir> <candidate dir> [--tolerance N] [--sheet out.png]

The runs are the same game frames (GALAXY_TEST_DETERMINISTIC=1, the same GALAXY_PRESENT_SNAPSHOT_* numbers) from two
renderers, e.g. the Vulkan development build and the Metal renderer. For each present number in both folders: the
largest and the mean difference of any channel, and how many pixels differ by more than the tolerance (default 1, as
the Vulkan renderer is held to against Direct3D 12). Exit status 1 when any picture has pixels past the tolerance.
--sheet writes the reference, the candidate and their difference (times 16) side by side for the worst picture.
Needs Pillow."""
import os
import re
import sys

from PIL import Image, ImageChops, ImageStat


def pictures(folder):
    found = {}
    for name in os.listdir(folder):
        match = re.search(r"present_(\d+)\.ppm$", name)
        if match:
            found[int(match.group(1))] = os.path.join(folder, name)
    return found


def main():
    tolerance = 1
    sheet = None
    args = []
    options = iter(sys.argv[1:])
    for option in options:
        if option == "--tolerance":
            tolerance = int(next(options))
        elif option == "--sheet":
            sheet = next(options)
        else:
            args.append(option)
    if len(args) != 2:
        print(__doc__)
        return 2
    reference, candidate = pictures(args[0]), pictures(args[1])
    shared = sorted(set(reference) & set(candidate))
    if not shared:
        print("no present number is in both folders")
        return 2
    worst = None
    failed = 0
    for number in shared:
        a = Image.open(reference[number]).convert("RGB")
        b = Image.open(candidate[number]).convert("RGB")
        if a.size != b.size:
            print(f"present {number}: sizes differ, {a.size} and {b.size}")
            failed += 1
            continue
        difference = ImageChops.difference(a, b)
        largest = max(high for _, high in difference.getextrema())
        mean = sum(ImageStat.Stat(difference).mean) / 3.0
        # Pixels with any channel past the tolerance.
        past = difference.point(lambda value: 255 if value > tolerance else 0).convert("L")
        count = sum(1 for value in past.getdata() if value)
        share = 100.0 * count / (a.size[0] * a.size[1])
        print(f"present {number}: largest {largest}, mean {mean:.4f}, past {tolerance}: {count} pixels ({share:.3f}%)")
        if count:
            failed += 1
        if worst is None or count > worst[0]:
            worst = (count, number, a, b, difference)
    if sheet and worst:
        _, number, a, b, difference = worst
        boosted = difference.point(lambda value: min(255, value * 16))
        out = Image.new("RGB", (a.size[0] * 3, a.size[1]))
        out.paste(a, (0, 0))
        out.paste(b, (a.size[0], 0))
        out.paste(boosted, (a.size[0] * 2, 0))
        out.save(sheet)
        print(f"sheet of present {number}: {sheet}")
    print(f"{len(shared)} pictures compared, {failed} with pixels past the tolerance")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
