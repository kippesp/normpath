"""Process byte fixtures and bounded real-handle failures for the np CLI."""

import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import time
import unittest


PRODUCT = None
VERSION = None
ADDRESS_SANITIZER = False
HELP = (
    "np - Path Normalizer for Windows\n\n"
    "Converts path spellings to Windows form. Run from PowerShell, np uses\n"
    "forward slashes and PowerShell quoting; otherwise, including cmd.exe,\n"
    "it uses backslashes and cmd.exe quoting.\n\n"
    "USAGE: np [options] [PATH...]\n\n"
    "ARGUMENTS:\n"
    "  PATH...    Paths to convert; - reads one path per line from stdin\n\n"
    "OPTIONS:\n"
    "  -h, --help     Print help\n"
    "      --version  Print version and build information\n"
).encode()


def diagnostic(location, reason):
    return f"np: error: {location}: {reason}\n".encode()


def environment(home=None):
    result = dict(os.environ)
    for key in list(result):
        if key.upper() == "USERPROFILE":
            del result[key]
    if home is not None:
        result["USERPROFILE"] = home
    return result


def checked(ok, action):
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error(), action)


def windows_api():
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenThread.restype = wintypes.HANDLE
    api.CancelSynchronousIo.argtypes = [wintypes.HANDLE]
    api.CancelSynchronousIo.restype = wintypes.BOOL
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.CloseHandle.restype = wintypes.BOOL
    return api


class CliTests(unittest.TestCase):
    def assert_process(self, args, stdout=b"", stderr=b"", status=0,
                       data=b"", home=None, cwd=None):
        result = subprocess.run(
            [str(PRODUCT), *args], input=data, capture_output=True,
            env=environment(home), cwd=cwd, timeout=10)
        self.assertEqual((result.returncode, result.stdout, result.stderr),
                         (status, stdout, stderr), args)

    def test_smb_arguments_and_stdin(self):
        self.assert_process(
            ["smb://192.168.1.86/share", "SmB://server/my share/docs"],
            b'\\\\192.168.1.86\\share\n"\\\\server\\my share\\docs"\n')
        self.assert_process(
            ["-"], b"\\\\server\\share\\docs\nC:\\100%\\x\n",
            diagnostic("stdin line 2", "percent signs are unsupported in SMB URLs"),
            status=1,
            data=b"smb://server/share/docs\nsmb://server/share/a%20b\nC:/100%/x\n")

    def test_whole_invocation_and_options(self):
        self.assert_process([], HELP)
        self.assert_process(["--"], stderr=b"np: error: No path provided; see --help\n",
                            status=2)
        for option in ("-a", "-p", "-q", "-u", "-f", "--escape", "-x",
                       "-hh", "-hV", "--Help", "-\ninvalid"):
            for args in ([option], ["a/b", "-", "~/x", "--help", option],
                         ["--version", option], [option, "--help"]):
                with self.subTest(args=args):
                    self.assert_process(
                        args, stderr=b"np: error: Unknown option; see --help\n", status=2)
        self.assert_process(["--", "-h", "--version", "--", "./-", ".\\-"],
                            b"-h\n--version\n--\n-\n-\n")
        self.assert_process(["/users/me", "/c/x"], b"\\users\\me\nC:\\x\n")
        self.assert_process(["--", "-", "./-", "-"], b"x\\y\n-\n", data=b"x/y\n")

    def test_version_resource_matches_project_version(self):
        api = ctypes.WinDLL("version", use_last_error=True)
        api.GetFileVersionInfoSizeW.argtypes = (wintypes.LPCWSTR, wintypes.LPDWORD)
        api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
        api.GetFileVersionInfoW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD,
                                            wintypes.DWORD, wintypes.LPVOID)
        api.GetFileVersionInfoW.restype = wintypes.BOOL
        api.VerQueryValueW.argtypes = (wintypes.LPCVOID, wintypes.LPCWSTR,
                                       ctypes.POINTER(ctypes.c_void_p),
                                       ctypes.POINTER(wintypes.UINT))
        api.VerQueryValueW.restype = wintypes.BOOL
        path = str(PRODUCT)
        size = api.GetFileVersionInfoSizeW(path, None)
        self.assertGreater(size, 0, "np.exe carries no VERSIONINFO resource")
        data = ctypes.create_string_buffer(size)
        self.assertTrue(api.GetFileVersionInfoW(path, 0, size, data))
        value = ctypes.c_void_p()
        length = wintypes.UINT()
        self.assertTrue(api.VerQueryValueW(
            data, "\\StringFileInfo\\040904B0\\ProductVersion",
            ctypes.byref(value), ctypes.byref(length)))
        product_version = ctypes.wstring_at(value.value, length.value).rstrip("\0")
        self.assertEqual(product_version, VERSION)
        self.assertTrue(api.VerQueryValueW(
            data, "\\", ctypes.byref(value), ctypes.byref(length)))
        # VS_FIXEDFILEINFO: dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS, ...
        fixed = (ctypes.c_uint32 * 4).from_address(value.value)
        numeric = (fixed[2] >> 16, fixed[2] & 0xFFFF, fixed[3] >> 16, fixed[3] & 0xFFFF)
        expected = tuple(int(part) for part in VERSION.split(".")) + (0,)
        self.assertEqual(numeric, expected)

    def test_information_precedence_without_reading_stdin(self):
        version = subprocess.run([str(PRODUCT), "--version"], capture_output=True,
                                 timeout=10)
        self.assertEqual((version.returncode, version.stderr), (0, b""))
        self.assertRegex(version.stdout,
                         rb"\Anp - Path Normalizer for Windows \(ver\. "
                         + re.escape(VERSION).encode()
                         + rb" - [0-9a-f]{7}(-dev)?\)\n"
                         rb"https://github\.com/kippesp/normpath\n\Z")
        cases = [
            ([], HELP), (["-h"], HELP), (["--help"], HELP),
            (["--help", "--version", "-", "~/x", "C:bad"], HELP),
            (["--version", "-h", "--version", "-"], HELP),
            (["-", "~/x", "--version"], version.stdout),
            (["--version", "--version"], version.stdout),
            (["--"], b""), (["a/b", "-", "--bad", "--help"], b""),
        ]
        for args, expected in cases:
            with self.subTest(args=args), subprocess.Popen(
                    [str(PRODUCT), *args], stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    env=environment("C:bad")) as child:
                # Keep the writer OPEN: a stdin read would block and time out.
                child.wait(timeout=5)
                child.stdin.close()
                self.assertEqual(child.stdout.read(), expected)
                error = child.stderr.read()
                self.assertEqual(child.returncode, 0 if expected else 2)
                self.assertEqual(error, b"" if expected else (
                    b"np: error: No path provided; see --help\n" if args == ["--"] else
                    b"np: error: Unknown option; see --help\n"))

    def test_ordering_and_rejection_continuation(self):
        self.assert_process(["a/b", "-", "c/d", "-"],
                            b"a\\b\ne\\f\ng\\h\nc\\d\n", data=b"e/f\ng/h\n")
        self.assert_process(["a/b", "C:bad", "c/d"], b"a\\b\nc\\d\n",
                            diagnostic("operand 2", "drive-relative path"), 1)
        self.assert_process(["a/b", "-", "", "last/x", "-"],
                            b"a\\b\ne\\f\nlast\\x\n",
                            diagnostic("stdin line 2", "empty path") +
                            diagnostic("stdin line 3", "ill-formed UTF-8") +
                            diagnostic("operand 3", "empty path"),
                            1, data=b"e/f\n\n\xff\n")
        self.assert_process(["--", "a/b", "C:bad", "c/d"], b"a\\b\nc\\d\n",
                            diagnostic("operand 2", "drive-relative path"), 1)
        self.assert_process(["-"], b"", b"", 0)
        self.assert_process(["-", "-"], b"", b"", 0)

    def test_sentence_period_in_operands_and_stdin(self):
        paths = [r"d:\dir1\dir2\dir3.", "src/main.cpp.", "~/dir.",
                 "C:/my projects/app.", "smb://server/share/dir.", ".", ".."]
        expected = [r"D:\dir1\dir2\dir3", r"src\main.cpp", r"C:\Users\me\dir",
                    '"C:\\my projects\\app"', r"\\server\share\dir", ".", ".."]
        output = ("\n".join(expected) + "\n").encode()
        self.assert_process(paths, output, home=r"C:\Users\me")
        self.assert_process(["-"], output, home=r"C:\Users\me",
                            data=("\n".join(paths) + "\n").encode())

    def test_record_fixtures(self):
        paths = ["/c:", "/C:/a/../../x", "/C:foo/a/../../x", "//C:/x"]
        output = b"C:\\\nC:\\x\n\\\\C:\\x\n"
        self.assert_process(paths, output,
                            diagnostic("operand 3", "drive-relative path"), 1)
        self.assert_process(["-"], output,
                            diagnostic("stdin line 3", "drive-relative path"),
                            1, data=("\n".join(paths) + "\n").encode())
        for delimiter in (b"\n", b"\r\n"):
            for final in (b"", delimiter):
                self.assert_process(["-"], b"a\\b\nc\\d\n",
                                    data=delimiter.join([b"a/b", b"c/d"]) + final)
        cases = [
            (b"a/b\n\nc/d\n", b"a\\b\nc\\d\n",
             diagnostic("stdin line 2", "empty path")),
            (b"a/b\n\xff\nc/d\n", b"a\\b\nc\\d\n",
             diagnostic("stdin line 2", "ill-formed UTF-8")),
            (b"a/b\n\xf0\x9f", b"a\\b\n",
             diagnostic("stdin line 2", "ill-formed UTF-8")),
            (b"\n\n", b"", diagnostic("stdin line 1", "empty path") +
             diagnostic("stdin line 2", "empty path")),
            (b"a/b\r", b"", diagnostic("stdin line 1", "prohibited character in filename")),
            (b"a\r\r\n", b"", diagnostic("stdin line 1", "prohibited character in filename")),
            (b"\x1a\na/b\n", b"a\\b\n",
             diagnostic("stdin line 1", "prohibited character in filename")),
            (b"a\x00b\nlast\n", b"last\n",
             diagnostic("stdin line 1", "prohibited character in filename")),
            (b'"a/b"\nlast\n', b"last\n",
             diagnostic("stdin line 1", "prohibited character in filename")),
        ]
        for data, output, error in cases:
            with self.subTest(data=data):
                self.assert_process(["-"], output, error, 1, data=data)
        for data in (b"\xef\xbb\xbfa/b\n", b"\xef\xbb\xbfa/b"):
            self.assert_process(["-"], b"a\\b\n", data=data)
        self.assert_process(["-"], data=b"\xef\xbb\xbf")
        self.assert_process(["-"], "\ufeffx\n".encode(),
                            data=b"\xef\xbb\xbf\xef\xbb\xbfx\n")
        self.assert_process(["-"], "a\n\ufeffx\n".encode(),
                            data="a\n\ufeffx\n".encode())

    def test_home_and_unicode_bytes(self):
        for home in (None, "", "C:bad"):
            self.assert_process(["a/b", "~user/x", "$HOME/x", "%USERPROFILE%/x"],
                                b"a\\b\n~user\\x\n$HOME\\x\n%USERPROFILE%\\x\n", home=home)
        for home in (None, ""):
            self.assert_process(["~/x", "a/b"], b"a\\b\n",
                                diagnostic("operand 1", "home value is absent or empty"),
                                1, home=home)
        self.assert_process(["~/foo", "a/b"], b"a\\b\n",
                            diagnostic("operand 1", "drive-relative path"), 1, home="C:Users")
        for home in ("C:\\Users\\me", "C:\\Users\\Jo Doe", "C:\\Users\\Зоя漢😀"):
            paths = ["~", "~/", "~\\src", "~/Documents", "~/x"]
            expected = [home, home, home + "\\src", home + "\\Documents", home + "\\x"]
            output = "".join(('"' + p + '"' if " " in p else p) + "\n"
                             for p in expected).encode()
            self.assert_process(paths, output, home=home)
            self.assert_process(["-"], output, home=home,
                                data=("\n".join(paths) + "\n").encode())
        self.assert_process(["~/x"], b"C:\\Users\\me\\x\n", home="C:\\Users\\me\\")
        paths = ["C:/Users/漢字/проект", "C:/cafe\u0301", "C:\\foo", " leading/x",
                 "C:/my projects/app", "C:/a(b)", "a&b", "a^b", "C:/100%/x", "C:/hi!"]
        expected = ['C:\\Users\\漢字\\проект', 'C:\\cafe\u0301', 'C:\\foo', '" leading\\x"',
                    '"C:\\my projects\\app"', '"C:\\a(b)"', '"a&b"', '"a^b"',
                    'C:\\100%\\x', 'C:\\hi!']
        output = ("\n".join(expected) + "\n").encode()
        self.assert_process(paths, output)
        self.assert_process(["-"], output, data=("\n".join(paths) + "\n").encode())
        with tempfile.TemporaryDirectory() as directory:
            self.assert_process(["relative/../x", "/logs/x"], b"x\n\\logs\\x\n",
                                cwd=directory)

    def test_diagnostics_have_no_input_control_bytes(self):
        self.assert_process(["a\nb", "a\rb", "a\tb", "a/b"],
                            b"a\\b\n", b"".join(
                                diagnostic(f"operand {n}", "prohibited character in filename")
                                for n in range(1, 4)), 1)

    def test_closed_stdout_and_stderr(self):
        for args in (["a/b", "-", "later"], ["C:bad", "a/b", "-"],
                     [], ["--help", "-"], ["--version", "-"]):
            with self.subTest(args=args):
                read, write = os.pipe()
                os.close(read)
                try:
                    with subprocess.Popen([str(PRODUCT), *args], stdin=subprocess.PIPE,
                                          stdout=write, stderr=subprocess.PIPE) as child:
                        child.wait(timeout=5)  # An open stdin also detects unwanted traversal.
                        error = child.stderr.read()
                        self.assertEqual(child.returncode, 2)
                        prior = (diagnostic("operand 1", "drive-relative path")
                                 if args and args[0] == "C:bad" else b"")
                        self.assertRegex(error[len(prior):],
                                         rb"\Anp: error: standard stream (write|flush) failed\n\Z")
                        self.assertTrue(error.startswith(prior))
                finally:
                    os.close(write)
        for args in (["first", "C:bad", "later", "-"], ["--"], ["--bad"],
                     ["first", "-", "later"]):
            read, write = os.pipe()
            os.close(read)
            try:
                with subprocess.Popen([str(PRODUCT), *args], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, stderr=write) as child:
                    if "-" in args and "C:bad" not in args:
                        child.stdin.write(b"\xff\n")
                        child.stdin.flush()
                    child.wait(timeout=5)
                    self.assertEqual(child.returncode, 2)
                    self.assertEqual(child.stdout.read(),
                                     b"first\n" if args and args[0] == "first" else b"")
            finally:
                os.close(write)

    def test_both_output_streams_failed(self):
        output_read, output_write = os.pipe()
        error_read, error_write = os.pipe()
        os.close(output_read)
        os.close(error_read)
        try:
            with subprocess.Popen([str(PRODUCT), "first", "-", "later"],
                                  stdin=subprocess.PIPE, stdout=output_write,
                                  stderr=error_write) as child:
                child.wait(timeout=5)
                self.assertEqual(child.returncode, 2)
        finally:
            os.close(output_write)
            os.close(error_write)

    def test_stdout_failure_mid_record(self):
        # Closing the reader after a delivered first record retains that result,
        # and interrupts a record larger than the pipe's buffer.
        with subprocess.Popen([str(PRODUCT), "-", "later"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
            child.stdin.write(b"first\n")
            child.stdin.flush()
            self.assertEqual(child.stdout.readline(), b"first\n")
            child.stdin.write(b"x" * 65536 + b"\n")
            child.stdin.flush()
            # Avoid buffered read-ahead draining the record before we close the reader.
            prefix = os.read(child.stdout.fileno(), 128)
            self.assertTrue(prefix)
            self.assertEqual(prefix, b"x" * len(prefix))
            child.stdout.close()
            child.wait(timeout=5)
            self.assertEqual(child.returncode, 2)
            self.assertRegex(child.stderr.read(),
                             rb"\Anp: error: standard stream (write|flush) failed\n\Z")

    def test_redirected_files(self):
        with tempfile.TemporaryFile() as source, tempfile.TemporaryFile() as output, \
                tempfile.TemporaryFile() as error:
            source.write("café/x\r\n\n漢字/проект".encode())
            source.seek(0)
            result = subprocess.run([str(PRODUCT), "-", "last/x", "-"], stdin=source,
                                    stdout=output, stderr=error, timeout=10)
            output.seek(0)
            error.seek(0)
            self.assertEqual((result.returncode, output.read(), error.read()),
                             (1, "café\\x\n漢字\\проект\nlast\\x\n".encode(),
                              diagnostic("stdin line 2", "empty path")))

    def test_read_failure_after_partial_record(self):
        class ThreadEntry(ctypes.Structure):
            _fields_ = [(name, wintypes.DWORD) for name in
                        ("size", "usage", "id", "owner")] + [
                            ("base_priority", wintypes.LONG), ("delta_priority", wintypes.LONG),
                            ("flags", wintypes.DWORD)]

        api = windows_api()
        api.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        for name in ("Thread32First", "Thread32Next"):
            getattr(api, name).argtypes = [wintypes.HANDLE, ctypes.POINTER(ThreadEntry)]
            getattr(api, name).restype = wintypes.BOOL
        with subprocess.Popen([str(PRODUCT), "-", "later"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
            child.stdin.write(b"first\npartial")
            child.stdin.flush()
            self.assertEqual(child.stdout.readline(), b"first\n")
            snapshot = api.CreateToolhelp32Snapshot(4, 0)  # TH32CS_SNAPTHREAD
            self.assertNotEqual(snapshot, ctypes.c_void_p(-1).value)
            thread = None
            try:
                entry = ThreadEntry()
                entry.size = ctypes.sizeof(entry)
                more = api.Thread32First(snapshot, ctypes.byref(entry))
                while more:
                    if entry.owner == child.pid:
                        thread = api.OpenThread(1, False, entry.id)  # THREAD_TERMINATE
                        checked(thread, "OpenThread")
                        break
                    more = api.Thread32Next(snapshot, ctypes.byref(entry))
                self.assertIsNotNone(thread)
                deadline = time.monotonic() + 5
                while not api.CancelSynchronousIo(thread):
                    self.assertEqual(ctypes.get_last_error(), 1168)  # ERROR_NOT_FOUND
                    self.assertLess(time.monotonic(), deadline, "stdin read never blocked")
                    threading.Event().wait(0.01)
                child.wait(timeout=5)
                self.assertEqual((child.returncode, child.stdout.read(), child.stderr.read()),
                                 (2, b"", b"np: error: standard input read failed\n"))
            finally:
                if thread:
                    api.CloseHandle(thread)
                api.CloseHandle(snapshot)

    def test_resource_exhaustion_on_actual_product(self):
        if ADDRESS_SANITIZER:
            self.skipTest("32 MiB job cap prevents ASan runtime bookkeeping allocation; "
                          "resource exhaustion is checked in ordinary and UBSan builds")
        # A per-process job memory limit forces std::string allocation failure
        # while the actual product reads one large record; no product test hook.
        class IoCounters(ctypes.Structure):
            _fields_ = [(name, ctypes.c_ulonglong) for name in
                        ("read_ops", "write_ops", "other_ops", "read", "write", "other")]

        class BasicLimits(ctypes.Structure):
            _fields_ = [
                ("process_time", ctypes.c_longlong), ("job_time", ctypes.c_longlong),
                ("flags", wintypes.DWORD), ("min_working_set", ctypes.c_size_t),
                ("max_working_set", ctypes.c_size_t), ("active", wintypes.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                ("scheduling", wintypes.DWORD),
            ]

        class ExtendedLimits(ctypes.Structure):
            _fields_ = [
                ("basic", BasicLimits), ("io", IoCounters),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t),
            ]

        api = windows_api()
        api.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        api.CreateJobObjectW.restype = wintypes.HANDLE
        api.SetInformationJobObject.argtypes = [
            wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        api.SetInformationJobObject.restype = wintypes.BOOL
        api.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        api.AssignProcessToJobObject.restype = wintypes.BOOL
        job = api.CreateJobObjectW(None, None)
        checked(job, "CreateJobObjectW")
        try:
            limits = ExtendedLimits()
            limits.basic.flags = 0x100  # JOB_OBJECT_LIMIT_PROCESS_MEMORY
            limits.process_memory = 32 * 1024 * 1024
            checked(api.SetInformationJobObject(job, 9, ctypes.byref(limits),
                                               ctypes.sizeof(limits)), "SetInformationJobObject")
            with subprocess.Popen([str(PRODUCT), "-", "later"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
                # Wait for startup and a completed result before limiting memory.
                child.stdin.write(b"first\n")
                child.stdin.flush()
                self.assertEqual(child.stdout.readline(), b"first\n")
                checked(api.AssignProcessToJobObject(job, int(child._handle)),
                        "AssignProcessToJobObject")
                try:
                    for _ in range(64):
                        child.stdin.write(b"x" * (1024 * 1024))
                    child.stdin.flush()
                except BrokenPipeError:
                    pass
                child.wait(timeout=10)
                self.assertEqual((child.returncode, child.stdout.read(), child.stderr.read()),
                                 (2, b"", b"np: error: unexpected failure\n"))
        finally:
            api.CloseHandle(job)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", type=Path, required=True)
    parser.add_argument("--version-string", required=True)
    parser.add_argument("--address-sanitizer", choices=("on", "off"), default="off")
    args = parser.parse_args()
    PRODUCT = args.product.resolve(strict=True)
    VERSION = args.version_string
    ADDRESS_SANITIZER = args.address_sanitizer == "on"
    unittest.main(argv=[__file__])
