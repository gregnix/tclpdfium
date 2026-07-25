#!/usr/bin/env wish
# etikett.tcl -- Etikettengröße wählen, Text eingeben, drucken.
#
# Druckweg ist derselbe wie der QL-Knopf in viewer2.tcl:
#   Text -> PDF (pdf4tcl) -> PNG (pdfium) -> lp (CUPS)
# Kein brother_ql, kein USB-Gefummel. Wenn viewer2 bei dir druckt,
# druckt das hier auch.
#
# Start:
#   export LD_LIBRARY_PATH=/pfad/zu/tclpdfium/vendor/pdfium-linux-x64/lib
#   export TCLLIBPATH="/pfad/zu/tclpdfium /pfad/zu/pdf4tcl/pkg"
#   wish etikett.tcl

package require Tk

# --- Fehler kopierbar anzeigen -------------------------------------------
proc zeigeFehler {titel text} {
    catch {
        set fh [open /tmp/etikett.log a]
        puts $fh "--- [clock format [clock seconds]]\n$text"
        close $fh
    }
    set w .err
    catch {destroy $w}
    toplevel $w
    wm title $w $titel
    text $w.t -width 88 -height 16 -wrap word
    pack $w.t -fill both -expand 1 -padx 8 -pady 8
    $w.t insert 1.0 $text
    $w.t tag add sel 1.0 end
    frame $w.b
    pack $w.b -fill x -padx 8 -pady {0 8}
    button $w.b.c -text "In Zwischenablage" -command \
        "clipboard clear; clipboard append \[$w.t get 1.0 end-1c\]"
    button $w.b.x -text "Schließen" -command [list destroy $w]
    pack $w.b.c -side left
    pack $w.b.x -side right
    focus $w.t
}
proc bgerror {msg} { zeigeFehler "Fehler" "$msg\n\n$::errorInfo" }

foreach p {pdf4tcl pdfiumtcl} {
    if {[catch {package require $p} e]} {
        wm withdraw .
        zeigeFehler "Paket fehlt" \
"$p lässt sich nicht laden:

    $e

TCLLIBPATH       [expr {[info exists ::env(TCLLIBPATH)] ? $::env(TCLLIBPATH) : {(nicht gesetzt)}}]
LD_LIBRARY_PATH  [expr {[info exists ::env(LD_LIBRARY_PATH)] ? $::env(LD_LIBRARY_PATH) : {(nicht gesetzt)}}]

Bei pdfiumtcl fehlt meist libpdfium.so. Sie liegt im Archiv unter
vendor/pdfium-linux-x64/lib/ und muss dorthin, wo die geladene .so
liegt -- oder in LD_LIBRARY_PATH."
        vwait ewig
    }
}

# --- Bänder ---------------------------------------------------------------
# Druckbreite in Pixeln bei 300 dpi. Gleiche Tabelle wie viewer2.tcl.
# Die Druckbreite ist KLEINER als das Band -- 54 mm Band druckt 590 px.
set ::baender {
    12  106   29  306   38  413   50  554
    54  590   62  696  102 1164
}

proc breitePx {mm} {
    if {[dict exists $::baender $mm]} { return [dict get $::baender $mm] }
    return [expr {int($mm/25.4*300 + 0.5)}]
}

# --- Drucker --------------------------------------------------------------
proc drucker {} {
    if {[catch {exec lpstat -a} out]} { return [list] }
    set l [list]
    foreach line [split $out \n] {
        set n [lindex [split $line] 0]
        if {$n ne ""} { lappend l $n }
    }
    return $l
}

# --- PDF bauen ------------------------------------------------------------
# Die Seite wird in der BANDBREITE angelegt (54 mm), damit das Etikett
# maßstabsgetreu wird. Die Länge ergibt sich aus dem Text.

proc bauePdf {datei} {
    set band $::v(band)
    set fs   $::v(groesse)
    set rand $::v(rand)
    set text [string trimright [.t get 1.0 end] \n]
    set zeilen [split $text \n]
    if {[llength $zeilen] == 0} { set zeilen [list ""] }

    set zeilenabstand [expr {$fs * 1.3 * 0.3528}]   ;# pt -> mm
    if {$::v(autolaenge)} {
        set hoehe [expr {2*$rand + [llength $zeilen]*$zeilenabstand}]
        if {$hoehe < 15} { set hoehe 15 }
    } else {
        set hoehe $::v(laenge)
    }

    set p [::pdf4tcl::new %AUTO% -paper [list ${band}m ${hoehe}m] -margin 0]
    $p setFont $fs $::v(schrift)
    set y [expr {$rand + $fs*0.3528}]
    foreach z $zeilen {
        switch -- $::v(ausrichtung) {
            zentriert { set x [expr {$band/2.0}]; set a center }
            rechts    { set x [expr {$band-$rand}]; set a right }
            default   { set x $rand; set a left }
        }
        if {[catch {$p text $z -x ${x}m -y ${y}m -align $a}]} {
            $p text $z -x ${rand}m -y ${y}m
        }
        set y [expr {$y + $zeilenabstand}]
    }
    $p write -file $datei
    $p destroy
    return [list $band $hoehe]
}

# --- Rendern (wie viewer2) ------------------------------------------------
proc renderePng {pdf png band} {
    set wpx [breitePx $band]
    if {$::v(dpi) == 600} { set wpx [expr {$wpx*2}] }
    set doc [pdfium::open $pdf]
    pdfium::render $doc 0 -width $wpx -imagename ::seite
    pdfium::close $doc
    ::seite write $png -format png
    set iw [image width ::seite]
    set ih [image height ::seite]
    # Länge aus Seitenverhältnis und Bandbreite -- nicht aus 300 dpi.
    # Sonst stimmt sie bei 600 dpi nicht und ist auch bei 300 dpi zu kurz.
    set hmm [format %.1f [expr {double($ih)/$iw*$band}]]
    return [list $iw $ih $hmm]
}

# --- Vorschau -------------------------------------------------------------
proc vorschau {} {
    if {[catch {
        set pdf /tmp/etikett-vorschau.pdf
        lassign [bauePdf $pdf] band hoehe
        lassign [renderePng $pdf /tmp/etikett-vorschau.png $band] iw ih hmm
    } e]} {
        .st configure -text "Vorschau: $e" -foreground #a33
        return
    }
    set teiler [expr {int(ceil($iw/520.0))}]
    if {$teiler < 1} { set teiler 1 }
    ::gezeigt blank
    ::gezeigt copy ::seite -subsample $teiler $teiler
    .vs configure -image ::gezeigt
    .st configure -text "${band} × ${hmm} mm   (${iw} × ${ih} px)" -foreground #333
}

proc entprellt {} {
    after cancel $::nachId
    set ::nachId [after 250 vorschau]
}

# --- Drucken --------------------------------------------------------------
proc drucke {{trocken 0}} {
    if {[string trim [.t get 1.0 end]] eq ""} {
        .st configure -text "Kein Text." -foreground #a33 ; return
    }
    if {$::v(drucker) eq ""} {
        .st configure -text "Kein Drucker gewählt." -foreground #a33 ; return
    }
    set png /tmp/etikett-druck.png
    if {[catch {
        lassign [bauePdf /tmp/etikett-druck.pdf] band hoehe
        lassign [renderePng /tmp/etikett-druck.pdf $png $band] iw ih hmm
    } e]} {
        zeigeFehler "Rendern fehlgeschlagen" "$e\n\n$::errorInfo" ; return
    }
    # Exakt der Aufruf aus viewer2.tcl
    set cmd [list lp -d $::v(drucker) \
                 -o "PageSize=Custom.${band}x${hmm}mm" \
                 -o MediaType=Roll \
                 -o CutMedia=$::v(schnitt) \
                 $png]
    if {$::v(stueck) > 1} { set cmd [linsert $cmd 2 -n $::v(stueck)] }
    if {$trocken} {
        zeigeFehler "Testlauf -- nicht gedruckt" \
            "Befehl:\n\n[join $cmd { }]\n\nBild: $png ($iw × $ih px)\nSeite: ${band} × ${hmm} mm"
        return
    }
    if {[catch {exec {*}$cmd 2>@1} out]} {
        zeigeFehler "Druckfehler" \
            "$out\n\nBefehl:\n[join $cmd { }]\n\nWeitere Hinweise:\n  lpstat -p $::v(drucker) -l\n  tail -30 /var/log/cups/error_log"
    } else {
        .st configure -text "Gesendet an $::v(drucker): ${band} × ${hmm} mm  $out" \
            -foreground #185
    }
}

proc speicherePdf {} {
    set f [tk_getSaveFile -defaultextension .pdf \
              -filetypes {{PDF {.pdf}} {Alle *}} -initialfile etikett.pdf]
    if {$f eq ""} return
    if {[catch {bauePdf $f} e]} {
        zeigeFehler "PDF schreiben" "$e\n\n$::errorInfo"
    } else {
        .st configure -text "PDF geschrieben: $f" -foreground #185
    }
}

# --- Oberfläche -----------------------------------------------------------
array set ::v {
    band 54  groesse 12  schrift Helvetica-Bold  ausrichtung links
    rand 3   dpi 300  autolaenge 1  laenge 40  stueck 1
    schnitt EndOfPage  drucker {}
}
set ::nachId ""

wm title . "Etikett drucken"
wm minsize . 600 420

frame .top -padx 8 -pady 6
pack .top -fill x
label .top.l1 -text "Band:"
ttk::combobox .top.band -width 5 -state readonly \
    -values [dict keys $::baender] -textvariable ::v(band)
label .top.l2 -text "mm    Schrift:"
ttk::spinbox .top.fs -width 4 -from 5 -to 60 -textvariable ::v(groesse)
label .top.l3 -text "pt    Ausrichtung:"
ttk::combobox .top.al -width 10 -state readonly \
    -values {links zentriert rechts} -textvariable ::v(ausrichtung)
label .top.l4 -text "Stück:"
ttk::spinbox .top.st -width 3 -from 1 -to 99 -textvariable ::v(stueck)
pack .top.l1 .top.band .top.l2 .top.fs .top.l3 .top.al .top.l4 .top.st \
     -side left -padx 2

frame .top2 -padx 8
pack .top2 -fill x
label .top2.l -text "Drucker:"
ttk::combobox .top2.d -width 28 -state readonly -textvariable ::v(drucker)
button .top2.r -text "neu suchen" -command {
    .top2.d configure -values [drucker]
    if {$::v(drucker) eq "" && [llength [drucker]] > 0} {
        set ql [lsearch -inline -all -nocase [drucker] "*ql*"]
        set ::v(drucker) [expr {[llength $ql] ? [lindex $ql 0] : [lindex [drucker] 0]}]
    }
}
pack .top2.l .top2.d .top2.r -side left -padx 2

text .t -height 6 -wrap none -font {TkDefaultFont 12} -undo 1 \
     -relief solid -borderwidth 1
pack .t -fill x -padx 8 -pady 6

image create photo ::seite
image create photo ::gezeigt
label .vs -relief solid -borderwidth 1 -background white -anchor nw
pack .vs -fill both -expand 1 -padx 8

frame .b -padx 8 -pady 6
pack .b -fill x
button .b.p -text "Drucken (Strg+Enter)" -command {drucke 0}
button .b.t -text "Testlauf" -command {drucke 1}
button .b.s -text "PDF speichern..." -command speicherePdf
button .b.c -text "Leeren" -command {.t delete 1.0 end; vorschau}
pack .b.p .b.t .b.s .b.c -side left -padx {0 6}

label .st -anchor w -padx 8
pack .st -fill x -pady {0 6}

bind .t <KeyRelease> entprellt
bind . <Control-Return> {drucke 0}
foreach w {.top.band .top.fs .top.al} {
    bind $w <<ComboboxSelected>> vorschau
    bind $w <KeyRelease> entprellt
}
trace add variable ::v(band) write {apply {args {entprellt}}}
trace add variable ::v(groesse) write {apply {args {entprellt}}}
trace add variable ::v(ausrichtung) write {apply {args {entprellt}}}

.top2.r invoke
.t insert 1.0 "Beispieltext\nZweite Zeile"
focus .t
vorschau
