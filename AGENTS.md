# Guidance for agents working in this repository

`BUILDING.md` describes the presets. This file covers how to launch builds
and tests on a Windows host so that failures surface as exit codes instead of
hangs.

## Launch builds and tests from a developer environment

Run CMake and CTest from a Visual Studio x64 developer environment, for example
after `VsDevCmd.bat -arch=x64 -host_arch=x64`. The resource compile step for
`version.rc` fails from a plain shell.

For the `clang-asan` preset, prepend LLVM's sanitizer runtime directory to
`PATH` after the developer script has run:

```text
<LLVM install>\lib\clang\<major>\lib\windows
```

The developer environment places Visual Studio's own
`clang_rt.asan_dynamic-x86_64.dll` on `PATH` first. Executables built with
LLVM's clang import symbols that DLL does not export, and every ASan test then
fails to load with exit code 0xC0000139 before `main` runs.

## Set the Windows error mode in the launcher

Start CMake, CTest, or any test executable from a parent process that has
first set the error mode. Child processes inherit it, so loader failures and
crashes return nonzero exit codes in seconds instead of blocking on a dialog
that nobody can click. A failure that occurs before `main` runs cannot be
suppressed from inside the product, so this has to happen in the launcher.

```python
import ctypes
import subprocess
import sys

SEM_FAILCRITICALERRORS = 0x0001
SEM_NOGPFAULTERRORBOX = 0x0002

kernel32 = ctypes.WinDLL("kernel32")
kernel32.SetErrorMode(kernel32.GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)
sys.exit(subprocess.call(sys.argv[1:]))
```

Save that as a script outside the repository and run commands through it, for
example `python -I run_quiet.py cmd /c build-and-test.cmd`, where the batch
file calls `VsDevCmd.bat`, adjusts `PATH`, and then invokes `cmake --workflow`
or `ctest --preset`.

## Escape Windows switches in Git Bash

Git Bash rewrites an argument such as `/c` into the path `C:/`. A command like
`cmd /c script.cmd` becomes `cmd C:/ script.cmd`, which starts an interactive
`cmd` that waits on input forever. Write Windows switches as `//c`, `//PID`,
`//T`, and so on, or run the command from PowerShell instead.
