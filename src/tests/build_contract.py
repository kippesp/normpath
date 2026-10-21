"""Executable contracts: embedded UTF-8 manifests and Git identity refresh."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(*command, cwd=None):
    result = subprocess.run(command, cwd=cwd, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(
            f"{command!r}: status {result.returncode}\n"
            f"{result.stdout.decode(errors='replace')}"
            f"{result.stderr.decode(errors='replace')}"
        )
    return result.stdout


def manifests(args):
    with tempfile.TemporaryDirectory(prefix="np-manifest-") as temporary:
        for executable in (args.product, *args.test):
            output = Path(temporary) / "embedded.manifest"
            run(args.mt, "-nologo", f"-inputresource:{executable};#1", f"-out:{output}")
            root = ET.parse(output).getroot()
            settings = root.findall(
                "./{urn:schemas-microsoft-com:asm.v3}application/"
                "{urn:schemas-microsoft-com:asm.v3}windowsSettings/"
                "{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage"
            )
            require(len(settings) == 1 and settings[0].text == "UTF-8",
                    f"Missing or incorrect UTF-8 manifest in {executable}")
            print(f"Embedded UTF-8 manifest: {executable}")


def identity(args):
    # Each run gets an isolated repository; never commit or change HEAD in np.
    parent = Path(args.work)
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as temporary:
        fixture = Path(temporary)
        source = fixture / "src"
        shutil.copytree(
            args.source, source,
            ignore=shutil.ignore_patterns("build", "__pycache__"),
        )
        run(args.git, "init", "--quiet", str(fixture))
        (fixture / ".gitignore").write_text("/build/\n/ignored.txt\n", encoding="utf-8")
        tracked = fixture / "tracked.txt"
        tracked.write_text("original\n", encoding="utf-8")
        run(args.git, "-C", str(fixture), "add", "src", ".gitignore", "tracked.txt")

        def commit(message):
            run(args.git, "-C", str(fixture), "-c", "user.name=np fixture",
                "-c", "user.email=np-fixture@example.invalid", "-c", "commit.gpgsign=false",
                "commit", "--quiet", "--allow-empty", "-m", message)
            return run(args.git, "-C", str(fixture), "rev-parse", "HEAD").strip()

        first = commit("first fixture revision")
        build = fixture / "build"
        configure = [args.cmake, "-S", str(source), "-B", str(build),
                     "-G", args.generator, "-DBUILD_TESTING=OFF",
                     "-DNP_ENABLE_ASAN=OFF", "-DNP_ENABLE_UBSAN=OFF",
                     f"-DCMAKE_BUILD_TYPE={args.config}"]
        if args.generator.startswith("Visual Studio"):
            configure.extend(["-A", "x64"])
            if args.toolset:
                configure.extend(["-T", args.toolset])
        else:
            configure.extend([f"-DCMAKE_CXX_COMPILER={args.compiler}",
                              "-DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc",
                              "-DCMAKE_LINKER_TYPE=LLD"])
        run(*configure)
        command = [args.cmake, "--build", str(build), "--config", args.config,
                   "--target", "np_exe"]
        executable = build / args.config / "np.exe" if args.generator.startswith(
            "Visual Studio") else build / "np.exe"
        header = build / "generated/version.h"

        def verify(revision, dev=False):
            run(*command)
            version = run(str(executable), "--version")
            build_revision = revision[:7] + (b"-dev" if dev else b"")
            expected = (b"np - Path Normalizer for Windows (ver. 1.0.0 - "
                        + build_revision + b")\nhttps://github.com/kippesp/normpath\n")
            require(version == expected,
                    f"Incorrect version output: {version!r}")
            require(revision in header.read_bytes(),
                    f"Generated header does not contain {revision!r}")

        verify(first)
        original_time = header.stat().st_mtime_ns
        original_binary_time = executable.stat().st_mtime_ns
        verify(first)
        require(header.stat().st_mtime_ns == original_time,
                "Unchanged header rewritten")
        require(executable.stat().st_mtime_ns == original_binary_time,
                "Unchanged binary rebuilt")
        second = commit("second fixture revision")
        require(first != second, "Fixture commits did not change HEAD")
        # No configure between HEAD changes: exercise the real build dependency.
        verify(second)
        second_time = header.stat().st_mtime_ns
        verify(second)
        require(header.stat().st_mtime_ns == second_time,
                "Unchanged header rewritten")

        untracked = fixture / "untracked.txt"
        untracked.write_text("new file\n", encoding="utf-8")
        verify(second, dev=True)
        run(args.git, "-C", str(fixture), "add", "untracked.txt")
        verify(second, dev=True)
        run(args.git, "-C", str(fixture), "restore", "--staged", "untracked.txt")
        untracked.unlink()
        verify(second)

        tracked.write_text("edited\n", encoding="utf-8")
        verify(second, dev=True)
        run(args.git, "-C", str(fixture), "add", "tracked.txt")
        verify(second, dev=True)
        run(args.git, "-C", str(fixture), "restore", "--staged", "--worktree", "tracked.txt")
        verify(second)

        clean_time = header.stat().st_mtime_ns
        clean_binary_time = executable.stat().st_mtime_ns
        (fixture / "ignored.txt").write_text("ignored file\n", encoding="utf-8")
        verify(second)
        require(header.stat().st_mtime_ns == clean_time
                and executable.stat().st_mtime_ns == clean_binary_time,
                "Ignored file changed the build identity")
        print(f"HEAD refresh without reconfigure: {first.decode()} -> {second.decode()}")
        print("Development marker follows staged, unstaged and untracked changes; ignores ignored files")
        print("Unchanged identity preserves header and binary timestamps")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    manifest_parser = commands.add_parser("manifests")
    for option in ("mt", "product"):
        manifest_parser.add_argument(f"--{option}", required=True)
    manifest_parser.add_argument("--test", required=True, action="append")
    identity_parser = commands.add_parser("identity")
    identity_parser.add_argument("--toolset", default="")
    for option in ("source", "cmake", "git", "generator", "compiler", "config", "work"):
        identity_parser.add_argument(f"--{option}", required=True)
    args = parser.parse_args()
    if args.command == "manifests":
        manifests(args)
    else:
        identity(args)


if __name__ == "__main__":
    main()
