#!/usr/bin/env python3
"""Generate bridge_v<major>.<minor>_{68k,ppc}.txt from the daemon's own strings.

The CAPABILITIES line is assembled from the output of the host-built unit test
binary (`amiga-bridge/host/test_bridge --dump-caps`), which links the same
`caps_util.c` the Amiga daemon ships, so the command / platform / features /
profiles fields are never typed by hand. The version comes from
`amiga-bridge/include/bridge_internal.h`. SYSINFO / VERSION / HB values are
synthetic (shapes from the source, see README.md).

Usage (from the repository root, needs gcc or clang):
    python3 amiga-devbench/tests/fixtures/gen_bridge_fixtures.py
"""
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
HOST = ROOT / "amiga-bridge" / "host"
HEADER = ROOT / "amiga-bridge" / "include" / "bridge_internal.h"

SYSINFO = {
    "68k": "SYSINFO|1835008|7340032|2097152|8388608|40|68|68030|50|serial",
    "ppc": "SYSINFO|67108864|201326592|134217728|268435456|54|16|PowerPC|60|tcp",
}
HB = {
    "68k": "HB|1200|1835008|7340032",
    "ppc": "HB|1200|67108864|201326592",
}


def main() -> int:
    hdr = HEADER.read_text()
    major = re.search(r"#define BRIDGE_VERSION_MAJOR (\d+)", hdr).group(1)
    minor = re.search(r"#define BRIDGE_VERSION_MINOR (\d+)", hdr).group(1)
    max_line = re.search(r"#define BRIDGE_MAX_LINE (\d+)", hdr).group(1)

    subprocess.run(["make", "-C", str(HOST)], check=True, stdout=subprocess.DEVNULL)
    out = subprocess.run([str(HOST / "test_bridge"), "--dump-caps"],
                         check=True, capture_output=True, text=True).stdout
    caps = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)

    for arch in ("68k", "ppc"):
        line = "CAPABILITIES|AmigaBridge v{}.{}|{}|{}|{}|{}|{}|{}".format(
            major, minor, caps[f"{arch}.level"], max_line,
            caps[f"{arch}.commands"], caps[f"{arch}.platform"],
            caps[f"{arch}.features"], caps[f"{arch}.profiles"])
        text = "\n".join([
            "READY|1.0",
            line,
            SYSINFO[arch],
            f"VERSION|AmigaBridge|{major}|{minor}|Aug 23 2026 12:00:00",
            HB[arch],
        ]) + "\n"
        path = HERE / f"bridge_v{major}.{minor}_{arch}.txt"
        path.write_text(text)
        print(f"wrote {path.relative_to(ROOT)} ({len(line)} byte CAPABILITIES line)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
