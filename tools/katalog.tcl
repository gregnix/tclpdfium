#!/usr/bin/env wish
# katalog.tcl -- was PDFium je Formularfeld zeichnet, und woraus
#
# Aufruf:  wish tools/katalog.tcl datei.pdf ?datei.pdf ...?
#          wish tools/katalog.tcl --csv datei.pdf > katalog.csv
#
# WOZU DAS DA IST
#
# "Sieht nicht aus wie in Chrome" hat mindestens vier verschiedene
# Ursachen, und sie sehen alle gleich aus:
#
#   A  /AP brauchbar        PDFium zeichnet -- vielleicht anders als Acrobat
#   B  /AP da, Laenge 0     PDFium zeichnet nichts. Eigenschaft der DATEI
#   C  /AP fehlt            dasselbe
#   D  /NeedAppearances     der Betrachter baut selbst, und jeder anders
#
# Die Testsuite unterscheidet das nicht. Sie kann es auch nicht: sie hat
# keine Vergleichsseite. Dieses Werkzeug misst die HAELFTE, die hier zu
# messen ist -- je Feld, was in der Datei steht und was PDFium daraus
# macht. Die andere Haelfte ist, dieselbe Datei in Chrome oder Acrobat
# zu oeffnen und Zelle fuer Zelle zu vergleichen.
#
# Ohne diese zweite Haelfte waere der Katalog eine Tabelle mit
# Meinungen.
#
# WAS GEMESSEN WIRD, je Feld:
#
#   art        Feldart, wie formfields sie nennt
#   name       Feldname
#   wert       /V -- was das Dokument sagt
#   ap         Laenge des Erscheinungsstroms; 0 heisst LEER
#   dunkel     dunkle Bildpunkte IM Feldrechteck, mit Formularschicht
#   ohne       dieselben ohne Formularschicht
#   fall       A / B / C / D, daraus abgeleitet
#
# "dunkel" gegen "ohne" trennt, wer zeichnet: bringt die Formularschicht
# nichts dazu, kommt alles aus dem Seiteninhalt.

package require Tk
package require pdfiumtcl

# Dunkle Punkte in einem Bildausschnitt.
#
# Zeilenweise ueber "image data": langsam, aber ohne weitere Pakete.
# Bei einem Feld von 150 mal 16 Punkten sind das ein paar tausend
# Vergleiche -- fuer ein Messwerkzeug schnell genug.
proc dunkelImFeld {img x1 y1 x2 y2} {
    set n 0
    if {$x2 <= $x1 || $y2 <= $y1} { return 0 }
    set breite [image width $img]
    set hoehe  [image height $img]
    if {$x1 < 0} { set x1 0 }
    if {$y1 < 0} { set y1 0 }
    if {$x2 > $breite} { set x2 $breite }
    if {$y2 > $hoehe}  { set y2 $hoehe }
    for {set y $y1} {$y < $y2} {incr y} {
        foreach zeile [$img data -from $x1 $y $x2 [expr {$y + 1}]] {
            foreach px $zeile {
                if {$px ne "#ffffff"} { incr n }
            }
        }
    }
    return $n
}

# Traegt die Datei /NeedAppearances?
#
# Roh gesucht, nicht ueber die Bindung: PDFium meldet die Flagge nicht,
# und fuer ein Messwerkzeug ist das Suchen im Rohtext ehrlicher als eine
# Annahme.
proc needAppearances {datei} {
    set ch [open $datei rb]
    set roh [read $ch]
    close $ch
    return [expr {[string first "/NeedAppearances true" $roh] >= 0}]
}

# Wie oft steht "/AP" in der Datei? Trennt Fall C von A und B.
proc apInDatei {datei} {
    set ch [open $datei rb]
    set roh [read $ch]
    close $ch
    set n 0
    set i 0
    while {[set i [string first "/AP" $roh $i]] >= 0} { incr n ; incr i 3 }
    return $n
}

# ERSTER FUND DES KATALOGS, 08.09.2026:
#
# C und B sind NICHT dasselbe, und zwar andersherum als erwartet.
#
#   Fall B -- /AP da, Laenge 0:   PDFium zeichnet NICHTS
#   Fall C -- /AP fehlt ganz:     PDFium BAUT sich einen und zeichnet
#
# Gemessen an derselben Feldart: ein Textfeld mit leerem Strom brachte
# 0 dunkle Punkte, dasselbe Feld ohne jeden Strom 581 -- und der Wert
# stand auf dem Blatt.
#
# KEIN Erscheinungsstrom ist also BESSER als ein leerer. Wer einen
# leeren schreibt, sagt dem Betrachter "so sieht das Feld aus:
# naemlich gar nicht", und der glaubt es. Wer keinen schreibt, laesst
# ihn rechnen.
#
# Das erklaert, warum leere Formulare aus pdf4tcl unsichtbare Felder
# haben, und wo die Behebung hingehoert: nicht in den Betrachter.
# Die Einstufung braucht die DATEI, nicht nur das, was PDFium meldet.
#
# Erster Anlauf: "ap > 0 heisst A". Damit wurde C zu A -- denn sobald
# PDFium sich selbst einen Strom baut, meldet apLength dessen Laenge.
# Die Zeile stimmte, die Einstufung nicht.
#
# Jetzt entscheidet zuerst die Datei: steht dort ueberhaupt kein /AP,
# ist es C, was immer PDFium daraus macht.
proc fallVon {ap needapp apInDatei} {
    if {$needapp}      { return D }
    if {$apInDatei < 1} { return C }
    if {$ap > 0}       { return A }
    return B
}

proc katalog {datei csv} {
    set doc [pdfium::open $datei]
    set seiten [pdfium::pagecount $doc]
    set needapp [needAppearances $datei]
    set apZahl [apInDatei $datei]
    set dpi 100
    set k [expr {$dpi / 72.0}]
    if {!$csv} {
        puts ""
        puts "== [file tail $datei]   NeedAppearances:\
                [expr {$needapp ? {ja} : {nein}}]   /AP in der Datei:\
                $apZahl"
        puts [format "  %-12s %-12s %-14s %5s %7s %6s  %s" \
                art name wert ap dunkel ohne fall]
    }
    for {set p 0} {$p < $seiten} {incr p} {
        lassign [pdfium::pagesize $doc $p] wmm hmm
        set hpt [expr {$hmm * 72.0 / 25.4}]
        pdfium::render $doc $p -dpi $dpi -forms 1 -imagename ::katMit
        pdfium::render $doc $p -dpi $dpi -imagename ::katOhne
        foreach f [pdfium::formfields $doc $p] {
            lassign $f art name wert flags rect opt erkl ap
            lassign $rect l u r o
            # Feldrechteck in Bildpunkte, y von oben. Zwei Punkte
            # Rand abziehen, damit der Feldrahmen nicht mitzaehlt.
            set x1 [expr {int($l * $k) + 2}]
            set x2 [expr {int($r * $k) - 2}]
            set y1 [expr {int(($hpt - $o) * $k) + 2}]
            set y2 [expr {int(($hpt - $u) * $k) - 2}]
            set mit  [dunkelImFeld ::katMit  $x1 $y1 $x2 $y2]
            set ohne [dunkelImFeld ::katOhne $x1 $y1 $x2 $y2]
            set fall [fallVon $ap $needapp $apZahl]
            if {$csv} {
                puts [join [list [file tail $datei] $p $art $name $wert \
                        $ap $mit $ohne $fall] ";"]
            } else {
                puts [format "  %-12s %-12s %-14s %5s %7s %6s  %s" \
                        $art $name [string range $wert 0 13] \
                        $ap $mit $ohne $fall]
            }
        }
        image delete ::katMit ::katOhne
    }
    pdfium::close $doc
}

set csv 0
set dateien {}
foreach a $argv {
    if {$a eq "--csv"} { set csv 1 ; continue }
    lappend dateien $a
}
if {![llength $dateien]} {
    puts stderr "Aufruf: wish tools/katalog.tcl ?--csv? datei.pdf ..."
    exit 1
}
if {$csv} {
    puts "datei;seite;art;name;wert;ap;dunkel;ohne;fall"
}
foreach d $dateien { katalog $d $csv }
if {!$csv} {
    puts ""
    puts "  A  /AP in der Datei und brauchbar -- PDFium zeichnet daraus"
    puts "  B  /AP in der Datei, aber LEER -- PDFium zeichnet nichts"
    puts "  C  gar kein /AP -- PDFium baut sich einen und zeichnet"
    puts "  D  /NeedAppearances gesetzt"
    puts ""
    puts "  B und C sind NICHT dasselbe: der leere Strom ist die"
    puts "  Aussage \"so sieht das Feld aus: gar nicht\", und PDFium"
    puts "  glaubt sie. Kein Strom laesst es rechnen."
    puts ""
    puts "  Dieselbe Datei in Chrome oder Acrobat oeffnen und je Zeile"
    puts "  vergleichen. Erst dann ist der Katalog vollstaendig."
    puts ""
    puts "  FIREFOX TAUGT DAFUER NICHT. Gemessen 08.09.2026 an allen"
    puts "  vier Faellen: pdf.js baut fuer jedes Formularfeld ein"
    puts "  eigenes HTML-Bedienelement und sieht sich den"
    puts "  Erscheinungsstrom gar nicht erst an. Darum sehen dort auch"
    puts "  die Felder aus Fall B aus wie Felder -- und darum sagt ein"
    puts "  Unterschied zu Firefox nichts ueber /AP."
    puts ""
    puts "  Chrome benutzt dasselbe PDFium und ist darum die Probe auf"
    puts "  die BINDUNG -- aber nur fuer das, was aus dem Strom kommt."
    puts ""
    puts "  ZWEI SCHICHTEN AUSEINANDERHALTEN. Gemessen 08.09.2026:"
    puts "  in Fall B zeigt Chrome hellblaue Flaechen, wo hier nichts"
    puts "  steht. Das ist Chromes BEDIENOBERFLAECHE ueber dem Strom,"
    puts "  nicht ein anderes Ergebnis von FFLDraw. Wer das der Bindung"
    puts "  anlastet, misst die falsche Schicht -- und baut hinterher"
    puts "  ein Overlay, weil er einen Zeichenfehler vermutet hat."
    puts ""
    puts "  Vergleichbar ist der INHALT: Text, Haken, Punkt, Auswahl."
    puts "  Acrobat ist die Probe auf PDFium selbst."
}
exit 0
