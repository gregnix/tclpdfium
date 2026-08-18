#!/usr/bin/env tclsh
# structure-dump.tcl -- den Strukturbaum einer Seite anzeigen, so wie
# PDFium ihn sieht.
#
#   tclsh examples/structure-dump.tcl datei.pdf ?seite?
#
# Der Sinn ist die ZWEITE Lesung. Wer einen Strukturbaum selbst schreibt
# und ihn mit dem eigenen Werkzeug wieder liest, bestaetigt sich per
# Konstruktion. PDFium ist die Engine hinter Chrome und Edge -- was hier
# nicht auftaucht, sieht dort auch niemand.

package require Tk
package require pdfiumtcl

if {$argc < 1} {
    puts stderr "Aufruf: structure-dump.tcl datei.pdf ?seite?"
    exit 2
}
set file [lindex $argv 0]
set page [expr {$argc > 1 ? [lindex $argv 1] : 0}]

set doc [::pdfium::open $file]
set n [::pdfium::pagecount $doc]
if {$page >= $n} {
    puts stderr "Seite $page gibt es nicht -- das Dokument hat $n"
    ::pdfium::close $doc
    exit 1
}

# Text je marked-content ID. Damit steht neben jedem Element, was
# darin wirklich gezeichnet wurde -- der Baum allein sagt das nicht.
set mctext [::pdfium::mctext $doc $page]

proc show {nodes {ind ""}} {
    global mctext
    foreach node $nodes {
        set line "$ind[dict get $node type]"
        foreach key {title alt actual lang id} {
            if {[dict exists $node $key]} {
                append line " $key='[dict get $node $key]'"
            }
        }
        set mcids [dict get $node mcids]
        if {[llength $mcids]} { append line "  mcids: $mcids" }
        if {[dict exists $node attrs]} {
            append line "  attrs: [dict get $node attrs]"
        }
        # Der Text der eigenen MCIDs, gekuerzt. Ein Element ohne eigenen
        # Text -- eine Table etwa -- bleibt leer.
        set txt ""
        foreach m $mcids {
            if {[dict exists $mctext $m]} {
                append txt [string trim [dict get $mctext $m]] " "
            }
        }
        set txt [string trim $txt]
        if {$txt ne ""} {
            if {[string length $txt] > 40} {
                set txt "[string range $txt 0 37]..."
            }
            append line "  \"$txt\""
        }
        puts $line
        show [dict get $node children] "$ind    "
    }
}

# MCIDs in der Reihenfolge des Strukturbaums.
proc treeOrder {nodes varName} {
    upvar 1 $varName out
    foreach node $nodes {
        foreach m [dict get $node mcids] { lappend out $m }
        treeOrder [dict get $node children] out
    }
}

set tree [::pdfium::structure $doc $page]
if {![llength $tree]} {
    puts "Seite $page hat keinen Strukturbaum."
} else {
    puts "Seite $page von $n:"
    show $tree "  "

    # Lesereihenfolge: liest das Dokument so, wie es getaggt ist?
    #
    # Ein Screenreader folgt dem Strukturbaum, gezeichnet wird in der
    # Reihenfolge des Content-Streams. Stimmen die nicht ueberein, ist
    # das Dokument trotzdem konform -- veraPDF prueft es nicht -- und
    # trotzdem falsch vorgelesen.
    #
    # WAS DIESE PRUEFUNG NICHT LEISTET: sie vergleicht Baum gegen
    # Content-Strom, nicht gegen die Anordnung auf der SEITE. Wer zwei
    # Absaetze in umgekehrter Reihenfolge zeichnet, bekommt hier ein
    # "stimmt ueberein" -- Baum und Strom sind ja einig -- obwohl der
    # Screenreader den unteren Absatz zuerst vorliest. Gemessen an einem
    # eigens dafuer gebauten Dokument.
    #
    # Dafuer braeuchte es die Koordinaten je MCID; die liefert pdfium
    # ueber die Textobjekte, aber mctext gibt sie derzeit nicht heraus.
    set tord {}
    treeOrder $tree tord
    set sord {}
    foreach {id t} $mctext { if {$id >= 0} { lappend sord $id } }
    # Ein Element kann eine MCID tragen, ohne eigenen Text zu haben --
    # eine Table tut das. Verglichen werden nur die mit Text.
    set tordText {}
    foreach m $tord { if {$m in $sord} { lappend tordText $m } }

    puts ""
    if {$tordText eq $sord} {
        puts "  Lesereihenfolge: stimmt mit dem Strukturbaum ueberein"
    } else {
        puts "  Lesereihenfolge WEICHT AB:"
        puts "    Baum:   $tordText"
        puts "    Inhalt: $sord"
    }

    # Text ausserhalb des Baums erreicht kein Screenreader.
    set lose 0
    foreach {id t} $mctext { if {$id < 0} { incr lose } }
    if {$lose} {
        puts "  $lose Textobjekt(e) OHNE marked-content ID --\
                nicht im Strukturbaum"
    }
}
::pdfium::close $doc
exit 0
