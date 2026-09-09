# tclpdfium

Tcl/Tk binding for PDFium — Google's PDF rendering engine (BSD license).

The first PDFium binding for Tcl. Enables PDF rendering, text extraction,
metadata, search, bookmarks, form fields and annotations directly from
Tcl/Tk — and, since 0.4, creating and editing PDFs (import/merge/split,
delete/rotate pages, crop, embed images, save).

**Version:** 0.6.4  
**License:** BSD  
**Platform:** Linux x86_64, Windows x64 (MinGW, cross-built or native)  
**Tcl/Tk:** 8.5, 8.6, 9.0  

---

## Features

### Reading

```tcl
pdfium::open      filename ?password?  -> doc-handle
pdfium::close     doc-handle
pdfium::pagecount doc-handle           -> integer
pdfium::pagesize  doc-handle pagenum   -> {width_mm height_mm}
pdfium::rotation  doc-handle pagenum   -> 0|90|180|270
pdfium::render    doc-handle pagenum   ?-dpi n? ?-width px?
pdfium::gettext   doc-handle pagenum   -> string
pdfium::search    doc-handle pagenum text ?-case 0|1?
pdfium::meta      doc-handle key       -> string
pdfium::links     doc-handle pagenum   -> {url ...}
pdfium::bookmarks doc-handle           -> {{title pagenum level} ...}
pdfium::formfields doc-handle pagenum
        -> {{type name value flags rect options description} ...}
pdfium::annot_list doc-handle pagenum  -> {{type rect content author date} ...}
pdfium::structure  doc-handle pagenum  -> tagged-PDF structure tree (nested dicts)
pdfium::mctext     doc-handle pagenum  -> {mcid text ...} in content-stream order
pdfium::catalog    doc-handle          -> tagged 0|1, language
pdfium::pageobjects doc-handle pagenum ?-marks 0|1? ?-fonts 0|1?
pdfium::images     doc-handle pagenum  -> per image: pixel size and dpi on paper
pdfium::signatures doc-handle          -> the parts of each signature (no check)
pdfium::attachments doc-handle         -> {{index name size} ...}
pdfium::attachment  doc-handle index   -> contents, as a byte array
```

> **Tk is only needed for `render` and `addimagebitmap`.** Both work on Tk photo
> images — one writes into a photo, the other reads from one. Tk is loaded
> lazily: neither `package require pdfiumtcl` nor any other command pulls it in.
> The binding therefore works in a plain `tclsh`, with no window and no event
> loop.

### Forms (0.6.3)

```tcl
pdfium::formfill  doc-handle pagenum dict   ;# text, combo, check, radio
pdfium::editbegin doc-handle pagenum        -> session
pdfium::editclick session page-x page-y
pdfium::editchar  session text
pdfium::editkey   session tab|back|del|up|down|left|right|home|end
pdfium::editrender session ?-dpi n? ?-imagename name?
pdfium::editend   session
```

`formfill` **replaces**; a typing session **inserts** at the caret, the
way a keyboard does. Both go through PDFium's form-fill environment, so
the appearance is rebuilt by the engine -- writing `/V` alone would leave
the field looking unchanged on paper.

`editrender` draws through the **session's** environment. `render -forms 1`
builds its own and does not see what is being typed.

Call `save` afterwards -- and give up the focus first (`editend`), because
PDFium commits a field's content on focus loss.

### Writing / editing (0.4)

```tcl
pdfium::newdoc                                   -> doc-handle (empty)
pdfium::newpage     doc index width height       -> page-handle   (points)
pdfium::closepage   page
pdfium::generatecontent page                     -> 0|1
pdfium::importpages dest src ?range? ?index?     -> 0|1   range "1,3,5-7" or ""
pdfium::setcropbox  doc pageindex l b r t        -> 1     (points)
pdfium::setmediabox doc pageindex l b r t        -> 1     (points)
pdfium::deletepage  doc index                    -> 1
pdfium::setrotation doc index degrees            -> 0|1   0|90|180|270
pdfium::addimagejpeg  page doc jpegfile x y w h  -> 0|1   (points)
pdfium::addimagebitmap page doc photo x y w h    -> 0|1   (points, lossless)
pdfium::save           doc filename ?flags?      -> 0|1   default FPDF_NO_INCREMENTAL
pdfium::savewithversion doc filename version ?flags? -> 0|1   version 14..17
```

> **Units:** `pagesize` returns millimetres, but every writing command
> expects **points** (`pt = mm * 72 / 25.4`). Page origin is bottom-left.
>
> Encrypted saving and vector/text generation are intentionally left to
> [pdf4tcl](https://sourceforge.net/projects/pdf4tcl/) — PDFium has no
> suitable public write API for those.

---

## Installation

Builds with TEA — `configure`, `make`, `make install`, like any other Tcl
extension. Full detail in [INSTALL.md](INSTALL.md).

### 1. Dependencies

```bash
sudo apt install tcl-dev tk-dev build-essential
```

The `-dev` packages matter: without them there is no `tclConfig.sh`, and
`configure` has nothing to work with.

### 2. Get PDFium

```bash
bash scripts/setup.sh
```

Downloads `libpdfium.so` and the headers from
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) into
`vendor/pdfium-linux-x64/`. About 7 MB; not in the repository.

### 3. Build

```bash
./configure --with-tcl=/usr/lib/tcl9.0 --with-tk=/usr/lib/tk9.0
make
make check
make test
sudo make install
```

**Name the Tcl.** `--with-tcl` and `--with-tk` take the *directory* holding
`tclConfig.sh` resp. `tkConfig.sh`. They decide which Tcl the extension is built
against and installed into. Omit them and a search heuristic guesses — on a
machine with several installations it may guess inconsistently, taking Tcl from
one and Tk from another. To see what is there:

```bash
tclsh tools/find-tclconfig.tcl
```

### Tcl 8 and Tcl 9 side by side

Build twice (with `make distclean` in between) and both land in the same
directory. TEA names the libraries differently, and `pkgIndex.tcl` picks at load
time — one directory, both interpreters.

### Windows

```bash
./tools/build-windows.sh core-9-0-2       # cross-compile, on Linux
```

Natively in MSYS2: see [INSTALL.md](INSTALL.md).

---

## Quick Start

```tcl
package require pdfiumtcl

set doc [pdfium::open document.pdf]
puts "Title:  [pdfium::meta $doc Title]"
puts "Pages:  [pdfium::pagecount $doc]"

# Bookmarks
foreach bm [pdfium::bookmarks $doc] {
    puts "[lindex $bm 2]-[lindex $bm 0]  (page [expr {[lindex $bm 1]+1}])"
}

# Render page 0 at 150 dpi -> Tk photo image
set img [pdfium::render $doc 0 -dpi 150]
label .l -image $img
pack .l

pdfium::close $doc
```

### Writing example — extract a page region into a new PDF

```tcl
package require pdfiumtcl

# Render a page region to a Tk photo, then embed it losslessly.
set src  [pdfium::open in.pdf]
set photo [pdfium::render $src 0 -dpi 150]   ;# -> Tk photo image
pdfium::close $src

# A4-sized point box would be {595 842}; here we make a page the
# image's size in points (150 dpi -> pt factor 72/150).
set k   [expr {72.0 / 150}]
set wpt [expr {[image width  $photo] * $k}]
set hpt [expr {[image height $photo] * $k}]

set doc  [pdfium::newdoc]
set page [pdfium::newpage $doc 0 $wpt $hpt]
pdfium::addimagebitmap $page $doc $photo 0 0 $wpt $hpt
pdfium::generatecontent $page
pdfium::closepage $page
pdfium::save $doc out.pdf
pdfium::close $doc
```

---

## Error handling

`pdfium::open` raises a catchable Tcl error when a file cannot be loaded. The
message is `cannot open PDF '<file>' (PDFium error N)`, where `N` is the PDFium
error code:

| N | Meaning |
|---|---------|
| 1 | Unknown error |
| 2 | File not found / cannot be opened |
| 3 | Not a PDF or corrupted |
| 4 | Password required or incorrect |
| 5 | Unsupported security scheme |
| 6 | Page not found / content error |

PDFium reads **PDF only**. A PostScript/EPS file (`%!PS...`, e.g. `pcal` output)
fails with error 3 even when it has a `.pdf` name — convert it first (e.g.
`ps2pdf in.ps out.pdf`). Wrap calls in `catch` and branch on the code:

```tcl
if {[catch {pdfium::open $f} doc]} {
    if {[string match {*error 4*} $doc]} {
        set doc [pdfium::open $f $password]   ;# encrypted: retry with password
    } else {
        puts stderr $doc                      ;# 2 = missing, 3 = not a PDF, ...
    }
}
```

---

## Viewer

A ready-made viewer application:

```bash
TCLLIBPATH=. wish app/viewer4.tcl document.pdf
```

**It fills forms.** Click a field on the page, type; `Tab` moves to the
next one, a check box toggles on double click. The field list on the
right shows the `/TU` description with the technical name beside it, and
"Gefuellt speichern..." writes a new file -- the template is kept.

There is no mode to switch on: the session runs as soon as a page has
form fields.

### pdfview -- a PDF page as a widget

`lib/pdfview-0.1.tm` is the same thing as a **widget**, for embedding in
an application rather than running on its own:

```tcl
tcl::tm::path add /path/to/tclpdfium/lib
package require pdfview

pdfview .p -file invoice.pdf
pack .p -fill both -expand 1
.p configure -page 3 -zoom fit
```

| Option | |
|---|---|
| `-file` | path; empty for an empty widget |
| `-page` | page number from 0, clamped to the document |
| `-zoom` | `fit`, `width`, or a number (1.0 = natural size) |
| `-dpi` | fallback while the window has no size yet |
| `-background` | |
| `-pagechangedcommand` | called with `{page total}` |

Anything else goes to the enclosing `ttk::frame` (`-relief`,
`-borderwidth`).

Methods: `pagecount`, `see 0|end`, `next`, `prev`, `text ?page?`,
`pagesize ?page?`, and `handle` -- the last one returns the pdfium
document handle for everything the widget does not wrap: `search`,
`bookmarks`, `annot_list`, `formfields`, `structure`, `mctext`.

State belongs to the instance, so several views can sit in one
application without interfering. TclOO, no dependency beyond Tcl and Tk.

---

## Directory Structure

```
tclpdfium/
  configure           TEA; generated from configure.ac (checked in)
  configure.ac        --with-pdfium, Tcl/Tk version guard
  Makefile.in         plus install-pdfium, pdfium-local, check
  pkgIndex.tcl.in     VFS-capable loader
  tclconfig/          TEA machinery (tcl.m4) — do not patch
  generic/            C source (tclpdfiumtcl.c)
  app/                Tcl applications (viewer4.tcl, etikett.tcl,
                      print-demo.tcl)
  lib/                pdfview, the viewer as an embeddable widget
  scripts/            setup.sh, get-pdfium.cmd, createpdf.sh
  tools/              build-windows.sh, make-win-stubs.sh,
                      find-tclconfig.tcl, test-windows.tcl, test-vfs.tcl
  tests/              tcltest suite
    basic.test        the commands, one at a time
    pdfview.test      the widget
    viewer-forms.test app/viewer4.tcl operated the way a person does --
                      the order of actions, not the single command
  win/                MSVC build (nmake -f makefile.vc)
  doc/                API reference, getting started, man page
  examples/           example scripts
  vendor/             PDFium (fetched by setup.sh, not in repo)
```

---

## Running the tests

```bash
make test
```

The suite should finish with **0 failures**. The count is deliberately
not written down here: it was `102` while the suite had long passed 139,
and a number in prose is wrong the moment someone adds a test. Whether
it is right is what `make test` says.

**Without a display, tests are skipped rather than failed**: `render`
needs a Tk photo and Tk needs a display.
Until 0.6.3 three of them failed there, because they check an *option
message* and carried no constraint -- it only came out the first time
the suite ran without Xvfb.

`viewer-forms.test` is the one that matters for the application: it
operates `app/viewer4.tcl` in the **order** a person would -- select in
the field list, then click the page, then type. Every bug reported by a
user on 2026-09-06 was of that kind, and the rest of the suite was green
throughout.

## Platform Notes

- **Linux x86_64:** fully supported, tested on Tcl 8.6 and 9.0
- **Windows x64:** cross-built on Linux (`tools/build-windows.sh`) or natively
  in MSYS2 — Tcl 8.6 and 9.0, both verified
- **macOS:** not yet tested
- **Stub-based:** one binary per Tcl major version; no `libtcl`/`libtk` link
- **Starpack-ready:** loads from a VFS without leaving `libpdfium` behind

---

## Changes

### 0.6.4

Forms, mostly. The detail behind each line sits in the code comment and
in the test named after it -- this list says *what*, not *why with
numbers*.

**Form environment**

- One `FPDFDOC_InitFormFillEnvironment` in the whole module, owned by
  the document (`_DocFormGet`), released by `close` (2.87, 2.80). The
  current page is set *before* the environment is built, or the first
  `editrender` crashes.
- A transient page load no longer steals the typing session's page
  (`sessionPage`, `_DocFormSeiteAb`, `viewer-forms-6.5`).
- The session implements PDFium's eight required `FPDF_FORMFILLINFO`
  callbacks; `pdfium::editstate` hands back what they report (2.81,
  2.82). Timers only hand out an id -- the caret does not blink.
- `editbegin` refuses a second session on the same document (2.84).
- `formfill` works during a session (2.79).

**New commands**

- `pdfium::formcheck` -- what is *suspicious*, next to `formfields`
  which says what is *there*. Three codes: `EMPTY_AP`, `VALUE_NO_AP`,
  `CHOICE_VALUE_INVALID` (2.94, 2.95).
- `pdfium::edittext` -- `{name text}` of the focused field before the
  value is committed (2.89 to 2.91).
- `pdfium::editstate` -- dirty rectangle, cursor shape, changed flag.
- `formfields` gained `apLength` as its eighth element (2.85).

**Filling and drawing**

- A radio group is named by its option, not just switched on.
- A field on several pages gets the appearance copied to widgets of the
  same size (2.74).
- `editclick` answers `0` nothing, `1` a field, `2` PDFium reacted
  without a field there -- that third case is an entry in an open
  dropdown (2.88).
- `editrender` no longer crashes when it is the very first call
  (`EnsureTk`, 2.86).
- `pdfview` draws form fields unless `-forms 0` (pdfview-9.1).

**Viewer**

- Outlines every field on the page, and leaves them out over an open
  dropdown (4.1, 4.2, 6.7).
- Shows `AP` in the field list, fills radio groups and choice fields
  properly, arrow keys work on the page, a value shows up as soon as it
  is chosen (5.1 to 5.4, 6.3, 6.4, 6.6).
- `VIEWER4_SPUR=/tmp/spur.txt` records every call to the binding with
  its answer and every click in both coordinate systems (6.1).
- Page size is fetched once per page, not once per field (6.2).

**Diagnosis**

- `tools/katalog.tcl` measures what PDFium draws per field and from
  what. **No appearance stream is better than an empty one**: with an
  empty one PDFium draws nothing, with none at all it builds one (2.92).
  `/NeedAppearances` makes it discard a good stream (2.93, 2.96).
  Firefox is no reference -- pdf.js never looks at the stream; Chrome's
  pale boxes are its own interface layer over it.
- `tools/pruefe-baum.sh` says what this directory would actually load,
  and names a stale library or a Windows index as such.
- The Windows package indexes return early on other platforms.

**Housekeeping**

- Every `Tcl_DecrRefCount` has its `Tcl_IncrRefCount`; `localtime_r`
  gets its POSIX feature macro; the page-restore rule sits in one
  function instead of five; every fixture shows something when opened.

### Older versions

The history of 0.6.3 and earlier lives in
[doc/changes-history.md](doc/changes-history.md). It is kept because a
line of it explains why something is the way it is today -- but not in
the file that is supposed to show how the package is used.
