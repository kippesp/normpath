# np — Path Normalizer for Windows

`np` converts path spellings from coding agents or other operating systems to
Windows form for use in cmd.exe or PowerShell.  It converts separators, expands
a leading `~`, and translates drive spellings such as `/mnt/c/users/me` to
`C:\users\me`.  It detects whether you ran it from cmd.exe or PowerShell and
spells and quotes the result for that shell.

For example, with `USERPROFILE=C:\Users\me`:

```text
> np ~/mystuff/projects/cli_utils/normpath.git
C:\Users\me\mystuff\projects\cli_utils\normpath.git

PS> np ~/mystuff/projects/cli_utils/normpath.git
C:/Users/me/mystuff/projects/cli_utils/normpath.git
```

`np` converts input text to Windows path text, reading `USERPROFILE` only to
expand a leading `~`.  It checks some lexical rules but never consults the
filesystem or the current directory.  The output is not verified: it may still
be invalid or refer to a location that does not exist.

<img src="https://static.kippesp.dev/np-v1.0.0.gif" width="800" height="432"
alt="np in cmd.exe converting WSL, Git Bash, SMB, and relative paths to Windows
form, quoting a path with spaces, and rejecting a drive-relative path">

## Getting Started

`np.exe` runs on Windows 10 version 1903 or later, x64.  The examples below
use cmd.exe unless the prompt is `PS>`; [Shell-Aware Output](#shell-aware-output)
describes the PowerShell differences.

`np`, `np -h`, and `np --help` show the same usage and options.  `np --version`
shows version information.  The seven-character Git revision has a `-dev`
suffix when the checkout contains uncommitted changes or untracked files at
build time. Git-ignored files do not count.

### Building and Testing

The project uses C++23 and Windows x64 toolchains.  Follow [Building and
testing np](BUILDING.md) to build the executable and run the tests and
development checks.  Then copy `np.exe` to a directory on your `PATH`.
The executable is standalone; no installer is needed.

## Converting Paths

Pass one or more paths as arguments.  `np` prints each converted path on its
own line:

```text
> np C:/work/project/src
C:\work\project\src

> np src/main.cpp
src\main.cpp
```

Both `/` and `\` are accepted as separators.  `np` also recognizes Unix-style
drive spellings and removes redundant separators and `.` or `..` segments.
Relative paths stay relative; `np` never prepends the current directory.

A single period attached to the final filename is treated as sentence
punctuation and removed: `d:\dir1\dir2\dir3.` becomes `D:\dir1\dir2\dir3`.
This applies to arguments and stdin. `.` and `..` path segments keep their
normal meaning; periods inside names and UNC server/share fields are preserved.

| Input                    | Output               |
| ------------------------ | -------------------- |
| `/c/work`, `/mnt/c/work` | `C:\work`            |
| `src/../include`         | `include`            |
| `../src`                 | `..\src`             |
| `//server/share/dir`     | `\\server\share\dir` |
| `smb://server/share/dir` | `\\server\share\dir` |

`smb://server/share[/path]` also accepts a case-insensitive `smb:` prefix.
Server and share are required.  Use literal spaces; percent signs and URL
encoding are rejected.  Credentials, ports, IPv6 literals, backslashes,
queries, and fragments are unsupported in SMB inputs.

In cmd.exe, when the converted path contains a space or any of `& ( ) ^`, `np`
wraps the path in double quotes.  The input needs quotes for the same reason; cmd.exe
consumes those before `np` sees the path.  `%` and `!` do not trigger quotes,
and quoting does not suppress cmd.exe variable expansion.

```text
> np "C:/my projects/app"
"C:\my projects\app"
```

If a path is rejected, `np` reports an error on stderr and continues with any
remaining paths.

## Shell-Aware Output

In PowerShell 7 and Windows PowerShell 5.1, `np` prints forward slashes.
It adds single quotes where needed to preserve the path text and doubles
embedded single quotes:

```text
PS> np "C:\my projects\app"
'C:/my projects/app'

PS> np "C:\Users\Paul's files"
'C:/Users/Paul''s files'
```

Detection uses the immediate parent process; other or unavailable parents
default to cmd.exe output.  Thus, `cmd /c np` uses cmd.exe output even when
invoked from PowerShell.

PowerShell cmdlets that take `-Path` treat `[` and `]` as wildcards; quoting
does not change that.  For such names, pass the path with `-LiteralPath`.

## Reading Paths from stdin

Use `-` to read one path per line from a UTF-8 text file.  Do not quote paths
in the file.

For example, `paths.txt`:

```text
C:/my projects/app
src/main.cpp
```

```text
> np - < paths.txt
"C:\my projects\app"
src\main.cpp
```

You can also pipe paths into `np`:

```bat
type paths.txt | np -
```

PowerShell has no `<` redirection; pipe the file instead:

```powershell
Get-Content -Encoding UTF8 paths.txt | np -
```

In Windows PowerShell 5.1, first set
`$OutputEncoding = [Text.UTF8Encoding]::new($false)` to preserve non-ASCII
characters in piped input.  PowerShell 7 already uses UTF-8.

To enter paths interactively, run `np -`.  Press **Ctrl+Z**, then **Enter**,
to end input.

## Rejected Inputs

`np` rejects these path forms:

- Drive-relative input such as `C:foo` or `C:`
- The `\\?\` and `\\.\` namespace prefixes
- UNC paths without both a server and a share
- Normalization that changes the root, such as `./c:/foo` or
  `\\server\share\..\other`

Filename components surviving normalization are rejected for:

- Prohibited characters: `< > : " | ? *` or control characters (U+0000–U+001F)
- Reserved device names, with or without an extension: `CON`, `PRN`, `AUX`,
  `NUL`, `COM1`–`COM9`, or `LPT1`–`LPT9` (case-insensitive, including the
  superscript digits `¹`, `²`, and `³` for COM and LPT)
- A trailing space or period remaining after the sentence-period removal

The drive designator and UNC server and share are root fields, not filename
components; UNC server and share fields are checked for control characters.
Length is never checked, and rejection is lexical, not a filesystem or
security check.

## Exit Status

| Status | Meaning                                                      |
| ------ | ------------------------------------------------------------ |
| 0      | All inputs converted, help/version delivered, or empty stdin |
| 1      | Processing completed with at least one input rejected        |
| 2      | Invocation aborted; takes precedence over 1                  |

Aborts include usage errors (such as `np --` without paths), stream failures,
and unexpected failures.
