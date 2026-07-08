# Installing tclpdfium

Build and install the `pdfiumtcl` package (the PDFium binding for Tcl/Tk,
version 0.5). PDFium itself is **not** in the repository — it is downloaded by
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

The binding is **stub-based**: once built it loads on any Tcl 8.5+ (8.6) or
9.x without recompiling, and `pdfiumtcl.so`/`.dll` does *not* link `libtcl`
or `libtk` directly.

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
`vendor/pdfium/` (`include/`, `lib/libpdfium.so`, and on Windows
`bin/pdfium.dll`). Override the platform with the `PDFIUM_PLATFORM`
environment variable, e.g. to fetch the **Windows** PDFium for a cross build:

```bash
PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh
```

Detected automatically: `linux-x64`, `linux-arm64`, `mac-arm64`, `mac-x64`,
`win-x64`.

## 3. Build

```bash
make                      # Linux, Tcl 8.6 (default) -> pdfiumtcl.so
make TCL_VERSION=9.0      # Linux, Tcl 9.0
make TCL_VERSION=8.5      # Linux, Tcl 8.5
make check                # verify the stub build (see below)
```

`make check` confirms the binding does not link `libtcl`/`libtk` directly
(`ldd` must not list them) and prints the exported init symbol
(`Pdfiumtcl_Init`). A direct `libtcl`/`libtk` link is a build error.

`make` writes `pdfiumtcl.so` into the repository root. `make clean` removes
the built `pdfiumtcl.so`/`pdfiumtcl.dll`.

## 4. Install

```bash
make install              # Tcl 8.6  -> out/tclpdfium0.5/linux64/
make install90            # Tcl 9.0  -> out/tclpdfium0.5/linux64-tcl9/
make both                 # clean + install (8.6) + clean + install90 (9.0)
```

Each `install` target creates the per-platform package tree under
`out/tclpdfium0.5/`:

```
out/tclpdfium0.5/
  pkgIndex.tcl                 # selects the subdir at load time
  linux64/                     # Tcl 8.x
    pdfiumtcl.so
    libpdfium.so
  linux64-tcl9/                # Tcl 9.x
    pdfiumtcl.so
    libpdfium.so
```

The subdirectory is chosen by the build:

| Build | Subdirectory |
|-------|--------------|
| Linux, Tcl 8.x | `linux64` |
| Linux, Tcl 9.x | `linux64-tcl9` |
| Windows, Tcl 8.x | `windows64` |
| Windows, Tcl 9.x | `windows64-tcl9` |

`pkgIndex.tcl` picks the right subdirectory at runtime from
`tcl_platform(platform)`, `tcl_platform(pointerSize)` and the Tcl version, so
one `tclpdfium0.5/` tree can hold several platforms side by side.

`make install` also runs `install-pdfium`, copying `libpdfium.so` (or
`pdfium.dll`) from `vendor/pdfium/` into the subdirectory next to the binding.

---

## Using it

Point Tcl at the package directory and require it. Either add the directory
that *contains* `tclpdfium0.5/` to `auto_path`/`TCLLIBPATH`, or the module
tree via `tcl::tm::path`:

```bash
TCLLIBPATH=/path/to/out wish your-script.tcl
```

```tcl
package require pdfiumtcl

set doc [pdfium::open document.pdf]
puts "Pages: [pdfium::pagecount $doc]"
pdfium::close $doc
```

A quick check that the package loads and finds PDFium:

```bash
TCLLIBPATH=out tclsh -c 'package require pdfiumtcl; puts ok'
```

---

## Tcl 8 vs Tcl 9

Two helper scripts set `TCLSH`/`TCLLIBPATH` for the matching interpreter so
the right Tcl is used for building *and* running:

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

### Cross-compile on Linux (recommended for both versions)

`scripts/build-win.sh` cross-builds the Windows `pdfiumtcl.dll` for **both**
Tcl 8.6 and Tcl 9.0 in one run (it builds the Tcl/Tk stubs from source for
each, then links twice). See `scripts/build-win.sh` and (if present)
`nogit/docs/de/windows-build.md` for details.

For a Tcl 8.6-only cross build straight from the Makefile (uses the system
MinGW stub libraries under `/usr/x86_64-w64-mingw32/`):

```bash
PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh   # fetch pdfium.dll first
make windows-cross                              # -> pdfiumtcl.dll (Tcl 8.6)
make dist-windows                               # collect into dist-win/windows64/
```

> Note: a Tcl 9.0 Windows binding cannot be produced with the plain Ubuntu
> MinGW stubs (`make dist-windows90` fails by design). Use
> `scripts/build-win.sh`, which builds the Tcl 9 stubs from source, or build
> natively.

### Native on Windows (BAWT / MinGW)

This is the path for building against a BAWT "Batteries Included" tree. It
needs **no `make`** — `build-tclpdfium-bawt.bat` calls `gcc` directly with
the exact flags from this Makefile.

Prerequisites:

- BAWT's MinGW gcc, extracted from `gcc14.2.0_x86_64-w64-mingw32.7z`; the
  compiler is `...\mingw64\bin\gcc.exe`.
- The PDFium SDK under `vendor\pdfium\` (`include\`, `lib\pdfium.dll.lib`,
  `bin\pdfium.dll`); run `bash scripts/setup.sh PDFIUM_PLATFORM=win-x64` if absent.
- One BAWT `...\Development\opt\tcl` tree per Tcl version. Each supplies
  `include\tcl.h` + `tk.h` and the stub libraries.

> **BAWT stub naming.** BAWT ships the **Tcl 8.6** stubs *versioned*
> (`libtclstub86.a`, `libtkstub86.a`) but the **Tcl 9** stubs *unversioned*
> (`libtclstub.a`, `libtkstub.a`, and `tclstub.lib`, `tkstub.lib`) — the
> Tcl 9 convention. The Makefile's 9.0 Windows wildcard matches both spellings.

Edit the six `CONFIG` lines at the top of `build-tclpdfium-bawt.bat`
(gcc path, C source, PDFium dir, project root, the two `opt\tcl` trees), then:

```bat
build-tclpdfium-bawt.bat
```

It builds Tcl 9.0 and 8.6 and drops a ready-to-use package into the project
layout — one flat folder per OS/Tcl combo, each with a one-line `pkgIndex.tcl`:

```
<PROJ>\libs\windows-tcl9.0\tclpdfium\   pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl
<PROJ>\libs\windows-tcl8.6\tclpdfium\   pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl
```

Verify it loads (Tk must be present — `Pdfiumtcl_Init` calls `Tk_InitStubs`):

```tcl
cd C:/.../libs/windows-tcl9.0/tclpdfium
lappend auto_path [pwd]
package require Tk
package require pdfiumtcl      ;# -> 0.5
```

If you do have GNU `make` / `mingw32-make`, the Makefile builds the same
binary directly (BAWT MinGW gcc on `PATH`):

```bat
mingw32-make PLATFORM=windows TCL_VERSION=9.0 ^
    WIN_TCL_ROOT=C:/Bawt/Bawt903/Windows/x64/Development/opt/tcl
```

### Install on Windows

```bash
make install-windows             # -> WIN_INSTALL_DIR/<subdir>/
```

`WIN_INSTALL_DIR` defaults to `WIN_TCL_ROOT/lib/tclpdfium0.5`. The
`pdfium.dll` is copied next to `pdfiumtcl.dll` in the subdirectory so the
Windows loader can resolve it.

---

## Troubleshooting

- **`make check` reports a libtcl/libtk link** — the stub library path is
  wrong; check `TCL_STUBLIB`/`TK_STUBLIB`. The binding must use stubs only.
- **`PDFium-Library nicht gefunden` during install** — run
  `bash scripts/setup.sh` first (and `PDFIUM_PLATFORM=win-x64` for Windows).
- **`package require pdfiumtcl` cannot load the library** — make sure the
  `pdfium` shared library sits in the same subdirectory as `pdfiumtcl.*`
  (`install-pdfium` does this), and that the directory containing
  `tclpdfium0.5/` is on `auto_path`/`TCLLIBPATH`.
- **`cannot open PDF '...' (PDFium error 3)`** — the file is not a valid PDF
  (corrupt, or actually PostScript/EPS such as `pcal` output). PDFium reads
  PDF only; convert first (`ps2pdf in.ps out.pdf`) or rewrite a broken PDF
  with `qpdf in.pdf out.pdf`. See the error-code table in
  [docs/en/api-reference.md](docs/en/api-reference.md).
