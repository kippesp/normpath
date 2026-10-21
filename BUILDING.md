# Building and Testing np

Use Visual Studio 2022 or 2026 to build, test, and package np for Windows x64.
Run commands from `src/`; paths in this document are relative to the
repository root. Every `vs2022` preset has a `vs2026` counterpart; substitute
throughout.

## Prerequisites

- Windows 10 version 1903 or newer, x64.
- Visual Studio 2022 with the v143 C++ toolset, or Visual Studio 2026 with the
  v145 C++ toolset, and a Windows SDK.
- CMake 4.2.5 or newer, Git, and Python 3 available on `PATH`.
- Network access on the first configure so CMake can fetch Catch2.
- An x64 developer shell with the Windows SDK's `mt.exe` on `PATH`, for the
  manifest tests.
- A Git checkout, so `--version` includes the commit.

For unattended runs, use the [error-mode
launcher](AGENTS.md#set-the-windows-error-mode-in-the-launcher) to prevent
crash dialogs from blocking tests.

## Build and Test

Each workflow configures, builds with warnings as errors, and runs the full
test suite, stopping if a step fails:

```text
cmake --workflow --preset vs2022-release
```

| Task                    | Workflow preset  | Output                                         |
| ----------------------- | ---------------- | ---------------------------------------------- |
| Release build and tests | `vs2022-release` | `src\build\vs2022\Release\np.exe`              |
| Debug build and tests   | `vs2022-debug`   | `src\build\vs2022\Debug\np.exe`                |
| Release, tests, and zip | `vs2022-package` | `src\build\vs2022\packages\` zip and `.sha256` |

The Release executable uses the static CRT with release hardening enabled.
Debug and Release share one build directory per Visual Studio version.

To configure, build, and test separately:

```text
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
```

List the tests with `ctest --preset vs2022-debug -N`; run a subset with `-R`.
The console tests are described in the
[stream console test guide](src/tests/CONSOLE-TEST.md).

## Additional Workflow Presets

These workflows require additional tools on `PATH`. Clang builds still need
the MSVC and Windows SDK components. For `clang-asan`, follow the
[runtime PATH instructions](AGENTS.md#launch-builds-and-tests-from-a-developer-environment).
List workflows with `cmake --list-presets=workflow`; definitions are in
[`src/CMakePresets.json`](src/CMakePresets.json).

- `clang-debug`, `clang-release`: Build and test with a second compiler.
  GNU-style `clang++`, Ninja, and `lld-link`; full test suite.
- `clang-tidy`: Static analysis. Enabled findings fail the build; also runs
  the full test suite.
- `format-check`: Check C++ formatting. Requires `clang-format`; never
  rewrites files and runs no tests.
- `vs2022-asan`, `clang-asan`: AddressSanitizer checks. Debug workflows;
  configure probes runtime compatibility.
- `clang-ubsan`: UndefinedBehaviorSanitizer checks. Release workflow to
  accommodate the installed runtime's CRT requirements.
- `clang-coverage`: Line and branch coverage. Requires `llvm-profdata` and
  `llvm-cov`; prints the report location. Test and collection errors fail the
  workflow; uncovered lines do not.

Run `clang-tidy` and `format-check` before committing; the primary builds do
not run them. A failed sanitizer probe stops configuration; see
`CMakeFiles\CMakeConfigureLog.yaml` in the build directory.

## Build Outputs and Clean Rebuilds

`<preset>` is the configure preset, such as `vs2022`; `<Config>` is `Debug`
or `Release`.

| Path                                       | Contents                           |
| ------------------------------------------ | ---------------------------------- |
| `src\build\<preset>\np.exe`                | Ninja executable                   |
| `src\build\<preset>\<Config>\np.exe`       | Visual Studio executable           |
| `src\build\clang-coverage\coverage\Debug\` | `report.txt` and `html\index.html` |

For a clean rebuild, delete `src\build\<preset>\` and rerun its workflow.
