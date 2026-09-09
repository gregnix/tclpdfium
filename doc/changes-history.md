# Änderungen vor 0.6.4

Die Liste der laufenden Fassung steht im README. Was hier steht, ist
Geschichte -- aufgehoben, weil eine Zeile davon erklärt, warum etwas
heute so ist, aber nicht in einer Datei, die zeigen soll, wie man das
Paket benutzt.

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
