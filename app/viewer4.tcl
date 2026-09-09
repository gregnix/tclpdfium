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
set state(suchtext) ""
# "So wird gedruckt": rendert mit FPDF_PRINTING, also OHNE Ebenen, die
# /Usage /Print /PrintState /OFF tragen. Das ist die einzige ehrliche
# Vorschau auf ein Wasserzeichen oder einen Vordruck, der nur auf dem
# Schirm stehen soll -- alles andere waere geraten.
set state(druckansicht) 0
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
label  .tb.info  -textvariable state(seiteninfo) -width 12 -anchor w
label  .tb.dpi_l -text "DPI:"
spinbox .tb.dpi  -from 72 -to 600 -increment 50 \
                 -textvariable state(dpi) -width 5 \
                 -command cmd_refresh
button .tb.panel -text "Info ▶◀"  -command cmd_toggle_panel
button .tb.text  -text "Text"     -command cmd_showtext
# Der Knopf schaltet nichts EIN -- die Sitzung laeuft von selbst. Er
# beendet sie, wenn jemand sie loswerden will (etwa um mit der Maus zu
# markieren, ohne in ein Feld zu geraten).
button .tb.tippen -text "Tippen aus"  -command {
    if {$state(edit) eq ""} { tippenAn } else { tippenAus }
}
label  .tb.such_l -text "Suchen:"
entry  .tb.such   -textvariable state(suchtext) -width 14
button .tb.suchall -text "alle Seiten" -command cmd_suchen_alle
checkbutton .tb.druck -text "Druckansicht" \
    -variable state(druckansicht) -command show_page
button .tb.fest  -text "Festschreiben" -command cmd_flatten
button .tb.print -text "Drucken"  -command cmd_print
button .tb.ql    -text "QL"       -command cmd_print_ql

pack .tb.open .tb.prev .tb.next .tb.info \
     .tb.dpi_l .tb.dpi .tb.panel \
     .tb.text .tb.tippen .tb.such_l .tb.such .tb.suchall .tb.druck \
     .tb.fest .tb.print .tb.ql \
     -side left -padx 3 -pady 3

# Eingabe abschicken heisst suchen; leeres Feld heisst: Markierung weg.
bind .tb.such <Return> cmd_suchen

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
frame .pw.right.nb.such ;# Suchtreffer im ganzen Dokument
frame .pw.right.nb.bau  ;# Woraus die Seite gezeichnet ist

.pw.right.nb add .pw.right.nb.bm   -text "Lesezeichen"
.pw.right.nb add .pw.right.nb.meta -text "Metadaten"
.pw.right.nb add .pw.right.nb.form -text "Formular"
.pw.right.nb add .pw.right.nb.such -text "Treffer"
.pw.right.nb add .pw.right.nb.bau  -text "Aufbau"

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

# Tab: Aufbau
#
# gettext sagt, WAS auf der Seite steht, die Lesezeichen sagen, wie sie
# gegliedert ist. Woraus sie GEZEICHNET ist, sagte nichts: ob ein Kasten
# ein Pfad oder ein Bild ist, ob hinter dem Text ein Scan liegt, wo ein
# Form-XObject sitzt. Beim Nachbauen fremder Vordrucke ist das die
# Frage, die man zuerst hat.
scrollbar .pw.right.nb.bau.sb -orient vertical \
    -command {.pw.right.nb.bau.tv yview}
ttk::treeview .pw.right.nb.bau.tv \
    -yscrollcommand {.pw.right.nb.bau.sb set} \
    -columns {nr art groesse box} \
    -displaycolumns {nr art groesse} \
    -show headings \
    -selectmode browse
.pw.right.nb.bau.tv heading nr      -text "#"
.pw.right.nb.bau.tv heading art     -text "Art"
.pw.right.nb.bau.tv heading groesse -text "Groesse (pt)"
.pw.right.nb.bau.tv column  nr      -width 35  -stretch 0 -anchor e
.pw.right.nb.bau.tv column  art     -width 60  -stretch 0
.pw.right.nb.bau.tv column  groesse -width 110 -stretch 1 -anchor e

bind .pw.right.nb.bau.tv <<TreeviewSelect>> {
    set sel [.pw.right.nb.bau.tv selection]
    .pw.left.c delete objmark
    if {$sel ne ""} {
        set box [.pw.right.nb.bau.tv set $sel box]
        if {[llength $box] == 4} { markiere_box $box objmark "#0060c0" }
    }
}
pack .pw.right.nb.bau.sb -side right -fill y
pack .pw.right.nb.bau.tv -side left  -fill both -expand 1

# Tab: Suchtreffer
#
# Die Suche im Werkzeugbalken sah nur die AKTUELLE Seite. Wer wissen
# will, ob ein Wort im Dokument vorkommt, blaettert dann von Hand --
# und uebersieht die Seite, auf der es steht.
scrollbar .pw.right.nb.such.sb -orient vertical \
    -command {.pw.right.nb.such.tv yview}
ttk::treeview .pw.right.nb.such.tv \
    -yscrollcommand {.pw.right.nb.such.sb set} \
    -columns {seite text seiteIdx} \
    -displaycolumns {seite text} \
    -show headings \
    -selectmode browse
.pw.right.nb.such.tv heading seite -text "Seite"
.pw.right.nb.such.tv heading text  -text "Umgebung"
.pw.right.nb.such.tv column  seite -width 45  -stretch 0 -anchor e
.pw.right.nb.such.tv column  text  -width 200 -stretch 1

bind .pw.right.nb.such.tv <<TreeviewSelect>> {
    set sel [.pw.right.nb.such.tv selection]
    if {$sel ne ""} {
        set pg [.pw.right.nb.such.tv set $sel seiteIdx]
        if {$pg ne "" && $pg != $state(page)} {
            set state(page) $pg
            show_page
        } else {
            cmd_suchen
        }
    }
}
pack .pw.right.nb.such.sb -side right -fill y
pack .pw.right.nb.such.tv -side left  -fill both -expand 1

# Tab: Formularfelder
scrollbar .pw.right.nb.form.sb -orient vertical \
    -command {.pw.right.nb.form.tv yview}
ttk::treeview .pw.right.nb.form.tv \
    -yscrollcommand {.pw.right.nb.form.sb set} \
    -columns {type value seite box id options ap} \
    -displaycolumns {type value id ap} \
    -show {tree headings} \
    -selectmode browse
.pw.right.nb.form.tv heading #0    -text "Name"
.pw.right.nb.form.tv heading type  -text "Typ"
.pw.right.nb.form.tv heading value -text "Wert"
.pw.right.nb.form.tv column  type  -width 60  -stretch 0
.pw.right.nb.form.tv column  value -width 120 -stretch 1
.pw.right.nb.form.tv heading id -text "Feldname"
.pw.right.nb.form.tv column  id -width 90 -stretch 0
# DIE LAENGE DES ERSCHEINUNGSSTROMS, SICHTBAR.
#
# 0 heisst: die Datei sagt nicht, wie das Feld aussieht -- PDFium
# zeichnet dann nichts, und man sucht den Fehler in der Bindung. Genau
# das hat am 07./08.09.2026 einen halben Tag gekostet.
#
# Die Zahl gab es seit 0.6.4 in "formfields", nur sehen konnte man sie
# nicht. Eine Diagnose, die man erst holen muss, holt niemand.
.pw.right.nb.form.tv heading ap -text "AP"
.pw.right.nb.form.tv column  ap -width 45 -stretch 0 -anchor e

# Ein Klick zeigt, WO das Feld liegt; ein Doppelklick fuellt es.
#
# Das Rechteck kommt seit pdfiumtcl 0.6.3 mit "formfields" -- vorher
# waere es ein zweiter Weg zu denselben Zahlen gewesen.
bind .pw.right.nb.form.tv <<TreeviewSelect>> ::demo_formSelect
bind .pw.right.nb.form.tv <Double-1> ::demo_formFill
ttk::button .pw.right.nb.form.save -text "Gefuellt speichern..." \
    -command ::demo_formSave

pack .pw.right.nb.form.save -side bottom -fill x -padx 4 -pady 4
pack .pw.right.nb.form.sb -side right -fill y
pack .pw.right.nb.form.tv -side left  -fill both -expand 1

# PanedWindow zusammensetzen
.pw add .pw.left  -stretch always
.pw add .pw.right -stretch never

# EINE STATUSZEILE UNTEN, ueber die ganze Breite.
#
# Vorher stand die Meldung als "label -width 16" MITTEN IN DER
# WERKZEUGLEISTE, zwischen den Knoepfen. "Schreibmarke gesetzt --
# tippen" kam dort als "eibmarke gesetzt -- ti" an: vorn und hinten
# abgeschnitten, dazwischen die DPI-Auswahl.
#
# Gemeldet mit einem Bildschirmfoto am 06.09.2026. Ich habe heute ein
# Dutzend Meldungen geschrieben und keine davon je vollstaendig gesehen
# -- alle Texte waren fuer eine Zeile gedacht, die es nicht gab.
label .status -textvariable state(pageinfo) -anchor w -relief sunken \
    -borderwidth 1 -padx 4

pack .tb -side top  -fill x
pack .status -side bottom -fill x
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
# Ein Klick waehlt das Feld, ein Doppelklick fuellt es -- dieselbe
# Geste wie in der Liste, nur auf der Seite.
bind .pw.left.c <Button-1> {
    if {$state(edit) ne ""} { tippenKlick %x %y } else { canvasKlick %x %y }
}
bind .pw.left.c <Double-1> {
    if {$state(edit) eq ""} { canvasKlick %x %y ; ::demo_formFill }
}
bind .pw.left.c <Key> {
    if {$state(edit) ne ""} { tippenTaste %K %A ; break }
}
# Der Canvas muss den Fokus nehmen koennen, sonst kommt keine Taste an.
.pw.left.c configure -takefocus 1
bind .pw.left.c <Button-4> { .pw.left.c yview scroll -3 units }
bind .pw.left.c <Button-5> { .pw.left.c yview scroll  3 units }

# ------------------------------------------------------------------ #
# Befehle                                                             #
# ------------------------------------------------------------------ #
proc cmd_open {} {
    # Erst die Sitzung schliessen: sie zeigt auf ein Dokument, das
    # gleich zugemacht wird. Eine Sitzung auf einem geschlossenen
    # Dokument stuerzt beim naechsten Tastendruck ab.
    tippenAus 1
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

# ------------------------------------------------------------------
# MITSCHNITT
#
# Was heute dreimal gefehlt hat, war nicht ein Test, sondern die
# ANTWORT auf "welche Aufrufe hat der Benutzer ausgeloest". Eine
# Fehlerbeschreibung in einem Satz laesst mir drei Deutungen; ein
# Mitschnitt laesst mir keine.
#
# Einschalten ueber die Umgebung, damit man nichts umbaut:
#
#     VIEWER4_SPUR=/tmp/spur.txt wish app/viewer4.tcl datei.pdf
#
# Aufgezeichnet wird JEDER Aufruf an die Bindung mit seinen Argumenten
# und seiner Antwort -- auch der Fehlerfall. Dazu die Klicks in
# Fensterpunkten UND in Seitenpunkten: die Umrechnung dazwischen war
# schon zweimal die eigentliche Frage.
#
# Was NICHT hineingehoert: Dateiinhalte. Ein Mitschnitt, den man nicht
# weitergeben mag, wird nicht weitergegeben.
# ------------------------------------------------------------------
# Das Seitenmass, einmal je Seite.
#
# AUS EINEM MITSCHNITT, 08.09.2026: bei jedem Neuzeichnen stand
# "pdfium::pagesize" sechzehnmal hintereinander -- einmal je Feld, weil
# markiere_box es selbst holt und felderRahmen ueber alle Felder laeuft.
#
# Aufgefallen ist es niemandem: es war nicht falsch, nur verschwendet,
# und im Betrieb sieht man den Unterschied nicht. Im Mitschnitt sieht
# man ihn sofort -- das ist der Nebennutzen einer Spur.
proc seitenmass {} {
    global state
    set schluessel "$state(doc),$state(page)"
    if {[info exists state(mass,$schluessel)]} {
        return $state(mass,$schluessel)
    }
    set m [pdfium::pagesize $state(doc) $state(page)]
    set state(mass,$schluessel) $m
    return $m
}

# Beim Dateiwechsel weg.
#
# Der Schluessel enthaelt das Dokument-Handle, also faellt ein neues
# Dokument meist von selbst auf einen neuen Schluessel. VERLASSEN darf
# man sich darauf nicht: PDFium vergibt fuer ein neues Dokument gern
# dieselbe Adresse -- daran ist am 08.09.2026 schon die
# Formularumgebung haengengeblieben, und der Klick meldete einen
# Treffer, waehrend nichts ankam.
#
# Ein Mass, das nach dem Wechsel stehenbleibt, setzt jeden Rahmen an
# die falsche Stelle.
proc seitenmassVergessen {} {
    global state
    foreach n [array names state mass,*] { unset state($n) }
}

proc spurSchreib {zeile} {
    global state
    if {![info exists state(spurkanal)] || $state(spurkanal) eq ""} return
    puts $state(spurkanal) [format "%s  %s" \
            [clock format [clock seconds] -format %H:%M:%S] $zeile]
    flush $state(spurkanal)
}

proc spurAn {datei} {
    global state
    if {[catch {open $datei w} ch]} {
        return -code error "Mitschnitt: $ch"
    }
    fconfigure $ch -encoding utf-8
    set state(spurkanal) $ch
    # Jeden Befehl der Bindung umhuellen. Umbenennen statt einer
    # Ausfuehrungsspur (trace execution): das laeuft in jeder Tcl-
    # Fassung gleich und ist im Fehlerfall leichter zu lesen.
    foreach cmd [info commands ::pdfium::*] {
        set kurz [namespace tail $cmd]
        if {[info commands ::pdfium::_echt_$kurz] ne ""} continue
        rename $cmd ::pdfium::_echt_$kurz
        proc $cmd {args} [format {
            set rc [catch {::pdfium::_echt_%s {*}$args} aus]
            spurSchreib [format "pdfium::%s %%s -> %%s%%s"                     [string range $args 0 200]                     [expr {$rc ? "FEHLER " : ""}]                     [string range $aus 0 200]]
            if {$rc} { return -code error $aus }
            return $aus
        } $kurz $kurz]
    }
    spurSchreib "Mitschnitt an -- pdfiumtcl [package provide pdfiumtcl]"
    return $datei
}

proc spurAus {} {
    global state
    if {![info exists state(spurkanal)] || $state(spurkanal) eq ""} return
    spurSchreib "Mitschnitt aus"
    close $state(spurkanal)
    set state(spurkanal) ""
}

proc open_pdf {filename {password ""}} {
    # ERST die Tippsitzung beenden.
    #
    # Sie haelt Seite und Formularumgebung des ALTEN Dokuments. Wird das
    # geschlossen, ist sie tot -- state(edit) trug danach ein Handle,
    # das beim naechsten Klick "this edit session is over" meldete.
    #
    # cmd_open tat das schon; open_pdf ist der zweite Eingang und tat es
    # nicht. Zwei Wege ins selbe Haus, einer ohne Schloss.
    tippenAus 1
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
    # Das Seitenmass des ALTEN Dokuments wegwerfen -- und zwar hier,
    # nachdem das neue steht. Weiter oben gerufen blieb ein Eintrag
    # zurueck, weil zwischen dem Loeschen und dem neuen Dokument noch
    # gezeichnet wurde. Gemessen: zwei Eintraege statt einem.
    seitenmassVergessen
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

# Den Wert des Feldes, in dem gerade getippt wird, in der Liste
# nachtragen.
#
# PDFium schreibt erst beim Fokusverlust fest -- "formfields" meldet
# waehrend des Tippens weiter den ALTEN Wert. Im Mitschnitt vom
# 08.09.2026 stand nach jedem Buchstaben wieder "f_name {}", bis ein Tab
# kam. Auf dem Bildschirm sieht das aus, als komme nichts an.
#
# Den Fokus abzugeben waere falsch: dann koennte man nicht weitertippen.
# FORM_GetFocusedText fragt PDFium direkt -- pdfium::edittext.
proc laufendenWertNachtragen {} {
    global state
    if {$state(edit) eq ""} return
    if {[catch {pdfium::edittext $state(edit)} paar]} return
    lassign $paar feldname txt
    # OHNE NAMEN NICHTS EINTRAGEN.
    #
    # Der erste Anlauf schrieb in die AUSGEWAEHLTE Zeile. Nach einem Tab
    # wandert der Fokus aber, und die Auswahl bleibt stehen -- gemessen
    # an einem Mitschnitt: "edittext -> 1" nach mehreren Tabs, der
    # Inhalt von f_menge, eingetragen bei dem Feld, das der Benutzer
    # zuletzt angeklickt hatte. Auf dem Bildschirm hatten "ploetzlich
    # auch die anderen Felder Daten".
    if {$feldname eq "" || $txt eq ""} return
    set tv .pw.right.nb.form.tv
    if {![winfo exists $tv]} return
    foreach id [$tv children {}] {
        if {[$tv set $id id] ne $feldname} continue
        if {[$tv set $id seite] ne $state(page)} continue
        # NUR TIPPBARE FELDER.
        #
        # Eine Optionsgruppe teilt sich einen Namen: drei Zeilen heissen
        # "prio". Der erste Treffer bekaeme den Eintrag, und ein leerer
        # Text loeschte das "Off" -- gemessen: nach vier Tabs waren aus
        # drei prio-Zeilen zwei geworden.
        #
        # Getippt wird nur in Text- und Kombinationsfeldern. Alles
        # andere holt sich update_info_panel beim Festschreiben.
        if {[$tv set $id type] ni {text combobox}} continue
        $tv set $id value $txt
        return
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

    set feldZahl 0
    for {set p 0} {$p < $state(total)} {incr p} {
        set fields [pdfium::formfields $state(doc) $p]
        incr feldZahl [llength $fields]
        foreach f $fields {
            set typ  [lindex $f 0]
            set name [lindex $f 1]
            set val  [lindex $f 2]
            # Den Namen FUER MENSCHEN zeigen, wenn die Datei einen
            # traegt: "Empfaenger, Name und Anschrift" statt
            # "f_kunde_2". Der technische Name bleibt daneben stehen --
            # ihn zu verstecken hiesse, dem Aufrufer die Auskunft zu
            # nehmen, mit der er das Feld ansprechen muss.
            set anzeige $name
            set tu [lindex $f 6]
            if {$tu ne "" && $tu ne $name} { set anzeige $tu }
            # Seite und Rechteck werden mitgefuehrt, aber nicht
            # angezeigt: sie sind fuer den Klick da, nicht fuer das Auge.
            $ftv insert {} end \
                -text $anzeige \
                -values [list $typ $val $p [lindex $f 4] $name \
                        [lindex $f 5] [lindex $f 7]]
        }
    }

    # DIE ZAHL IN DEN REITER, und bei einem Formular ohne Lesezeichen
    # gleich hinblaettern.
    #
    # Vorher standen die Felder da und niemand sah sie: der Viewer
    # oeffnet auf "Lesezeichen", und wer nicht weiss, dass es einen
    # Reiter "Formular" gibt, haelt das Dokument fuer nicht ausfuellbar.
    # Gemeldet am 06.09.2026 -- die Liste war gefuellt, der Reiter
    # unsichtbar.
    if {$feldZahl > 0} {
        .pw.right.nb tab .pw.right.nb.form -text " Formular ($feldZahl) "
        # Nur wenn nichts anderes anzubieten ist: ein Dokument mit
        # Lesezeichen soll dort aufgehen, wo sein Verfasser es gemeint
        # hat. Ungefragt umzuschalten waere sonst eine Bevormundung.
        if {![llength [.pw.right.nb.bm.tree children {}]]} {
            .pw.right.nb select .pw.right.nb.form
        }
    } else {
        .pw.right.nb tab .pw.right.nb.form -text " Formular "
    }
}

# ------------------------------------------------------------------ #
# Suchen mit Markierung                                               #
#                                                                     #
# Moeglich seit "search -rects 1": vorher sagte ein Treffer, DASS das  #
# Wort auf der Seite steht, nicht WO. Damit liess sich nichts          #
# anzeigen.                                                           #
#                                                                     #
# Die Rechnung ist die eine Stelle, an der man sich vertut: pdfium     #
# gibt Punkte in Seitenkoordinaten mit Ursprung UNTEN LINKS, der       #
# Canvas zaehlt Pixel von OBEN LINKS. Also skalieren UND spiegeln.     #
# ------------------------------------------------------------------ #
# ------------------------------------------------------------------ #
# Suche ueber ALLE Seiten                                             #
#                                                                     #
# Die Suche im Werkzeugbalken sieht nur die aktuelle Seite. Wer wissen #
# will, ob ein Wort im Dokument vorkommt, blaettert sonst von Hand --  #
# und uebersieht die Seite, auf der es steht.                         #
# ------------------------------------------------------------------ #
# ------------------------------------------------------------------ #
# Felder und Anmerkungen festschreiben                                #
#                                                                     #
# Schreibt eine NEUE Datei; das Original bleibt, wie es ist. Danach    #
# sind Felder und Anmerkungen Zeichnung: nicht mehr anklickbar, aber   #
# auch nicht mehr davon abhaengig, ob ein Betrachter sie darstellt.    #
#                                                                     #
# "-forms 1" ist dabei nicht wahlweise: ohne das brennt flatten einen  #
# LEEREN Appearance-Strom ein, wenn die Datei mit fillForms gefuellt   #
# wurde -- und der Wert ist danach ganz weg. Gemessen 05.09.2026.      #
# ------------------------------------------------------------------ #
# ------------------------------------------------------------------ #
# Ein Rechteck aus PDF-Punkten auf dem Canvas markieren                #
#                                                                     #
# EINE Stelle fuer die Umrechnung, nicht zwei: pdfium gibt Punkte mit  #
# Ursprung unten links, der Canvas zaehlt Pixel von oben links -- also #
# skalieren UND spiegeln. Der Massstab kommt aus dem BILD, nicht aus   #
# der DPI-Einstellung: beim Rendern wird gerundet, und ein halbes      #
# Pixel verschiebt jede Markierung.                                    #
#                                                                     #
# Suche und Aufbau benutzen dieselbe Prozedur. Zwei Rechnungen fuer    #
# dieselbe Sache waeren zwei Gelegenheiten, sich zu vertun -- und eine #
# davon faellt spaeter auf als die andere.                             #
# ------------------------------------------------------------------ #
# Ein Formularfeld anklicken: hinblaettern und umranden.
# Nach update_info_panel ist die Feldliste NEU AUFGEBAUT -- die alte
# Auswahl zeigt dann auf einen geloeschten Eintrag. Wer danach noch
# einmal doppelklickt, trifft ins Leere: beim zweiten Anlauf blieb das
# Kaestchen angekreuzt und die Statuszeile zeigte die ALTE Meldung.
# Gemessen 06.09.2026.
#
# Wiedergefunden wird ueber Name UND Seite: ein Name kann auf mehreren
# Seiten vorkommen, und dann waere die Zeile sonst geraten.
proc ::demo_reselect {name seite} {
    set tv .pw.right.nb.form.tv
    foreach id [$tv children {}] {
        if {[$tv set $id id] eq $name && [$tv set $id seite] eq $seite} {
            $tv selection set $id
            $tv see $id
            return
        }
    }
}

proc ::demo_formSelect {} {
    global state
    set tv .pw.right.nb.form.tv
    set sel [$tv selection]
    .pw.left.c delete feldmark
    if {$sel eq ""} return
    set p [$tv set $sel seite]
    if {![string is integer -strict $p]} return
    if {$p != $state(page)} {
        set state(page) $p
        show_page
    }
    set box [$tv set $sel box]
    if {[llength $box] == 4} { markiere_box $box feldmark "#008000" }
}

# Ein Formularfeld ausfuellen.
#
# Ueber "pdfium::formfill", also ueber PDFiums Formularumgebung: die
# baut den Appearance-Strom selbst neu. Wer nur /V setzte, haette den
# Wert in der Datei und nicht auf dem Papier.
#
# Das GEOEFFNETE Dokument wird dabei im Speicher geaendert. Gespeichert
# wird erst auf Nachfrage -- und in eine NEUE Datei, damit die Vorlage
# bleibt.
# Die Sitzung fuer die Dauer eines Aufrufs aussetzen.
#
# NOETIG WAR DAS BIS 0.6.4: PDFium vertraegt nur EINE Formularumgebung
# je Dokument, jeder Aufruf baute sich seine eigene, und solange die
# Tippsitzung lief, fand formfill die Felder nicht. Seit die Umgebung
# dem DOKUMENT gehoert, geht beides nebeneinander -- gemessen, formfill
# waehrend einer offenen Sitzung fuellt.
#
# Die Pause bleibt trotzdem: sie gibt vor dem Fuellen den Fokus ab, und
# PDFium schreibt den Feldinhalt beim Fokusverlust fest. Ohne sie ginge
# ein halb getippter Wert verloren, sobald jemand daneben in der Liste
# doppelklickt.
proc mitPause {skript} {
    global state
    set war [expr {$state(edit) ne ""}]
    if {$war} { tippenAus 1 }
    set rc [catch {uplevel 1 $skript} e opts]
    if {$war} { tippenAn 1 }
    if {$rc} { return -options $opts $e }
    return $e
}

# Eine Auswahl aus einer Liste. Kein Freitext.
#
# Der Viewer fragte bei einem Kombinationsfeld nach Text -- und eines
# ohne Bearbeitungsflagge nimmt keinen. Die erlaubten Werte stehen seit
# 0.6.3 in "formfields", man muss sie nur anbieten.
proc demo_fragWahl {titel werte jetzt} {
    set w .wahl
    destroy $w
    toplevel $w
    wm title $w $titel
    wm transient $w .
    ttk::label $w.l -text $titel
    listbox $w.lb -height [expr {min([llength $werte], 10)}] -width 30 \
            -exportselection 0
    foreach v $werte { $w.lb insert end $v }
    set i [lsearch -exact $werte $jetzt]
    if {$i >= 0} { $w.lb selection set $i ; $w.lb see $i }
    ttk::frame $w.b
    ttk::button $w.b.ok  -text "Waehlen" -command {set ::demo_wahlOk 1}
    ttk::button $w.b.ab  -text "Abbruch" -command {set ::demo_wahlOk 0}
    pack $w.b.ok $w.b.ab -side left -padx 4
    pack $w.l -padx 8 -pady 6
    pack $w.lb -padx 8 -fill both -expand 1
    pack $w.b -pady 8
    bind $w.lb <Double-1> {set ::demo_wahlOk 1}
    bind $w <Escape> {set ::demo_wahlOk 0}
    set ::demo_wahlOk 0
    # ERST darstellen lassen, DANN greifen -- "grab" verlangt ein
    # sichtbares Fenster, und "tkwait visibility" kehrt ohne
    # Fenstermanager nie zurueck.
    update
    focus $w.lb
    if {[catch {grab $w}]} { focus -force $w.lb }
    tkwait variable ::demo_wahlOk
    set aus ""
    if {$::demo_wahlOk && [llength [$w.lb curselection]]} {
        set aus [$w.lb get [lindex [$w.lb curselection] 0]]
    }
    catch {grab release $w}
    destroy $w
    return $aus
}

proc ::demo_formFill {} {
    global state
    set tv .pw.right.nb.form.tv
    set sel [$tv selection]
    if {$sel eq ""} return
    # Der TECHNISCHE Name, nicht der angezeigte: formfill spricht das
    # Feld ueber /T an, und /TU ist nur Beschriftung.
    set name [$tv set $sel id]
    set typ  [$tv set $sel type]
    set p    [$tv set $sel seite]
    # Ein Kaestchen wird UMGESCHALTET, nicht beschrieben. Es hat einen
    # Zustand und keinen Wert -- ein Eingabefeld dafuer zu oeffnen waere
    # eine Frage nach etwas, das es nicht gibt.
    #
    # Ein Optionsfeld laesst sich nicht abwaehlen: in einer Gruppe ist
    # immer eines gewaehlt. Es wird darum nur GESETZT.
    # EIN OPTIONSFELD: DIESE ZEILE, nicht der Gruppenname.
    #
    # Eine Gruppe teilt sich einen Namen. "formfill $name 1" trifft
    # darum immer das ERSTE Widget -- Express und Overnight waren in
    # form-gruppe.pdf nicht waehlbar, und nach Normal hiess es "schon
    # gewaehlt".
    #
    # Die Feldliste fuehrt je Zeile ihr eigenes Rechteck. Ein Klick
    # dorthin trifft genau dieses Widget. Die API konnte es laengst
    # (basic.test 2.60 mit dem Exportwert) -- die Bedienung nicht, und
    # kein Test sah es.
    if {$typ eq "radiobutton"} {
        set box [$tv set $sel box]
        if {[llength $box] != 4} {
            tk_messageBox -icon error -message \
                    "\"$name\": kein Rechteck fuer diese Zeile"
            return
        }
        lassign $box bl bu br bo
        set war [expr {$state(edit) ne ""}]
        if {!$war} { tippenAn 1 }
        if {$state(edit) eq ""} {
            tk_messageBox -icon error -message \
                    "\"$name\": keine Sitzung fuer den Klick"
            return
        }
        pdfium::editclick $state(edit) [expr {($bl + $br) / 2.0}] \
                [expr {($bu + $bo) / 2.0}]
        # Der Wert wird beim Fokusverlust festgeschrieben.
        tippenAus 1
        if {$war} { tippenAn 1 }
        update_info_panel
        demo_reselect $name $p
        show_page
        set state(pageinfo) "\"$name\" gewaehlt -- noch nicht gespeichert"
        return
    }
    if {$typ eq "checkbox"} {
        set jetztAn [expr {[$tv set $sel value] ni {Off {} 0}}]
        set soll [expr {!$jetztAn}]
        if {[catch {mitPause {pdfium::formfill $state(doc) $p [list $name $soll]}} e]} {
            tk_messageBox -icon error -message "Umschalten: $e"
            return
        }
        update_info_panel
        demo_reselect $name $p
        show_page
        set state(pageinfo) "\"$name\" [expr {$soll ? {angekreuzt}\
                : {abgewaehlt}}] -- noch nicht gespeichert"
        return
    }
    # AUSWAHLFELDER: aus den Optionen waehlen, nicht tippen.
    #
    # "formfields" liefert sie seit 0.6.3 mit; die Feldliste zeigte sie
    # nur nicht. Der Viewer fragte statt dessen nach Freitext -- und ein
    # Kombinationsfeld ohne Bearbeitungsflagge nimmt keinen.
    if {$typ in {combobox listbox}} {
        set opts [$tv set $sel options]
        if {![llength $opts]} {
            tk_messageBox -icon info -message \
                    "\"$name\" nennt keine Auswahlwerte."
            return
        }
        set namen {}
        foreach paar $opts { lappend namen [lindex $paar 1] }
        set neu [demo_fragWahl "Feld \"$name\"" $namen [$tv set $sel value]]
        if {$neu eq ""} return
        if {[catch {mitPause {pdfium::formfill $state(doc) $p \
                [list $name $neu]}} e]} {
            tk_messageBox -icon error -message "Ausfuellen: $e"
            return
        }
        update_info_panel
        demo_reselect $name $p
        show_page
        set state(pageinfo) "\"$name\" auf \"$neu\" -- noch nicht gespeichert"
        return
    }
    if {$typ ni {text}} {
        tk_messageBox -icon info -title "Ausfuellen" -message \
            "\"$name\" ist ein $typ und laesst sich hier nicht ausfuellen.\
\n\nText- und Kombinationsfelder nehmen einen Wert, Ankreuz- und\
 Optionsfelder einen Zustand -- eine Schaltflaeche oder Signatur\
 keines von beidem."
        return
    }
    set alt [$tv set $sel value]
    set neu [demo_fragText "Feld \"$name\"" $alt]
    if {$neu eq ""} return
    if {[catch {mitPause {pdfium::formfill $state(doc) $p [list $name $neu]}} e]} {
        tk_messageBox -icon error -message "Ausfuellen: $e"
        return
    }
    update_info_panel
    demo_reselect $name $p
    show_page
    set state(pageinfo) "\"$name\" gefuellt -- noch nicht gespeichert"
}

# Ein kleiner Eingabedialog. tk_getString gibt es nicht, und einen
# eigenen Toplevel dafuer zu bauen ist weniger Arbeit als eine
# Fremdabhaengigkeit.
# Das gefuellte Dokument sichern.
#
# In eine NEUE Datei, und derselbe Name wird abgelehnt: die Vorlage soll
# bleiben. Dasselbe Muster wie beim Festschreiben.
proc ::demo_formSave {} {
    global state
    if {$state(doc) eq ""} return
    set aus [tk_getSaveFile -title "Gefuelltes Formular speichern unter" \
            -defaultextension .pdf \
            -initialfile [file rootname [file tail $state(file)]]-gefuellt.pdf \
            -filetypes {{PDF {.pdf}} {Alle *}}]
    if {$aus eq ""} return
    if {[file normalize $aus] eq [file normalize $state(file)]} {
        tk_messageBox -icon error -message \
            "Bitte einen anderen Namen: die Vorlage soll bleiben."
        return
    }
    # VOR dem Speichern den Fokus abgeben.
    #
    # PDFium schreibt den Feldinhalt erst beim FOKUSVERLUST ins
    # Dokument. Wer waehrend einer offenen Sitzung speichert, verliert
    # sonst genau das zuletzt Getippte -- gemessen:
    #
    #   im Dokument waehrend der Sitzung:  "Muster GmbH"
    #   in der gespeicherten Datei:        "Muster GmbH"
    #   nach editend im Speicher:          "Muster GmbHVreden"
    #
    # Der Wert stand in der Sitzung und nicht im Dokument. Auf dem
    # Bildschirm war er zu sehen -- das ist das Tueckische daran.
    #
    # Die Sitzung wird danach wieder aufgesetzt, damit man weitertippen
    # kann: sie zu beenden waere richtig und unbequem.
    set warEdit [expr {$state(edit) ne ""}]
    if {$warEdit} { tippenAus 1 }
    if {[catch {pdfium::save $state(doc) $aus} e]} {
        tk_messageBox -icon error -message "Speichern: $e"
        if {$warEdit} { tippenAn 1 }
        return
    }
    if {$warEdit} { tippenAn 1 }
    set state(pageinfo) "Geschrieben: [file tail $aus]"
}

proc ::demo_fragText {titel vorgabe} {
    set w .frage
    catch {destroy $w}
    toplevel $w
    wm title $w $titel
    wm transient $w .
    set ::demo_frageWert $vorgabe
    set ::demo_frageOk 0
    ttk::label $w.l -text $titel
    ttk::entry $w.e -textvariable ::demo_frageWert -width 40
    ttk::frame $w.b
    ttk::button $w.b.ok  -text "Uebernehmen" \
        -command {set ::demo_frageOk 1 ; destroy .frage}
    ttk::button $w.b.ab  -text "Abbrechen" \
        -command {set ::demo_frageOk 0 ; destroy .frage}
    pack $w.b.ok $w.b.ab -side left -padx 4
    pack $w.l -padx 8 -pady {8 2} -anchor w
    pack $w.e -padx 8 -pady 2 -fill x
    pack $w.b -padx 8 -pady 8
    bind $w.e <Return> {set ::demo_frageOk 1 ; destroy .frage}
    bind $w <Escape>   {set ::demo_frageOk 0 ; destroy .frage}
    $w.e selection range 0 end

    # ERST darstellen lassen, DANN greifen.
    #
    # "grab" verlangt ein dargestelltes Fenster. Direkt nach dem
    # Erzeugen ist es das noch nicht, und Tk meldet
    #
    #     grab failed: window not viewable
    #
    # Gemeldet am 06.09.2026. Auf einer schnellen Maschine geht es
    # manchmal gut -- das ist das Schlimmste daran, denn dann faellt es
    # erst beim Benutzer auf.
    #
    # "update" und nicht "tkwait visibility": letzteres ist die
    # Lehrbuchform, kehrt aber ohne Fenstermanager NIE zurueck -- ohne
    # X-Sitzung gemessen, das Programm blieb haengen. Ein Haenger statt
    # einer Fehlermeldung ist der schlechtere Tausch.
    update
    focus $w.e
    # Und selbst danach kann der Griff scheitern, wenn ein anderes
    # Fenster ihn haelt. Kein Grund, den Dialog nicht zu zeigen -- er
    # ist dann eben nicht modal.
    if {[catch {grab $w}]} {
        # Ohne Griff muss wenigstens die Tastatur hier landen.
        focus -force $w.e
    }
    tkwait window $w
    if {!$::demo_frageOk} { return "" }
    return $::demo_frageWert
}

# Canvas-Pixel zurueck in Seitenkoordinaten.
#
# Die Gegenrichtung zu markiere_box, und mit DENSELBEN Zahlen: Massstab
# aus dem Bild, y gespiegelt. Zwei Rechnungen fuer dieselbe Umrechnung
# gingen auseinander, und dann traefe der Klick knapp daneben -- was man
# fuer ein zu kleines Feld hielte statt fuer einen Rechenfehler.
proc pixelZuPunkt {cx cy} {
    global state
    lassign [seitenmass] wmm hmm
    set wpt [expr {$wmm * 72.0 / 25.4}]
    set hpt [expr {$hmm * 72.0 / 25.4}]
    if {$wpt <= 0 || $hpt <= 0} { return {} }
    set sx [expr {[image width  pdfpage] / $wpt}]
    set sy [expr {[image height pdfpage] / $hpt}]
    if {$sx <= 0 || $sy <= 0} { return {} }
    return [list [expr {$cx / $sx}] [expr {$hpt - $cy / $sy}]]
}

# ---------------------------------------------------------------------
# Tippmodus: direkt in die Seite schreiben
# ---------------------------------------------------------------------
#
# Waehrend einer Sitzung bleiben Formularumgebung UND Seite offen --
# Fokus und Schreibmarke sind Zustand, und der ist nach jedem Aufruf
# weg. Darum ein eigener Modus mit sichtbarem Anfang und Ende, statt
# ihn stillschweigend im Hintergrund zu halten.
#
# Beendet wird beim Umblaettern, beim Schliessen und auf Escape: eine
# Sitzung, die man vergisst, haelt eine Seite offen und schreibt den
# zuletzt getippten Wert nicht fest.
set state(edit) ""
set state(spurkanal) ""
# Der Bereich einer offenen Aufklappliste, in Seitenpunkten. Leer =
# nichts offen. Siehe felderRahmen.
set state(offeneliste) {}
set state(seiteninfo) ""

# Die Sitzung laeuft, sobald die Seite Formularfelder hat -- ohne Knopf.
#
# Andere Betrachter haben keinen "Tippmodus": man klickt hinein und
# schreibt. Ein Modus, den man erst einschalten muss, ist eine Huerde vor
# einer Selbstverstaendlichkeit -- und wer ihn nicht findet, haelt das
# Dokument fuer nicht ausfuellbar. Genau das ist heute schon einmal
# passiert, mit dem Reiter "Formular".
#
# Auf einer Seite OHNE Felder entsteht keine Sitzung: sie haette nichts
# zu tun und hielte nur eine Seite offen.
proc tippenAn {{still 0}} {
    global state
    if {$state(doc) eq "" || $state(edit) ne ""} return
    if {![llength [pdfium::formfields $state(doc) $state(page)]]} return
    if {[catch {pdfium::editbegin $state(doc) $state(page)} h]} {
        if {!$still} { tk_messageBox -icon error -message "Tippen: $h" }
        return
    }
    set state(edit) $h
    .pw.left.c configure -cursor xterm
    focus .pw.left.c
    if {!$still} {
        set state(pageinfo) "Ins Feld klicken und schreiben; Tab zum\
                naechsten Feld."
    }
}

proc tippenAus {{still 0}} {
    global state
    if {$state(edit) eq ""} return
    # editend gibt den Fokus ab, und PDFium schreibt den Feldinhalt beim
    # Fokusverlust fest. Ohne das ginge das zuletzt getippte Feld
    # verloren -- genau das, an dem man gerade gearbeitet hat.
    catch {pdfium::editend $state(edit)}
    set state(edit) ""
    .pw.left.c configure -cursor {}
    update_info_panel
    show_page
    if {!$still} {
        set state(pageinfo) "Tippmodus beendet -- noch nicht gespeichert"
    }
}

proc tippenKlick {wx wy} {
    global state
    # DEN TASTATURFOKUS HOLEN.
    #
    # Ein Canvas bekommt ihn beim Anklicken NICHT von selbst -- anders
    # als ein Eingabefeld. "focus" stand nur beim Aufsetzen der Sitzung;
    # wer danach einmal in die Feldliste geklickt hatte, war ihn los und
    # bekam ihn nie zurueck.
    #
    # Gemeldet am 06.09.2026 mit drei Bildschirmfotos: die Statuszeile
    # sagte "Schreibmarke gesetzt -- tippen", und es liess sich nichts
    # eingeben. Die Marke SASS, nur kam keine Taste an.
    focus .pw.left.c
    set cx [.pw.left.c canvasx $wx]
    set cy [.pw.left.c canvasy $wy]
    set pt [pixelZuPunkt $cx $cy]
    if {![llength $pt]} return
    lassign $pt px py
    # Klick in BEIDEN Koordinaten: die Umrechnung war schon zweimal die
    # eigentliche Frage.
    spurSchreib [format "Klick Fenster %s/%s -> Seite %.1f/%.1f" \
            $wx $wy $px $py]
    set traf [pdfium::editclick $state(edit) $px $py]
    # WAS PDFIUM NEU GEZEICHNET HABEN WILL.
    #
    # Ist die gemeldete Flaeche deutlich HOEHER als das Feld selbst, hat
    # sich etwas aufgeklappt. Nur die Hoehe, nicht "irgendwas ist
    # anders": nach jedem Tastendruck meldet PDFium ebenfalls einen
    # Bereich, und der ist das Feld.
    set state(offeneliste) {}
    if {$traf} {
        set zu [pdfium::editstate $state(edit)]
        set r [dict get $zu rect]
        if {[llength $r] == 4} {
            lassign $r rl ru rr ro
            # DIE HOEHE DES GEKLICKTEN FELDES, nicht die der Auswahl.
            #
            # Der erste Anlauf las sie aus der Feldliste. Wer direkt auf
            # die Seite klickt, hat dort aber nichts ausgewaehlt -- dann
            # blieb die Hoehe 0 und die Erkennung stumm. Gemeldet
            # 08.09.2026, nachdem der Rahmen "immer noch drin" war.
            #
            # Gesucht wird das Feld, in dem der Klick LIEGT. Das ist
            # dieselbe Auskunft, unabhaengig davon, was gerade markiert
            # ist.
            set feldhoehe 0
            foreach e [pdfium::formfields $state(doc) $state(page)] {
                set b [lindex $e 4]
                if {[llength $b] != 4} continue
                lassign $b bl bu br bo
                if {$px >= $bl && $px <= $br && $py >= $bu && $py <= $bo} {
                    set feldhoehe [expr {$bo - $bu}]
                    break
                }
            }
            if {$feldhoehe > 0 && $ro - $ru > $feldhoehe * 1.5} {
                set state(offeneliste) $r
            }
        }
    }
    if {$traf == 2} {
        # EIN EINTRAG IN EINER OFFENEN AUFKLAPPLISTE.
        #
        # Die Wahl ist damit fertig. PDFium schreibt den Wert aber erst
        # beim FOKUSVERLUST fest -- ohne das zeigt die Feldliste weiter
        # leer und die Seite den alten Stand, und es sieht aus, als
        # waere nichts geschehen. Genau so gemeldet: gewaehlt, kurz
        # sichtbar, dann wieder leer -- waehrend in der gespeicherten
        # Datei der Wert stand.
        # Die Liste ist zu.
        set state(offeneliste) {}
        tippenAus 1
        tippenAn 1
        update_info_panel
        tippenZeichnen
        set state(pageinfo) "gewaehlt -- noch nicht gespeichert"
        return
    }
    if {!$traf} {
        # KEIN Treffer -- dann wenigstens den alten Weg anbieten.
        #
        # Bis hierher endete es mit "Dort liegt kein Feld", und damit war
        # auch die Auswahl ueber die Feldliste nicht mehr per Klick
        # erreichbar: der Tippweg hatte den anderen verdraengt. Wer also
        # aus irgendeinem Grund nicht getroffen wurde, konnte gar nichts
        # mehr anklicken.
        #
        # canvasKlick rechnet mit DENSELBEN Zahlen, prueft aber gegen die
        # Rechtecke aus "formfields" statt gegen PDFiums Trefferpruefung.
        # Gehen die beiden auseinander, sieht man es an der Meldung --
        # und das ist eine Auskunft und keine Sackgasse.
        canvasKlick $wx $wy
        set tv .pw.right.nb.form.tv
        if {[llength [$tv selection]]} {
            # PDFium hat den Punkt nicht getroffen, das Rechteck aus der
            # Feldliste aber schon. Dann NOCH EINMAL, in der MITTE des
            # Feldes.
            #
            # Ein Optionsfeld ist zwoelf Punkt gross; wer knapp daneben
            # trifft, liegt im Rechteck und ausserhalb dessen, was
            # PDFium als Treffer gelten laesst. Gemessen: auf die Mitte
            # geklickt trifft JEDES Feld der Demo.
            #
            # Nur zu melden waere richtig und unbrauchbar -- der Benutzer
            # hat ja auf das Feld gezeigt.
            set box [$tv set [$tv selection] box]
            set gesetzt 0
            if {[llength $box] == 4} {
                lassign $box bl bu br bo
                set gesetzt [pdfium::editclick $state(edit) \
                        [expr {($bl + $br) / 2.0}] [expr {($bu + $bo) / 2.0}]]
            }
            if {$gesetzt} {
                tippenZeichnen
                tippenMarkieren [expr {($bl + $br) / 2.0}] \
                        [expr {($bu + $bo) / 2.0}]
                set state(pageinfo) "Schreibmarke in\
                        \"[$tv set [$tv selection] id]\" -- tippen;\
                        Tab zum naechsten"
                return
            }
            set state(pageinfo) "\"[$tv set [$tv selection] id]\"\
                    gewaehlt -- Doppelklick in der Liste fuellt es"
        } else {
            set state(pageinfo) [format \
                "Dort liegt kein Feld (Seitenpunkt %.1f / %.1f)" $px $py]
        }
        return
    }
    # Reihenfolge ist jetzt gleichgueltig: der Seitenzaehler steht in
    # "seiteninfo", die Meldung in "pageinfo". Vorher teilten sie sich
    # eine Variable, und show_page hat jede Meldung ueberschrieben.
    # Und zeigen, WO die Marke sitzt. Ohne Markierung sieht der Klick
    # aus, als sei nichts passiert -- auf dem Bildschirmfoto war das
    # Feld nach dem Klick nicht zu unterscheiden von vorher.
    tippenZeichnen
    tippenMarkieren $px $py
    set state(pageinfo) "Schreibmarke gesetzt -- tippen; Tab zum naechsten"
}

proc tippenTaste {keysym zeichen} {
    spurSchreib "Taste $keysym '[string map {\n \\n} $zeichen]'"
    global state
    switch -- $keysym {
        Escape    { tippenAus ; return }
        Tab       { pdfium::editkey $state(edit) tab }
        ISO_Left_Tab { pdfium::editkey $state(edit) tab }
        BackSpace { pdfium::editkey $state(edit) back }
        Delete    { pdfium::editkey $state(edit) del }
        Up        { pdfium::editkey $state(edit) up }
        Down      { pdfium::editkey $state(edit) down }
        Left      { pdfium::editkey $state(edit) left }
        Right     { pdfium::editkey $state(edit) right }
        Home      { pdfium::editkey $state(edit) home }
        End       { pdfium::editkey $state(edit) end }
        default   {
            # Nur druckbare Zeichen weiterreichen. Umschalt, Strg und
            # die Funktionstasten liefern ein leeres %A -- die als
            # Zeichen zu senden hiesse, Steuertasten in den Text zu
            # schreiben.
            if {$zeichen eq "" || [string is control -strict $zeichen]} return
            pdfium::editchar $state(edit) $zeichen
        }
    }
    tippenZeichnen
    laufendenWertNachtragen
}

# Neu zeichnen, waehrend getippt wird.
#
# Ueber den gewoehnlichen Weg: die Seite wird ohnehin mit -forms
# gerendert, und PDFium zeichnet den Feldinhalt aus derselben Umgebung,
# in der getippt wird. Ein eigener Renderpfad waere ein zweiter Weg zu
# demselben Bild.
proc tippenZeichnen {} {
    global state
    # UEBER DIE SITZUNG zeichnen, nicht ueber show_page.
    #
    # "render -forms 1" zeigt den FESTGESCHRIEBENEN Stand, nicht den
    # laufenden. PDFium schreibt einen Feldwert erst beim Fokusverlust
    # fest. Gemessen: drei Zeichen getippt, das Bild blieb bei 1406
    # dunklen Punkten; erst nach dem Beenden 1503.
    #
    # (Bis 0.6.4 baute render sich dafuer auch noch eine eigene
    # Umgebung. Das ist vorbei -- es gibt eine je Dokument. Der Grund
    # hier bleibt aber derselbe.)
    #
    # Man tippte blind. Wer nicht sieht, was er schreibt, kann es auch
    # nicht berichtigen.
    if {$state(edit) eq ""} { show_page ; return }
    if {[catch {pdfium::editrender $state(edit) -dpi $state(dpi) \
            -imagename pdfpage}]} {
        show_page
        return
    }
    .pw.left.c delete all
    .pw.left.c create image 0 0 -anchor nw -image pdfpage
    .pw.left.c configure -scrollregion \
        [list 0 0 [image width pdfpage] [image height pdfpage]]
    felderRahmen
    # Die Auswahl wieder drauf: "delete all" hat die gruene Marke mit
    # weggenommen. Ohne das verschwindet sie beim Tippen.
    set tv .pw.right.nb.form.tv
    set sel [$tv selection]
    if {$sel ne ""} {
        set box [$tv set $sel box]
        if {[llength $box] == 4} { markiere_box $box feldmark "#008000" }
    }
}

# Das Feld umranden, in dem die Marke sitzt.
#
# Gesucht wird ueber die Rechtecke aus der Feldliste -- dieselbe Quelle
# wie bei der Auswahl in der Liste, also dieselbe Markierung und keine
# zweite Rechnung.
proc tippenMarkieren {px py} {
    global state
    .pw.left.c delete feldmark
    set tv .pw.right.nb.form.tv
    foreach id [$tv children {}] {
        if {[$tv set $id seite] ne $state(page)} continue
        set box [$tv set $id box]
        if {[llength $box] != 4} continue
        lassign $box l u r o
        if {$px >= $l && $px <= $r && $py >= $u && $py <= $o} {
            markiere_box $box feldmark "#008000"
            return
        }
    }
}

# Ein Klick auf die Seite: liegt dort ein Formularfeld?
#
# Die Canvas-Koordinaten muessen durch canvasx/canvasy -- ein
# gescrolltes Bild liegt sonst um den Scrollbetrag daneben, und das
# faellt erst auf, wenn jemand weit unten klickt.
proc canvasKlick {wx wy} {
    global state
    if {$state(doc) eq ""} return
    set cx [.pw.left.c canvasx $wx]
    set cy [.pw.left.c canvasy $wy]
    set pt [pixelZuPunkt $cx $cy]
    if {![llength $pt]} return
    lassign $pt px py

    set tv .pw.right.nb.form.tv
    foreach id [$tv children {}] {
        if {[$tv set $id seite] ne $state(page)} continue
        set box [$tv set $id box]
        if {[llength $box] != 4} continue
        lassign $box l u r o
        if {$px >= $l && $px <= $r && $py >= $u && $py <= $o} {
            # Auswaehlen loest ueber <<TreeviewSelect>> schon die
            # Markierung aus -- nicht noch einmal von Hand malen.
            .pw.right.nb select .pw.right.nb.form
            $tv selection set $id
            $tv see $id
            return
        }
    }
    # Kein Treffer: die Markierung wegnehmen, sonst zeigt sie auf ein
    # Feld, das der Benutzer gar nicht mehr meint.
    .pw.left.c delete feldmark
    $tv selection set {}
}

# Wo die Felder liegen -- immer, nicht erst nach Auswahl in der Liste.
# Liegt das Rechteck ganz in der offenen Aufklappliste?
proc inOffenerListe {box} {
    global state
    if {![llength $state(offeneliste)] || [llength $box] != 4} { return 0 }
    lassign $state(offeneliste) ol ou orr oo
    lassign $box bl bu br bo
    return [expr {$bl >= $ol - 1 && $br <= $orr + 1
               && $bu >= $ou - 1 && $bo <= $oo + 1}]
}

proc felderRahmen {} {
    global state
    .pw.left.c delete feldrahmen
    if {$state(doc) eq ""} return
    if {![info exists state(page)]} return
    foreach e [pdfium::formfields $state(doc) $state(page)] {
        set box [lindex $e 4]
        if {[llength $box] == 4} {
            # KEIN RAHMEN UEBER EINER OFFENEN LISTE.
            #
            # Die Rahmen liegen auf dem Canvas UEBER dem gerenderten
            # Bild -- also auch ueber der Aufklappliste, die PDFium in
            # das Bild zeichnet. Gemeldet 08.09.2026 mit einem
            # Bildschirmfoto: ein blauer Kasten mitten in der offenen
            # Liste.
            #
            # Es war der Rahmen von "f_menge": das Feld liegt bei
            # y 631..647, die Liste oeffnet sich von 673 bis 606
            # darueber. Der Rahmen war richtig, nur an einer Stelle
            # sichtbar, an der er nichts zu suchen hat.
            #
            # PDFium nennt die Flaeche selbst: FFI_Invalidate meldet
            # nach dem Klick {159 606 361 673} -- Feld PLUS Liste.
            if {[inOffenerListe $box]} { continue }
            markiere_box $box feldrahmen "#4a90d9"
        }
    }
    .pw.left.c raise feldmark
}

proc markiere_box {box tag farbe {fuellung ""}} {
    global state
    lassign $box links unten rechts oben
    lassign [seitenmass] wmm hmm
    set wpt [expr {$wmm * 72.0 / 25.4}]
    set hpt [expr {$hmm * 72.0 / 25.4}]
    if {$wpt <= 0 || $hpt <= 0} return
    set sx [expr {[image width  pdfpage] / $wpt}]
    set sy [expr {[image height pdfpage] / $hpt}]
    set args [list -outline $farbe -width 1 -tags $tag]
    if {$fuellung ne ""} { lappend args -fill $fuellung -stipple gray25 }
    .pw.left.c create rectangle \
        [expr {$links  * $sx}] [expr {($hpt - $oben)  * $sy}] \
        [expr {$rechts * $sx}] [expr {($hpt - $unten) * $sy}] \
        {*}$args
    if {$tag eq "feldmark"} { .pw.left.c raise feldmark }
}

# Den Aufbau der aktuellen Seite auflisten.
proc update_aufbau {} {
    global state
    set tv .pw.right.nb.bau.tv
    $tv delete [$tv children {}]
    .pw.left.c delete objmark
    if {$state(doc) eq ""} return
    if {[catch {pdfium::pageobjects $state(doc) $state(page)} objekte]} return
    foreach e $objekte {
        lassign $e nr art box
        set groesse ""
        if {[llength $box] == 4} {
            lassign $box l u r o
            set groesse [format "%.0f x %.0f" [expr {$r-$l}] [expr {$o-$u}]]
        }
        $tv insert {} end -values [list $nr $art $groesse $box]
    }
}

proc cmd_flatten {} {
    global state
    if {$state(doc) eq ""} return
    set aus [tk_getSaveFile -title "Festgeschrieben speichern unter" \
            -defaultextension .pdf \
            -initialfile [file rootname [file tail $state(file)]]-fest.pdf \
            -filetypes {{PDF {.pdf}} {Alle *}}]
    if {$aus eq ""} return
    if {[file normalize $aus] eq [file normalize $state(file)]} {
        tk_messageBox -icon error -message \
            "Bitte einen anderen Namen: das Original soll bleiben."
        return
    }
    set n 0 ; set nichts 0
    if {[catch {
        for {set p 0} {$p < $state(total)} {incr p} {
            set state(pageinfo) "Schreibe Seite [expr {$p+1}] / $state(total) ..."
            update idletasks
            if {[pdfium::flatten $state(doc) $p -forms 1] eq "nothing"} {
                incr nichts
            } else {
                incr n
            }
        }
        pdfium::save $state(doc) $aus
    } err]} {
        tk_messageBox -icon error -message "Festschreiben: $err"
        set state(seiteninfo) "Seite [expr {$state(page)+1}] / $state(total)"
        return
    }
    # Das GEOEFFNETE Dokument ist jetzt im Speicher veraendert. Wer
    # weiterblaettert, saehe die eingebrannte Fassung, ohne es zu
    # wissen -- also neu laden, damit Anzeige und Datei zusammenpassen.
    set datei $state(file)
    open_pdf $datei
    set state(pageinfo) "$n Seite(n) festgeschrieben, $nichts ohne Inhalt"
    tk_messageBox -icon info -message \
        "Geschrieben: $aus\n\n$n Seite(n) festgeschrieben, $nichts hatten nichts einzubrennen.\n\nDas Original ist unveraendert."
}

proc cmd_suchen_alle {} {
    global state
    set tv .pw.right.nb.such.tv
    $tv delete [$tv children {}]
    if {$state(doc) eq "" || [string trim $state(suchtext)] eq ""} {
        set state(pageinfo) "Nichts zu suchen"
        return
    }
    .pw.right.nb select .pw.right.nb.such
    set gesamt 0
    for {set p 0} {$p < $state(total)} {incr p} {
        # Bei vielen Seiten dauert das; die Statuszeile sagt, wo es
        # steht. Ohne das haelt man den Viewer fuer haengengeblieben.
        set state(pageinfo) "Suche Seite [expr {$p+1}] / $state(total) ..."
        update idletasks
        if {[catch {pdfium::search $state(doc) $p $state(suchtext)} treffer]} {
            continue
        }
        if {![llength $treffer]} continue
        # Fuer die Umgebung den Seitentext EINMAL holen, nicht je
        # Treffer -- sonst liest man dieselbe Seite zehnmal.
        set text [pdfium::gettext $state(doc) $p]
        foreach t $treffer {
            lassign $t pos cnt
            set von [expr {$pos - 20}]
            if {$von < 0} { set von 0 }
            set bis [expr {$pos + $cnt + 20}]
            set um [string range $text $von $bis]
            # Zeilenumbrueche wuerden die Tabellenzeile zerreissen.
            set um [string map [list \n " " \r ""] $um]
            $tv insert {} end -values [list [expr {$p+1}] [string trim $um] $p]
            incr gesamt
        }
    }
    if {$gesamt == 0} {
        set state(pageinfo) "\"$state(suchtext)\": nichts gefunden"
        bell
    } else {
        set state(pageinfo) "$gesamt Treffer auf allen Seiten"
    }
}

proc cmd_suchen {} {
    global state
    .pw.left.c delete treffer
    if {$state(doc) eq "" || [string trim $state(suchtext)] eq ""} {
        set state(seiteninfo) "Seite [expr {$state(page)+1}] / $state(total)"
        return
    }
    if {[catch {
        pdfium::search $state(doc) $state(page) $state(suchtext) -rects 1
    } treffer]} {
        tk_messageBox -icon error -message "Suche: $treffer"
        return
    }
    # Die Umrechnung steht in markiere_box -- eine Stelle, nicht zwei.
    set n 0
    foreach t $treffer {
        lassign $t pos cnt rects
        foreach r $rects {
            lassign $r links unten rechts oben
            markiere_box $r treffer "#c00000" "#ffff00"
            incr n
        }
    }
    set state(pageinfo) "[llength $treffer] Treffer"
    if {$n == 0} { bell }
}

# Nach dem Zeichnen die Sitzung fuer DIESE Seite aufsetzen.
#
# Eine Sitzung gilt fuer genau eine Seite. Sie beim Blaettern
# mitzunehmen hiesse, auf einer Seite zu tippen und eine andere
# anzusehen.
proc tippenNachziehen {} {
    global state
    if {$state(doc) eq "" || $state(edit) ne ""} return
    after idle {catch {tippenAn 1}}
}

proc show_page {} {
    global state
    if {$state(doc) eq ""} return

    set p $state(page)
    set n $state(total)
    # Der reine Zaehler gehoert in die Werkzeugleiste, Meldungen in die
    # Statuszeile. Vorher teilten sie sich eine Variable, und jede
    # Meldung wurde vom naechsten Seitenaufbau ueberschrieben -- das war
    # der Grund, warum ich "Schreibmarke gesetzt" nach dem Zeichnen
    # setzen musste. Zwei Dinge, zwei Variablen.
    set state(seiteninfo) "Seite [expr {$p+1}] / $n"

    # Rendern: Bild heißt immer "pdfpage"
    #
    # "-forms 1", damit AUSGEFUELLTE FELDER zu sehen sind. Ohne das
    # fehlten sie im Bild, waehrend die Feldliste rechts sie auffuehrte:
    # gemessen an tests/fixtures/form.pdf -- "Muster GmbH" stand in der
    # Liste und nicht auf der Seite. pdfium zeichnet Widget-Annotationen
    # nicht mit dem Seiteninhalt, sondern ueber die Formularschicht.
    if {[catch {
        pdfium::render $state(doc) $p \
            -dpi $state(dpi) -forms 1 \
            -printing $state(druckansicht) -imagename pdfpage
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

    # ALLE Felder umranden, nicht nur das angeklickte.
    #
    # Die Fixtures tragen fuer leere Widgets einen leeren Appearance-
    # Strom (/Length 0, kein Rechteck). PDFium zeichnet dann nichts:
    # gemessen an tests/fixtures/form-drei.pdf -- die Liste nannte
    # eins/zwei/drei, auf der Seite standen nur die Beschriftungen.
    # Ohne Rahmen sieht das Blatt leer aus und laesst sich nicht treffen.
    felderRahmen

    # Die Markierung gehoert zum Bild, nicht zur Suche: nach Zoom oder
    # Seitenwechsel muss sie neu gerechnet werden, sonst steht sie am
    # alten Ort und behauptet etwas Falsches. Loeschen allein waere
    # ehrlicher als stehenlassen, aber unbequem.
    if {[string trim $state(suchtext)] ne ""} { cmd_suchen }
    update_aufbau
    tippenNachziehen
}

proc cmd_prev {} {
    global state
    if {$state(doc) eq "" || $state(page) == 0} return
    tippenAus 1
    incr state(page) -1
    show_page
}

proc cmd_next {} {
    global state
    if {$state(doc) eq ""} return
    if {$state(page) >= $state(total) - 1} return
    # Eine Sitzung gilt fuer GENAU EINE Seite -- sie ueber den
    # Seitenwechsel mitzunehmen hiesse, auf einer Seite zu tippen und
    # eine andere anzusehen. Und der zuletzt getippte Wert wird beim
    # Fokusverlust festgeschrieben, also muss sie sauber enden.
    tippenAus 1
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
# ------------------------------------------------------------------ #
# Temporaeres Verzeichnis (plattformabhaengig)                        #
#                                                                     #
# Ersetzt das fest verdrahtete /tmp. Unter Windows gibt es das nicht; #
# TEMP zeigt dort ueblicherweise nach AppData\Local\Temp.             #
# ------------------------------------------------------------------ #
proc tmpdir {} {
    foreach v {TMPDIR TEMP TMP} {
        if {[info exists ::env($v)] && [file isdirectory $::env($v)]} {
            return $::env($v)
        }
    }
    return [expr {$::tcl_platform(platform) eq "windows" ? "C:/Temp" : "/tmp"}]
}

proc get_printers {} {
    # Windows: aus dem Spooler. Unix: aus CUPS.
    if {$::tcl_platform(platform) eq "windows"} {
        # Erst pruefen, OB diese pdfiumtcl-Version drucken kann. Fehlt der
        # Windows-Druck (aeltere .so, oder eine zweite Kopie ohne Druck im
        # Modulpfad), gibt es ::pdfium::printers gar nicht -- ein blankes
        # `catch ... return [list]` verschluckt das und liefert eine leere
        # Liste, ohne den Grund zu nennen.
        if {![llength [info commands ::pdfium::canprint]] || ![::pdfium::canprint]} {
            tk_messageBox -icon warning -title "Kein Windows-Druck" -message \
                "Diese pdfiumtcl-Version kann unter Windows nicht drucken.\n\n\
                 Vermutlich wird eine Kopie ohne Druckunterstuetzung geladen.\n\
                 Pruefe mit:\n\
                 \    echo puts \[info commands ::pdfium::*\] | tclsh\n\n\
                 Fehlen dort printers/print/canprint, liegt eine alte .so im\
                 Modulpfad (TCLLIBPATH) vor der installierten Fassung."
            return [list]
        }
        if {[catch {::pdfium::printers} out]} {
            tk_messageBox -icon error -title "Druckerliste" -message \
                "::pdfium::printers ist fehlgeschlagen:\n$out"
            return [list]
        }
        return $out
    }
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

    # ---------------------------------------------------------------- #
    #  Windows: direkt in den Drucker-Device-Context.                   #
    #                                                                   #
    #  Kein Rendern nach PNG, keine Zwischendateien, kein lp. Der       #
    #  Parameter dpi ist hier gegenstandslos -- die Ausgabe hat immer   #
    #  die Aufloesung, die der Treiber vorgibt.                        #
    # ---------------------------------------------------------------- #
    if {$::tcl_platform(platform) eq "windows"} {
        if {$range eq "current"} {
            set from $state(page)
            set to   $state(page)
        } else {
            set from 0
            set to   [expr {$state(total) - 1}]
        }

        set args [list -from $from -to $to -copies $copies -fit 1]
        if {$printer ne "(Standard)"} { lappend args -printer $printer }

        if {[catch {::pdfium::print $state(doc) {*}$args} res]} {
            tk_messageBox -icon error -message "Druckfehler: $res"
        } else {
            tk_messageBox -icon info \
                -message "Druckauftrag gesendet ($res Seite(n))."
        }
        return
    }

    # ---------------------------------------------------------------- #
    #  Unix: rendern und an CUPS geben                                  #
    # ---------------------------------------------------------------- #

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

        set tmpfile [file join [tmpdir] "pdfprint_p${p}_[pid].png"]

        # Seite mit Druckauflösung rendern
        if {[catch {
            # Auch hier -forms: was man sieht, soll man drucken.
            pdfium::render $state(doc) $p \
                -dpi $dpi -forms 1 -imagename printpage
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
    set sz  [seitenmass]
    set pdf_wmm [format "%.1f" [lindex $sz 0]]
    set pdf_hmm [format "%.1f" [lindex $sz 1]]

    # Variablen
    set ::_ql_printer  ""
    set ::_ql_band     54
    set ::_ql_dpi      300
    set ::_ql_range    "current"
    set ::_ql_cut      "EndOfPage"
    set ::_ql_fit      0

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
    # Skalierung --------------------------------------------------- #
    # Etiketten drucken normalerweise 1:1 -- jede Skalierung verschiebt
    # den Druck auf dem Band. Passt die PDF-Breite aber nicht zum
    # eingelegten Band, lehnt der Drucker den Auftrag sonst ab. Dann ist
    # Einpassen die einzige Alternative zum Abbruch.
    label $w.f.fl -text "Skalierung:" -anchor w
    frame $w.f.ff
    radiobutton $w.f.ff.r1 -text "1:1 (exakt)" \
        -variable ::_ql_fit -value 0 -command update_ql_info
    radiobutton $w.f.ff.r2 -text "auf Band einpassen" \
        -variable ::_ql_fit -value 1 -command update_ql_info
    pack $w.f.ff.r1 $w.f.ff.r2 -side left -padx 2
    grid $w.f.fl $w.f.ff -sticky w -pady 3

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
        set sz   [seitenmass]
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
                    $::_ql_range  $::_ql_cut $::_ql_fit
    }
}

proc do_print_ql {printer band dpi range cut {fit 0}} {
    global state

    # ---------------------------------------------------------------- #
    #  Windows: Band ueber das Papierformat, nicht ueber Pixelbreite.   #
    #                                                                   #
    #  Zuerst freies Format -paperw/-paperh: nur so bekommt das Etikett #
    #  seine tatsaechliche Laenge. Die benannten Treiberformulare       #
    #  ("54mm") tragen eine Nennlaenge von 29 mm -- alles Laengere      #
    #  wuerde abgeschnitten.                                            #
    #                                                                   #
    #  Verwirft der Treiber dmPaperLength, wird auf das benannte        #
    #  Formular zurueckgefallen; dann stimmt die Breite und die Laenge  #
    #  bestimmt der Treiber. Der Anwender erfaehrt das.                 #
    #                                                                   #
    #  cut bleibt wirkungslos: das Schneideverhalten steckt in den      #
    #  privaten Treiberdaten hinter dmDriverExtra und laesst sich nur   #
    #  ueber ein Profil im Treiberdialog setzen.                        #
    # ---------------------------------------------------------------- #
    if {$::tcl_platform(platform) eq "windows"} {
        if {$range eq "current"} {
            set pages [list $state(page)]
        } else {
            set pages [list]
            for {set i 0} {$i < $state(total)} {incr i} { lappend pages $i }
        }

        set done 0
        set fallback 0

        foreach p $pages {
            # Laufrichtung ist die Kante, die nicht der Bandbreite gleicht
            set sz [pdfium::pagesize $state(doc) $p]
            lassign $sz w_mm h_mm
            set len [expr {abs($w_mm - $band) < abs($h_mm - $band)
                           ? $h_mm : $w_mm}]

            # Passt die Seitenbreite nicht zum eingelegten Band, lehnt
            # der Drucker einen 1:1-Auftrag ab. Statt kommentarlos zu
            # scheitern wird das hier vorher geprueft.
            set pw [expr {abs($w_mm - $band) < abs($h_mm - $band)
                          ? $w_mm : $h_mm}]
            if {!$fit && abs($pw - $band) > 2.0} {
                set frage "Die Seite ist [format %.1f $pw] mm breit,\
                           das gewaehlte Band ${band} mm.\n\n\
                           Bei 1:1 lehnt der Drucker den Auftrag ab.\n\n\
                           Auf Bandbreite einpassen?"
                if {[tk_messageBox -icon question -type yesno \
                        -title "Breite passt nicht" -message $frage] ne "yes"} {
                    return
                }
                set fit 1
            }

            set base [list -printer $printer -from $p -to $p -fit $fit]

            set rc [catch {
                ::pdfium::print $state(doc) {*}$base \
                    -paperw $band -paperh $len
            } res]

            if {$rc} {
                set rc [catch {
                    ::pdfium::print $state(doc) {*}$base -paper "${band}mm"
                } res]
                if {!$rc} { set fallback 1 }
            }

            if {$rc} {
                tk_messageBox -icon error \
                    -message "QL-Druckfehler Seite [expr {$p+1}]: $res"
                return
            }
            incr done
        }

        set msg "QL-Druckauftrag gesendet ($done Etikett(en))."
        if {$fallback} {
            append msg "\n\nHinweis: Der Treiber hat das freie Format\
                        abgelehnt. Verwendet wurde das Formular\
                        \"${band}mm\" -- die Etikettenlaenge bestimmt dann\
                        der Treiber, nicht das PDF."
        }
        if {$cut ne "" && $cut ne "EndOfPage"} {
            append msg "\n\nHinweis: Die Schnitteinstellung \"$cut\" wird\
                        unter Windows nicht gesetzt. Sie laesst sich nur im\
                        Treiberdialog des Druckers hinterlegen."
        }
        tk_messageBox -icon info -message $msg
        return
    }

    # ---------------------------------------------------------------- #
    #  Unix: rendern und an CUPS geben                                  #
    # ---------------------------------------------------------------- #

    # Die Breite wird jetzt JE SEITE bestimmt (siehe Schleife), weil sie
    # von der Skalierung abhängt:
    #   einpassen (fit=1) -> Druckbreite des Bandes, Seite wird skaliert
    #   1:1       (fit=0) -> tatsächliche Seitenbreite bei $dpi

    # Seiten
    if {$range eq "current"} {
        set pages [list $state(page)]
    } else {
        set pages [list]
        for {set i 0} {$i < $state(total)} {incr i} {
            lappend pages $i
        }
    }

    # Bei 1:1 vorher prüfen, ob die Seite überhaupt aufs Band passt.
    # Sonst wird stillschweigend seitlich abgeschnitten. Gleiche Rückfrage
    # wie im Windows-Zweig -- nur wird dort der Auftrag abgelehnt, hier
    # beschnitten.
    if {!$fit} {
        foreach p $pages {
            lassign [pdfium::pagesize $state(doc) $p] pw_mm ph_mm
            if {abs($pw_mm - $band) > 2.0} {
                set frage "Die Seite ist [format %.1f $pw_mm] mm breit,\
                           das gewählte Band ${band} mm.\n\n\
                           Bei 1:1 wird seitlich abgeschnitten.\n\n\
                           Auf Bandbreite einpassen?"
                if {[tk_messageBox -icon question -type yesno \
                        -title "Breite passt nicht" -message $frage] eq "yes"} {
                    set fit 1
                }
                break
            }
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
        lassign [pdfium::pagesize $state(doc) $p] pw_mm ph_mm

        if {$fit} {
            # Auf Band einpassen: auf die DRUCKbreite rendern (54 mm -> 590 px)
            # und dem Treiber die volle Bandbreite melden.
            set w_mm $band
            set w_px [ql_width_px $band]
            if {$dpi == 600} { set w_px [expr {$w_px * 2}] }
        } else {
            # 1:1: die Seite behält ihre Maße. Breite in Pixeln direkt aus
            # der Seitenbreite bei der gewählten Auflösung.
            set w_mm $pw_mm
            set w_px [expr {int($pw_mm / 25.4 * $dpi + 0.5)}]
        }

        $pw.l configure -text \
            "Rendere Seite [expr {$p+1}] — [format %.1f $w_mm]mm,\
             ${w_px}px, ${dpi}DPI..."
        update

        set tmpfile [file join [tmpdir] "ql_p${p}_[pid].png"]

        if {[catch {
            pdfium::render $state(doc) $p \
                -width $w_px -forms 1 -imagename qlpage
            qlpage write $tmpfile -format png
        } err]} {
            tk_messageBox -icon error \
                -message "Render-Fehler Seite [expr {$p+1}]: $err"
            set ok 0
            break
        }

        # Höhe aus Seitenverhältnis und gemeldeter Breite -- NICHT aus
        # 300 dpi. Die gerenderte Breite ist beim Einpassen die Druckbreite
        # (49,95 mm für 54), gemeldet wird aber das Band. Wer da zwei
        # Maßstäbe mischt, bekommt zu kurze Etiketten, und bei 600 dpi
        # doppelt so lange.
        set iw [image width qlpage]
        set ih [image height qlpage]
        set h_mm [format "%.1f" [expr {double($ih) / $iw * $w_mm}]]
        lappend tmpfiles [list $tmpfile [format %.1f $w_mm] $h_mm]
    }

    if {$ok && [llength $tmpfiles] > 0} {
        foreach entry $tmpfiles {
            lassign $entry f wmm hmm

            $pw.l configure -text \
                "Sende an $printer  (${wmm} × ${hmm} mm)..."
            update

            set cmd [list lp -d $printer \
                -o "PageSize=Custom.${wmm}x${hmm}mm" \
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
${band}mm Band, ${dpi} DPI,\
[expr {$fit ? {eingepasst} : {1:1}}]."
    }

    foreach entry $tmpfiles {
        catch { file delete [lindex $entry 0] }
    }
    destroy $pw
}

# ------------------------------------------------------------------ #
# Start: Datei aus Kommandozeile?                                     #
# ------------------------------------------------------------------ #
# Mitschnitt, falls die Umgebung ihn verlangt. VOR dem Oeffnen, damit
# auch das erste Laden darin steht.
if {[info exists ::env(VIEWER4_SPUR)] && $::env(VIEWER4_SPUR) ne ""} {
    if {[catch {spurAn $::env(VIEWER4_SPUR)} sperr]} {
        puts stderr $sperr
    } else {
        puts stderr "Mitschnitt: $::env(VIEWER4_SPUR)"
    }
}

if {[llength $argv] >= 1} {
    after 100 [list open_pdf [lindex $argv 0]]
}
