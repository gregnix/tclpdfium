#!/usr/bin/env tclsh
# tests/all.tcl — TEA-konforme Test-Suite-Eintrittsstelle
package require tcltest 2.5
namespace import tcltest::*

# Beim Aufruf via `make test` lädt das Makefile das Package über die
# tcltest -load Option. Bei direktem `tclsh tests/all.tcl` müssen wir
# das auto_path so erweitern, dass das frisch gebaute Paket gefunden
# wird. Das Build-Verzeichnis ist eine Ebene über tests/.
set buildDir [file dirname [file dirname [file normalize [info script]]]]
if {[file exists [file join $buildDir pkgIndex.tcl]]} {
    lappend auto_path $buildDir
}
package require pdfiumtcl

configure -testdir [file dirname [file normalize [info script]]]
configure -singleproc 1

runAllTests
