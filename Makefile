# Makefile fuer pdfiumtcl 0.5.2
#
# Paketstruktur (analog zu sha-2.2.0):
#   tclpdfium0.5.2/
#     pkgIndex.tcl
#     linux64/          pdfiumtcl.so + libpdfium.so
#     linux64-tcl9/     pdfiumtcl.so + libpdfium.so
#     windows64/        pdfiumtcl.dll + pdfium.dll
#     windows64-tcl9/   pdfiumtcl.dll + pdfium.dll
#
# Aufruf:
#   make                      Linux, Tcl 8.6
#   make TCL_VERSION=9.0      Linux, Tcl 9
#   make PLATFORM=windows     Windows cross-compile (mingw64)
#   make install              -> ~/lib/tcltk/tclpdfium0.5.2/linux64/
#   make install90            -> ~/lib/tcltk/tclpdfium0.5.2/linux64-tcl9/
#   make install-windows      -> WIN_INSTALL_DIR/windows64/
#   make install-pdfium       libpdfium.so/pdfium.dll ins Paketverzeichnis
#   make both                 Tcl 8.6 + Tcl 9 bauen
#   make check                Stub-Korrektheit pruefen
#   make clean                Alle .so/.dll loeschen

# ------------------------------------------------------------------ #
# Plattform                                                           #
# ------------------------------------------------------------------ #
# MSYS2 sets MSYSTEM_PREFIX to /ucrt64 or /mingw64 (empty when cross-compiling
# from Linux). Used to auto-select the platform and locate Tcl/Tk headers/stubs.
MSYS2_PREFIX := $(MSYSTEM_PREFIX)

ifeq ($(MSYS2_PREFIX),)
    PLATFORM ?= linux
else
    PLATFORM ?= windows
endif

WIN_TCL_ROOT ?= C:/Tcl

ifeq ($(PLATFORM),windows)
    ifeq ($(MSYS2_PREFIX),)
        CC          = x86_64-w64-mingw32-gcc
    else
        CC          = gcc
    endif
    PDFIUM_LIB_FILE = pdfium.dll
    OS_CFLAGS       = -DWIN32 -D_WIN32 -DWIN32_LEAN_AND_MEAN
    # Self-contained DLL: statically link the MinGW runtime so it does not
    # depend on libgcc_s_seh-1.dll / libwinpthread-1.dll being on PATH
    # (those live in the MSYS2 bin dir and are missing outside that shell).
    OS_LFLAGS       = -lws2_32 -static-libgcc \
                      -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic \
                      -Wl,--export-all-symbols
    BINARY_NAME     = pdfiumtcl.dll
    ifeq ($(TCL_VERSION),9.0)
        SUBDIR = windows64-tcl9
    else
        SUBDIR = windows64
    endif
else
    CC              = gcc
    PDFIUM_LIB_FILE = libpdfium.so
    OS_CFLAGS       =
    OS_LFLAGS       =
    BINARY_NAME     = pdfiumtcl.so
    ifeq ($(TCL_VERSION),9.0)
        SUBDIR = linux64-tcl9
    else
        SUBDIR = linux64
    endif
endif

# TARGET ist immer pdfiumtcl.so / pdfiumtcl.dll (kein "90" im Namen)
TARGET = $(BINARY_NAME)

# ------------------------------------------------------------------ #
# PDFium                                                              #
#                                                                     #
# Plattformspezifisches vendor-Verzeichnis zuerst. Das ist noetig,    #
# damit beim Cross-Compile das Linux- und das Windows-PDFium im       #
# selben Baum nebeneinander liegen koennen -- ein gemeinsames         #
# vendor/pdfium wuerde sich sonst gegenseitig ueberschreiben.         #
# vendor/pdfium bleibt als Fallback erhalten (aeltere Bauemume).      #
# ------------------------------------------------------------------ #
ifeq ($(PLATFORM),windows)
    PDFIUM_VENDOR = vendor/pdfium-win-x64
else
    PDFIUM_VENDOR = vendor/pdfium-linux-x64
endif

PDFIUM_SEARCH = $(PDFIUM_VENDOR) vendor/pdfium /opt/pdfium
PDFIUM_DIR := $(firstword $(foreach d,$(PDFIUM_SEARCH),\
    $(if $(or $(wildcard $(d)/lib/$(PDFIUM_LIB_FILE)),\
              $(wildcard $(d)/bin/$(PDFIUM_LIB_FILE))),$(d))))

ifeq ($(PDFIUM_DIR),)
    PDFIUM_DIR = /opt/pdfium
endif

# Die dist-Ziele laufen im aeusseren Make, wo PLATFORM noch "linux" ist --
# PDFIUM_DIR zeigt dort auf den Linux-Baum. Fuer das Einsammeln der
# Windows-DLL brauchen wir den Windows-Baum, unabhaengig von PLATFORM.
PDFIUM_WIN_DIR := $(firstword $(foreach d,vendor/pdfium-win-x64 vendor/pdfium,\
    $(if $(wildcard $(d)/bin/pdfium.dll),$(d))))

# RPATH zeigt auf $ORIGIN — PDFium-Library liegt im gleichen Subdir
ifeq ($(PLATFORM),windows)
    RPATH_FLAG =
else
    RPATH_FLAG = -Wl,-rpath,'$$ORIGIN'
endif

PDFIUM_INC = $(PDFIUM_DIR)/include
PDFIUM_LIB = $(PDFIUM_DIR)/lib

# ------------------------------------------------------------------ #
# Tcl/Tk Version (default 8.6, ueberschreibbar)                      #
# ------------------------------------------------------------------ #
TCL_VERSION ?= 8.6

ifeq ($(TCL_VERSION),9.0)
    TCL_STUB_NAME  = tclstub9.0
    TK_STUB_NAME   = tkstub9.0
    ifeq ($(PLATFORM),windows)
        TCL_INC    := -I$(WIN_TCL_ROOT)/include -I$(MSYS2_PREFIX)/include
        TK_INC     := -I$(WIN_TCL_ROOT)/include -I$(MSYS2_PREFIX)/include
        TCL_STUBLIB := $(firstword $(wildcard \
            $(WIN_TCL_ROOT)/lib/libtclstub.a \
            $(WIN_TCL_ROOT)/lib/tclstub.lib \
            $(WIN_TCL_ROOT)/lib/libtclstub90.a \
            $(WIN_TCL_ROOT)/lib/tclstub90.lib \
            $(MSYS2_PREFIX)/lib/libtclstub9.0.a \
            /mingw64/lib/libtclstub9.0.a))
        TK_STUBLIB  := $(firstword $(wildcard \
            $(WIN_TCL_ROOT)/lib/libtkstub.a \
            $(WIN_TCL_ROOT)/lib/tkstub.lib \
            $(WIN_TCL_ROOT)/lib/libtkstub90.a \
            $(WIN_TCL_ROOT)/lib/tkstub90.lib \
            $(MSYS2_PREFIX)/lib/libtkstub9.0.a \
            /mingw64/lib/libtkstub9.0.a))
    else
        TCL_INC    := -I/usr/include/tcl9.0
        TK_INC     := -I/usr/include/tcl9.0
        TCL_STUBLIB := $(firstword $(wildcard \
            /usr/lib/x86_64-linux-gnu/lib$(TCL_STUB_NAME).a \
            /usr/lib/lib$(TCL_STUB_NAME).a \
            $(HOME)/lib/tcl9.0/lib$(TCL_STUB_NAME).a))
        TK_STUBLIB  := $(firstword $(wildcard \
            /usr/lib/x86_64-linux-gnu/lib$(TK_STUB_NAME).a \
            /usr/lib/lib$(TK_STUB_NAME).a))
    endif
else ifeq ($(TCL_VERSION),8.5)
    TCL_STUB_NAME  = tclstub8.5
    TK_STUB_NAME   = tkstub8.5
    ifeq ($(PLATFORM),windows)
        TCL_INC    := -I$(WIN_TCL_ROOT)/include
        TK_INC     := -I$(WIN_TCL_ROOT)/include
        TCL_STUBLIB := $(firstword $(wildcard \
            $(WIN_TCL_ROOT)/lib/libtclstub85.a \
            /mingw64/lib/libtclstub8.5.a))
        TK_STUBLIB  := $(firstword $(wildcard \
            $(WIN_TCL_ROOT)/lib/libtkstub85.a \
            /mingw64/lib/libtkstub8.5.a))
    else
        TCL_INC    := $(shell pkg-config --cflags tcl 2>/dev/null || echo -I/usr/include/tcl8.5)
        TK_INC     := $(shell pkg-config --cflags tk  2>/dev/null || echo -I/usr/include/tcl8.5)
        TCL_STUBLIB := $(firstword $(wildcard \
            /usr/lib/x86_64-linux-gnu/lib$(TCL_STUB_NAME).a \
            /usr/lib/lib$(TCL_STUB_NAME).a))
        TK_STUBLIB  := $(firstword $(wildcard \
            /usr/lib/x86_64-linux-gnu/lib$(TK_STUB_NAME).a \
            /usr/lib/lib$(TK_STUB_NAME).a))
    endif
else
    # 8.6 Standard
    TCL_STUB_NAME  = tclstub8.6
    TK_STUB_NAME   = tkstub8.6
    ifeq ($(PLATFORM),windows)
        TCL_INC    := -I$(WIN_TCL_ROOT)/include -I$(MSYS2_PREFIX)/include
        TK_INC     := -I$(WIN_TCL_ROOT)/include -I$(MSYS2_PREFIX)/include
        TCL_STUBLIB := $(firstword $(wildcard \
            $(WIN_TCL_ROOT)/lib/libtclstub86.a \
            $(WIN_TCL_ROOT)/lib/tclstub86.lib \
            $(MSYS2_PREFIX)/lib/libtclstub8.6.a \
            /mingw64/lib/libtclstub8.6.a))
        TK_STUBLIB  := $(firstword $(wildcard \
            $(WIN_TCL_ROOT)/lib/libtkstub86.a \
            $(WIN_TCL_ROOT)/lib/tkstub86.lib \
            $(MSYS2_PREFIX)/lib/libtkstub8.6.a \
            /mingw64/lib/libtkstub8.6.a))
    else
        TCL_INC    := $(shell pkg-config --cflags tcl 2>/dev/null || echo -I/usr/include/tcl8.6)
        TK_INC     := $(shell pkg-config --cflags tk  2>/dev/null || echo -I/usr/include/tcl8.6)
        TCL_STUBLIB := $(firstword $(wildcard \
            $(shell pkg-config --variable=libdir tcl 2>/dev/null)/lib$(TCL_STUB_NAME).a \
            /usr/lib/x86_64-linux-gnu/lib$(TCL_STUB_NAME).a \
            /usr/lib/lib$(TCL_STUB_NAME).a))
        TK_STUBLIB  := $(firstword $(wildcard \
            $(shell pkg-config --variable=libdir tk 2>/dev/null)/lib$(TK_STUB_NAME).a \
            /usr/lib/x86_64-linux-gnu/lib$(TK_STUB_NAME).a \
            /usr/lib/lib$(TK_STUB_NAME).a))
    endif
endif

# ------------------------------------------------------------------ #
# Compiler-Flags                                                      #
# ------------------------------------------------------------------ #
ifeq ($(PLATFORM),windows)
    SHARED_FLAG = -shared
else
    SHARED_FLAG = -shared -fPIC
endif

CFLAGS = $(SHARED_FLAG) -O2 -Wall \
         -I$(PDFIUM_INC) \
         $(TCL_INC) $(TK_INC) \
         -DUSE_TCL_STUBS -DUSE_TK_STUBS \
         $(OS_CFLAGS)

ifeq ($(PLATFORM),windows)
    PDFIUM_LINK = $(PDFIUM_LIB)/pdfium.dll.lib
    LDFLAGS = -L$(PDFIUM_LIB) \
              $(RPATH_FLAG) \
              $(PDFIUM_LINK) \
              $(TCL_STUBLIB) $(TK_STUBLIB) \
              $(OS_LFLAGS)
else
    LDFLAGS = -L$(PDFIUM_LIB) \
              $(RPATH_FLAG) \
              -lpdfium \
              $(TCL_STUBLIB) $(TK_STUBLIB) \
              $(OS_LFLAGS)
endif

SRC = src/pdfiumtcl.c

PKGVERSION   = 0.5.2
PKGNAME      = tclpdfium$(PKGVERSION)
# Gemeinsames Installverzeichnis fuer alle Plattformen:
INSTALL_BASE = $(CURDIR)/out/$(PKGNAME)
INSTALL_DIR  = $(INSTALL_BASE)/$(SUBDIR)

# Windows
WIN_INSTALL_DIR ?= $(WIN_TCL_ROOT)/lib/$(PKGNAME)

# ------------------------------------------------------------------ #
# Targets                                                             #
# ------------------------------------------------------------------ #
all: $(TARGET)

$(TARGET): $(SRC)
	@echo "PLATFORM    = $(PLATFORM)"
	@echo "TCL_VERSION = $(TCL_VERSION)"
	@echo "TARGET      = $(TARGET)"
	@echo "SUBDIR      = $(SUBDIR)"
	@echo "PDFIUM_DIR  = $(PDFIUM_DIR)"
	@echo "TCL_INC     = $(TCL_INC)"
	@echo "TCL_STUBLIB = $(TCL_STUBLIB)"
	@echo "TK_STUBLIB  = $(TK_STUBLIB)"
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "OK: $(TARGET) erzeugt"

all86:
	$(MAKE) TCL_VERSION=8.6

all90:
	$(MAKE) TCL_VERSION=9.0

windows:
	$(MAKE) PLATFORM=windows

windows90:
	$(MAKE) PLATFORM=windows TCL_VERSION=9.0

# Nativ auf Windows mit BAWT 3.2 bauen
# Aufruf in cmd.exe:
#   set GCCBIN=C:\Bawt\Bawt86\Tools\gcc14.2.0_x86_64-w64-mingw32\mingw64\bin
#   set PATH=%GCCBIN%;%PATH%
#   mingw32-make windows-bawt
BAWT86  = C:/Bawt/Bawt86/Windows/x64/Development/opt/Tcl
BAWT903 = C:/Bawt/Bawt903/Windows/x64/Development/opt/Tcl

windows-bawt:
	mingw32-make PLATFORM=windows \
	    WIN_TCL_ROOT=$(BAWT86)

windows-bawt90:
	mingw32-make PLATFORM=windows TCL_VERSION=9.0 \
	    WIN_TCL_ROOT=$(BAWT903) \
	    TCL_STUBLIB=$(BAWT903)/lib/libtclstub.a \
	    TK_STUBLIB=$(BAWT903)/lib/libtkstub.a

# Cross-Compile auf Linux: Stub-Libraries aus /usr/x86_64-w64-mingw32/
CROSS_INC     = -I/usr/x86_64-w64-mingw32/include
CROSS_STUB86  = /usr/x86_64-w64-mingw32/lib/libtclstub86.a
CROSS_TKSTUB86 = /usr/x86_64-w64-mingw32/lib/libtkstub86.a
# Tcl-9-Header fuer Windows: von C:\Tcl90\include auf Linux kopieren nach:
# /usr/x86_64-w64-mingw32/include/tcl9-win/
CROSS_INC90   = -I/usr/x86_64-w64-mingw32/include/tcl9-win -I/usr/x86_64-w64-mingw32/include
CROSS_STUB90   = $(firstword $(wildcard \
    /usr/x86_64-w64-mingw32/lib/libtclstub9.0.a \
    /usr/x86_64-w64-mingw32/lib/libtclstub90.a \
    /usr/x86_64-w64-mingw32/lib/libtclstub.a))
CROSS_TKSTUB90 = $(firstword $(wildcard \
    /usr/x86_64-w64-mingw32/lib/libtkstub9.0.a \
    /usr/x86_64-w64-mingw32/lib/libtkstub90.a \
    /usr/x86_64-w64-mingw32/lib/libtkstub.a))

windows-cross:
	$(MAKE) PLATFORM=windows \
	    TCL_INC="$(CROSS_INC)" \
	    TCL_STUBLIB=$(CROSS_STUB86) \
	    TK_STUBLIB=$(CROSS_TKSTUB86)

windows-cross90:
	$(MAKE) PLATFORM=windows TCL_VERSION=9.0 \
	    TCL_INC="$(CROSS_INC90)" \
	    TK_INC="$(CROSS_INC90)" \
	    TCL_STUBLIB=$(CROSS_STUB90) \
	    TK_STUBLIB=$(CROSS_TKSTUB90)

dist-windows:
	$(MAKE) windows-cross
	mkdir -p dist-win/windows64
	cp pdfiumtcl.dll dist-win/windows64/
	cp pkgIndex.tcl  dist-win/
	@if [ -f $(PDFIUM_WIN_DIR)/bin/pdfium.dll ]; then \
	    cp $(PDFIUM_WIN_DIR)/bin/pdfium.dll dist-win/windows64/; \
	    echo "OK: dist-win/windows64/ befuellt"; \
	    echo "    pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl"; \
	else \
	    echo "WARNUNG: pdfium.dll nicht gefunden (vendor/pdfium-win-x64/bin)"; \
	    echo "         PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh"; \
	fi

# Windows + Tcl 9 via cross-compile.
#
# This used to bail out with "not possible: MSYS2 has no mingw-w64-x86_64-tcl9
# package". That was a wrong diagnosis. A stub library is not extracted from a
# DLL -- it is generic/tclStubLib.c compiled from the Tcl source tree, roughly
# 160 lines of C. tools/make-win-stubs.sh builds it, and Tk's, in seconds.
# No MSYS2 package and no Windows machine are involved.
WIN_STUBS ?= $(CURDIR)/win-stubs

$(WIN_STUBS)/lib/libtclstub.a:
	./tools/make-win-stubs.sh core-9-0-2 $(WIN_STUBS)

win-stubs: $(WIN_STUBS)/lib/libtclstub.a

windows-cross90-stubs: $(WIN_STUBS)/lib/libtclstub.a
	$(MAKE) PLATFORM=windows TCL_VERSION=9.0 \
	    TCL_INC="-I$(WIN_STUBS)/include" \
	    TK_INC="" \
	    TCL_STUBLIB=$(WIN_STUBS)/lib/libtclstub.a \
	    TK_STUBLIB=$(WIN_STUBS)/lib/libtkstub.a

dist-windows90: windows-cross90-stubs
	mkdir -p dist-win/windows64-tcl9
	cp pdfiumtcl.dll dist-win/windows64-tcl9/
	cp pkgIndex.tcl  dist-win/
	@if [ -f $(PDFIUM_WIN_DIR)/bin/pdfium.dll ]; then \
	    cp $(PDFIUM_WIN_DIR)/bin/pdfium.dll dist-win/windows64-tcl9/; \
	    echo "OK: dist-win/windows64-tcl9/ befuellt"; \
	else \
	    echo "WARNUNG: pdfium.dll nicht gefunden (vendor/pdfium-win-x64/bin)"; \
	    echo "         PDFIUM_PLATFORM=win-x64 bash scripts/setup.sh"; \
	fi

check: $(TARGET)
	@echo "--- ldd-Pruefung (libtcl/libtk darf nicht erscheinen) ---"
	@ldd $(TARGET) | grep -E 'libtcl|libtk' && \
	  (echo "FEHLER: libtcl oder libtk direkt gelinkt"; exit 1) || \
	  echo "OK: kein direkter libtcl/libtk Link"
	@echo "--- Exportiertes Init-Symbol ---"
	@nm -D $(TARGET) | grep -i init

# Binary + pkgIndex + PDFium-Library installieren
install: $(TARGET)
	mkdir -p $(INSTALL_DIR)
	cp $(TARGET) $(INSTALL_DIR)/
	cp pkgIndex.tcl $(INSTALL_BASE)/
	@echo "OK: $(INSTALL_DIR)/$(TARGET)"
	$(MAKE) install-pdfium

install90:
	$(MAKE) TCL_VERSION=9.0 install
	$(MAKE) TCL_VERSION=9.0 install-pdfium

# Build + install both Tcl 8.6 and Tcl 9.0 in one go (clean between).
both:
	$(MAKE) clean
	$(MAKE) install
	$(MAKE) clean
	$(MAKE) install90

# PDFium-Library ins Subdir kopieren (nach make install ausfuehren)
install-pdfium:
	@if [ -f $(PDFIUM_DIR)/lib/libpdfium.so ]; then \
	    cp $(PDFIUM_DIR)/lib/libpdfium.so $(INSTALL_DIR)/; \
	    echo "OK: libpdfium.so -> $(INSTALL_DIR)/"; \
	elif [ -f $(PDFIUM_DIR)/bin/pdfium.dll ]; then \
	    cp $(PDFIUM_DIR)/bin/pdfium.dll $(INSTALL_DIR)/; \
	    echo "OK: pdfium.dll -> $(INSTALL_DIR)/"; \
	else \
	    echo "FEHLER: PDFium-Library nicht gefunden (setup.sh ausfuehren)"; \
	    exit 1; \
	fi

# Windows
install-windows: $(TARGET)
	mkdir -p "$(WIN_INSTALL_DIR)/$(SUBDIR)"
	cp $(TARGET) "$(WIN_INSTALL_DIR)/$(SUBDIR)/"
	cp pkgIndex.tcl "$(WIN_INSTALL_DIR)/"
	@if [ -f "$(PDFIUM_DIR)/bin/pdfium.dll" ]; then \
	    cp "$(PDFIUM_DIR)/bin/pdfium.dll" "$(WIN_INSTALL_DIR)/$(SUBDIR)/"; \
	    echo "OK: pdfium.dll -> $(WIN_INSTALL_DIR)/$(SUBDIR)/"; \
	elif [ -f "$(PDFIUM_DIR)/lib/pdfium.dll" ]; then \
	    cp "$(PDFIUM_DIR)/lib/pdfium.dll" "$(WIN_INSTALL_DIR)/$(SUBDIR)/"; \
	    echo "OK: pdfium.dll -> $(WIN_INSTALL_DIR)/$(SUBDIR)/"; \
	else \
	    echo "WARNUNG: pdfium.dll nicht gefunden unter $(PDFIUM_DIR)/bin oder lib"; \
	    echo "         -> setup.sh ausfuehren und pdfium.dll manuell kopieren."; \
	fi
	@echo "OK: $(WIN_INSTALL_DIR)/$(SUBDIR)/$(TARGET)"

clean:
	rm -f pdfiumtcl.so pdfiumtcl.dll

.PHONY: all all86 all90 both windows windows90 windows-bawt windows-bawt90 \
        windows-cross windows-cross90 windows-cross90-stubs win-stubs \
        dist-windows dist-windows90 \
        check install install90 install-pdfium install-windows clean
