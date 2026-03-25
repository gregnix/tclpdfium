# pkgIndex.tcl — pdfiumtcl 0.3
# Unterstützt Linux (.so) und Windows (.dll), Tcl 8.5+ und 9.0

if {[string match "windows*" $::tcl_platform(os)]} {
    set _ext dll
} else {
    set _ext so
}

package ifneeded pdfiumtcl 0.3 \
    [list load [file join $dir pdfiumtcl.$_ext] Pdfiumtcl]

unset _ext
