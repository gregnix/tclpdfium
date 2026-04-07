#!/usr/bin/env tclsh
# test-etikett-54mm.tcl
#
# Erzeugt ein Test-Etikett 54 x H mm fuer den Brother QL-820
#
# Aufruf:  tclsh test-etikett-54mm.tcl ?hoehe_mm?
# Beispiel: tclsh test-etikett-54mm.tcl 120
# Default:  56mm

package require pdf4tcl

proc mm {mm} { expr {$mm / 25.4 * 72.0} }

set W_mm  54.0
set H_mm  [expr {[llength $argv] > 0 ? double([lindex $argv 0]) : 56.0}]

set W_pt  [mm $W_mm]
set H_pt  [mm $H_mm]
set M_pt  [mm 3.0]

set pdf [::pdf4tcl::new %AUTO% \
    -paper [list $W_pt $H_pt] \
    -orient true \
    -compress 1]

$pdf startPage

# Rahmen
$pdf setStrokeColor 0 0 0
$pdf setLineWidth 0.5
$pdf rectangle $M_pt $M_pt \
    [expr {$W_pt - 2*$M_pt}] \
    [expr {$H_pt - 2*$M_pt}]

# Titel
$pdf setFont 14 Helvetica-Bold
$pdf setFillColor 0 0 0
$pdf text "Test-Etikett" \
    -x [expr {$W_pt / 2.0}] \
    -y [expr {$M_pt + 6}] \
    -align center

# Trennlinie
set y1 [expr {$M_pt + 18}]
$pdf setLineWidth 0.3
$pdf line $M_pt $y1 [expr {$W_pt - $M_pt}] $y1

# Infozeilen
$pdf setFont 9 Helvetica
set y [expr {$y1 + 10}]
set lh 12

foreach zeile [list \
    "Breite:  54 mm" \
    "Hoehe:   ${H_mm} mm" \
    "Drucker: QL-820NWB" \
    "DPI:     300" \
    "Format:  PDF 54mm" \
] {
    $pdf text $zeile \
        -x [expr {$M_pt + 3}] \
        -y $y
    set y [expr {$y + $lh}]
}

# Trennlinie 2
set y2 [expr {$y + 4}]
$pdf line $M_pt $y2 [expr {$W_pt - $M_pt}] $y2

# Zeitstempel
set bc_x [expr {$M_pt + 3}]
set bc_y [expr {$y2 + 6}]
set bc_w [expr {$W_pt - 2*$M_pt - 6}]
set bc_h [mm 12.0]

$pdf setStrokeColor 0.5 0.5 0.5
$pdf setLineWidth 0.3
$pdf rectangle $bc_x $bc_y $bc_w $bc_h

$pdf setFont 9 Helvetica
$pdf setFillColor 0 0 0
set timenow [clock format [clock seconds] -format {%Y-%m-%d %H:%M:%S}]
$pdf text $timenow \
    -x [expr {$bc_x + $bc_w/2.0}] \
    -y [expr {$bc_y + $bc_h/2.0 - 3}] \
    -align center

# Fusslinie
set yf [expr {$H_pt - $M_pt - 12}]
$pdf setFillColor 0 0 0
$pdf setFont 7 Helvetica
$pdf text "pdf4tcl + pdfiumtcl  |  54mm QL-820 Test" \
    -x [expr {$W_pt / 2.0}] \
    -y $yf \
    -align center

$pdf endPage

set outfile "etikett54.pdf"
$pdf write -file $outfile
$pdf destroy

puts "PDF: $outfile  (${W_mm} x ${H_mm} mm)  $timenow"

# PNG rendern
lappend auto_path [file dirname [info script]]
package require pdfiumtcl

set doc [pdfium::open $outfile]
pdfium::render $doc 0 -width 590 -imagename qlpage
qlpage write img/etikett54.png -format png
pdfium::close $doc

set iw [image width  qlpage]
set ih [image height qlpage]
# Hoehe direkt aus PDF -- nicht per DPI-Rueckrechnung (Rundungsfehler)

puts "PNG: img/etikett54.png  ${iw} x ${ih} px  (54 x ${H_mm} mm)"
