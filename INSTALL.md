# Installing tclpdfium

`pdfiumtcl` builds with TEA — `configure`, `make`, `make install`, like any other
Tcl extension.

---

## Requirements

- Linux, macOS, or Windows (MSYS2)
- Tcl/Tk 8.6 or 9.0 **with development headers**
- `gcc`, `make`

```bash
sudo apt install tcl-dev tk-dev build-essential      # Debian, Ubuntu
sudo dnf install tcl-devel tk-devel gcc make         # Fedora, RHEL
```

Without the development package there is no `tclConfig.sh`, and `configure` has
nothing to work with. That file is the whole point: it records which compiler
built this Tcl, where its headers are, and what its stub library is called.

The binding is **stub-based**: the shared library does not link `libtcl` or
`libtk`, so one binary runs on any interpreter of the same Tcl generation.
Across generations it does not — Tcl 9 is source compatible with 8.6 but not
binary compatible. Build once per generation; both results can live in the same
directory (see below).

**Tk is optional at run time.** The package loads in a plain `tclsh` with no
display. Tk stubs are initialised lazily, and only `pdfium::render` and
`pdfium::addimagebitmap` need them. Everything else — reading, text extraction,
writing, page manipulation — is headless. Tk headers *are* needed to compile.

---

## 1. Get the source

```bash
git clone https://github.com/gregnix/tclpdfium.git
cd tclpdfium
```

## 2. Get PDFium

PDFium is a prebuilt binary, about 7 MB. It is not in the repository.

```bash
bash scripts/setup.sh                             # -> vendor/pdfium-linux-x64/
PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh     # -> vendor/pdfium-win-x64/
```

On Windows without a shell: `scripts\get-pdfium.cmd`.

## 3. Build

```bash
./configure --with-tcl=/usr/lib/tcl9.0 --with-tk=/usr/lib/tk9.0
make
make check
make test
sudo make install
```

### Which Tcl? — the question that decides everything

`--with-tcl` and `--with-tk` take the **directory** holding `tclConfig.sh` resp.
`tkConfig.sh`, not the file. They decide which Tcl the extension is built
against **and installed into**.

Leave them out and a search heuristic guesses. With one Tcl installation that is
fine. With several it is not, and the failure is a nasty one: Debian ships an
*unversioned* `/usr/lib/tclConfig.sh` next to the versioned ones, and the Tcl one
and the Tk one need not point at the same generation. `configure` then compiles
`tk.h` from Tcl 9 against `tcl.h` from Tcl 8.6 — which used to succeed and fail
later, somewhere unrelated. This build aborts instead, but only because both were
found. Name them and the question does not arise.

List what is on the machine:

```bash
tclsh tools/find-tclconfig.tcl
```

It prints every `tclConfig.sh`/`tkConfig.sh` with its version, pairs them, and
gives the matching `configure` line. Entries marked `->` are unversioned
forwarders — do not use those.

### Where it installs

Into the Tcl it was built against — `<tcl-exec-prefix>/lib/pdfiumtcl<version>/`.
That is deliberate: only there does `package require` find it without anyone
touching `auto_path`.

Check before installing:

```bash
grep -E '^prefix|^exec_prefix|^libdir' Makefile
```

To install somewhere else — a staging directory, say — pass **both** prefixes:

```bash
./configure --with-tcl=... --prefix=/opt/mytcl --exec-prefix=/opt/mytcl
```

`--prefix` alone does not move the library. TEA still takes `exec_prefix` from
the Tcl configuration, and the installation falls apart: headers under the given
prefix, the library under Tcl's. The message says one thing, the file is
elsewhere.

---

## Tcl 8 and Tcl 9 side by side

Build twice, install twice, into the **same** directory:

```bash
./configure --with-tcl=/usr/lib/tcl8.6 --with-tk=/usr/lib/tk8.6
make && sudo make install
make distclean

./configure --with-tcl=/usr/lib/tcl9.0 --with-tk=/usr/lib/tk9.0
make && sudo make install
```

The `make distclean` between the two is not optional — otherwise object files of
two ABIs get mixed, and that fails at run time, not at link time.

The result:

```
/usr/lib/pdfiumtcl0.5.3/
    libpdfiumtcl0.5.3.so         Tcl 8.6
    libtcl9pdfiumtcl0.5.3.so     Tcl 9.0
    libpdfium.so                 shared by both
    pkgIndex.tcl                 picks at load time
```

TEA gives the two libraries different names, and `pkgIndex.tcl` chooses. One
directory, both interpreters:

```
$ echo 'puts [package require pdfiumtcl]' | tclsh8.6
0.5.3
$ echo 'puts [package require pdfiumtcl]' | tclsh9.0
0.5.3
```

---

## Using it

```tcl
package require pdfiumtcl

set doc [pdfium::open document.pdf]
puts "[pdfium::pagecount $doc] pages"
puts [pdfium::gettext $doc 0]
pdfium::close $doc
```

No `auto_path` fiddling — the package sits in Tcl's own library directory.

---

## Windows, natively (MSYS2)

In the **MSYS2 MinGW64 shell**. Neither cmd.exe nor PowerShell will do:
`configure` is a shell script, and there is no `make`.

```bash
tclsh tools/find-tclconfig.tcl               # what is installed?
bash scripts/setup.sh                        # PDFium

./configure --with-tcl=/c/Tcl9.0.4/lib --with-tk=/c/Tcl9.0.4/lib \
            --prefix=/c/Tcl9.0.4 --exec-prefix=/c/Tcl9.0.4
make
make check
make install
```

For the 8.x series, the same with that installation's path. `make distclean`
between the two runs.

Three things matter more here than on Linux.

**Give both prefixes.** Windows distributions are built and then moved; their
`tclConfig.sh` still carries `TCL_EXEC_PREFIX` from the machine that built them.
Without `--prefix` and `--exec-prefix`, TEA installs *there* — into a directory
that may not exist on this machine, or into somebody's staging tree. The package
then sits where nothing looks for it.

**Tcl and Tk from the same tree.** MSYS2 ships its own Tcl/Tk 8.6. Without
`--with-tk`, the search may pick `/mingw64/lib/tkConfig.sh` while Tcl comes from
`C:/Tcl/lib`. Both say "8.6", so no version check fires — and Tk stubs of one
installation get linked against Tcl stubs of another. On Windows both config
files usually live in the same `lib` directory; give both options the same path.

**The stub library is there.** Even in MSVC-built distributions: Tcl 9 ships
`libtclstub.a` (no version number — that is the new convention), Tcl 8.6 ships
`libtclstub86.a`. MinGW gcc handles both.

## Windows, from a Linux machine

No Windows box, no MSYS2:

```bash
sudo apt install mingw-w64

./tools/build-windows.sh core-9-0-2      # Tcl 9.0
./tools/build-windows.sh core-8-6-18     # Tcl 8.6
```

Result in `dist-win/windows64-tcl9/` resp. `dist-win/windows64/`: the DLL,
`pdfium.dll` beside it, and the matching `pkgIndex.tcl`. Copy the directory to
the Windows machine, into a directory on `auto_path`.

The script compiles the Tcl and Tk **stub libraries** for the MinGW toolchain
straight from the source tree — two C files. An MSYS2 `tcl9` package is not
needed for that: a stub library is not derived from a DLL (that would be an
*import* library, and would need `link.exe`), it is just `generic/tclStubLib.c`,
compiled. The script also writes a `tclConfig.sh` for the target, without which
TEA does not know what it is building against.

Then it inspects the DLL: no `tcl90.dll`/`tk90.dll` among the dependencies (the
stubs would not be doing their job), MinGW runtime linked statically, and the
right Tcl generation in the `Tcl_InitStubs` call.

---

## Starpacks and virtual filesystems

Tcl copies a *directly loaded* extension out of a VFS into a temp directory
before handing it to the OS loader — but not its dependencies. `libpdfium` would
stay behind in the VFS, where the loader cannot see it.

The generated `pkgIndex.tcl` unpacks **both** libraries into one temp directory,
so they find each other there. Under Unix via the `$ORIGIN` runpath; under
Windows because the loader searches the directory of the loaded DLL. Different
mechanisms, same result: **a starpack stays a single file.**

If `/tmp` is mounted `noexec`, `dlopen` refuses. Redirect:

```bash
export TCLPDFIUM_TMPDIR=/var/tmp
```

---

## Troubleshooting

**`configure: error: Tcl and Tk versions do not match`**
The heuristic picked Tcl from one installation and Tk from another. The message
names both and suggests a matching pair. See *Which Tcl?* above.

**`configure: error: PDFium not found`**
Run `bash scripts/setup.sh` first, or pass `--with-pdfium=DIR`.

**`make check` reports a libtcl/libtk link**
The extension is linked against Tcl directly instead of through the stubs. It
would then run only with the exact Tcl it was built against. This is not a link
error — it surfaces at the user's site.

**`make test`: `libpdfium.so: cannot open shared object file`**
`make test` loads the extension from the *build* directory, so `$ORIGIN` points
there — and PDFium is not there. The `pdfium-local` target copies it, and runs
automatically. If the test was invoked some other way, copy `libpdfium.so` next
to the built library.

**`version conflict for package "tcl": have 9.0.4, need 8.5`**
An 8.x build loaded into Tcl 9. Not a link error: the library loaded, and
`Tcl_InitStubs` rejected it. Build for the right generation.

**`package require pdfiumtcl` finds nothing**
`make install` probably wrote somewhere unexpected. Check
`grep -E '^prefix|^exec_prefix|^libdir' Makefile` — on Windows especially, see
above.

**Segfault instead of an error from `pdfium::addimagebitmap`**
Fixed in 0.5.2. Earlier builds reached into the Tk stub table without having
called `Tk_InitStubs`; `tkStubsPtr` was NULL and the process died silently.
Replace the binary.

**`cannot open PDF '...' (PDFium error 3)`**
Not a valid PDF — corrupt, or actually PostScript/EPS (`pcal` output, for
instance). PDFium reads PDF only. Convert first (`ps2pdf in.ps out.pdf`), or
rewrite a broken PDF with `qpdf in.pdf out.pdf`. Error codes are listed in
[doc/api-reference.md](doc/api-reference.md).
