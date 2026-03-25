# Makefile für pdfiumtcl 0.3
#
# Aufruf:
#   make                    Linux, Tcl auto-erkannt
#   make TCL_VERSION=9.0    Linux, Tcl 9
#   make PLATFORM=windows   Windows cross-compile (mingw64)
#   make check              Stub-Korrektheit prüfen
#   make install            Nach ~/lib/pdfiumtcl/ installieren
#   make clean              .so/.dll löschen

# ------------------------------------------------------------------ #
# Plattform                                                           #
# ------------------------------------------------------------------ #
PLATFORM ?= linux

ifeq ($(PLATFORM),windows)
    CC              = x86_64-w64-mingw32-gcc
    TARGET          = pdfiumtcl.dll
    PDFIUM_LIB_FILE = pdfium.dll
    OS_CFLAGS       = -DWIN32
    OS_LFLAGS       = -lws2_32
else
    CC              = gcc
    TARGET          = pdfiumtcl.so
    PDFIUM_LIB_FILE = libpdfium.so
    OS_CFLAGS       =
    OS_LFLAGS       =
endif

# ------------------------------------------------------------------ #
# PDFium: vendor/ bevorzugt, Fallback /opt/pdfium                    #
# ------------------------------------------------------------------ #
VENDOR_PDFIUM = vendor/pdfium

ifneq ($(wildcard $(VENDOR_PDFIUM)/lib/$(PDFIUM_LIB_FILE)),)
    PDFIUM_DIR = $(VENDOR_PDFIUM)
    RPATH_FLAG = -Wl,-rpath,'$$ORIGIN/vendor/pdfium/lib'
else
    PDFIUM_DIR = /opt/pdfium
    RPATH_FLAG = -Wl,-rpath,'$$ORIGIN'
endif

PDFIUM_INC = $(PDFIUM_DIR)/include
PDFIUM_LIB = $(PDFIUM_DIR)/lib

# ------------------------------------------------------------------ #
# Tcl/Tk Version (default 8.6, überschreibbar)                       #
# ------------------------------------------------------------------ #
TCL_VERSION ?= 8.6

ifeq ($(TCL_VERSION),9.0)
    TCL_STUB_NAME  = tclstub9.0
    TK_STUB_NAME   = tkstub9.0
    TCL_INC_SUFFIX = tcl9.0
else ifeq ($(TCL_VERSION),8.5)
    TCL_STUB_NAME  = tclstub8.5
    TK_STUB_NAME   = tkstub8.5
    TCL_INC_SUFFIX = tcl8.5
else
    TCL_STUB_NAME  = tclstub8.6
    TK_STUB_NAME   = tkstub8.6
    TCL_INC_SUFFIX = tcl8.6
endif

TCL_INC     := $(shell pkg-config --cflags tcl 2>/dev/null \
                || echo -I/usr/include/$(TCL_INC_SUFFIX))
TK_INC      := $(shell pkg-config --cflags tk 2>/dev/null \
                || echo -I/usr/include/$(TCL_INC_SUFFIX))

TCL_STUBLIB := $(firstword $(wildcard \
    $(shell pkg-config --variable=libdir tcl 2>/dev/null)/lib$(TCL_STUB_NAME).a \
    /usr/lib/x86_64-linux-gnu/lib$(TCL_STUB_NAME).a \
    /usr/lib/lib$(TCL_STUB_NAME).a))

TK_STUBLIB  := $(firstword $(wildcard \
    $(shell pkg-config --variable=libdir tk 2>/dev/null)/lib$(TK_STUB_NAME).a \
    /usr/lib/x86_64-linux-gnu/lib$(TK_STUB_NAME).a \
    /usr/lib/lib$(TK_STUB_NAME).a))

# ------------------------------------------------------------------ #
# Compiler-Flags                                                      #
# ------------------------------------------------------------------ #
CFLAGS = -shared -fPIC -O2 -Wall \
         -I$(PDFIUM_INC) \
         $(TCL_INC) $(TK_INC) \
         -DUSE_TCL_STUBS -DUSE_TK_STUBS \
         $(OS_CFLAGS)

LDFLAGS = -L$(PDFIUM_LIB) \
          $(RPATH_FLAG) \
          -lpdfium \
          $(TCL_STUBLIB) $(TK_STUBLIB) \
          $(OS_LFLAGS)

SRC = src/pdfiumtcl.c

# ------------------------------------------------------------------ #
# Targets                                                             #
# ------------------------------------------------------------------ #
all: $(TARGET)

$(TARGET): $(SRC)
	@echo "PLATFORM    = $(PLATFORM)"
	@echo "TCL_VERSION = $(TCL_VERSION)"
	@echo "PDFIUM_DIR  = $(PDFIUM_DIR)"
	@echo "TCL_STUBLIB = $(TCL_STUBLIB)"
	@echo "TK_STUBLIB  = $(TK_STUBLIB)"
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "OK: $(TARGET) erzeugt"

check: $(TARGET)
	@echo "--- ldd-Prüfung (libtcl/libtk darf nicht erscheinen) ---"
	@ldd $(TARGET) | grep -E 'libtcl|libtk' && \
	  (echo "FEHLER: libtcl oder libtk direkt gelinkt"; exit 1) || \
	  echo "OK: kein direkter libtcl/libtk Link — Stubs korrekt"
	@echo "--- Exportiertes Init-Symbol ---"
	@nm -D $(TARGET) | grep -i init

install: $(TARGET)
	mkdir -p $(HOME)/lib/pdfiumtcl
	cp $(TARGET) pkgIndex.tcl $(HOME)/lib/pdfiumtcl/
	@echo "OK: installiert nach $(HOME)/lib/pdfiumtcl/"

clean:
	rm -f pdfiumtcl.so pdfiumtcl.dll

.PHONY: all check install clean
