#!/usr/bin/env python3
"""Validate CMake installation and the contents of the release ZIP."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[1]
VERSION_MATCH = re.search(
    r'#define THIMBLE_VERSION_STRING "([^"]+)"',
    (ROOT / "include" / "thimble" / "version.hpp").read_text(),
)
if not VERSION_MATCH:
    raise SystemExit("Could not read the Thimble version")
VERSION = VERSION_MATCH.group(1)
BUILD = ROOT / "build" / "release-validation"
INSTALL = BUILD / "install"
CONSUMER = BUILD / "consumer"
UNPACKED = BUILD / "archive"


def run(command: list[str], cwd: Path = ROOT) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


if BUILD.exists():
    shutil.rmtree(BUILD)
BUILD.mkdir(parents=True)

run(["cmake", "-S", str(ROOT), "-B", str(BUILD / "project"),
     "-DTHIMBLE_BUILD_EXAMPLES=OFF"])
run(["cmake", "--build", str(BUILD / "project"), "--parallel"])
run(["ctest", "--test-dir", str(BUILD / "project"), "--output-on-failure"])
run(["cmake", "--install", str(BUILD / "project"), "--prefix", str(INSTALL)])
run(["cmake", "-S", str(ROOT / "tests" / "install_consumer"),
     "-B", str(CONSUMER), f"-DCMAKE_PREFIX_PATH={INSTALL}"])
run(["cmake", "--build", str(CONSUMER), "--parallel"])
run(["ctest", "--test-dir", str(CONSUMER), "--output-on-failure"])

run([sys.executable, str(ROOT / "tools" / "package_release.py")])
archive = ROOT / "build" / f"thimble-{VERSION}.zip"
with zipfile.ZipFile(archive) as bundle:
    bundle.extractall(UNPACKED)

source = BUILD / "archive_consumer.cpp"
source.write_text(
    '#include "thimble.hpp"\n'
    'int main() { auto p = thimble::compile("return 42;"); '
    'if (!p) return 1; auto r = p.value().execute({}); '
    'return r && r.value().as_int().value() == 42 ? 0 : 1; }\n'
)
compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
if not compiler:
    raise SystemExit("No C++ compiler found for archive validation")
binary = BUILD / ("archive_consumer.exe" if os.name == "nt" else
                  "archive_consumer")
name = Path(compiler).name.lower()
include = UNPACKED / f"thimble-{VERSION}"
run(["cmake", "-S", str(include), "-B", str(BUILD / "archive_cmake"),
     "-DTHIMBLE_BUILD_TESTS=OFF", "-DTHIMBLE_BUILD_EXAMPLES=OFF"])
run(["cmake", "--build", str(BUILD / "archive_cmake"), "--parallel"])
if name in {"cl", "cl.exe"}:
    run([compiler, "/nologo", "/std:c++17", "/W4", "/WX",
         f"/I{include}", str(source), f"/Fe:{binary}"])
else:
    run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-pedantic", f"-I{include}", str(source), "-o", str(binary)])
run([str(binary)])
print("Release archive and installed package passed validation.")
