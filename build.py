#!/usr/bin/env python3
"""Build and run the Thimble tests on platforms with Python 3 and C++17."""

from __future__ import annotations

import argparse
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


def compiler_commands(compiler: str, include: Path, source: Path, output: Path,
                      sanitize: bool = False, warnings_as_errors: bool = False,
                      optimise: bool = False) -> list[str]:
    """Build one translation unit with either GCC/Clang or MSVC."""
    name = Path(compiler).name.lower()
    if name in {"cl", "cl.exe"}:
        command = [compiler, "/nologo", "/std:c++17", "/W4", "/EHsc"]
        if sanitize:
            command.append("/fsanitize=address")
        if warnings_as_errors:
            command.append("/WX")
        if optimise:
            command.append("/O2")
        return command + [f"/I{include}", str(source), f"/Fe:{output}"]
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-pedantic"]
    if sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if warnings_as_errors:
        command.append("-Werror")
    if optimise:
        command.append("-O2")
    return command + [f"-I{include}", str(source), "-o", str(output)]


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and test Thimble")
    parser.add_argument("--sanitize", action="store_true",
                        help="enable address and undefined-behaviour sanitizers")
    parser.add_argument("--benchmark", action="store_true",
                        help="compile and run the runtime benchmark")
    parser.add_argument("--warnings-as-errors", action="store_true",
                        help="treat compiler warnings as errors")
    args = parser.parse_args()
    if args.sanitize:
        # LeakSanitizer cannot run inside some containers and debuggers. Address
        # and undefined-behaviour checks remain enabled.
        os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")

    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler was not found. Set CXX to its path.")

    BUILD.mkdir(parents=True, exist_ok=True)
    test_binary = BUILD / ("test_language.exe" if os.name == "nt" else "test_language")
    focused_tests = [
        "test_diagnostics",
        "test_host_boundaries",
        "test_limits_and_cycles",
        "smoke",
    ]
    amalgamated_binary = BUILD / (
        "test_amalgamated.exe" if os.name == "nt" else "test_amalgamated"
    )
    geometry_binary = BUILD / (
        "geometry_runtime.exe" if os.name == "nt" else "geometry_runtime"
    )
    policy_binary = BUILD / (
        "policy_runtime.exe" if os.name == "nt" else "policy_runtime"
    )
    fuzz_binary = BUILD / (
        "parser_fuzz_smoke.exe" if os.name == "nt" else "parser_fuzz_smoke"
    )
    benchmark_binary = BUILD / (
        "runtime_benchmark.exe" if os.name == "nt" else "runtime_benchmark"
    )
    generated_header = BUILD / "thimble.hpp"

    common = {"sanitize": args.sanitize,
              "warnings_as_errors": args.warnings_as_errors}
    run(compiler_commands(compiler, ROOT / "include", ROOT / "tests" / "test_language.cpp", test_binary, **common))
    run([str(test_binary)])

    for test_name in focused_tests:
        binary = BUILD / (f"{test_name}.exe" if os.name == "nt" else test_name)
        run(compiler_commands(compiler, ROOT / "include",
                              ROOT / "tests" / f"{test_name}.cpp",
                              binary, **common))
        run([str(binary)])

    run(compiler_commands(compiler, ROOT / "include",
                          ROOT / "tests" / "parser_fuzz_smoke.cpp",
                          fuzz_binary, **common))
    run([str(fuzz_binary)])

    run([sys.executable, str(ROOT / "tools" / "amalgamate.py"), str(generated_header)])
    run(compiler_commands(compiler, BUILD, ROOT / "tests" / "amalgamated.cpp", amalgamated_binary, **common))
    run([str(amalgamated_binary)])

    run(compiler_commands(compiler, ROOT / "include",
                          ROOT / "examples" / "geometry_runtime.cpp",
                          geometry_binary, **common))
    run([str(geometry_binary), str(ROOT / "examples" / "geometry.thimble")])

    run(compiler_commands(compiler, ROOT / "include",
                          ROOT / "examples" / "policy_runtime.cpp",
                          policy_binary, **common))
    run([str(policy_binary)])

    if args.benchmark:
        run(compiler_commands(compiler, ROOT / "include",
                              ROOT / "benchmarks" / "runtime_benchmark.cpp",
                              benchmark_binary, optimise=True, **common))
        run([str(benchmark_binary)])

    print("Thimble tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
