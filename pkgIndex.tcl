# pkgIndex.tcl -- tclpdfium 0.4
#
# Waehlt automatisch das passende Unterverzeichnis:
#   linux64/          Linux  x86_64, Tcl 8.x
#   linux64-tcl9/     Linux  x86_64, Tcl 9.x
#   windows64/        Windows x64,   Tcl 8.x
#   windows64-tcl9/   Windows x64,   Tcl 9.x
#
# Hinweis: pointerSize statt wordSize verwenden --
# auf Windows (MSVC/Magicsplat) ist wordSize=4 auch bei 64-bit Tcl
# (LLP64-Modell), pointerSize=8 ist der zuverlaessige 64-bit Indikator.

set _ver  0.4
set _bits 64
if {$::tcl_platform(pointerSize) == 4} { set _bits 32 }

if {$::tcl_platform(platform) eq "windows"} {
    set _subdir "windows${_bits}"
    # Hinweis: windows64-tcl9 nicht verfuegbar (kein MinGW Tcl-9-Stub fuer Windows)
    # Tcl-9-Windows-Build erfordert natives MSYS2 mingw-w64-x86_64-tcl9 Paket
} else {
    set _subdir "linux${_bits}"
}

if {[package vsatisfies [package provide Tcl] 9.0-]} {
    append _subdir "-tcl9"
}

package ifneeded pdfiumtcl $_ver \
    [list load [file join $dir $_subdir \
        pdfiumtcl[info sharedlibextension]] Pdfiumtcl]

unset _ver _bits _subdir
