#!/usr/bin/env tclsh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Gregor Ebbing
# viewer.tcl  --  PDF-Viewer auf Basis von pdfiumtcl — Version 0.3
#
# Aufruf:  tclsh viewer.tcl datei.pdf


package require Tk
package require pdfiumtcl

# ------------------------------------------------------------------ #
# Globaler Zustand                                                    #
# ------------------------------------------------------------------ #
set state(doc)      ""
set state(page)     0
set state(total)    0
set state(dpi)      150
set state(file)     ""
set state(panel)    1    ;# Infopanel sichtbar

# ------------------------------------------------------------------ #
# GUI aufbauen                                                        #
# ------------------------------------------------------------------ #
wm title . "PDF Viewer"
wm minsize . 600 400

# Toolbar
frame .tb -relief raised -bd 1
button .tb.open  -text "Öffnen"   -command cmd_open
button .tb.prev  -text "◀"         -command cmd_prev
button .tb.next  -text "▶"         -command cmd_next
label  .tb.info  -textvariable state(pageinfo) -width 16
label  .tb.dpi_l -text "DPI:"
spinbox .tb.dpi  -from 72 -to 600 -increment 50 \
                 -textvariable state(dpi) -width 5 \
                 -command cmd_refresh
button .tb.panel -text "Info ▶◀"  -command cmd_toggle_panel
button .tb.text  -text "Text"     -command cmd_showtext
button .tb.print -text "Drucken"  -command cmd_print
button .tb.ql    -text "QL"       -command cmd_print_ql

pack .tb.open .tb.prev .tb.next .tb.info \
     .tb.dpi_l .tb.dpi .tb.panel \
     .tb.text .tb.print .tb.ql \
     -side left -padx 3 -pady 3

# Hauptbereich: PanedWindow
panedwindow .pw -orient horizontal -sashwidth 4 -sashrelief raised

# Linkes Panel: Canvas
frame .pw.left
scrollbar .pw.left.sby -orient vertical   -command {.pw.left.c yview}
scrollbar .pw.left.sbx -orient horizontal -command {.pw.left.c xview}
canvas .pw.left.c \
    -yscrollcommand {.pw.left.sby set} \
    -xscrollcommand {.pw.left.sbx set} \
    -background #808080

grid .pw.left.c   -row 0 -column 0 -sticky nsew
grid .pw.left.sby -row 0 -column 1 -sticky ns
grid .pw.left.sbx -row 1 -column 0 -sticky ew
grid rowconfigure    .pw.left 0 -weight 1
grid columnconfigure .pw.left 0 -weight 1

# Rechtes Panel: Notebook mit Tabs
frame .pw.right -width 280
ttk::notebook .pw.right.nb
frame .pw.right.nb.bm   ;# Lesezeichen
frame .pw.right.nb.meta ;# Metadaten
frame .pw.right.nb.form ;# Formularfelder

.pw.right.nb add .pw.right.nb.bm   -text "Lesezeichen"
.pw.right.nb add .pw.right.nb.meta -text "Metadaten"
.pw.right.nb add .pw.right.nb.form -text "Formular"

pack .pw.right.nb -fill both -expand 1

# Tab: Lesezeichen
scrollbar .pw.right.nb.bm.sb -orient vertical \
    -command {.pw.right.nb.bm.tree yview}
ttk::treeview .pw.right.nb.bm.tree \
    -yscrollcommand {.pw.right.nb.bm.sb set} \
    -columns {page} \
    -displaycolumns {page} \
    -show {tree headings} \
    -selectmode browse
.pw.right.nb.bm.tree heading #0   -text "Titel"
.pw.right.nb.bm.tree heading page -text "S."
.pw.right.nb.bm.tree column  page -width 30 -stretch 0

bind .pw.right.nb.bm.tree <<TreeviewSelect>> {
    set sel [.pw.right.nb.bm.tree selection]
    if {$sel ne ""} {
        set pg [.pw.right.nb.bm.tree set $sel page]
        if {$pg ne "" && $pg >= 0} {
            set state(page) $pg
            show_page
        }
    }
}

pack .pw.right.nb.bm.sb   -side right -fill y
pack .pw.right.nb.bm.tree -side left  -fill both -expand 1

# Tab: Metadaten
scrollbar .pw.right.nb.meta.sb -orient vertical \
    -command {.pw.right.nb.meta.tv yview}
ttk::treeview .pw.right.nb.meta.tv \
    -yscrollcommand {.pw.right.nb.meta.sb set} \
    -columns {value} \
    -displaycolumns {value} \
    -show {tree headings} \
    -selectmode none
.pw.right.nb.meta.tv heading #0    -text "Feld"
.pw.right.nb.meta.tv heading value -text "Wert"
.pw.right.nb.meta.tv column  value -width 180 -stretch 1

pack .pw.right.nb.meta.sb -side right -fill y
pack .pw.right.nb.meta.tv -side left  -fill both -expand 1

# Tab: Formularfelder
scrollbar .pw.right.nb.form.sb -orient vertical \
    -command {.pw.right.nb.form.tv yview}
ttk::treeview .pw.right.nb.form.tv \
    -yscrollcommand {.pw.right.nb.form.sb set} \
    -columns {type value} \
    -displaycolumns {type value} \
    -show {tree headings} \
    -selectmode none
.pw.right.nb.form.tv heading #0    -text "Name"
.pw.right.nb.form.tv heading type  -text "Typ"
.pw.right.nb.form.tv heading value -text "Wert"
.pw.right.nb.form.tv column  type  -width 60  -stretch 0
.pw.right.nb.form.tv column  value -width 120 -stretch 1

pack .pw.right.nb.form.sb -side right -fill y
pack .pw.right.nb.form.tv -side left  -fill both -expand 1

# PanedWindow zusammensetzen
.pw add .pw.left  -stretch always
.pw add .pw.right -stretch never

pack .tb -side top  -fill x
pack .pw -side top  -fill both -expand 1

# Tastatur
bind . <Left>  cmd_prev
bind . <Right> cmd_next
bind . <Prior> cmd_prev
bind . <Next>  cmd_next

# Mausrad
bind .pw.left.c <Control-MouseWheel> {
    if {%D > 0} { cmd_zoom_in  } else { cmd_zoom_out }
}
bind .pw.left.c <Control-Button-4> { cmd_zoom_in  }
bind .pw.left.c <Control-Button-5> { cmd_zoom_out }
bind .pw.left.c <Button-4> { .pw.left.c yview scroll -3 units }
bind .pw.left.c <Button-5> { .pw.left.c yview scroll  3 units }

# ------------------------------------------------------------------ #
# Befehle                                                             #
# ------------------------------------------------------------------ #
proc cmd_open {} {
    global state
    set f [tk_getOpenFile \
        -title "PDF öffnen" \
        -filetypes {{"PDF-Dokumente" .pdf} {"Alle Dateien" *}}]
    if {$f eq ""} return
    open_pdf $f
}

proc ask_password {filename} {
    set w .pwdialog
    if {[winfo exists $w]} { destroy $w }
    toplevel $w
    wm title $w "Passwort"
    wm resizable $w 0 0
    wm transient $w .

    label  $w.l -text "Passwort für [file tail $filename]:"
    entry  $w.e -show * -width 30
    frame  $w.f
    button $w.f.ok     -text "OK"        -default active \
        -command "set ::_pw_result \[$w.e get\]; destroy $w"
    button $w.f.cancel -text "Abbrechen" \
        -command "set ::_pw_result {}; destroy $w"

    pack $w.l       -padx 10 -pady 8
    pack $w.e       -padx 10 -pady 4
    pack $w.f.ok $w.f.cancel -side left -padx 4 -pady 6
    pack $w.f

    bind $w <Return> "$w.f.ok invoke"
    bind $w <Escape> "$w.f.cancel invoke"
    focus $w.e

    set ::_pw_result {}
    tkwait window $w
    return $::_pw_result
}

proc open_pdf {filename {password ""}} {
    global state
    if {$state(doc) ne ""} {
        pdfium::close $state(doc)
        set state(doc) ""
    }
    if {[catch {pdfium::open $filename $password} doc]} {
        # PDFium Fehlercode 4 = fehlendes/falsches Passwort
        if {[string match "*error 4*" $doc] ||
            [string match "*assword*" $doc]} {
            set pw [ask_password $filename]
            if {$pw eq ""} return
            open_pdf $filename $pw
            return
        }
        tk_messageBox -icon error -message "Fehler: $doc"
        return
    }
    set state(doc)   $doc
    set state(file)  $filename
    set state(total) [pdfium::pagecount $doc]
    set state(page)  0
    wm title . "PDF Viewer – [file tail $filename]"
    update_info_panel
    show_page
}

proc cmd_zoom_in {} {
    global state
    set state(dpi) [expr {min(int($state(dpi) * 1.25), 600)}]
    show_page
}

proc cmd_zoom_out {} {
    global state
    set state(dpi) [expr {max(int($state(dpi) / 1.25), 36)}]
    show_page
}

proc cmd_toggle_panel {} {
    global state
    if {$state(panel)} {
        .pw forget .pw.right
        set state(panel) 0
    } else {
        .pw add .pw.right -stretch never
        set state(panel) 1
    }
}

proc update_info_panel {} {
    global state
    if {$state(doc) eq ""} return

    # --- Lesezeichen ---
    set tree .pw.right.nb.bm.tree
    $tree delete [$tree children {}]

    set bmarks [pdfium::bookmarks $state(doc)]
    array set parents {}
    set parents(-1) {}

    foreach bm $bmarks {
        set titel [lindex $bm 0]
        set seite [lindex $bm 1]
        set level [lindex $bm 2]
        set parent_level [expr {$level - 1}]
        set parent_node [expr {$parent_level >= 0 && \
            [info exists parents($parent_level)] ? \
            $parents($parent_level) : {}}]
        set id [$tree insert $parent_node end \
            -text $titel \
            -values [list $seite]]
        set parents($level) $id
        if {$level == 0} { $tree item $id -open 1 }
    }

    # --- Metadaten ---
    set mtv .pw.right.nb.meta.tv
    $mtv delete [$mtv children {}]

    foreach key {Title Author Subject Keywords Creator Producer
                 CreationDate ModDate} {
        set val [pdfium::meta $state(doc) $key]
        if {$val ne ""} {
            $mtv insert {} end -text $key -values [list $val]
        }
    }
    # Extra: Seitenanzahl und Dateiname
    $mtv insert {} end -text "Seiten"   -values [list $state(total)]
    $mtv insert {} end -text "Datei"    \
        -values [list [file tail $state(file)]]

    # --- Formularfelder (alle Seiten) ---
    set ftv .pw.right.nb.form.tv
    $ftv delete [$ftv children {}]

    for {set p 0} {$p < $state(total)} {incr p} {
        set fields [pdfium::formfields $state(doc) $p]
        foreach f $fields {
            set typ  [lindex $f 0]
            set name [lindex $f 1]
            set val  [lindex $f 2]
            $ftv insert {} end \
                -text $name \
                -values [list $typ $val]
        }
    }
}

proc show_page {} {
    global state
    if {$state(doc) eq ""} return

    set p $state(page)
    set n $state(total)
    set state(pageinfo) "Seite [expr {$p+1}] / $n"

    # Rendern: Bild heißt immer "pdfpage"
    if {[catch {
        pdfium::render $state(doc) $p \
            -dpi $state(dpi) -imagename pdfpage
    } err]} {
        tk_messageBox -icon error -message "Render-Fehler: $err"
        return
    }

    # Canvas aktualisieren
    .pw.left.c delete all
    .pw.left.c create image 0 0 -anchor nw -image pdfpage

    # Scrollregion anpassen
    set iw [image width  pdfpage]
    set ih [image height pdfpage]
    .pw.left.c configure -scrollregion [list 0 0 $iw $ih]
    .pw.left.c yview moveto 0
    .pw.left.c xview moveto 0
}

proc cmd_prev {} {
    global state
    if {$state(doc) eq "" || $state(page) == 0} return
    incr state(page) -1
    show_page
}

proc cmd_next {} {
    global state
    if {$state(doc) eq ""} return
    if {$state(page) >= $state(total) - 1} return
    incr state(page)
    show_page
}

proc cmd_refresh {} {
    show_page
}

proc cmd_showtext {} {
    global state
    if {$state(doc) eq ""} return

    set txt [pdfium::gettext $state(doc) $state(page)]

    # Einfaches Textfenster
    set w .textwin
    if {[winfo exists $w]} { destroy $w }
    toplevel $w
    wm title $w "Text – Seite [expr {$state(page)+1}]"

    text $w.t -wrap word -width 80 -height 30 \
        -yscrollcommand "$w.sb set"
    scrollbar $w.sb -command "$w.t yview"
    pack $w.sb -side right -fill y
    pack $w.t  -side left  -fill both -expand 1

    $w.t insert end $txt
    $w.t configure -state disabled
}

# ------------------------------------------------------------------ #
# Drucken                                                             #
# ------------------------------------------------------------------ #

# Verfügbare Drucker per lpstat ermitteln
proc get_printers {} {
    if {[catch {exec lpstat -a} out]} {
        return [list]
    }
    set printers [list]
    foreach line [split $out \n] {
        set name [lindex [split $line] 0]
        if {$name ne ""} { lappend printers $name }
    }
    return $printers
}

# Druckdialog
proc cmd_print {} {
    global state
    if {$state(doc) eq ""} return

    set w .printdlg
    if {[winfo exists $w]} { destroy $w }
    toplevel $w
    wm title $w "Drucken"
    wm resizable $w 0 0
    wm transient $w .

    # Drucker-Liste
    set printers [get_printers]
    if {[llength $printers] == 0} {
        set printers [list "(Standard)"]
    }

    # Variablen
    set ::_print_printer [lindex $printers 0]
    set ::_print_range   "current"
    set ::_print_dpi     300
    set ::_print_copies  1

    # Layout
    frame $w.f -padx 10 -pady 8
    pack  $w.f -fill both -expand 1

    # Drucker
    label $w.f.pl -text "Drucker:" -anchor w
    ttk::combobox $w.f.pc \
        -textvariable ::_print_printer \
        -values $printers -width 30 -state readonly
    grid $w.f.pl $w.f.pc -sticky w -pady 3

    # Seitenbereich
    label $w.f.rl -text "Seiten:" -anchor w
    frame $w.f.rf
    radiobutton $w.f.rf.cur -text "Aktuelle Seite" \
        -variable ::_print_range -value current
    radiobutton $w.f.rf.all -text "Alle Seiten ([expr {$state(total)}])" \
        -variable ::_print_range -value all
    pack $w.f.rf.cur $w.f.rf.all -anchor w
    grid $w.f.rl $w.f.rf -sticky w -pady 3

    # DPI
    label $w.f.dl -text "Druckqualität (DPI):" -anchor w
    frame $w.f.df
    foreach d {150 300 600} {
        radiobutton $w.f.df.$d -text "${d} DPI" \
            -variable ::_print_dpi -value $d
        pack $w.f.df.$d -side left -padx 4
    }
    grid $w.f.dl $w.f.df -sticky w -pady 3

    # Kopien
    label $w.f.cl -text "Kopien:" -anchor w
    spinbox $w.f.cs -from 1 -to 99 -width 4 \
        -textvariable ::_print_copies
    grid $w.f.cl $w.f.cs -sticky w -pady 3

    # Buttons
    frame $w.bf
    button $w.bf.ok  -text "Drucken" -default active \
        -command "set ::_print_ok 1; destroy $w"
    button $w.bf.can -text "Abbrechen" \
        -command "set ::_print_ok 0; destroy $w"
    pack $w.bf.ok $w.bf.can -side left -padx 6 -pady 8
    pack $w.bf

    bind $w <Return> "$w.bf.ok invoke"
    bind $w <Escape> "$w.bf.can invoke"

    set ::_print_ok 0
    tkwait window $w

    if {$::_print_ok} {
        do_print $::_print_printer $::_print_range \
                 $::_print_dpi     $::_print_copies
    }
}

# Eigentliches Drucken
proc do_print {printer range dpi copies} {
    global state

    # Welche Seiten?
    if {$range eq "current"} {
        set pages [list $state(page)]
    } else {
        set pages [list]
        for {set i 0} {$i < $state(total)} {incr i} {
            lappend pages $i
        }
    }

    # Fortschrittsfenster
    set w .progwin
    if {[winfo exists $w]} { destroy $w }
    toplevel $w
    wm title $w "Drucken läuft..."
    wm transient $w .
    label $w.l -text "Bereite Druck vor..." -padx 20 -pady 10
    pack  $w.l
    update

    set tmpfiles [list]
    set ok 1

    foreach p $pages {
        $w.l configure -text \
            "Rendere Seite [expr {$p+1}] / $state(total) bei ${dpi} DPI..."
        update

        set tmpfile [file join /tmp "pdfprint_p${p}_[pid].png"]

        # Seite mit Druckauflösung rendern
        if {[catch {
            pdfium::render $state(doc) $p \
                -dpi $dpi -imagename printpage
            printpage write $tmpfile -format png
        } err]} {
            tk_messageBox -icon error \
                -message "Render-Fehler Seite [expr {$p+1}]: $err"
            set ok 0
            break
        }
        lappend tmpfiles $tmpfile
    }

    if {$ok && [llength $tmpfiles] > 0} {
        $w.l configure -text "Sende an Drucker $printer ..."
        update

        # lp-Befehl zusammensetzen
        set cmd [list lp -n $copies]
        if {$printer ne "(Standard)"} {
            lappend cmd -d $printer
        }
        foreach f $tmpfiles { lappend cmd $f }

        if {[catch {eval exec $cmd} err]} {
            tk_messageBox -icon error \
                -message "Druckfehler: $err"
        } else {
            tk_messageBox -icon info \
                -message "Druckauftrag gesendet ([llength $tmpfiles] Seite(n))."
        }
    }

    # Temporäre Dateien aufräumen
    foreach f $tmpfiles {
        catch { file delete $f }
    }

    destroy $w
}

# ------------------------------------------------------------------ #
# Brother QL Druck — Version 0.2                                      #
# Korrekte Pixelbreiten laut brother_ql / labelutil:                 #
#   54mm -> 590 px, 62mm -> 696 px etc.                              #
# ------------------------------------------------------------------ #

# Pixelbreite fuer QL-Etikettenbreite in mm
proc ql_width_px {mm} {
    set table {12 106  29 306  38 413  50 554  54 590  62 696  102 1164}
    if {[dict exists $table $mm]} {
        return [dict get $table $mm]
    }
    return [expr {int($mm / 25.4 * 300 + 0.5)}]
}

proc cmd_print_ql {} {
    global state
    if {$state(doc) eq ""} return

    set w .qldlg
    if {[winfo exists $w]} { destroy $w }
    toplevel $w
    wm title $w "Brother QL Druck"
    wm resizable $w 0 0
    wm transient $w .

    # Seitengröße der aktuellen PDF-Seite
    set sz  [pdfium::pagesize $state(doc) $state(page)]
    set pdf_wmm [format "%.1f" [lindex $sz 0]]
    set pdf_hmm [format "%.1f" [lindex $sz 1]]

    # Variablen
    set ::_ql_printer  ""
    set ::_ql_band     54
    set ::_ql_dpi      300
    set ::_ql_range    "current"
    set ::_ql_cut      "EndOfPage"

    # Alle Drucker holen
    set all_printers [get_printers]

    # QL/Brother vorauswaehlen falls vorhanden
    set ql_list [lsearch -inline -all -nocase $all_printers "*ql*"]
    set br_list [lsearch -inline -all -nocase $all_printers "*brother*"]
    set ql_first [lsort -unique [concat $ql_list $br_list]]
    if {[llength $ql_first] > 0} {
        set ::_ql_printer [lindex $ql_first 0]
    } elseif {[llength $all_printers] > 0} {
        set ::_ql_printer [lindex $all_printers 0]
    }

    frame $w.f -padx 12 -pady 8
    pack  $w.f -fill both

    # Info: PDF-Seitengröße
    label $w.f.info \
        -text "PDF-Seite: ${pdf_wmm} × ${pdf_hmm} mm" \
        -foreground navy -font {TkDefaultFont 9 bold}
    grid $w.f.info - -sticky w -pady 4

    # Drucker — alle verfügbaren
    label $w.f.pl -text "Drucker:" -anchor w
    ttk::combobox $w.f.pc \
        -textvariable ::_ql_printer \
        -values $all_printers -width 30 -state readonly
    grid $w.f.pl $w.f.pc -sticky w -pady 3

    # Bandbreite
    label $w.f.bl -text "Bandbreite:" -anchor w
    frame $w.f.bf
    foreach {bw label} {
        29  "29 mm  (306 px)"
        38  "38 mm  (413 px)"
        54  "54 mm  (590 px)"
        62  "62 mm  (696 px)"
        102 "102 mm (1164 px)"
    } {
        radiobutton $w.f.bf.r$bw \
            -text $label \
            -variable ::_ql_band -value $bw \
            -command update_ql_info
        pack $w.f.bf.r$bw -anchor w
    }
    grid $w.f.bl $w.f.bf -sticky nw -pady 3

    # Auflösung
    label $w.f.dl -text "Auflösung:" -anchor w
    frame $w.f.df
    radiobutton $w.f.df.r300 -text "300 DPI" \
        -variable ::_ql_dpi -value 300 -command update_ql_info
    radiobutton $w.f.df.r600 -text "600 DPI" \
        -variable ::_ql_dpi -value 600 -command update_ql_info
    pack $w.f.df.r300 $w.f.df.r600 -side left -padx 4
    grid $w.f.dl $w.f.df -sticky w -pady 3

    # Schnitt
    label $w.f.cl -text "Auto-Cut:" -anchor w
    frame $w.f.cf
    radiobutton $w.f.cf.rend  -text "Nach Etikett" \
        -variable ::_ql_cut -value EndOfPage
    radiobutton $w.f.cf.rnone -text "Kein Schnitt" \
        -variable ::_ql_cut -value None
    pack $w.f.cf.rend $w.f.cf.rnone -side left -padx 4
    grid $w.f.cl $w.f.cf -sticky w -pady 3

    # Ausgabegröße (berechnet)
    label $w.f.sl   -text "Ausgabe:" -anchor w
    label $w.f.sval -textvariable ::_ql_sizeinfo -anchor w -foreground darkgreen
    grid $w.f.sl $w.f.sval -sticky w -pady 3

    # Seitenbereich
    label $w.f.rl -text "Seiten:" -anchor w
    frame $w.f.rf
    radiobutton $w.f.rf.cur -text "Aktuelle Seite" \
        -variable ::_ql_range -value current
    radiobutton $w.f.rf.all \
        -text "Alle Seiten ($state(total))" \
        -variable ::_ql_range -value all
    pack $w.f.rf.cur $w.f.rf.all -anchor w
    grid $w.f.rl $w.f.rf -sticky w -pady 3

    # Buttons
    frame $w.bf
    button $w.bf.ok  -text "Drucken" -default active \
        -command "set ::_ql_ok 1; destroy $w"
    button $w.bf.can -text "Abbrechen" \
        -command "set ::_ql_ok 0; destroy $w"
    pack $w.bf.ok $w.bf.can -side left -padx 6 -pady 8
    pack $w.bf

    bind $w <Return> "$w.bf.ok invoke"
    bind $w <Escape> "$w.bf.can invoke"

    # Ausgabegröße berechnen
    proc update_ql_info {} {
        global state
        set sz   [pdfium::pagesize $state(doc) $state(page)]
        set wmm  [lindex $sz 0]
        set hmm  [lindex $sz 1]
        set band $::_ql_band
        set dpi  $::_ql_dpi
        set w_px [ql_width_px $band]
        if {$dpi == 600} { set w_px [expr {$w_px * 2}] }
        set h_px [expr {int($hmm / $wmm * $w_px + 0.5)}]
        set h_mm [format "%.1f" $hmm]
        set ::_ql_sizeinfo \
            "${band} × ${h_mm} mm  (${w_px} × ${h_px} px, ${dpi} DPI)"
    }
    update_ql_info

    set ::_ql_ok 0
    tkwait window $w

    if {$::_ql_ok} {
        do_print_ql $::_ql_printer $::_ql_band $::_ql_dpi \
                    $::_ql_range  $::_ql_cut
    }
}

proc do_print_ql {printer band dpi range cut} {
    global state

    # Pixelbreite für gewähltes Band
    set w_px [ql_width_px $band]
    if {$dpi == 600} { set w_px [expr {$w_px * 2}] }

    # Seiten
    if {$range eq "current"} {
        set pages [list $state(page)]
    } else {
        set pages [list]
        for {set i 0} {$i < $state(total)} {incr i} {
            lappend pages $i
        }
    }

    # Fortschrittsfenster
    set pw .qlprog
    if {[winfo exists $pw]} { destroy $pw }
    toplevel $pw
    wm title $pw "QL Druck läuft..."
    wm transient $pw .
    label $pw.l -text "Vorbereitung..." -padx 20 -pady 10
    pack  $pw.l
    update

    set tmpfiles [list]
    set ok 1

    foreach p $pages {
        $pw.l configure -text \
            "Rendere Seite [expr {$p+1}] — ${band}mm, ${w_px}px, ${dpi}DPI..."
        update

        set tmpfile [file join /tmp "ql_p${p}_[pid].png"]

        if {[catch {
            pdfium::render $state(doc) $p \
                -width $w_px -imagename qlpage
            qlpage write $tmpfile -format png
        } err]} {
            tk_messageBox -icon error \
                -message "Render-Fehler Seite [expr {$p+1}]: $err"
            set ok 0
            break
        }

        # Höhe in mm aus tatsächlicher PNG-Höhe berechnen
        set ih [image height qlpage]
        set h_mm [format "%.1f" [expr {$ih / 300.0 * 25.4}]]
        lappend tmpfiles [list $tmpfile $h_mm]
    }

    if {$ok && [llength $tmpfiles] > 0} {
        foreach entry $tmpfiles {
            set f   [lindex $entry 0]
            set hmm [lindex $entry 1]

            $pw.l configure -text \
                "Sende an $printer  (${band} × ${hmm} mm)..."
            update

            set cmd [list lp -d $printer \
                -o "PageSize=Custom.${band}x${hmm}mm" \
                -o MediaType=Roll \
                -o CutMedia=$cut \
                $f]

            if {[catch {eval exec $cmd} err]} {
                tk_messageBox -icon error \
                    -message "Druckfehler: $err\n\nBefehl: $cmd"
            }
        }
        tk_messageBox -icon info \
            -message "Gesendet: [llength $tmpfiles] Etikett(en),\
${band}mm Band, ${dpi} DPI."
    }

    foreach entry $tmpfiles {
        catch { file delete [lindex $entry 0] }
    }
    destroy $pw
}

# ------------------------------------------------------------------ #
# Start: Datei aus Kommandozeile?                                     #
# ------------------------------------------------------------------ #
if {[llength $argv] >= 1} {
    after 100 [list open_pdf [lindex $argv 0]]
}
