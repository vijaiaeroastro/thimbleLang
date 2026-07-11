#!/usr/bin/env python3
"""Build and run the Thimble tests on platforms with Python 3 and C++17."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def compiler_commands(compiler: str, include: Path, source: Path, output: Path) -> list[str]:
    """Build one translation unit with either GCC/Clang or MSVC."""
    name = Path(compiler).name.lower()
    if name in {"cl", "cl.exe"}:
        return [compiler, "/nologo", "/std:c++17", "/W4", "/EHsc",
                f"/I{include}", str(source), f"/Fe:{output}"]
    return [compiler, "-std=c++17", "-Wall", "-Wextra", "-pedantic",
            f"-I{include}", str(source), "-o", str(output)]


def main() -> int:
    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler was not found. Set CXX to its path.")

    BUILD.mkdir(parents=True, exist_ok=True)
    test_binary = BUILD / ("test_language.exe" if os.name == "nt" else "test_language")
    amalgamated_binary = BUILD / (
        "test_amalgamated.exe" if os.name == "nt" else "test_amalgamated"
    )
    geometry_binary = BUILD / (
        "geometry_runtime.exe" if os.name == "nt" else "geometry_runtime"
    )
    generated_header = BUILD / "thimble.hpp"

    run(compiler_commands(compiler, ROOT / "include", ROOT / "tests" / "test_language.cpp", test_binary))
    run([str(test_binary)])

    run([sys.executable, str(ROOT / "tools" / "amalgamate.py"), str(generated_header)])
    run(compiler_commands(compiler, BUILD, ROOT / "tests" / "amalgamated.cpp", amalgamated_binary))
    run([str(amalgamated_binary)])

    run(compiler_commands(compiler, ROOT / "include",
                          ROOT / "examples" / "geometry_runtime.cpp",
                          geometry_binary))
    run([str(geometry_binary), str(ROOT / "examples" / "geometry.thimble")])

    print("Thimble tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
