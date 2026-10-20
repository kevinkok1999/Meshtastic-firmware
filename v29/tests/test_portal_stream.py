#!/usr/bin/env python3
"""Compile and execute the actual pure-C++ V29 request-line parser on the CI host."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = root / "v29/tests/test_portal_stream.cpp"
with tempfile.TemporaryDirectory() as tmp:
    exe = pathlib.Path(tmp) / "v29_portal_stream_test"
    subprocess.run(
        ["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(exe)],
        cwd=root, check=True, timeout=45,
    )
    subprocess.run([str(exe)], cwd=root, check=True, timeout=20)
