# test-windows.tcl -- Abnahmetest fuer tclpdfium unter Windows
#
# Aufruf (im Wurzelverzeichnis des Pakets):
#
#     tclsh tools/test-windows.tcl                      # nimmt beides selbst
#     tclsh tools/test-windows.tcl dist-win/windows64-tcl9
#     tclsh tools/test-windows.tcl dist-win/windows64-tcl9  eigene.pdf
#
# Laeuft ohne Tk. Wo Tk gebraucht wird, laedt der Test es selbst.
# Jeder Punkt meldet PASS / FAIL / SKIP. Am Ende steht eine Zusammenfassung.

set here [file dirname [file normalize [info script]]]
set root [file dirname $here]

set pkgDir  [lindex $argv 0]
set pdfFile [lindex $argv 1]

# Beides ist zu ERRATEN, nicht zu verlangen.
#
# Vorher brach das Skript ohne zwei Argumente mit einer Aufrufzeile ab --
# und verlangte eine Test-PDF, die im Baum laengst liegt. Wer den
# Abnahmetest laufen laesst, will wissen, ob das Paket geht, und nicht
# erst eine Datei suchen.

# EIN Argument, und es ist eine Datei? Dann ist es die PDF, nicht das
# Paketverzeichnis. Gemeldet am 05.09.2026: "test-windows.tcl
# sample.pdf" wurde als Verzeichnisangabe gelesen und lief ins Leere.
# Ein Verzeichnis und eine Datei lassen sich unterscheiden -- also
# unterscheiden, statt auf die Reihenfolge zu pochen.
if {$pdfFile eq "" && $pkgDir ne "" && [file isfile $pkgDir]} {
    set pdfFile $pkgDir
    set pkgDir ""
}

if {$pdfFile eq ""} {
    # Mehrere Orte durchsehen: die Datei ist im Laufe der Zeit von
    # tools/ nach tests/fixtures/ gewandert, und beide Baeume gibt es
    # draussen. Ein fester Pfad haette bei der Haelfte nicht gepasst.
    foreach k [list \
            [file join $root tests fixtures sample.pdf] \
            [file join $root tools sample.pdf] \
            [file join $here sample.pdf]] {
        if {[file readable $k]} { set pdfFile $k ; break }
    }
    if {$pdfFile eq ""} {
        puts "Keine Test-PDF gefunden. Gesucht in:"
        puts "  [file join $root tests fixtures sample.pdf]"
        puts "  [file join $root tools sample.pdf]"
        puts "Bitte eine angeben:"
        puts "  tclsh tools/test-windows.tcl <test.pdf>"
        exit 2
    }
}
if {$pkgDir eq ""} {
    # Die zur laufenden Tcl-Generation passende Fassung waehlen. Beide
    # anzubieten und die falsche zu nehmen waere schlimmer als zu fragen.
    set kandidat [expr {[package vsatisfies [info patchlevel] 9-]
                        ? "windows64-tcl9" : "windows64"}]
    set pkgDir [file join $root dist-win $kandidat]
    if {![file isdirectory $pkgDir]} {
        puts "Kein Paketverzeichnis gefunden ($pkgDir) -- bitte eines angeben:"
        puts "  tclsh tools/test-windows.tcl <pkg-verzeichnis> <test.pdf>"
        exit 2
    }
    puts "Paketverzeichnis: $kandidat (zu Tcl [info patchlevel])"
}
set pkgDir  [file normalize $pkgDir]
set pdfFile [file normalize $pdfFile]
if {![file readable $pdfFile]} {
    puts "Test-PDF nicht lesbar: $pdfFile"
    exit 2
}

set ::pass 0
set ::fail 0
set ::skip 0

# VOR dem Laden merken, ob Tk schon da ist.
#
# Punkt 3 fragt, ob das PAKET Tk hereinzieht. Unter "wish" ist Tk aber
# ohnehin geladen, und die Pruefung meldete dann "Tk wurde beim Laden
# hereingezogen" -- eine Aussage ueber den Aufruf, nicht ueber das
# Paket. Gemessen am 05.09.2026 mit "wish tools/test-windows.tcl".
set ::tkVorher [info exists ::tk_version]

proc bits {} { return [expr {$::tcl_platform(pointerSize) * 8}] }

proc _tmp {} {
    foreach v {TCLPDFIUM_TMPDIR TMPDIR TEMP TMP} {
        if {[info exists ::env($v)] && [file isdirectory $::env($v)]} {
            return $::env($v)
        }
    }
    return [expr {$::tcl_platform(platform) eq "windows" ? "C:/Temp" : "/tmp"}]
}

proc check {name script} {
    # Code 2 (TCL_RETURN) ist kein Fehler: die Pruefskripte melden ihr
    # Ergebnis mit [return]. Nur Code 1 (TCL_ERROR) ist ein Fehlschlag.
    set code [catch {uplevel 1 $script} result]
    if {$code == 1} {
        puts [format "  FAIL  %-38s %s" $name $result]
        incr ::fail
        return 0
    }
    if {$result eq "SKIP"} {
        puts [format "  SKIP  %-38s" $name]
        incr ::skip
        return 1
    }
    puts [format "  PASS  %-38s %s" $name $result]
    incr ::pass
    return 1
}

# Auf der falschen Plattform ist die Meldung sonst raetselhaft: der
# Lader sagt dann "invalid ELF header", und das klingt nach einer
# kaputten Datei statt nach der falschen Maschine. Kein Abbruch -- wer
# den Rest sehen will, soll ihn sehen.
if {$::tcl_platform(platform) ne "windows"} {
    puts "HINWEIS: Das hier ist ein WINDOWS-Test, und diese Maschine ist"
    puts "  $::tcl_platform(platform)/$::tcl_platform(os). Das Laden der DLL"
    puts "  wird scheitern (\"invalid ELF header\" o.ae.) -- das ist dann"
    puts "  kein Befund ueber das Paket."
    puts ""
}
puts "tclpdfium Windows-Abnahmetest"
puts "  Tcl       : [info patchlevel] ([bits]-bit)"
puts "  Paket     : $pkgDir"
puts "  PDF       : $pdfFile"
puts ""

# ---------------------------------------------------------------- 1. Laden
puts "1) Laden vom Dateisystem"

check "Verzeichnis vorhanden" {
    if {![file isdirectory $::pkgDir]} { error "nicht gefunden: $::pkgDir" }
    return ok
}

check "package require pdfiumtcl" {
    # VORNE anhaengen. Sonst gewinnt eine bereits installierte Fassung
    # (etwa C:/Tcl/lib/pdfium), die frueher in auto_path steht -- und der
    # Test prueft klaglos die falsche DLL.
    set ::auto_path [linsert $::auto_path 0 $::pkgDir]
    set v [package require pdfiumtcl]
    set ::geladen 1
    return $v
}

# Ohne Paket hat der Rest nichts zu messen.
#
# Vorher lief der Test weiter und stuerzte beim ersten Aufruf in eine
# Fehlerkaskade -- "invalid command name pdfium::open", zwanzig Zeilen
# Tcl-Ablaufverfolgung, und der eigentliche Grund (die DLL laedt nicht)
# stand ganz oben und ging unter. Gemessen am 05.09.2026, als das Skript
# ohne Argumente die Windows-DLL auf Linux zu laden versuchte.
if {![info exists ::geladen]} {
    puts ""
    puts "Das Paket liess sich nicht laden -- alles Weitere haette nichts"
    puts "zu messen. Haeufigste Gruende:"
    puts "  * falsches Paketverzeichnis (hier: $pkgDir)"
    puts "  * DLL fuer die andere Tcl-Generation oder Plattform"
    puts "  * pdfium.dll fehlt NEBEN der Paket-DLL"
    puts ""
    puts "  PASS $::pass   FAIL $::fail   SKIP $::skip"
    exit 1
}

check "geladen aus dem Pruefverzeichnis" {
    set from ""
    foreach l [info loaded] {
        if {[lindex $l 1] eq "Pdfiumtcl"} { set from [lindex $l 0] }
    }
    if {$from eq ""} { error "nicht in [info loaded]" }

    # Der entscheidende Punkt: kommt die DLL wirklich aus $pkgDir?
    set want [file normalize $::pkgDir]
    set got  [file normalize $from]
    if {![string match "$want/*" $got]} {
        error "FALSCHE DLL: $got\n        erwartet unterhalb von: $want\n\
               Eine andere Installation steht in auto_path und gewinnt."
    }
    return $from
}

# ------------------------------------------------------------ 2. Namespace
puts ""
puts "2) Namespace (0.5.2: voll qualifizierte Kommandonamen)"

check "Kommandos in ::pdfium" {
    set n [llength [info commands ::pdfium::*]]
    if {$n < 20} { error "nur $n Kommandos" }
    return "$n Kommandos"
}

check "nichts in ::pdfium::pdfium" {
    set n [llength [info commands ::pdfium::pdfium::*]]
    if {$n > 0} { error "$n Kommandos im falschen Namespace!" }
    return "leer, korrekt"
}

# ------------------------------------------------------------- 3. Headless
puts ""
puts "3) Headless (Tk darf nicht geladen sein)"
if {$::tkVorher} {
    puts "   Unter wish gestartet -- Tk war schon vor dem Laden da."
    puts "   Fuer diese Frage \"tclsh\" nehmen."
}

check "Tk nicht geladen" {
    if {$::tkVorher} {
        # Unter wish ist die Frage nicht zu beantworten: Tk war schon
        # da. Uebersprungen und gesagt, warum -- eine Pruefung, die
        # nichts messen kann, darf nicht so tun als ob.
        return SKIP
    }
    if {[info exists ::tk_version]} { error "Tk wurde beim Laden hereingezogen" }
    return "kein Tk"
}

# ------------------------------------------------------------ 4. PDF lesen
puts ""
puts "4) PDF lesen"

check "pdfium::open" {
    set ::doc [pdfium::open $::pdfFile]
    return ok
}

check "pdfium::pagecount" {
    return "[pdfium::pagecount $::doc] Seite(n)"
}

check "pdfium::pagesize" {
    lassign [pdfium::pagesize $::doc 0] w h
    return [format "%.1f x %.1f pt" $w $h]
}

check "pdfium::gettext" {
    set t [pdfium::gettext $::doc 0]
    if {[string length $t] == 0} { return "leer (Bild-PDF?)" }
    return "[string length $t] Zeichen"
}

catch {pdfium::close $::doc}

# ------------------------------------------- 4b. Was in 0.6.2 dazukam
#
# Der Abnahmetest stammte aus 0.5.2 und pruefte nur, dass das Paket
# ueberhaupt laedt und liest. Wer wissen will, ob die NEUEN Sachen auf
# dieser Maschine tun, was sie sollen, braucht sie hier -- sonst laeuft
# der Test gruen durch und sagt nichts ueber sie.
puts ""
puts "4b) Neu in 0.6.2"

set ::doc [pdfium::open $::pdfFile]
set ::wort ""

check "search: irgendein Wort finden" {
    # Ein Wort aus dem Text der Seite nehmen, nicht eines erfinden --
    # sonst prueft der Test die Testdatei und nicht das Paket.
    set t [pdfium::gettext $::doc 0]
    foreach w [regexp -all -inline {[[:alpha:]]{4,}} $t] {
        set ::wort $w ; break
    }
    if {$::wort eq ""} { return SKIP }
    set n [llength [pdfium::search $::doc 0 $::wort]]
    if {$n < 1} { error "\"$::wort\" nicht gefunden" }
    return "\"$::wort\": $n Treffer"
}

check "search -rects: Rechtecke, geordnet und auf dem Blatt" {
    if {$::wort eq ""} { return SKIP }
    lassign [pdfium::pagesize $::doc 0] wmm hmm
    set wpt [expr {$wmm * 72.0 / 25.4 + 1}]
    set hpt [expr {$hmm * 72.0 / 25.4 + 1}]
    set n 0
    foreach t [pdfium::search $::doc 0 $::wort -rects 1] {
        lassign $t pos cnt rects
        if {[llength $t] != 3} { error "kein drittes Element" }
        foreach r $rects {
            lassign $r l u re o
            if {$l > $re || $u > $o} { error "Kanten vertauscht: $r" }
            if {$l < -1 || $re > $wpt || $u < -1 || $o > $hpt} {
                error "ausserhalb des Blattes: $r"
            }
            incr n
        }
    }
    if {$n < 1} { error "keine Rechtecke" }
    return "$n Rechteck(e)"
}

check "charboxes -range trifft genau den Treffer" {
    if {$::wort eq ""} { return SKIP }
    lassign [lindex [pdfium::search $::doc 0 $::wort] 0] pos cnt
    set teil [pdfium::charboxes $::doc 0 -range [list $pos $cnt]]
    set gelesen [join [lmap e $teil {lindex $e 0}] ""]
    if {$gelesen ne $::wort} {
        error "gelesen \"$gelesen\", gesucht \"$::wort\""
    }
    return "\"$gelesen\" aus [llength $teil] Zeichen"
}

check "pageobjects: Arten und Rechtecke" {
    set arten [dict create]
    foreach e [pdfium::pageobjects $::doc 0] {
        if {[llength $e] != 3} { error "kein Tripel: $e" }
        lassign $e idx typ box
        if {$typ ni {text path image shading form unknown}} {
            error "unbekannte Art: $typ"
        }
        if {[llength $box] ni {0 4}} { error "kein Rechteck: $box" }
        dict incr arten $typ
    }
    if {![dict size $arten]} { error "keine Objekte" }
    return [join [lmap {k v} $arten {format "%s %d" $k $v}] ", "]
}

check "flatten: Anmerkung wird Zeichnung" {
    # Braucht eine Datei MIT Anmerkung -- die Test-PDF hat meist keine.
    set ff [file join $root tests fixtures form.pdf]
    if {![file readable $ff]} {
        foreach k [list [file join $root tools form.pdf] \
                        [file join $here form.pdf]] {
            if {[file readable $k]} { set ff $k ; break }
        }
    }
    if {![file readable $ff]} { return SKIP }
    set fd [pdfium::open $ff]
    set annotVorher [llength [pdfium::annot_list $fd 0]]
    set r [pdfium::flatten $fd 0]
    set annotNachher [llength [pdfium::annot_list $fd 0]]
    set objNachher [llength [pdfium::pageobjects $fd 0]]
    pdfium::close $fd
    if {$annotVorher < 1} { return SKIP }
    if {$r ne "flattened"} { error "flatten meldete \"$r\"" }
    if {$annotNachher != 0} { error "$annotNachher Anmerkung(en) uebrig" }
    return "$annotVorher Anmerkung -> $objNachher Objekte"
}

check "flatten -forms rettet einen gefuellten Wert" {
    # Die Zusammenarbeit zweier Pakete: pdf4tcl::fillForms setzt /V und
    # laesst den Appearance-Strom leer; flatten ohne -forms brennt den
    # LEEREN ein, und der Wert ist danach ganz weg.
    set gf [file join $root tests fixtures form-filled.pdf]
    if {![file readable $gf]} {
        foreach k [list [file join $root tools form-filled.pdf] \
                        [file join $here form-filled.pdf]] {
            if {[file readable $k]} { set gf $k ; break }
        }
    }
    if {![file readable $gf]} { return SKIP }
    set erg {}
    foreach opt [list {} {-forms 1}] {
        set aus [file join [_tmp] tclpdfium-flat-[pid].pdf]
        set fd [pdfium::open $gf]
        pdfium::flatten $fd 0 {*}$opt
        pdfium::save $fd $aus
        pdfium::close $fd
        set fe [pdfium::open $aus]
        lappend erg [string match "*Spedition Muster*" [pdfium::gettext $fe 0]]
        pdfium::close $fe
        file delete $aus
    }
    if {$erg ne {0 1}} {
        error "ohne/mit -forms ergab {$erg}, erwartet {0 1}"
    }
    return "ohne -forms verloren, mit -forms erhalten"
}

check "mctext -boxes: Tripel statt Wechselliste" {
    set ohne [pdfium::mctext $::doc 0]
    set mit  [pdfium::mctext $::doc 0 -boxes 1]
    if {[llength $ohne] % 2 != 0} { error "ungerade Wechselliste" }
    if {[llength $mit] != [llength $ohne] / 2} {
        error "[llength $mit] Tripel gegen [expr {[llength $ohne]/2}] Paare"
    }
    foreach e $mit {
        if {[llength $e] != 3} { error "kein Tripel: $e" }
    }
    return "[llength $mit] Eintrag/Eintraege"
}

catch {pdfium::close $::doc}

# ------------------------------------- 5. addimagebitmap ohne Tk: kein Crash
puts ""
puts "5) addimagebitmap ohne Tk (0.5.1 stuerzte hier ab)"

check "Fehler statt Absturz" {
    if {[catch {pdfium::addimagebitmap 0 0 nichtda 0 0 10 10} e]} {
        return "Tcl-Fehler, kein Crash"
    }
    error "haette einen Fehler liefern muessen"
}

# ------------------------------------------------------------------- 6. Tk
puts ""
puts "6) Tk-Pfad (render + addimagebitmap)"

if {[catch {package require Tk} tkerr]} {
    check "Tk verfuegbar" { return SKIP }
} else {
    check "pdfium::render" {
        set ::doc [pdfium::open $::pdfFile]
        set ::img [pdfium::render $::doc 0 -width 200]
        return "[image width $::img]x[image height $::img] px"
    }
    check "render -clip: kleiner als die ganze Seite" {
        lassign [pdfium::pagesize $::doc 0] wmm hmm
        set wpt [expr {$wmm * 72.0 / 25.4}]
        set hpt [expr {$hmm * 72.0 / 25.4}]
        pdfium::render $::doc 0 -dpi 72 -imagename ::pruefGanz
        pdfium::render $::doc 0 -dpi 72 -imagename ::pruefTeil \
                -clip [list 0 0 [expr {$wpt/2.0}] [expr {$hpt/2.0}]]
        set gw [image width ::pruefGanz] ; set tw [image width ::pruefTeil]
        set gh [image height ::pruefGanz] ; set th [image height ::pruefTeil]
        image delete ::pruefGanz ::pruefTeil
        if {$tw >= $gw || $th >= $gh} {
            error "Ausschnitt ${tw}x${th} nicht kleiner als ${gw}x${gh}"
        }
        return "${tw}x${th} statt ${gw}x${gh} px"
    }

    check "render -forms: Feldwerte werden mitgezeichnet" {
        set ff [file join $root tests fixtures form.pdf]
        if {![file readable $ff]} {
            foreach k [list [file join $root tools form.pdf] \
                            [file join $here form.pdf]] {
                if {[file readable $k]} { set ff $k ; break }
            }
        }
        if {![file readable $ff]} { return SKIP }
        set fd [pdfium::open $ff]
        pdfium::render $fd 0 -dpi 100 -imagename ::prfOhne
        pdfium::render $fd 0 -dpi 100 -forms 1 -imagename ::prfMit
        set ohne 0 ; set mit 0
        # "img" waere hier fatal: check laeuft auf GLOBALER Ebene, und
        # $::img haelt weiter oben das Bild fuer addimagebitmap. Die
        # Schleifenvariable hat es ueberschrieben, das Bild wurde
        # danach geloescht, und der naechste Punkt meldete "no such
        # photo image" -- ein Fehler, der wie ein Paketfehler aussieht
        # und keiner ist.
        foreach {bildname var} {::prfOhne ohne ::prfMit mit} {
            set h [image height $bildname]
            for {set y 0} {$y < $h} {incr y 3} {
                foreach zeile [$bildname data -from 0 $y \
                        [image width $bildname] [expr {$y+1}]] {
                    foreach px $zeile {
                        if {$px ne "#ffffff"} { incr $var }
                    }
                }
            }
        }
        image delete ::prfOhne ::prfMit
        pdfium::close $fd
        if {$mit <= $ohne} {
            error "ohne $ohne, mit $mit dunkle Punkte -- kein Unterschied"
        }
        return "$ohne -> $mit dunkle Punkte"
    }

    check "render -printing: laeuft und liefert dasselbe Mass" {
        # Ob sich der INHALT unterscheidet, haengt am Dokument -- eine
        # Ebene mit /PrintState /OFF hat nicht jede Datei. Geprueft wird
        # hier, dass die Option angenommen wird und ein Bild derselben
        # Groesse entsteht.
        pdfium::render $::doc 0 -dpi 72 -imagename ::pruefS
        pdfium::render $::doc 0 -dpi 72 -printing 1 -imagename ::pruefD
        set gleich [expr {[image width ::pruefS] == [image width ::pruefD]
                       && [image height ::pruefS] == [image height ::pruefD]}]
        set masz "[image width ::pruefS]x[image height ::pruefS]"
        image delete ::pruefS ::pruefD
        # Die Groesse VOR dem Loeschen merken -- "$::img" ist ein
        # anderes Bild und existiert hier nicht mehr. Mein Fehler,
        # gefangen vom Lauf.
        if {!$gleich} { error "verschiedene Bildmasse" }
        return "beide $masz px"
    }

    check "pdfium::addimagebitmap" {
        set nd [pdfium::newdoc]
        set pg [pdfium::newpage $nd 0 200 200]
        set r [pdfium::addimagebitmap $pg $nd $::img 10 10 100 100]
        pdfium::generatecontent $pg
        pdfium::closepage $pg
        set out [file join [_tmp] tclpdfium-test-out.pdf]
        pdfium::save $nd $out
        return "geschrieben: [file size $out] Bytes"
    }
    catch {pdfium::close $::doc}
}

# ---------------------------------------------------- 7. VFS / Starpack
puts ""
puts "7) Laden aus einem VFS"
puts "   Dafuer gibt es ein eigenes Skript, weil es eine andere Paketstruktur"
puts "   braucht als die flache aus build-tclpdfium-bawt.bat:"
puts ""
puts "     tclsh test-vfs.tcl <dieses-verzeichnis> <test.pdf>"

# ----------------------------------------------------------------- Fazit
puts ""
puts "----------------------------------------------------------"
puts [format "  PASS %d   FAIL %d   SKIP %d" $::pass $::fail $::skip]
if {$::fail == 0} {
    puts "  Alles gruen."
} else {
    puts "  Fehlgeschlagen -- Ausgabe oben durchsehen."
}
exit [expr {$::fail > 0 ? 1 : 0}]
