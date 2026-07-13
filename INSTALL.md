# Installing tclpdfium

Build and install the `pdfiumtcl` package (the PDFium binding for Tcl/Tk,
version 0.5.2). PDFium itself is **not** in the repository — it is downloaded by
`scripts/setup.sh` from
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries).

---

## Requirements

- Linux x86_64 (Windows: see [Windows builds](#windows-builds))
- Tcl/Tk 8.5, 8.6, or 9.0 with development headers
- `gcc`, `make`

```bash
sudo apt install tcl-dev tk-dev build-essential
```

The binding is **stub-based**: `pdfiumtcl.so`/`.dll` does not link `libtcl` or
`libtk` directly, so one binary runs on any interpreter of the same Tcl major
version. Across major versions it does not — Tcl 9 is source compatible with
8.6 but not binary compatible, so 8.x and 9.x need separate builds. That is what
the `linux64` / `linux64-tcl9` split below is for.

**Tk is optional.** The package loads and works in a plain `tclsh` with no
display. Tk stubs are initialised lazily, and only `pdfium::render` and
`pdfium::addimagebitmap` need them — everything else (reading, text extraction,
writing, page manipulation) is headless.

---

## 1. Get the source

```bash
git clone https://github.com/gregnix/tclpdfium.git
cd tclpdfium
```

## 2. Download PDFium

```bash
bash scripts/setup.sh
```

`setup.sh` auto-detects the platform and fetches the matching archive into
**`vendor/pdfium-<platform>/`** — `include/`, `lib/libpdfium.so`, and on Windows
`bin/pdfium.dll` plus `lib/pdfium.dll.lib`.

The directory carries the platform in its name on purpose: a cross build needs
the Linux **and** the Windows PDFium in the tree at the same time. A shared
`vendor/pdfium/` would have them overwrite each other, and you would not notice
until the link step.

```bash
bash scripts/setup.sh                           # -> vendor/pdfium-linux-x64
PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh   # -> vendor/pdfium-win-x64
```

Detected automatically: `linux-x64`, `linux-arm64`, `mac-arm64`, `mac-x64`,
`win-x64`. The Makefile picks the matching directory from `PLATFORM`; a legacy
`vendor/pdfium/` still works as a fallback.

**On Windows without a shell:** `setup.sh` is bash. Use the cmd.exe counterpart:

```cmd
scripts\get-pdfium.cmd
```

It uses `curl.exe` and `tar.exe`, both shipped with Windows 10 (1803) and later.
No bash, no PowerShell, no MSYS2.

## 3. Build

```bash
make                      # Linux, Tcl 8.6 (default) -> pdfiumtcl.so
make TCL_VERSION=9.0      # Linux, Tcl 9.0
make TCL_VERSION=8.5      # Linux, Tcl 8.5
make check                # verify the stub build (see below)
```

`make check` confirms the binding does not link `libtcl`/`libtk` directly (`ldd`
must not list them) and prints the exported init symbol (`Pdfiumtcl_Init`). A
direct `libtcl`/`libtk` link is a build error, not a warning: it would nail the
binding to one specific Tcl installation.

`make` writes `pdfiumtcl.so` into the repository root. `make clean` removes the
built `pdfiumtcl.so`/`pdfiumtcl.dll`.

## 4. Install

```bash
make install              # Tcl 8.6  -> out/tclpdfium0.5.2/linux64/
make install90            # Tcl 9.0  -> out/tclpdfium0.5.2/linux64-tcl9/
make both                 # clean + install (8.6) + clean + install90 (9.0)
```

Each `install` target creates the per-platform package tree:

```
out/tclpdfium0.5.2/
  pkgIndex.tcl                 # selects the subdir at load time
  linux64/                     # Tcl 8.x
    pdfiumtcl.so
    libpdfium.so
  linux64-tcl9/                # Tcl 9.x
    pdfiumtcl.so
    libpdfium.so
```

| Build | Subdirectory |
|-------|--------------|
| Linux, Tcl 8.x | `linux64` |
| Linux, Tcl 9.x | `linux64-tcl9` |
| Windows, Tcl 8.x | `windows64` |
| Windows, Tcl 9.x | `windows64-tcl9` |

`pkgIndex.tcl` picks the right subdirectory at runtime from
`tcl_platform(platform)`, `tcl_platform(pointerSize)` and the Tcl version, so one
`tclpdfium0.5.2/` tree can hold several platforms side by side.

> `pointerSize`, not `wordSize`: on Windows, `wordSize` is 4 even on a 64-bit Tcl
> (LLP64). `wordSize` sends the loader looking for `windows32/`.

`make install` also runs `install-pdfium`, copying `libpdfium.so` (or
`pdfium.dll`) into the subdirectory **next to** the binding. That placement is
not cosmetic — it is what the `$ORIGIN` runpath in `pdfiumtcl.so` resolves
against.

---

## Using it

Point Tcl at the directory that *contains* `tclpdfium0.5.2/`:

```bash
TCLLIBPATH=/path/to/out wish your-script.tcl
```

```tcl
package require pdfiumtcl

set doc [pdfium::open document.pdf]
puts "Pages: [pdfium::pagecount $doc]"
pdfium::close $doc
```

A quick check that the package loads and finds PDFium — note this works in
`tclsh`, with no display:

```bash
echo 'package require pdfiumtcl; puts ok' | TCLLIBPATH=out tclsh
```

(`tclsh` has no `-c` option. Passing one makes it read the script from standard
input instead, and it sits there waiting.)

---

## Starpacks and virtual filesystems

`pdfiumtcl` depends on a second shared library, and **nobody distributes
dependent libraries for you**. When Tcl loads an extension out of a VFS —
starpack, zipkit, `zipfs` — it copies only the *directly loaded* library to a
temporary directory. `libpdfium.so` would stay behind in the VFS, where the
operating system loader cannot see it. On Linux this is fatal: the `$ORIGIN`
runpath then points at the temp directory, and `ld.so` does not search the
executable's directory either.

The shipped `pkgIndex.tcl` handles this. If the package is not on a native
filesystem, it unpacks **both** libraries itself, into the *same* directory, and
loads from there. `$ORIGIN` lines up again, and the starpack stays a single file.

Set **`TCLPDFIUM_TMPDIR`** to redirect the unpack directory. You need this when
`/tmp` is mounted `noexec` — common on hardened servers — because `dlopen` then
refuses the file and the error message points nowhere useful.

---

## Tcl 8 vs Tcl 9

Two helper scripts set `TCLSH`/`TCLLIBPATH` for the matching interpreter, so the
right Tcl is used for building *and* running:

```bash
. tools/tcl8env.sh            # Tcl 8.6
make clean && make
make install

. tools/tcl9env.sh            # Tcl 9.0
make clean && make TCL_VERSION=9.0
make install90
```

Source the appropriate script before switching versions.

---

## Windows builds

### Cross-compile on Linux (recommended, both Tcl versions)

No Windows machine, no MSYS2, no BAWT.

```bash
sudo apt install mingw-w64 curl

PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh

make dist-windows        # Tcl 8.6  -> dist-win/windows64/
make dist-windows90      # Tcl 9.0  -> dist-win/windows64-tcl9/
```

`dist-windows90` builds the Tcl/Tk stub libraries first, by calling
`tools/make-win-stubs.sh`. That script fetches the Tcl and Tk source trees and
compiles exactly two files — `generic/tclStubLib.c` and `generic/tkStubLib.c`,
about 160 lines each. Nothing else from the Tcl tree is needed, and no MSYS2
`tcl9` package: a *stub* library is plain C from the source tree, unlike an
*import* library, which is derived from a DLL and does need MSVC. Confusing the
two is what made Tcl 9 on Windows look impossible for a while.

To pin a different Tcl version, run it by hand:

```bash
./tools/make-win-stubs.sh core-8-6-16     # -> win-stubs/
./tools/make-win-stubs.sh core-9-0-2
```

Verify the result without running it:

```bash
x86_64-w64-mingw32-objdump -p dist-win/windows64-tcl9/pdfiumtcl.dll \
    | grep "DLL Name"
```

```
DLL Name: pdfium.dll          <- the vendored library, expected
DLL Name: KERNEL32.dll        <- Windows
DLL Name: msvcrt.dll          <- msvcrt, not UCRT: matches MinGW/BAWT
```

`tcl90.dll` and `tk90.dll` must **not** appear — if they do, stubs are not in
effect. `libgcc_s_seh-1.dll` and `libwinpthread-1.dll` must not appear either;
the Makefile links the MinGW runtime statically.

And the version string `Tcl_InitStubs` asks for:

```bash
strings -n 3 dist-win/windows64-tcl9/pdfiumtcl.dll | grep -x '9\.0'
```

`strings` drops anything shorter than four characters by default. Without
`-n 3` you never see `9.0` and wrongly conclude the build is broken.

`scripts/build-win.sh` does the same job by cross-building **all** of Tcl and Tk
with `configure && make && make install` and linking against the result. It is
slower, and its `REPO=` path at the top is hardcoded — edit it before use.

### Native on Windows (BAWT / MinGW)

For building against a BAWT tree. `build-tclpdfium-bawt.bat` needs **no `make`**
— it calls `gcc` directly with the flags from this Makefile.

Prerequisites:

- BAWT's MinGW gcc from `gcc14.2.0_x86_64-w64-mingw32.7z`; the compiler is
  `...\mingw64\bin\gcc.exe`.
- The PDFium SDK under `vendor\pdfium-win-x64\`; run `scripts\get-pdfium.cmd` if
  absent.
- One BAWT `...\Development\opt\tcl` tree per Tcl version, each supplying
  `include\tcl.h` + `tk.h` and the stub libraries.

> **BAWT stub naming.** The **Tcl 8.6** stubs are *versioned*
> (`libtclstub86.a`, `libtkstub86.a`); the **Tcl 9** stubs are *unversioned*
> (`libtclstub.a`, `libtkstub.a`) — the Tcl 9 convention. The Makefile's 9.0
> Windows wildcard matches both spellings, but if it comes up empty, pass the
> paths explicitly.

Edit the `CONFIG` lines at the top of `build-tclpdfium-bawt.bat`, then:

```bat
build-tclpdfium-bawt.bat
```

With GNU `make` available, the Makefile builds the same binary:

```bat
mingw32-make PLATFORM=windows TCL_VERSION=9.0 ^
    WIN_TCL_ROOT=C:/Bawt/Bawt903/Windows/x64/Development/opt/tcl ^
    TCL_STUBLIB=C:/Bawt/Bawt903/Windows/x64/Development/opt/tcl/lib/libtclstub.a ^
    TK_STUBLIB=C:/Bawt/Bawt903/Windows/x64/Development/opt/tcl/lib/libtkstub.a
```

Verify it loads — **no `package require Tk` needed**:

```tcl
cd C:/.../libs/windows-tcl9.0/tclpdfium
lappend auto_path [pwd]
package require pdfiumtcl      ;# -> 0.5.2
```

### Install on Windows

```bash
make install-windows             # -> WIN_INSTALL_DIR/<subdir>/
```

`WIN_INSTALL_DIR` defaults to `WIN_TCL_ROOT/lib/tclpdfium0.5.2`. `pdfium.dll` is
copied next to `pdfiumtcl.dll` so the Windows loader can resolve it.

---

## Troubleshooting

- **`make check` reports a libtcl/libtk link** — the stub library path is wrong;
  check `TCL_STUBLIB`/`TK_STUBLIB`. The binding must use stubs only.

- **`PDFium-Library nicht gefunden` during install** — run `bash
  scripts/setup.sh` first (`PDFIUM_PLATFORM=win-x64` for Windows,
  `scripts\get-pdfium.cmd` in cmd.exe).

- **`package require pdfiumtcl` cannot load the library** — the `pdfium` shared
  library must sit in the *same subdirectory* as `pdfiumtcl.*` (`install-pdfium`
  does this), and the directory containing `tclpdfium0.5.2/` must be on
  `auto_path`/`TCLLIBPATH`.

- **`version conflict for package "tcl": have 9.0.4, need 8.5`** — an 8.x build
  loaded into Tcl 9. Not a link error: the library loaded and `Tcl_InitStubs`
  rejected it. Rebuild for the right generation (`make install90`).

- **`libpdfium.so: cannot open shared object file`, and the path in the message
  is under `/tmp`** — the package is being loaded from a VFS with an old
  `pkgIndex.tcl`. See [Starpacks](#starpacks-and-virtual-filesystems). If `/tmp`
  is `noexec`, set `TCLPDFIUM_TMPDIR`.

- **Segfault instead of an error from `pdfium::addimagebitmap`** — fixed in
  0.5.2. Earlier builds reached into the Tk stub table without having called
  `Tk_InitStubs`; `tkStubsPtr` was NULL and the process died silently. Replace
  the binary.

- **`cannot open PDF '...' (PDFium error 3)`** — the file is not a valid PDF
  (corrupt, or actually PostScript/EPS such as `pcal` output). PDFium reads PDF
  only; convert first (`ps2pdf in.ps out.pdf`) or rewrite a broken PDF with
  `qpdf in.pdf out.pdf`. See the error-code table in
  [docs/en/api-reference.md](docs/en/api-reference.md).
