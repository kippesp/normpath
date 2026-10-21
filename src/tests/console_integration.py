"""Exercise the stream adapter or np CLI in an isolated UTF-8 pseudoconsole."""

import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import re
import subprocess
import sys
import threading

EXTENDED_STARTUPINFO_PRESENT = 0x00080000
PSEUDOCONSOLE_ATTRIBUTE = 0x00020016
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258
ERROR_BROKEN_PIPE = 109

EXIT_REASONS = {
    12: "interactive read failed",
    13: "the first record was not UTF-8 café on line 1",
    14: "input echo write failed",
    15: "non-ASCII stdout write failed",
    16: "non-ASCII stderr write failed",
    17: "Ctrl+Z/Enter did not produce persistent EOF",
    18: "EOF marker write failed",
    19: "driver exception",
    20: "isolated console UTF-8 code-page setup failed",
    21: "disabling input echo failed",
    22: "ready marker write failed",
    23: "opening CONIN$ for the borrowed CRT input failed",
    24: "opening CONOUT$ for the borrowed CRT stdout failed",
    25: "opening CONOUT$ for the borrowed CRT stderr failed",
    26: "np CLI child failed",
    27: "np CLI changed the console code pages",
    28: "CLI completion marker write failed",
    30: "borrowed CRT stdin was not a console handle",
    31: "borrowed CRT stdout was not a console handle",
    32: "borrowed CRT stderr was not a console handle",
}


class Coord(ctypes.Structure):
    _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short)]


class StartupInfo(ctypes.Structure):
    _fields_ = [
        ("size", wintypes.DWORD), ("reserved", wintypes.LPWSTR),
        ("desktop", wintypes.LPWSTR), ("title", wintypes.LPWSTR),
        ("x", wintypes.DWORD), ("y", wintypes.DWORD),
        ("x_size", wintypes.DWORD), ("y_size", wintypes.DWORD),
        ("x_count_chars", wintypes.DWORD), ("y_count_chars", wintypes.DWORD),
        ("fill_attribute", wintypes.DWORD), ("flags", wintypes.DWORD),
        ("show_window", wintypes.WORD), ("reserved2_size", wintypes.WORD),
        ("reserved2", ctypes.POINTER(ctypes.c_byte)),
        ("std_input", wintypes.HANDLE), ("std_output", wintypes.HANDLE),
        ("std_error", wintypes.HANDLE),
    ]


class StartupInfoEx(ctypes.Structure):
    _fields_ = [("startup", StartupInfo), ("attributes", ctypes.c_void_p)]


class ProcessInfo(ctypes.Structure):
    _fields_ = [
        ("process", wintypes.HANDLE), ("thread", wintypes.HANDLE),
        ("process_id", wintypes.DWORD), ("thread_id", wintypes.DWORD),
    ]


def windows_api():
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreatePipe.argtypes = [
        ctypes.POINTER(wintypes.HANDLE), ctypes.POINTER(wintypes.HANDLE),
        ctypes.c_void_p, wintypes.DWORD,
    ]
    api.CreatePipe.restype = wintypes.BOOL
    api.CreatePseudoConsole.argtypes = [
        Coord, wintypes.HANDLE, wintypes.HANDLE, wintypes.DWORD,
        ctypes.POINTER(wintypes.HANDLE),
    ]
    api.CreatePseudoConsole.restype = ctypes.c_long
    api.ClosePseudoConsole.argtypes = [wintypes.HANDLE]
    api.InitializeProcThreadAttributeList.argtypes = [
        ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    api.InitializeProcThreadAttributeList.restype = wintypes.BOOL
    api.UpdateProcThreadAttribute.argtypes = [
        ctypes.c_void_p, wintypes.DWORD, ctypes.c_size_t, ctypes.c_void_p,
        ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p,
    ]
    api.UpdateProcThreadAttribute.restype = wintypes.BOOL
    api.DeleteProcThreadAttributeList.argtypes = [ctypes.c_void_p]
    api.CreateProcessW.argtypes = [
        wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
        wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
        ctypes.POINTER(StartupInfoEx), ctypes.POINTER(ProcessInfo),
    ]
    api.CreateProcessW.restype = wintypes.BOOL
    api.WriteFile.argtypes = [
        wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
    ]
    api.WriteFile.restype = wintypes.BOOL
    api.ReadFile.argtypes = [
        wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
    ]
    api.ReadFile.restype = wintypes.BOOL
    api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    api.WaitForSingleObject.restype = wintypes.DWORD
    api.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    api.GetExitCodeProcess.restype = wintypes.BOOL
    api.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    api.TerminateProcess.restype = wintypes.BOOL
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.CloseHandle.restype = wintypes.BOOL
    return api


def checked(ok, action):
    if not ok:
        raise RuntimeError(f"{action} failed: Win32 error {ctypes.get_last_error()}")


def create_pipe(api):
    read, write = wintypes.HANDLE(), wintypes.HANDLE()
    checked(api.CreatePipe(ctypes.byref(read), ctypes.byref(write), None, 0),
            "CreatePipe")
    return read, write


def write_all(api, handle, payload):
    buffer = ctypes.create_string_buffer(payload)
    count = wintypes.DWORD()
    checked(api.WriteFile(handle, buffer, len(payload), ctypes.byref(count), None),
            "WriteFile(pseudoconsole input)")
    if count.value != len(payload):
        raise RuntimeError(f"pseudoconsole input wrote {count.value}/{len(payload)} bytes")


def visible_bytes(payload):
    # ConPTY emits VT control sequences; compare the resulting visible text.
    payload = re.sub(rb"\x1b\[[0-9;]*H", b"\n", payload)
    payload = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", payload)
    payload = re.sub(rb"\x1b\][^\x07]*(?:\x07|\x1b\\)", b"", payload)
    return payload


def terminal_lines(payload):
    return [line for line in visible_bytes(payload).decode("utf-8").replace("\r", "").split("\n")
            if line]


def run(driver, product=None):
    if os.name != "nt":
        raise RuntimeError("Windows ConPTY is required; console check unavailable")
    api = windows_api()
    handles = []
    console = wintypes.HANDLE()
    process = ProcessInfo()
    attributes_initialized = False
    attributes = None
    reader = None
    read_done, ready = threading.Event(), threading.Event()
    chunks, reader_errors = [], []

    def close_handle(handle):
        if handle:
            api.CloseHandle(handle)
            handles.remove(handle)

    def read_output(handle):
        try:
            while True:
                buffer = ctypes.create_string_buffer(4096)
                count = wintypes.DWORD()
                if not api.ReadFile(handle, buffer, len(buffer), ctypes.byref(count), None):
                    error = ctypes.get_last_error()
                    if error != ERROR_BROKEN_PIPE:
                        reader_errors.append(f"ReadFile(output) failed: Win32 error {error}")
                    break
                if count.value == 0:
                    break
                chunks.append(buffer.raw[:count.value])
                if not ready.is_set() and b"READY" in visible_bytes(b"".join(chunks)):
                    ready.set()
        finally:
            read_done.set()

    try:
        input_read, input_write = create_pipe(api)
        output_read, output_write = create_pipe(api)
        handles.extend((input_read, input_write, output_read, output_write))
        result = api.CreatePseudoConsole(Coord(120, 30), input_read, output_write,
                                         0, ctypes.byref(console))
        if result != 0:
            raise RuntimeError(
                f"CreatePseudoConsole unavailable/failed: HRESULT 0x{result & 0xffffffff:08x}")

        size = ctypes.c_size_t()
        api.InitializeProcThreadAttributeList(None, 1, 0, ctypes.byref(size))
        attributes = ctypes.create_string_buffer(size.value)
        checked(api.InitializeProcThreadAttributeList(attributes, 1, 0,
                                                      ctypes.byref(size)),
                "InitializeProcThreadAttributeList")
        attributes_initialized = True
        checked(api.UpdateProcThreadAttribute(
            attributes, 0, PSEUDOCONSOLE_ATTRIBUTE, console,
            ctypes.sizeof(console), None, None),
            "UpdateProcThreadAttribute(PSEUDOCONSOLE)")
        startup = StartupInfoEx()
        startup.startup.size = ctypes.sizeof(startup)
        startup.attributes = ctypes.cast(attributes, ctypes.c_void_p)
        arguments = ([str(driver), "--cli-console", str(product)] if product else
                     [str(driver), "--console-probe"])
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline(arguments))
        checked(api.CreateProcessW(None, command, None, None, False,
                                   EXTENDED_STARTUPINFO_PRESENT, None,
                                   str(driver.parent), ctypes.byref(startup),
                                   ctypes.byref(process)),
                "CreateProcessW(pseudoconsole)")
        close_handle(input_read)
        close_handle(output_write)

        reader = threading.Thread(target=read_output, args=(output_read,), daemon=True)
        reader.start()
        if not ready.wait(5):
            status = api.WaitForSingleObject(process.process, 0)
            code = wintypes.DWORD()
            if status == WAIT_OBJECT_0:
                api.GetExitCodeProcess(process.process, ctypes.byref(code))
            raise RuntimeError(
                f"driver did not announce READY in 5 seconds; wait={status}, "
                f"exit={code.value}, output={ascii(b''.join(chunks))}")
        # These bytes enter the console terminal channel, not redirected stdin.
        write_all(api, input_write, "café\r".encode("utf-8"))
        write_all(api, input_write, b"\x1a\r")
        wait = api.WaitForSingleObject(process.process, 10000)
        if wait != WAIT_OBJECT_0:
            raise RuntimeError(f"driver did not exit in 10 seconds (wait={wait})")
        exit_code = wintypes.DWORD()
        checked(api.GetExitCodeProcess(process.process, ctypes.byref(exit_code)),
                "GetExitCodeProcess")
        close_handle(input_write)
        api.ClosePseudoConsole(console)
        console = wintypes.HANDLE()
        if not read_done.wait(3):
            raise RuntimeError("pseudoconsole output did not close in 3 seconds")
        if reader_errors:
            raise RuntimeError("; ".join(reader_errors))
        lines = terminal_lines(b"".join(chunks))
        if exit_code.value:
            reason = EXIT_REASONS.get(exit_code.value, "unrecognized driver failure")
            raise RuntimeError(
                f"driver exit {exit_code.value}: {reason}; lines={ascii(lines)}")
        expected = (["READY", "café", "after\\x", "CLI_DONE"] if product else
                    ["READY", "IN:café", "OUT:é漢😀", "ERR:ñ界🚀", "EOF2"])
        if lines != expected:
            raise RuntimeError(
                f"console output differed: expected={ascii(expected)}, actual={ascii(lines)}, "
                f"raw={ascii(b''.join(chunks))}")
        if product:
            print("np CLI in isolated UTF-8 pseudoconsole: café input, Ctrl+Z/Enter, "
                  "ordered operands, persistent EOF, unchanged console code pages passed")
        else:
            print("isolated UTF-8 pseudoconsole: real console handles, UTF-8 input, "
                  "Ctrl+Z/Enter, persistent EOF, stdout and stderr output passed")
    finally:
        if process.process:
            if api.WaitForSingleObject(process.process, 0) == WAIT_TIMEOUT:
                api.TerminateProcess(process.process, 1)
                api.WaitForSingleObject(process.process, 2000)
        if console:
            api.ClosePseudoConsole(console)
        if reader:
            read_done.wait(2)
        for handle in handles:
            api.CloseHandle(handle)
        if attributes_initialized:
            api.DeleteProcThreadAttributeList(attributes)
        if process.thread:
            api.CloseHandle(process.thread)
        if process.process:
            api.CloseHandle(process.process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--product", type=Path)
    args = parser.parse_args()
    try:
        run(args.driver.resolve(strict=True),
            args.product.resolve(strict=True) if args.product else None)
    except Exception as error:
        print(f"console integration failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
