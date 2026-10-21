# Console stream regression check

`standard_streams_console` is a CTest case using
`console_integration.py` and `np_stream_console_driver.exe --console-probe`.
Run it after building the driver, for example from `src`:

```powershell
cmake --build --preset clang-debug --target np_stream_console_driver
ctest --preset clang-debug -R '^standard_streams_console$' --output-on-failure
```

The test requires Windows with ConPTY (Windows 10 version 1809 or newer),
Python 3, and the project's configured build toolchain. A missing or unusable
pseudoconsole is a test failure, not a skip. The product requires Windows 10
version 1903 or newer. To run the full test suite,
use `cmake --build --preset clang-debug` and
`ctest --preset clang-debug --output-on-failure`; substitute
`clang-release`, `vs2022-debug`, or `vs2022-release` as needed.

The Python host creates a pseudoconsole, starts the test driver inside it, and
captures its terminal output. The test-only driver opens `CONIN$` and
`CONOUT$` for its borrowed CRT streams, checks each underlying handle with
`GetConsoleMode`, sets **that isolated console's** input and output code
pages to 65001, and disables input echo. The invoking terminal's code pages
and persistent settings are untouched. A `READY` marker means setup is
complete; only then does the host send UTF-8 `café`, Enter, Ctrl+Z, and
Enter through the pseudoconsole input channel.
Run `--console-probe` only through this harness: running it directly in an
existing terminal changes that terminal's code pages and disables input echo.

The driver asserts the adapter receives `café` as the first valid UTF-8
record and that two later reads both return EOF. It calls `writeStdout` and
`writeStderr` separately with text containing two-, three-, and four-byte
UTF-8 characters. The host decodes the ConPTY output channel as UTF-8 and
compares the visible output markers. Input echo is disabled, and the `IN:`,
`OUT:`, `ERR:`, and `EOF2` markers are written only by the adapter.
This proves that the console received the expected text through real console
handles on this host. It cannot distinguish stdout from stderr by terminal
channel because a Windows console combines them; the driver calls the two
methods separately and checks both borrowed handles. The existing file tests
check exact stream bytes.

The ConPTY check can pass on hosts that reassemble byte-at-a-time writes.
`np_stream_tests` also contains a message-mode named-pipe test: with an
unbuffered CRT stream, the messages must concatenate to each tested LF-terminated
record without splitting a UTF-8 code point at a message boundary. The cases
include both odd and even byte lengths. The original per-byte `fputc` loop
fails that test. This stream models write boundaries; the console test verifies
the actual console path. For records larger than the UCRT's temporary console
buffer, even `fwrite` can split a code point at a buffer boundary, so that
long-record behavior is not covered by this check.

Startup, execution, and output-drain waits are bounded. On failure, the host
terminates the child, closes the pseudoconsole, and prints the failing API,
driver exit reason, or observed output. No human input is required.
