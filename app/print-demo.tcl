#!/usr/bin/env wish
# print-demo.tcl -- the smallest complete example of Windows printing
#                   with pdfiumtcl.
#
# A tiny GUI: pick a printer, pick a mode, hit Print. Two modes:
#
#   Document   send the whole PDF to the printer, scaled to the page
#              (-fit 1). This is the ordinary case.
#
#   QL label   send one page 1:1 to a Brother QL, sized to the tape.
#              Follows the rule from the field notes: try exact
#              -paperw/-paperh first, fall back to the named band form
#              "<mm>mm" if the driver rejects the free size.
#
# Windows only. On other platforms ::pdfium::canprint returns 0 and the
# window explains why instead of offering a dead button.
#
#   wish print-demo.tcl ?file.pdf?

package require Tk
package require pdfiumtcl

set PDF [expr {[llength $argv] > 0 ? [lindex $argv 0] : ""}]

wm title . "pdfiumtcl print demo"
wm resizable . 0 0

# ---------------------------------------------------------------- no print?
# If this build cannot print, say so plainly and stop. Better than a
# greyed-out button the user has to guess about.
if {![::pdfium::canprint]} {
    label .msg -justify left -padx 16 -pady 16 -text \
"This pdfiumtcl build cannot print.

::pdfium::canprint returned 0. Printing is Windows only and needs a build
with the GDI/DEVMODE path (pdfiumtcl 0.6+). On Linux and macOS use CUPS
(lp) instead."
    pack .msg
    return
}

# ---------------------------------------------------------------- state
set ::mode    document
set ::printer [expr {[catch {::pdfium::defaultprinter} p] ? "" : $p}]
set ::band    54
set ::copies  1
set ::pdfpath $PDF

# ---------------------------------------------------------------- widgets
set f [frame .f -padx 12 -pady 12]
pack $f

# PDF file
label $f.lpdf -text "PDF file:" -anchor w
entry $f.epdf -width 34 -textvariable ::pdfpath
button $f.bpdf -text "..." -width 3 -command {
    set p [tk_getOpenFile -filetypes {{PDF {.pdf}} {All *}}]
    if {$p ne ""} { set ::pdfpath $p }
}
grid $f.lpdf $f.epdf $f.bpdf -sticky w -padx 2 -pady 3

# Printer
label $f.lpr -text "Printer:" -anchor w
ttk::combobox $f.epr -width 32 -textvariable ::printer \
    -values [expr {[catch {::pdfium::printers} l] ? {} : $l}]
grid $f.lpr $f.epr -sticky w -padx 2 -pady 3

# Mode
label $f.lmode -text "Mode:" -anchor w
frame $f.mode
radiobutton $f.mode.doc -text "Document (fit to page)" \
    -variable ::mode -value document -command refreshMode
radiobutton $f.mode.ql  -text "QL label (1:1)" \
    -variable ::mode -value ql -command refreshMode
pack $f.mode.doc $f.mode.ql -side left
grid $f.lmode $f.mode -sticky w -padx 2 -pady 3

# QL band (only shown in ql mode)
label $f.lband -text "Band (mm):" -anchor w
ttk::combobox $f.eband -width 6 -textvariable ::band \
    -values {12 29 38 50 54 62 102}
grid $f.lband $f.eband -sticky w -padx 2 -pady 3

# Copies (only in document mode)
label $f.lcop -text "Copies:" -anchor w
spinbox $f.ecop -width 6 -from 1 -to 99 -textvariable ::copies
grid $f.lcop $f.ecop -sticky w -padx 2 -pady 3

# Print button + status
button $f.print -text "Print" -command doPrint
grid $f.print -columnspan 3 -pady {10 4}
label $f.status -text "" -anchor w -width 46 -justify left
grid $f.status -columnspan 3 -sticky w

# --------------------------------------------------- show/hide by mode
proc refreshMode {} {
    if {$::mode eq "ql"} {
        grid .f.lband .f.eband
        grid remove .f.lcop .f.ecop
    } else {
        grid remove .f.lband .f.eband
        grid .f.lcop .f.ecop
    }
}
refreshMode

# --------------------------------------------------- the actual printing
proc doPrint {} {
    if {$::pdfpath eq "" || ![file readable $::pdfpath]} {
        status "Pick a readable PDF file first." err ; return
    }
    if {$::printer eq ""} {
        status "No printer selected." err ; return
    }
    if {[catch {set doc [::pdfium::open $::pdfpath]} e]} {
        status "Cannot open PDF: $e" err ; return
    }

    set rc [catch {
        if {$::mode eq "ql"} {
            printLabel $doc
        } else {
            printDocument $doc
        }
    } res]
    catch {::pdfium::close $doc}

    if {$rc} {
        status "Print failed: $res" err
    } else {
        status $res ok
    }
}

# Ordinary document: scale each page to the printable area.
proc printDocument {doc} {
    set n [::pdfium::print $doc \
               -printer $::printer -fit 1 -copies $::copies]
    return "Sent $n page(s) to $::printer."
}

# QL label: one page, 1:1, sized to the tape. The band prints slightly
# narrower than its nominal width; the driver handles that when the size
# is named. Exact free size first, named band as fallback -- some QL
# drivers reject a free -paperw/-paperh and only accept their own form.
proc printLabel {doc} {
    set band $::band
    # a QL label PDF is one page; print page 0
    set base [list -printer $::printer -from 0 -to 0 -fit 0]
    if {[catch {
        ::pdfium::print $doc {*}$base -paper "${band}mm"
    } res]} {
        # fall back to an explicit width if the named form is unknown
        return -code error $res
    }
    return "Sent one ${band}mm label to $::printer."
}

proc status {text {kind info}} {
    set col [dict get {info black ok #185818 err #a01818} $kind]
    .f.status configure -text $text -foreground $col
}

status "Ready. Printer: $::printer"
