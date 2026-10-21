"""Run CTest and produce advisory native LLVM text/HTML reports."""

import argparse
import os
from pathlib import Path
import subprocess


def run(command, *, env=None, output=None):
    subprocess.run(command, env=env, stdout=output, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("ctest", "build", "profdata", "cov", "output"):
        parser.add_argument(f"--{option}", required=True)
    parser.add_argument("--object", required=True, action="append")
    parser.add_argument("--source", required=True, action="append")
    args = parser.parse_args()
    output = Path(args.output)
    raw = output / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    # Only this build's profiles are reset; never delete a computed directory tree.
    for profile in raw.glob("*.profraw"):
        profile.unlink()
    env = os.environ.copy()
    env["LLVM_PROFILE_FILE"] = str(raw / "%m-%p.profraw")
    tests = subprocess.run(
        [args.ctest, "--test-dir", args.build, "--output-on-failure"],
        env=env, check=False,
    )
    profiles = sorted(raw.glob("*.profraw"))
    if not profiles:
        raise RuntimeError("CTest produced no LLVM profiles")
    # Native input-list syntax also handles spaces without shell quoting.
    inputs = output / "profiles.txt"
    inputs.write_text("".join(f"1,{profile}\n" for profile in profiles), encoding="utf-8")
    merged = output / "coverage.profdata"
    run([args.profdata, "merge", "-sparse", f"-input-files={inputs}", "-o", str(merged)])
    objects = [args.object[0], *[f"-object={binary}" for binary in args.object[1:]]]
    options = [*objects, f"-instr-profile={merged}", *args.source]
    with (output / "report.txt").open("w", encoding="utf-8") as report:
        run([args.cov, "report", *options], output=report)
    with (output / "sources.txt").open("w", encoding="utf-8") as report:
        run([args.cov, "show", *options, "-show-branches=count"], output=report)
    run([args.cov, "show", *options, "-format=html", f"-output-dir={output / 'html'}",
         "-show-branches=count"])
    print((output / "report.txt").read_text(encoding="utf-8"))
    print(f"HTML coverage: {output / 'html/index.html'}")
    if tests.returncode:
        raise SystemExit(tests.returncode)


if __name__ == "__main__":
    main()
