#!/usr/bin/env python3
"""Create a source and single-header release bundle without external tools."""

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
STAGING = ROOT / "build" / f"thimble-{VERSION}"
ARCHIVE = ROOT / "build" / f"thimble-{VERSION}.zip"

if STAGING.exists():
    shutil.rmtree(STAGING)
STAGING.mkdir(parents=True)

subprocess.run([sys.executable, str(ROOT / "tools" / "amalgamate.py"),
                str(STAGING / "thimble.hpp")], check=True)
for name in ["LICENSE", "README.md", "SPEC.md", "CHANGELOG.md", "CMakeLists.txt",
             "build.py", "wrangler.jsonc"]:
    shutil.copy2(ROOT / name, STAGING / name)
for directory in ["include", "cmake", "examples", "tests", "benchmarks",
                  "tools", "docs", "site"]:
    shutil.copytree(ROOT / directory, STAGING / directory,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))

with zipfile.ZipFile(ARCHIVE, "w", zipfile.ZIP_DEFLATED) as bundle:
    for path in sorted(STAGING.rglob("*")):
        if path.is_file():
            bundle.write(path, Path(STAGING.name) / path.relative_to(STAGING))

print(ARCHIVE)
