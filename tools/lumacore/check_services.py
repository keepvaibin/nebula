"""check_services.py luma_services.json INCLUDE_DIR: the services catalog the game module wrote (luma_describe
--services, the host's --luma-services) against the services the SDK's headers declare (each LUMA_<X>_NAME with its
_MAJOR and _MINOR in INCLUDE_DIR/luma_*.h). Exit 0 when the file is in the validator's form (services-registry 9:
{"services": [{"name", "major", "minor", "permission"}]}, by name in byte order then major, each once) and lists
every header's service at the header's version, and nothing the headers lack; else 1, each difference named.

One header service is not catalogued, on purpose: "harness", the test extension's (tools/test_driver/extension),
which only the harness's game half finds by LumaCore.service in a test run; no mod imports it."""
import json
import pathlib
import re
import sys

NOT_CATALOGUED = {"harness"}
NAME = re.compile(r"^[a-z][a-z0-9]*(\.[a-z][a-z0-9]*)*$")


def header_services(include):
    """{name: (major, minor)} from every luma_*.h: a LUMA_<X>_NAME "text" with LUMA_<X>_MAJOR and LUMA_<X>_MINOR."""
    defines = {}
    for header in sorted(include.glob("luma_*.h")):
        for line in header.read_text(encoding="utf-8").splitlines():
            m = re.match(r"\s*#define\s+(LUMA_[A-Z0-9_]+)\s+(\S+)", line)
            if m:
                defines.setdefault(m.group(1), (m.group(2), header.name))
    services = {}
    for key, (value, header) in defines.items():
        if not key.endswith("_NAME") or not value.startswith('"'):
            continue
        stem = key[:-len("_NAME")]
        if stem + "_MAJOR" not in defines or stem + "_MINOR" not in defines:
            continue
        number = lambda k: int(defines[k][0].rstrip("uU"))
        services[value.strip('"')] = (number(stem + "_MAJOR"), number(stem + "_MINOR"), header)
    return services


def main():
    if len(sys.argv) != 3:
        print("usage: check_services.py luma_services.json INCLUDE_DIR", file=sys.stderr)
        return 1
    path, include = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    faults = []
    try:
        rows = json.loads(path.read_text(encoding="utf-8"))["services"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("check_services: %s does not read: %s" % (path, error), file=sys.stderr)
        return 1
    catalog = {}
    last = None
    for i, row in enumerate(rows):
        at = "services[%d]" % i
        if not isinstance(row, dict) or sorted(row) != ["major", "minor", "name", "permission"]:
            faults.append("%s: not exactly name, major, minor, permission" % at)
            continue
        name, major, minor, permission = row["name"], row["major"], row["minor"], row["permission"]
        if not isinstance(name, str) or len(name) > 31 or not NAME.match(name):
            faults.append("%s: %r is not a service name" % (at, name))
            continue
        if any(not isinstance(n, int) or isinstance(n, bool) or not 0 <= n <= 65535 for n in (major, minor)):
            faults.append("%s: %s's major and minor are not whole numbers from 0 to 65535" % (at, name))
            continue
        if permission is not None and not (isinstance(permission, str) and NAME.match(permission)):
            faults.append("%s: %s's permission %r is neither null nor a permission" % (at, name, permission))
        key = (name.encode("utf-8"), major)
        if last is not None and last >= key:
            faults.append("%s: %s %d is not after the row before it (by name in byte order, then major)" % (at, name, major))
        last = key
        catalog[name] = (major, minor)
    headers = header_services(include)
    for name, (major, minor, header) in sorted(headers.items()):
        if name in NOT_CATALOGUED:
            if name in catalog:
                faults.append("%s is catalogued; it was the test extension's only" % name)
            continue
        if name not in catalog:
            faults.append("%s (%s) is not in %s" % (name, header, path.name))
        elif catalog[name] != (major, minor):
            faults.append("%s is %d.%d in %s and %d.%d in %s" % (name, catalog[name][0], catalog[name][1], path.name,
                                                              major, minor, header))
    for name in sorted(set(catalog) - set(headers)):
        faults.append("%s is in %s and declared by no header" % (name, path.name))
    for fault in faults:
        print("check_services: " + fault, file=sys.stderr)
    if not faults:
        print("check_services: %d services, as the headers declare them" % len(catalog))
    return 1 if faults else 0


if __name__ == "__main__":
    sys.exit(main())
