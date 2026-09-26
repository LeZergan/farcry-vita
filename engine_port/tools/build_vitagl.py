"""Rebuild the pinned upstream renderer without modifying the shared VitaSDK.

Usage: python -S engine_port/tools/build_vitagl.py (VITASDK must be set).
Build output uses the same bounded supervisor as the engine diagnostic runner.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys

from vita3k_evidence import atomic_json, sha256
from vita3k_lab import LAB, ROOT, lab_lock, now, supervise

COMMIT = "df63ce8211ce4a42d7092824ffa487bc9671105a"
REPOSITORY = "https://github.com/Rinnegatamante/vitaGL.git"


def build() -> None:
    sdk = Path(os.environ["VITASDK"])
    compiler = sdk / "bin" / "arm-vita-eabi-gcc.exe"
    nm = sdk / "bin" / "arm-vita-eabi-nm.exe"
    if not compiler.is_file() or not nm.is_file():
        raise RuntimeError("A complete Windows VitaSDK toolchain is required")
    os.environ["PATH"] = str(sdk / "bin") + os.pathsep + os.environ.get("PATH", "")
    make = shutil.which("mingw32-make") or shutil.which("make")
    if not make:
        raise RuntimeError("GNU make is required (mingw32-make is supported)")
    source = LAB / "vendor" / "vitaGL"
    output = LAB / "vendor-build-heap"
    output.mkdir(parents=True, exist_ok=True)
    with lab_lock():
        if not source.exists():
            source.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "clone", "--quiet", REPOSITORY, str(source)], check=True, timeout=120)
            subprocess.run(["git", "-C", str(source), "checkout", "--quiet", COMMIT], check=True, timeout=30)
        head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip()
        if head != COMMIT or dirty:
            raise RuntimeError("Existing dependency checkout is not the pinned clean source; it was not changed")
        # The local optional PAF replacement pulls C++ declarations into C.
        # vitaGL does not use PAF; skip only that header, only for this build.
        flags = ["ENABLE_LEGACY_PIPELINE=1", "HAVE_CUSTOM_HEAP=1", "CPPFLAGS=-D_PSP2_PAF_H_"]
        command = [make, "-B", "-j4", *flags]
        code, reason, _ = supervise(command, source, output, 300)
        if code or reason != "process_exit":
            raise RuntimeError(f"vitaGL build failed ({code}, {reason}); bounded logs: {output}")
        archive = source / "libvitaGL.a"
        undefined = subprocess.check_output([str(nm), "-u", str(archive)], text=True, stderr=subprocess.DEVNULL)
        if "sceClibMspaceMallocStats" in undefined:
            raise RuntimeError("Archive still depends on the emulator's unimplemented heap statistics")
        defined = subprocess.check_output([str(nm), "-S", "--defined-only", str(archive)], text=True,
                                          stderr=subprocess.DEVNULL)
        legacy = [line for line in defined.splitlines() if line.endswith(" T vglDrawObjects")]
        if len(legacy) != 1 or int(legacy[0].split()[1], 16) <= 4:
            raise RuntimeError("Required legacy geometry implementation is missing or a no-op")
        kit = ROOT / "engine_port" / "vita_kit"
        destination = kit / "libvitaGL_farcry_heap.a"
        temporary = destination.with_suffix(".tmp")
        shutil.copy2(archive, temporary)
        if sha256(temporary) != sha256(archive):
            raise RuntimeError("Archive copy failed hash verification")
        header = kit / "vitagl" / "include" / "vitaGL.h"
        header.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / "source" / "vitaGL.h", header)
        for name in ("COPYING", "COPYING.LESSER"):
            shutil.copy2(source / name, kit / "vitagl" / name)
        os.replace(temporary, destination)
        atomic_json(kit / "libvitaGL_farcry_heap.build.json", {
            "repository": REPOSITORY, "commit": head, "flags": flags, "built_utc": now(),
            "archive_sha256": sha256(destination), "header_sha256": sha256(header),
            "compiler_sha256": sha256(compiler), "legacy_symbol": legacy[0],
            "unimplemented_mspace_statistics_import": False})
        print(f"Built {destination}\nSHA-256: {sha256(destination)}")


if __name__ == "__main__":
    try:
        build()
    except (KeyError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
