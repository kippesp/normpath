"""Launch np through actual cmd.exe, PowerShell 7, and Windows PowerShell 5.1."""

import argparse
import base64
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest


PRODUCT = None
DRIVER = None
HOME = "C:\\Users\\Зоя漢😀"
# Input, cmd.exe result, PowerShell result.
FIXTURES = [
    ("C:/my projects/app", '"C:\\my projects\\app"', "'C:/my projects/app'"),
    ("c:\\users\\kippesp\\dir1\\..", "C:\\users\\kippesp", "C:/users/kippesp"),
    ("d:\\dir1\\dir2\\dir3.", "D:\\dir1\\dir2\\dir3", "D:/dir1/dir2/dir3"),
    ("src/../include", "include", "include"),
    ("../src", "..\\src", "../src"),
    (".", ".", "."),
    ("C:/", "C:\\", "C:/"),
    ("/mnt/d/", "D:\\", "D:/"),
    ("/", "\\", "/"),
    ("//server/share/dir", "\\\\server\\share\\dir", "//server/share/dir"),
    ("\\\\server\\share\\dir\\..", "\\\\server\\share\\", "//server/share/"),
    ("smb://server/my share/docs", '"\\\\server\\my share\\docs"',
     "'//server/my share/docs'"),
    ("~", HOME, HOME.replace("\\", "/")),
    ("~/Documents", HOME + "\\Documents", HOME.replace("\\", "/") + "/Documents"),
    ("C:/Users/漢字/проект", "C:\\Users\\漢字\\проект", "C:/Users/漢字/проект"),
    ("C:/Users/Paul's files", "\"C:\\Users\\Paul's files\"",
     "'C:/Users/Paul''s files'"),
    ("C:/it’s here", '"C:\\it’s here"', "'C:/it’’s here'"),
    ("//localhost/c$/Users", "\\\\localhost\\c$\\Users", "'//localhost/c$/Users'"),
    ("C:/Program Files (x86)/x", '"C:\\Program Files (x86)\\x"',
     "'C:/Program Files (x86)/x'"),
    ("C:/work/research&notes", '"C:\\work\\research&notes"',
     "'C:/work/research&notes'"),
    ("./-dash", "-dash", "'-dash'"),
    ("C:/hi!", "C:\\hi!", "C:/hi!"),
]
INPUTS = [fixture[0] for fixture in FIXTURES]


def lines(texts):
    return "".join(text + "\n" for text in texts).encode()


def environment():
    result = {key: value for key, value in os.environ.items()
              if key.upper() != "USERPROFILE"}
    result["USERPROFILE"] = HOME
    return result


def cmd(executable, args, source=None, data=b""):
    """Run one command line in cmd.exe; source is pasted text, not quoted."""
    if source is None:
        source = " ".join(f'"{arg}"' for arg in args)
    line = f'"{executable}" {source}'
    return subprocess.run(f'cmd.exe /d /s /c "{line}"', input=data,
                          capture_output=True, env=environment(), timeout=30)


def powershell_literal(text):
    return "'" + re.sub("(['\u2018-\u201b])", r"\1\1", text) + "'"


def powershell(shell, executable, args, source=None, stdin_lines=None):
    """Run one command in PowerShell; source is pasted text, not quoted."""
    if source is None:
        source = " ".join(map(powershell_literal, args))
    command = f"& {powershell_literal(str(executable))} {source}"
    if stdin_lines is not None:
        # Windows PowerShell 5.1 otherwise sends native stdin as ASCII.
        command = ("$OutputEncoding = [Text.UTF8Encoding]::new($false); @("
                   + ", ".join(map(powershell_literal, stdin_lines))
                   + ") | " + command)
    # Windows PowerShell 5.1 reports module-loading progress as CLIXML on stderr.
    command = f"$ProgressPreference = 'SilentlyContinue'; {command}; exit $LASTEXITCODE"
    encoded = base64.b64encode(command.encode("utf-16-le"))
    return subprocess.run(
        [shell, "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand",
         encoded.decode()], stdin=subprocess.DEVNULL, capture_output=True,
        env=environment(), timeout=60)


def find_shell(name):
    shell = shutil.which(name)
    if shell is None:
        raise FileNotFoundError(f"{name} is required for shell integration tests")
    return shell


class ShellTests(unittest.TestCase):
    def assert_result(self, result, stdout, stderr=b"", status=0):
        self.assertEqual((result.returncode, result.stdout, result.stderr),
                         (status, stdout, stderr))

    def test_detection_names_the_invoking_shell(self):
        # The driver exits with the np::Shell value: unknown, cmd, PowerShell.
        unknown, cmd_exe, powershell_shell = 0, 1, 2
        pwsh = find_shell("pwsh")
        self.assert_result(cmd(DRIVER, []), b"", status=cmd_exe)
        self.assert_result(powershell(pwsh, DRIVER, []), b"", status=powershell_shell)
        self.assert_result(powershell(find_shell("powershell"), DRIVER, []), b"",
                           status=powershell_shell)
        self.assert_result(subprocess.run([str(DRIVER)], capture_output=True,
                                          timeout=30), b"", status=unknown)
        # The nearest shell wins: cmd.exe started from PowerShell is cmd.exe.
        self.assert_result(powershell(pwsh, "cmd.exe", ["/d", "/c", str(DRIVER)]),
                           b"", status=cmd_exe)

    def test_cmd_keeps_backslashes_and_double_quotes(self):
        expected = lines(fixture[1] for fixture in FIXTURES)
        self.assert_result(cmd(PRODUCT, INPUTS), expected)
        self.assert_result(cmd(PRODUCT, ["-"], data=lines(INPUTS)), expected)
        pasted = " ".join(fixture[1] for fixture in FIXTURES)
        self.assert_result(cmd(PRODUCT, None, source="-- " + pasted), expected)

    def test_powershell_uses_forward_slashes_and_single_quotes(self):
        expected = lines(fixture[2] for fixture in FIXTURES)
        for name in ("pwsh", "powershell"):
            with self.subTest(shell=name):
                shell = find_shell(name)
                self.assert_result(powershell(shell, PRODUCT, INPUTS), expected)
                self.assert_result(
                    powershell(shell, PRODUCT, ["-"], stdin_lines=INPUTS), expected)
                pasted = " ".join(fixture[2] for fixture in FIXTURES)
                self.assert_result(
                    powershell(shell, PRODUCT, None, source="-- " + pasted), expected)

    def test_help_and_rejection_are_shell_independent(self):
        rejection = b"np: error: operand 2: drive-relative path\n"
        self.assert_result(cmd(PRODUCT, ["a/b", "C:bad"]), b"a\\b\n", rejection, 1)
        for name in ("pwsh", "powershell"):
            with self.subTest(shell=name):
                shell = find_shell(name)
                self.assert_result(powershell(shell, PRODUCT, ["a/b", "C:bad"]),
                                   b"a/b\n", rejection, 1)
                help_text = powershell(shell, PRODUCT, [])
                self.assertEqual((help_text.returncode, help_text.stdout),
                                 (0, cmd(PRODUCT, []).stdout))
                self.assertTrue(help_text.stdout.startswith(b"np - Path Normalizer"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", type=Path, required=True)
    parser.add_argument("--driver", type=Path, required=True)
    args = parser.parse_args()
    PRODUCT = args.product.resolve(strict=True)
    DRIVER = args.driver.resolve(strict=True)
    unittest.main(argv=[__file__])
