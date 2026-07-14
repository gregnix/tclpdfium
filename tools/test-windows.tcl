# test-windows.tcl -- Abnahmetest fuer tclpdfium 0.5.2 unter Windows
#
# Aufruf (im Verzeichnis, das dist-win/ enthaelt):
#
#     tclsh90 test-windows.tcl dist-win  pfad\zu\test.pdf
#     tclsh86 test-windows.tcl dist-win  pfad\zu\test.pdf
#
# Laeuft ohne Tk. Wo Tk gebraucht wird, laedt der Test es selbst.
# Jeder Punkt meldet PASS / FAIL / SKIP. Am Ende steht eine Zusammenfassung.

set pkgDir [lindex $argv 0]
set pdfFile [lindex $argv 1]

if {$pkgDir eq "" || $pdfFile eq ""} {
    puts "Aufruf: tclsh test-windows.tcl <pkg-verzeichnis> <test.pdf>"
    exit 2
}
set pkgDir  [file normalize $pkgDir]
set pdfFile [file normalize $pdfFile]

set ::pass 0
set ::fail 0
set ::skip 0

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
    package require pdfiumtcl
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

check "Tk nicht geladen" {
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
