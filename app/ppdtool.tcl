#!/usr/bin/env tclsh
# ppdtool.tcl — PPD-Einträge für Brother QL verwalten
#
# Aufruf:
#   tclsh ppdtool.tcl list   [drucker]
#   tclsh ppdtool.tcl add    breite hoehe [drucker]
#   tclsh ppdtool.tcl delete name [drucker]
#
# Beispiele:
#   tclsh ppdtool.tcl list
#   tclsh ppdtool.tcl add 54 56
#   tclsh ppdtool.tcl add 54 90
#   tclsh ppdtool.tcl delete custom54x56
#
# Standard-Drucker: Brother_QL-820NWB_IPP
# PPD-Datei: /etc/cups/ppd/${drucker}.ppd

# ------------------------------------------------------------------ #
# Konfiguration                                                       #
# ------------------------------------------------------------------ #
set DEFAULT_PRINTER "Brother_QL-820NWB_IPP"
set PPD_DIR         "/etc/cups/ppd"

# ------------------------------------------------------------------ #
# Hilfsfunktionen                                                     #
# ------------------------------------------------------------------ #

proc mm2pt {mm} {
    return [expr {$mm / 25.4 * 72.0}]
}

proc pt2mm {pt} {
    return [expr {$pt * 25.4 / 72.0}]
}

# PPD-Datei lesen
proc ppd_read {ppdfile} {
    set fd [open $ppdfile r]
    set content [read $fd]
    close $fd
    return $content
}

# PPD-Datei schreiben
proc ppd_write {ppdfile content} {
    set fd [open $ppdfile w]
    puts -nonewline $fd $content
    close $fd
}

# Backup erstellen
proc ppd_backup {ppdfile} {
    set ts [clock format [clock seconds] -format {%Y%m%d_%H%M%S}]
    set backup "${ppdfile}.bak_${ts}"
    file copy $ppdfile $backup
    puts "Backup: $backup"
    return $backup
}

# PPD-Datei ermitteln
proc get_ppdfile {printer} {
    global PPD_DIR
    set f [file join $PPD_DIR "${printer}.ppd"]
    if {![file exists $f]} {
        error "PPD nicht gefunden: $f"
    }
    return $f
}

# Prüfen ob ein Format bereits existiert
proc format_exists {content name} {
    return [expr {[string first "*PageSize ${name}/" $content] >= 0}]
}

# ------------------------------------------------------------------ #
# detail — einzelnen Eintrag anzeigen                                 #
# ------------------------------------------------------------------ #
proc cmd_detail {printer name} {
    set ppdfile [get_ppdfile $printer]
    set content [ppd_read $ppdfile]

    set found 0
    foreach line [split $content "\n"] {
        foreach key {PageSize PageRegion ImageableArea PaperDimension} {
            if {[string match "*${key} ${name}/*" $line]} {
                puts $line
                incr found
            }
        }
    }
    if {!$found} {
        puts "Format '$name' nicht gefunden."
    }
}

# ------------------------------------------------------------------ #
# list — alle benutzerdefinierten Formate anzeigen                    #
# ------------------------------------------------------------------ #
proc cmd_list {printer} {
    set ppdfile [get_ppdfile $printer]
    set content [ppd_read $ppdfile]

    puts "PPD: $ppdfile"
    puts ""
    puts "Alle PageSize-Einträge:"
    puts [string repeat "-" 60]

    set found 0
    foreach line [split $content "\n"] {
        if {[regexp {^\*PageSize\s+(\S+)/(.+):\s+} $line -> name label]} {
            set pt_w ""
            set pt_h ""
            if {[regexp {\[(\d+)\s+(\d+)\]} $line -> pw ph]} {
                set mm_w [format "%.1f" [pt2mm $pw]]
                set mm_h [format "%.1f" [pt2mm $ph]]
                puts [format "  %-25s %s x %s mm  (%s x %s pt)" \
                    $name $mm_w $mm_h $pw $ph]
            } else {
                puts "  $name  /  $label"
            }
            incr found
        }
    }
    puts [string repeat "-" 60]
    puts "$found Format(e) gefunden."
}

# ------------------------------------------------------------------ #
# add — neues Format hinzufügen                                       #
# ------------------------------------------------------------------ #
proc cmd_add {printer w_mm h_mm} {
    set ppdfile [get_ppdfile $printer]

    # Maße berechnen
    set w_pt [expr {round([mm2pt $w_mm])}]
    set h_pt [expr {round([mm2pt $h_mm])}]

    # Druckbereich: HWMargins aus PPD sind 4.32 8.40 4.32 8.40
    set ia_x1 4.32
    set ia_y1 8.40
    set ia_x2 [expr {$w_pt - 4.32}]
    set ia_y2 [expr {$h_pt - 8.40}]

    # Formatname
    set name "custom${w_mm}x${h_mm}"
    # Punkt statt Komma im Namen vermeiden
    set name [string map {. _} $name]
    # .0 entfernen wenn ganzzahlig
    set name [regsub {_0\b} $name ""]
    set name [regsub {_0x} $name "x"]
    set label "${w_mm}mm x ${h_mm}mm"

    set content [ppd_read $ppdfile]

    if {[format_exists $content $name]} {
        puts "Format '$name' existiert bereits — abgebrochen."
        return
    }

    # Backup
    ppd_backup $ppdfile

    # 4 Einträge erzeugen
    set ps_entry  "*PageSize ${name}/${label}: \"<</PageSize\[${w_pt} ${h_pt}\]/ImagingBBox null>>setpagedevice\""
    set pr_entry  "*PageRegion ${name}/${label}: \"<</PageSize\[${w_pt} ${h_pt}\]/ImagingBBox null>>setpagedevice\""
    set ia_entry  "*ImageableArea ${name}/${label}: \"[format "%.2f %.2f %.2f %.2f" $ia_x1 $ia_y1 $ia_x2 $ia_y2]\""
    set pd_entry  "*PaperDimension ${name}/${label}: \"${w_pt} ${h_pt}\""

    # Einfügen: jeweils vor *CloseUI
    set content [string map \
        [list "*CloseUI: *PageSize" "${ps_entry}\n*CloseUI: *PageSize"] \
        $content]

    set content [string map \
        [list "*CloseUI: *PageRegion" "${pr_entry}\n*CloseUI: *PageRegion"] \
        $content]

    # ImageableArea und PaperDimension: nach letztem Eintrag
    # Suche nach *DefaultImageableArea und füge nach dem letzten *ImageableArea ein
    set content [regsub \
        {(\*ImageableArea [^\n]+\n)(\n*\*DefaultPaperDimension)} \
        $content \
        "\\1${ia_entry}\n\\2" \
        ]

    set content [regsub \
        {(\*PaperDimension [^\n]+\n)(\n*\*OpenUI)} \
        $content \
        "\\1${pd_entry}\n\\2" \
        ]

    ppd_write $ppdfile $content

    puts "Format '$name' hinzugefügt:"
    puts "  Breite:  ${w_mm} mm = ${w_pt} pt"
    puts "  Höhe:    ${h_mm} mm = ${h_pt} pt"
    puts ""
    puts "CUPS neu starten:"
    puts "  sudo systemctl restart cups"
    puts ""
    puts "Drucken:"
    puts "  lp -d $printer -o PageSize=${name} -o MediaType=Tape etikett.png"
}

# ------------------------------------------------------------------ #
# delete — Format entfernen                                           #
# ------------------------------------------------------------------ #
proc cmd_delete {printer name} {
    set ppdfile [get_ppdfile $printer]
    set content [ppd_read $ppdfile]

    if {![format_exists $content $name]} {
        puts "Format '$name' nicht gefunden."
        return
    }

    # Backup
    ppd_backup $ppdfile

    # Alle 4 Zeilen mit diesem Namen entfernen
    set lines [split $content "\n"]
    set newlines [list]
    foreach line $lines {
        if {[string match "*PageSize ${name}/*" $line] ||
            [string match "*PageRegion ${name}/*" $line] ||
            [string match "*ImageableArea ${name}/*" $line] ||
            [string match "*PaperDimension ${name}/*" $line]} {
            # Zeile überspringen
        } else {
            lappend newlines $line
        }
    }

    ppd_write $ppdfile [join $newlines "\n"]

    puts "Format '$name' gelöscht."
    puts "CUPS neu starten: sudo systemctl restart cups"
}

# ------------------------------------------------------------------ #
# Hauptprogramm                                                       #
# ------------------------------------------------------------------ #
proc usage {} {
    puts "Aufruf:"
    puts "  tclsh ppdtool.tcl list   \[drucker\]"
    puts "  tclsh ppdtool.tcl detail formatname \[drucker\]"
    puts "  tclsh ppdtool.tcl add    breite_mm hoehe_mm \[drucker\]"
    puts "  tclsh ppdtool.tcl delete formatname \[drucker\]"
    puts ""
    puts "Beispiele:"
    puts "  tclsh ppdtool.tcl list"
    puts "  tclsh ppdtool.tcl detail 54mm"
    puts "  tclsh ppdtool.tcl add 54 56"
    puts "  tclsh ppdtool.tcl add 54 90"
    puts "  tclsh ppdtool.tcl delete custom54x56"
    exit 1
}

# Root-Check
if {[catch {exec id -u} uid] || $uid != 0} {
    puts "Hinweis: PPD-Dateien in /etc/cups/ppd benötigen root."
    puts "Starte mit: sudo tclsh ppdtool.tcl ..."
    puts ""
}

if {[llength $argv] < 1} { usage }

set cmd [lindex $argv 0]

switch $cmd {
    list {
        set printer [expr {[llength $argv] > 1 ? [lindex $argv 1] : $DEFAULT_PRINTER}]
        if {[catch {cmd_list $printer} err]} {
            puts "Fehler: $err"; exit 1
        }
    }
    detail {
        if {[llength $argv] < 2} { usage }
        set name    [lindex $argv 1]
        set printer [expr {[llength $argv] > 2 ? [lindex $argv 2] : $DEFAULT_PRINTER}]
        if {[catch {cmd_detail $printer $name} err]} {
            puts "Fehler: $err"; exit 1
        }
    }
    add {
        if {[llength $argv] < 3} { usage }
        set w  [lindex $argv 1]
        set h  [lindex $argv 2]
        set printer [expr {[llength $argv] > 3 ? [lindex $argv 3] : $DEFAULT_PRINTER}]
        if {[catch {cmd_add $printer $w $h} err]} {
            puts "Fehler: $err"
            exit 1
        }
    }
    delete {
        if {[llength $argv] < 2} { usage }
        set name    [lindex $argv 1]
        set printer [expr {[llength $argv] > 2 ? [lindex $argv 2] : $DEFAULT_PRINTER}]
        if {[catch {cmd_delete $printer $name} err]} {
            puts "Fehler: $err"
            exit 1
        }
    }
    default {
        usage
    }
}
