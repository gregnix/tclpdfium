# Getting Started with pdfiumtcl

pdfiumtcl is a Tcl/Tk binding for PDFium — Google's PDF engine (BSD license).
It is the first PDFium binding for Tcl.

---

## Requirements

- Linux x86_64 (Windows: experimental via mingw64)
- Tcl/Tk 8.5, 8.6, or 9.0
- gcc, make
- tcl-dev, tk-dev

---

## Installation

### 1. Get the source

```bash
git clone git@github.com:gregnix/tclpdfium.git
cd tclpdfium
```

### 2. Download PDFium

```bash
bash scripts/setup.sh
```

Downloads `libpdfium.so` from
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)
into `vendor/pdfium/lib/`.

### 3. Build

```bash
make clean && make
make check
```

`make check` verifies that the Stubs are correct — `libtcl` and `libtk`
must not appear in `ldd pdfiumtcl.so`.

---

## Quick Start

```tcl
package require pdfiumtcl

set doc [pdfium::open myfile.pdf]
puts "Pages: [pdfium::pagecount $doc]"
puts "Title: [pdfium::meta $doc Title]"
pdfium::close $doc
```

---

## Viewer

```bash
wish app/viewer.tcl myfile.pdf
```

The viewer includes:
- Page navigation (arrow keys) and zoom (Ctrl+scroll)
- Mouse wheel scrolling (works on all platforms)
- Middle mouse button drag-scroll
- Bookmarks panel (click to jump) — supports all Unicode including non-BMP
- Metadata panel
- Form fields panel
- Brother QL label printer dialog (29/38/54/62/102mm bands)

---

## Build Options

```bash
./configure --with-tcl=DIR --with-tk=DIR    # DIR holds tclConfig.sh / tkConfig.sh
make
make check                                  # stubs only? Init symbol exported?
make test
sudo make install
```

| Option | Effect |
|--------|--------|
| `--with-tcl=DIR` | directory holding `tclConfig.sh` |
| `--with-tk=DIR` | directory holding `tkConfig.sh` |
| `--with-pdfium=DIR` | PDFium SDK; without it, `vendor/pdfium-<platform>/` is found |
| `--prefix`, `--exec-prefix` | install elsewhere — **both** are needed |

Which Tcl gets named decides which one the extension is built against **and
installed into**. To see what is on the machine:

```bash
tclsh tools/find-tclconfig.tcl
```

### Tcl 8 and Tcl 9 side by side

Build twice, into the same directory:

```bash
./configure --with-tcl=/usr/lib/tcl8.6 --with-tk=/usr/lib/tk8.6
make && sudo make install
make distclean

./configure --with-tcl=/usr/lib/tcl9.0 --with-tk=/usr/lib/tk9.0
make && sudo make install
```

TEA names the two libraries differently — `libpdfiumtcl<ver>.so` and
`libtcl9pdfiumtcl<ver>.so` — and `pkgIndex.tcl` picks at load time. One
directory serves both interpreters:

```bash
wish   app/viewer.tcl myfile.pdf     # Tcl 8.6
wish9.0 app/viewer.tcl myfile.pdf    # Tcl 9.0
```

`make distclean` between the two runs is not optional — otherwise object files
of two ABIs get mixed, and that fails at run time, not at link time.

### Windows

```bash
./tools/build-windows.sh core-9-0-2       # cross-compile, on Linux
```

Natively in MSYS2, see [INSTALL.md](../INSTALL.md).

---

## Tcl 8 and Tcl 9 Compatibility

pdfiumtcl compiles and runs on both Tcl 8.6 and Tcl 9.0.

Key compatibility details in `generic/tclpdfiumtcl.c`:

- `Tcl_Size` — Tcl 9 replaced `int` with `Tcl_Size` for string lengths.
  pdfiumtcl defines `typedef int Tcl_Size` for Tcl 8 automatically —
  guarded against TEA passing `-DTcl_Size=int` on the command line, which would
  otherwise expand the typedef to `typedef int int;`.
- Stubs — `Tcl_InitStubs` uses `"9.0"` for Tcl 9, `"8.5"` for Tcl 8.
- UTF-16LE encoding — bookmark titles use `"utf-16le"` (Tcl 9) with
  fallback to `"unicode"` (Tcl 8) for correct Unicode handling including
  non-BMP characters (emoji, hieroglyphs etc.).

---

## Next Steps

- [API Reference](api-reference.md) — all 26 commands
- [README](../../README.md) — overview, install, examples
