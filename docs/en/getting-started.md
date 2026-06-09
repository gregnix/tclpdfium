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
make                      # Linux, Tcl 8.6 (default)
make TCL_VERSION=9.0      # Tcl 9  -> pdfiumtcl90.so
make TCL_VERSION=8.5      # Tcl 8.5
make PLATFORM=windows     # Windows cross-compile (mingw64)
make both                 # Tcl 8.6 and 9.0 at once
```

### Tcl Version Environments

Two helper scripts set the correct environment before building and running:

**Tcl 8.6 (default):**

```bash
. tools/tcl8env.sh
make clean && make
make install
wish app/viewer.tcl myfile.pdf
```

**Tcl 9.0:**

```bash
. tools/tcl9env.sh
make clean && make TCL_VERSION=9.0
make install90
wish9.0 app/viewer.tcl myfile.pdf
```

The scripts set `TCLSH` and `TCLLIBPATH` so that the correct Tcl version
is used for both building and running. Always source the appropriate script
before switching between Tcl versions.

### Install Locations

| Version | Command | Target |
|---------|---------|--------|
| Tcl 8.6 | `make install` | `~/lib/share/tcltk/tclpdfium0.4/` |
| Tcl 9.0 | `make install90` | `~/lib/share/tcl9.0/tclpdfium0.4/` |

---

## Tcl 8 and Tcl 9 Compatibility

pdfiumtcl compiles and runs on both Tcl 8.6 and Tcl 9.0.

Key compatibility details in `src/pdfiumtcl.c`:

- `Tcl_Size` — Tcl 9 replaced `int` with `Tcl_Size` for string lengths.
  pdfiumtcl defines `typedef int Tcl_Size` for Tcl 8 automatically.
- Stubs — `Tcl_InitStubs` uses `"9.0"` for Tcl 9, `"8.5"` for Tcl 8.
- UTF-16LE encoding — bookmark titles use `"utf-16le"` (Tcl 9) with
  fallback to `"unicode"` (Tcl 8) for correct Unicode handling including
  non-BMP characters (emoji, hieroglyphs etc.).

---

## Next Steps

- [API Reference](api-reference.md) — all 12 commands
- [todo.md](../todo.md) — roadmap
- [feature-matrix.md](../feature-matrix.md) — comparison table
