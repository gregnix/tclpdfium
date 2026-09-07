# tclpdfium

Tcl/Tk binding for PDFium — Google's PDF rendering engine (BSD license).

The first PDFium binding for Tcl. Enables PDF rendering, text extraction,
metadata, search, bookmarks, form fields and annotations directly from
Tcl/Tk — and, since 0.4, creating and editing PDFs (import/merge/split,
delete/rotate pages, crop, embed images, save).

**Version:** 0.6.3  
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

102 tests, 0 failures with a display. **Without one, six are skipped
rather than failing**: `render` needs a Tk photo and Tk needs a display.
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

### 0.6.3

- **Typing into a page** through a session -- see below.
- **`::pdfium::images`** reports image resolution -- see below.
- **`::pdfium::formfill`** fills fields; **`::pdfium::formfields`** reads
  through the form interface, with changed type names -- see below.
- **The viewer fills forms.** `app/viewer4.tcl`: click a field **on the
  page** to select it, double click to fill it -- a text field asks for a
  value, a check box toggles. The same works from the field list. The tab
  carries the field count, and a document with fields but no bookmarks
  opens on it -- otherwise the fields sat in a tab nobody looked at.

- **`::pdfium::addannot` / `::pdfium::delannot`** create and remove
  annotations: `highlight`, `underline`, `strikeout`, `squiggly`,
  `square`, `text`. The third way to mark a word, next to stamping and
  flattening -- and the only one that stays removable and
  machine-readable. No appearance stream is written; viewers draw text
  markup from `/QuadPoints` and `/C`, and a test measures that PDFium
  does.

- **`::pdfium::formfill`** fills text, combo-box, check-box and
  radio-button fields through
  PDFium's form-fill environment, so the appearance is rebuilt by the
  engine -- not by writing `/V` and hoping. A field with several widgets
  is filled by one call.

- **Field type names changed** with it: `button` became `pushbutton`,
  `checkbox` or `radiobutton`; `choice` became `combobox` or `listbox`.
  Code testing for the old names will no longer match, and the entry
  grew from three elements to five.

- **`formfields` reports the field's `/TU`**, the text a viewer shows as
  a tooltip -- "Empfänger, Name und Anschrift" instead of `f_kunde_2`.
  `app/viewer4.tcl` shows it in the list and keeps the technical name in
  a column beside it, because that is the one you fill by.

- **Choice fields report their permitted values.** `formfields` used to
  say `combobox` and keep quiet about which values were allowed; and
  `formfill` reported a success while the value stayed empty, because a
  combo box without the edit flag cannot be written to. It now reads the
  value back, navigates with the arrow keys to the option whose label
  matches, and names the permitted values when there is no match. List
  boxes too.

- **`::pdfium::formfields` reads through the form interface.** `/T`, `/V`
  and `/FT` may be inherited from the parent field; reading them off the
  widget returned empty type, name and value for a field with several
  widgets, and a nested name without its parent. Each entry now also
  carries the flags and the rectangle.

- **`::pdfium::signatures`** reports the parts of each signature.
  **Not a verification** -- see below.

- **Embedded files:** `::pdfium::attachments`, `::pdfium::attachment`,
  `::pdfium::addattachment`, `::pdfium::delattachment`. Reading and
  **writing**, natively -- `tclpdfreader` goes through qpdf for this.
  Addressed by index, since names need not be unique; contents are byte
  arrays, since an attachment is arbitrary binary material. Note that
  `delattachment` removes the entry, not the data.

- **`::pdfium::catalog`** reports `tagged` and `language` for the whole
  document. Asked indirectly via an empty structure tree, "not tagged"
  cannot be told apart from "this page has nothing in it"; and without
  `/Lang` a screen reader pronounces German text in English.

- **`::pdfium::pageobjects -marks 1`** reports the **names** of the
  marked-content marks an object sits in -- `Artifact`, `OC`, `P` and so
  on. Without them an artifact (a running header, a page number) cannot
  be told apart from untagged content: both carry no MCID, and PDFium
  reports the same `-1` for either.

  Only the names. The parameters of an `OC` mark come back as type 0, so
  PDFium says *that* an object is in a layer but not which one.

  Without `-marks` the entry stays a triple, as before.

### 0.6.2

- **`::pdfium::search -rects 1`** returns the rectangles of each hit, not
  only the character position. A hit becomes `{startpos count {rect ...}}`,
  each rect `{left bottom right top}` in points, page coordinates, origin
  bottom left -- the numbers a strike-through line or a highlight is drawn
  with. Until now the position said *that* something is there, not *where*;
  crossing a word out in a foreign PDF was not possible.

  Several rectangles per hit are normal: a match can run across a line
  break or sit in more than one text run, and PDFium returns one rectangle
  per contiguous piece. Returning only the first would draw the line-break
  case silently wrong.

- **`::pdfium::render -clip {left bottom right top}`** renders only that
  part of the page. Every zoom used to build the whole page; on an A0
  drawing that is the difference between usable and not.

- **`::pdfium::render -printing 1`** renders the way a printer would
  (`FPDF_PRINTING`). This makes `/Usage /Print /PrintState /OFF`
  measurable instead of believed. Measured on pdf4tcl's demo-layers.pdf:
  58656 dark pixels on screen, 52782 when printing, and the difference is
  exactly the layer marked `-print 0`.

- **`::pdfium::pageobjects`** reports what a page is made of: one entry
  `{index type {left bottom right top}}` per object, with type `text`,
  `path`, `image`, `shading`, `form` or `unknown`. `gettext` says what is
  on the page, `structure` how it is tagged; what it is drawn from was
  not available.

- **Typing into a page:** `::pdfium::editbegin`, `editclick`, `editchar`,
  `editkey`, `editrender`, `editend`. A session, because focus and caret are state that
  every other command throws away per call. `tab` moves to the next
  field. Typing inserts at the caret; `formfill` replaces. In
  `app/viewer4.tcl` it just works: click into a field on the page and
  type, tab moves on -- no mode to switch on first.

- **`::pdfium::pageobjects -fonts 1`** reports face, size, flags and
  whether the font is embedded. The last one is the portability
  question: a face that is not embedded looks different on another
  machine, and that only shows up there.

- **`::pdfium::images`** reports pixel size and the resolution each image
  actually lands on the paper with. A scan can look fine on screen and
  come out flat in print; this says so beforehand. The figure comes from
  PDFium, which accounts for the transformation matrix -- "width divided
  by points" would be wrong for a rotated image, and wrong without
  looking wrong.

- **Embedded files:** `::pdfium::attachments`, `::pdfium::attachment`,
  `::pdfium::addattachment`, `::pdfium::delattachment`. Reading and
  **writing**, natively -- `tclpdfreader` goes through qpdf for this.
  Addressed by index, since names need not be unique; contents are byte
  arrays, since an attachment is arbitrary binary material. Note that
  `delattachment` removes the entry, not the data.

- **`::pdfium::catalog`** reports `tagged` and `language` for the whole
  document. Asked indirectly via an empty structure tree, "not tagged"
  cannot be told apart from "this page has nothing in it"; and without
  `/Lang` a screen reader pronounces German text in English.

- **`::pdfium::pageobjects -marks 1`** reports the names of the
  marked-content marks an object sits in. Without them an artifact (a
  running header, a page number) cannot be told apart from untagged
  content: both carry no MCID.

- **`::pdfium::charboxes`** gives one rectangle per character, optionally
  for a `-range {start count}` -- exactly the two numbers `search` returns
  without `-rects`. Where a single character sits was not available.

- **`::pdfium::mctext -boxes 1`** adds the rectangle of each text object.
  It returns a different shape (triples) on purpose: appending a third
  element to the flat alternating list would silently break `dict get`
  at the caller.

- **`::pdfium::render -forms 1`** draws form fields as well, and the
  viewer uses it. Without it a filled field is listed by `formfields` but
  missing from the picture: PDFium draws widget annotations through the
  form layer, not with the page contents.

- **`::pdfium::flatten`** burns annotations and form fields into the page
  contents. Afterwards they are drawing -- not clickable, not removable,
  but not dependent on the viewer either. The opposite of overlaying:
  there the original stays untouched, here it is changed. Returns
  `nothing` when there is nothing to burn in, which is not an error.

- **The viewer searches and highlights.** `app/viewer4.tcl` has a search
  box; hits are marked on the page. Possible only since `search -rects 1`
  -- before, a hit said the word is on the page, not where.

- **`search` reads its options in pairs.** Only one pair at a fixed
  position was read before, so `search $doc 0 word -case 1 -rects 1` would
  have dropped the rectangles without a word. An option without a value is
  now reported.

### 0.6.1

- **`::pdfium::mctext`** returns the text of a page grouped by
  marked-content ID, in the order the objects sit in the content stream.
  Together with `structure`, which gives the order of the structure tree,
  this makes the READING ORDER measurable: does a document read the way it
  is tagged, or the way it happens to be drawn? A screen reader follows the
  tree, and no validator checks this -- a conformant file can be tagged in
  one order and drawn in another. Text objects without a marked-content ID
  land under the key `-1`; in a tagged document that key should not appear,
  since it is content a screen reader cannot reach.

- **Unknown options are refused rather than ignored.** `render` knows
  `-dpi`, `-width` and `-imagename` and used to drop anything else without
  a word: `render -scale 2.0` -- an option only `print` has -- produced the
  same image for every factor, with no way of telling why. `render` and
  `search` now name the offending option and the ones they accept, and an
  odd number of trailing words is refused instead of silently dropping the
  last one. `search -case` also checks its value, which it did not.

- **`::pdfium::structure`** returns the tagged-PDF structure tree of a page
  as nested Tcl dicts: role, `/Alt`, `/ActualText`, `/Lang`, `/ID`,
  attributes such as `/Scope` and `/ListNumbering`, and the marked-content
  ids. `mcids` is a **list** — an element spanning a page break has more
  than one, and PDFium's single-value getter drops the rest silently. Pages
  without a structure tree return an empty list rather than an error, since
  most PDFs have none. See `examples/structure-dump.tcl`.

  The point of the command is a second opinion: PDFium is the engine in
  Chrome and Edge, so it reads a tagged document the way a large share of
  readers do. In its first use it showed that structure elements living in
  a form XObject do not appear at all — in a file that veraPDF accepts as
  PDF/UA.

### 0.6.0

- **Windows printing (GDI/DEVMODE).** PDFium renders into a Windows device
  context, so printing works without any external program. New commands, all
  Windows-only: `::pdfium::canprint`, `::pdfium::printers`,
  `::pdfium::defaultprinter`, `::pdfium::papers`, `::pdfium::print`, and
  `::pdfium::printercaps`. On other platforms `canprint` returns 0 and the
  rest are not created, so callers branch on it instead of catching errors.
  Built and verified with both MSVC/nmake and MSYS2; Brother QL label
  printing works. Full option reference in `doc/api-reference.md`.
- **`::pdfium::print` covers the label and booklet cases.** Per-cell N-up with
  a gutter, margins measured from the paper edge, exact 1:1 (`-fit 0`), and
  scaling computed here rather than handed to the driver — `dmScale`/`dmNup`
  are reported by many drivers and then silently ignored.
- **`::pdfium::printercaps` answers the borderless question** by measuring the
  printable area of each form instead of trusting a flag.
- **Build wiring for the print path.** `configure.ac` adds
  `-lgdi32 -lwinspool` on Windows; `win/makefile.vc` takes `PDFIUMDIR`, links
  the pdfium import library and the Tk stubs (`PROJECT_REQUIRES_TK`), and
  stops with a clear error if pdfium is not found rather than failing at the
  final link.

### 0.5.3

- **TEA build.** `configure && make && make test && make install`, like any other
  Tcl extension. `--with-pdfium` points at the SDK; without it,
  `vendor/pdfium-<platform>/` is found automatically. `configure` probes for the
  link library name rather than guessing it, sets the `$ORIGIN` runpath, and
  `make install` copies the PDFium runtime next to the extension. The
  hand-written Makefile is gone.
- **One directory serves both Tcl generations.** TEA names the libraries
  `libpdfiumtcl<ver>.so` and `libtcl9pdfiumtcl<ver>.so`; `pkgIndex.tcl` picks at
  load time.
- **`configure` aborts on a Tcl/Tk version mismatch.** Without `--with-tcl` and
  `--with-tk` the search heuristic guesses, and with several installations it may
  guess inconsistently — unversioned symlinks such as `/usr/lib/tclConfig.sh` are
  a common cause. Compiling `tk.h` from one generation against `tcl.h` from
  another used to succeed and fail later, somewhere unrelated.
- **`Tcl_Size` shim fixed.** It tested only `TCL_SIZE_MAX`. TEA passes
  `-DTcl_Size=int` on the command line for a Tcl 8 build, so the typedef expanded
  to `typedef int int;` and the compiler stopped.
- **Windows cross-build in one command** — `tools/build-windows.sh`. It compiles
  the Tcl/Tk stub libraries for MinGW from source, writes a `tclConfig.sh` for the
  target, builds in its own directory, and inspects the DLL before shipping it.
- **`tools/find-tclconfig.tcl`** lists every `tclConfig.sh`/`tkConfig.sh` on the
  machine with its version, pairs them, and prints the matching `configure` line.
  Runs anywhere a `tclsh` does, Windows included.

### 0.5.2

- **Crash fixed in `addimagebitmap`** — the command read a Tk photo without ever
  calling `Tk_InitStubs`. `tkStubsPtr` was NULL, and calling it before any
  `pdfium::render` killed the process with no message. It now initializes Tk
  lazily, exactly like `render` does, and reports a normal Tcl error when Tk is
  unavailable.
- **Commands registered with fully qualified names** (`::pdfium::open` instead of
  `pdfium::open`). The unqualified form is resolved against the *current*
  namespace; loading the package from inside a proc silently put all 26 commands
  into `::pdfium::pdfium::`. This never surfaced while `package ifneeded` loaded
  at global level — the new VFS-aware loader does not.
- **`pkgIndex.tcl` works inside a starpack.** Tcl copies only the directly loaded
  library out of a VFS, leaving `libpdfium` behind; on Linux the `$ORIGIN`
  runpath then points at the temp directory. The loader now unpacks *both*
  libraries into one directory and loads from there, so a starpack stays a single
  file. `TCLPDFIUM_TMPDIR` redirects the unpack directory when `/tmp` is `noexec`.
- **Windows / Tcl 9 cross-build works.** `make dist-windows90` no longer bails
  out: `tools/make-win-stubs.sh` compiles the Tcl and Tk stub libraries from
  source (two C files) for the MinGW toolchain. No MSYS2 `tcl9` package and no
  Windows machine required.
- **`scripts/get-pdfium.cmd`** — PDFium download for cmd.exe, using the `curl.exe`
  and `tar.exe` that ship with Windows. `setup.sh` needs a shell; this does not.
- **PDFium now lands in `vendor/pdfium-<platform>/`** so the Linux and Windows
  SDKs can coexist in one tree, which the cross-build requires.

### 0.5.1

- **Tk loaded lazily** — only `pdfium::render` initializes Tk now. Loading the
  package no longer opens the `.` window or traps a headless `tclsh` script in
  the event loop.
- **Text correct under Tcl 9** — `gettext`, `meta`, `search`, link URLs and form
  field text now convert UTF-16 ↔ UTF-8 portably (via `utf-16le` encoding)
  instead of relying on `Tcl_UniChar`, which changed width in Tcl 9. Fixes
  garbled output and broken search under Tcl 9.

---

## See Also

- [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) — prebuilt PDFium
- [pdf4tcl](https://sourceforge.net/projects/pdf4tcl/) — create PDFs
- [tkmcairo](https://github.com/gregnix/tkmcairo) — Cairo 2D graphics for Tcl
- [doc/api-reference.md](doc/api-reference.md) — full API documentation
- [INSTALL.md](INSTALL.md) — building, installing, troubleshooting

---

## License

tclpdfium: BSD  
PDFium: BSD (Apache CLA)  
Third-party licenses: see `vendor/pdfium-<platform>/licenses/`
