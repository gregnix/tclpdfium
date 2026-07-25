# Windows printing (GDI/DEVMODE)

Windows printing is **already built into `generic/tclpdfiumtcl.c`** and
proven in practice: built with MSVC/nmake and with MSYS2, Brother QL
printing works. This file records what the feature covers.

PDFium can render into a Windows device context, so printing works without
any external program. The commands exist only on Windows; elsewhere
`::pdfium::canprint` returns 0 and the other print commands are not created.

    ::pdfium::canprint             -> 0/1
    ::pdfium::printers             -> list of printer names
    ::pdfium::defaultprinter       -> default printer
    ::pdfium::papers ?printer?     -> forms the driver offers
    ::pdfium::print doc ?opts?     -> number of pages printed
    ::pdfium::printercaps ...      -> sheet geometry, borderless check

The full option reference for `::pdfium::print` and `::pdfium::printercaps`
is in `doc/api-reference.md`, section *Printing (Windows only)*.

## System libraries

The GDI/DEVMODE path links against `gdi32` and `winspool` — Windows system
libraries, always present. The build wiring is in place: `configure.ac`
adds `-lgdi32 -lwinspool` on Windows, `win/makefile.vc` lists them in
`PRJ_LIBS`. See `INSTALL.md` for the MSVC and MSYS2 build recipes.

## Provenance

The code was assembled from four insertion blocks — `pdfium-print-win.c`,
`-devmode.c`, `-layout.c`, `-caps.c` — that are no longer part of the
repository; their content lives entirely in the main source. 

## Still open

* **Auto-cut** (`dmDriverExtra`): a Brother QL cut-behaviour setting that
  sits in the private DEVMODE area. Overwriting only public DEVMODE fields
  passes the cut behaviour through unchanged. A clean solution would expose
  a DEVMODE blob via `::pdfium::getdevmode` — sketched, not built.
