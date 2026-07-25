# ==========================================================================
#  test-print-windows.tcl -- print diagnostics for pdfiumtcl on Windows
#
#      tclsh test-print-windows.tcl ?-printer NAME? ?-pdf FILE? ?-print? ?-mode N?
#
#  Without -print nothing is printed: the script only collects what the
#  driver reports. That is the point -- the test data in
#  test-pdfium-backend.tcl is modelled, not measured.
#
#  Final output: a Tcl literal of the real form list that can be pasted
#  straight into the mock of the test script.
#
#  With -print ONE page is sent to the selected printer. On label
#  printers that costs stock -- hence it is not the default.
# ==========================================================================

set opt(printer) ""
set opt(pdf)     ""
set opt(print)   0
set opt(mode)    0
set opt(paper)   ""
set opt(paperw)  ""
set opt(paperh)  ""

for {set i 0} {$i < [llength $argv]} {incr i} {
    switch -- [lindex $argv $i] {
        -printer { set opt(printer) [lindex $argv [incr i]] }
        -pdf     { set opt(pdf)     [lindex $argv [incr i]] }
        -print   { set opt(print)   1 }
        -mode    { set opt(mode)    [lindex $argv [incr i]] }
        -paper   { set opt(paper)   [lindex $argv [incr i]] }
        -paperw  { set opt(paperw)  [lindex $argv [incr i]] }
        -paperh  { set opt(paperh)  [lindex $argv [incr i]] }
        -help - --help {
            puts "usage: tclsh test-print-windows.tcl ?options?"
            puts "  -printer NAME   printer to query (default: system default)"
            puts "  -pdf FILE       document for the test print"
            puts "  -print          actually print one page"
            puts "  -mode N         FPDF print mode: 0=EMF (default),"
            puts "                  2/3=PostScript, 4/5=PS passthrough,"
            puts "                  6=EMF with image masks"
            puts "  -paper FORM     form name or code for the geometry probe"
            puts "  -paperw MM      custom width, needs -paperh"
            puts "  -paperh MM      custom height, needs -paperw"
            puts ""
            puts "Continuous tape: the named forms carry only a nominal"
            puts "length (29 mm on a QL-820NWB). For a label of a given"
            puts "length use -paperw/-paperh instead of -paper."
            exit 0
        }
        default {
            puts stderr "unknown option: [lindex $argv $i]"
            exit 2
        }
    }
}

proc head {t} { puts "" ; puts "== $t" ; puts [string repeat - 66] }
proc row  {k v} { puts [format "  %-24s %s" $k $v] }

# --------------------------------------------------------------------------
head "Environment"
# --------------------------------------------------------------------------
row "Tcl"        [info patchlevel]
row "Platform"  "$::tcl_platform(platform) / $::tcl_platform(os)"
row "Machine"   $::tcl_platform(machine)

if {$::tcl_platform(platform) ne "windows"} {
    puts ""
    puts "This script checks the Windows print path (GDI/DEVMODE)."
    puts "On Linux/macOS CUPS is in charge -- there is nothing to measure."
    exit 1
}

if {[catch {package require pdfiumtcl} e]} {
    puts "" ; puts "pdfiumtcl cannot be loaded: $e"
    puts "auto_path:" ; foreach p $::auto_path { puts "    $p" }
    exit 1
}
row "pdfiumtcl"  [package provide pdfiumtcl]

foreach cmd {canprint printers defaultprinter papers print} {
    if {![llength [info commands ::pdfium::$cmd]]} {
        puts ""
        puts "MISSING: ::pdfium::$cmd"
        puts "The DLL was built without print support."
        puts "Check: gdi32/winspool linked? source built with the print block?"
        exit 1
    }
}
row "canprint"   [::pdfium::canprint]

# --------------------------------------------------------------------------
head "Printers"
# --------------------------------------------------------------------------
set printers [::pdfium::printers]
if {![llength $printers]} {
    puts "  no printers found"
    exit 1
}

set default ""
catch {set default [::pdfium::defaultprinter]}

foreach p $printers {
    row [expr {$p eq $default ? "* $p" : "  $p"}] ""
}
puts ""
row "Default" [expr {$default ne "" ? $default : "(none)"}]

set printer $opt(printer)
if {$printer eq ""} { set printer $default }
if {$printer eq ""} { set printer [lindex $printers 0] }

if {$printer ni $printers} {
    puts ""
    puts "WARNING: '$printer' is not in the list -- typo?"
}

# --------------------------------------------------------------------------
head "Forms of '$printer'"
# --------------------------------------------------------------------------
if {[catch {::pdfium::papers $printer} forms]} {
    puts "  error: $forms"
    set forms {}
}

if {![llength $forms]} {
    puts "  no forms reported"
} else {
    puts [format "  %-32s %6s %9s %9s" "Name" "Code" "Width" "Height"]
    foreach f $forms {
        lassign $f name code w h
        puts [format "  %-32s %6d %7.1fmm %7.1fmm" $name $code $w $h]
    }
    puts ""
    row "Count" [llength $forms]
}

# --------------------------------------------------------------------------
head "Label matching"
# --------------------------------------------------------------------------
# Only if printlib is present: check whether the matcher finds a form
# for every known DK format. This is the real regression test -- it runs
# against actual driver data instead of the mock.
if {[catch {package require printlib} e]} {
    puts "  printlib cannot be loaded ($e) -- skipped"
} elseif {![llength [info commands ::printlib::_pdfiumFormForLabel]]} {
    puts "  printlib without the pdfium backend -- skipped"
} elseif {![::printlib::isLabelPrinter $printer]} {
    puts "  '$printer' is not a label printer -- skipped"
} else {
    set codes [::printlib::labelformats $printer]
    if {![llength $codes]} {
        puts "  no DK formats known for this model"
    } else {
        set hit 0 ; set miss 0
        foreach code $codes {
            set meta [::printlib::labelsize $code]
            set form ""
            catch {set form [::printlib::_pdfiumFormForLabel \
                                 $printer $code ""]}
            if {$form ne ""} {
                incr hit
                puts [format "  %-12s -> %s" $code $form]
            } else {
                incr miss
                if {[dict exists $meta width_mm]} {
                    set dim "[dict get $meta width_mm] mm"
                } else {
                    set dim "d[dict get $meta diameter_mm] mm"
                }
                puts [format "  %-12s -- no form (%s, %s)" \
                          $code $dim [dict get $meta kind]]
            }
        }
        puts ""
        row "matched" $hit
        row "no form" $miss
        puts ""
        puts "  Note: the match ran without a PDF, so without a length."
        puts "  Continuous tapes are matched on width alone here."
    }
}

# --------------------------------------------------------------------------
head "Sheet geometry"
# --------------------------------------------------------------------------
# Hardware margins. The printable area is smaller than the sheet on every
# ordinary printer; borderless forms are the exception and show margins of
# zero or below (bleed).
set capsArgs [list $printer]
if {$opt(paperw) ne "" && $opt(paperh) ne ""} {
    lappend capsArgs -paperw $opt(paperw) -paperh $opt(paperh)
} elseif {$opt(paper) ne ""} {
    lappend capsArgs -paper $opt(paper)
}

if {[catch {::pdfium::printercaps {*}$capsArgs} caps]} {
    puts "  error: $caps"
} else {
    if {$opt(paperw) ne "" && $opt(paperh) ne ""} {
        row "requested" "$opt(paperw) x $opt(paperh) mm (custom size)"
    } else {
        row "form"      [expr {$opt(paper) ne "" ? $opt(paper) : "(driver default)"}]
    }
    row "resolution" "[dict get $caps dpi_x] x [dict get $caps dpi_y] dpi"
    row "sheet"      [format "%.1f x %.1f mm" \
                          [dict get $caps paper_w_mm] [dict get $caps paper_h_mm]]
    row "printable"  [format "%.1f x %.1f mm" \
                          [dict get $caps print_w_mm] [dict get $caps print_h_mm]]
    row "margins"    [format "l %.1f  r %.1f  t %.1f  b %.1f mm" \
                          [dict get $caps margin_l_mm] [dict get $caps margin_r_mm] \
                          [dict get $caps margin_t_mm] [dict get $caps margin_b_mm]]
    row "borderless" [dict get $caps borderless]
    row "colour"     "[dict get $caps bitspixel] bit, [dict get $caps numcolors] colours"

    # Did the driver honour a custom size? This is the whole point of
    # asking: on continuous-tape printers the named forms only fix the
    # width, and a label of a given length can only be had through
    # DMPAPER_USER -- if the driver accepts it.
    if {[dict get $caps want_w_mm] > 0} {
        set gotW [dict get $caps paper_w_mm]
        set gotH [dict get $caps paper_h_mm]
        set okW [expr {abs($gotW - [dict get $caps want_w_mm]) < 1.0}]
        set okH [expr {abs($gotH - [dict get $caps want_h_mm]) < 1.0}]
        puts ""
        if {$okW && $okH} {
            puts "  -> custom size accepted by the driver"
        } else {
            puts "  -> CUSTOM SIZE IGNORED: asked for\
                  [dict get $caps want_w_mm] x [dict get $caps want_h_mm] mm,\
                  got [format %.1f $gotW] x [format %.1f $gotH] mm"
        }
    }
}

# --------------------------------------------------------------------------
head "Test print"
# --------------------------------------------------------------------------
if {!$opt(print)} {
    puts "  skipped (enable with -print)"
} elseif {$opt(pdf) eq ""} {
    puts "  no PDF given (-pdf FILE)"
} elseif {![file exists $opt(pdf)]} {
    puts "  file not found: $opt(pdf)"
} else {
    set doc [::pdfium::open $opt(pdf)]
    row "File"  $opt(pdf)
    row "Pages" [::pdfium::pagecount $doc]

    row "mode"  $opt(mode)
    if {$opt(paper) ne ""} { row "paper" $opt(paper) }

    set pargs [list -printer $printer -from 0 -to 0 -mode $opt(mode) \
                    -docname "pdfiumtcl test print"]
    if {$opt(paperw) ne "" && $opt(paperh) ne ""} {
        lappend pargs -paperw $opt(paperw) -paperh $opt(paperh)
        row "paper" "$opt(paperw) x $opt(paperh) mm (custom)"
    } elseif {$opt(paper) ne ""} {
        lappend pargs -paper $opt(paper)
    }

    set t0 [clock milliseconds]
    set rc [catch {::pdfium::print $doc {*}$pargs} res]
    set dt [expr {[clock milliseconds] - $t0}]
    catch {::pdfium::close $doc}

    if {$rc} {
        puts "  ERROR: $res"
    } else {
        row "printed" "$res page(s) in ${dt} ms"
    }
}

# --------------------------------------------------------------------------
head "Form list as test data"
# --------------------------------------------------------------------------
puts "  Paste into test-pdfium-backend.tcl (::mockPapers):"
puts ""
puts "variable ::mockPapers \[list \\"
foreach f $forms {
    lassign $f name code w h
    puts [format "    {%-34s %5d %6.1f %6.1f} \\" "\"$name\"" $code $w $h]
}
puts "\]"
puts ""
