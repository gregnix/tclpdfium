#!/bin/bash
# install-brother-ql820nwb.sh
# Installiert den Brother QL-820NWB CUPS-Filter und Hilfsprogramme
#
# Aufruf: sudo bash install-brother-ql820nwb.sh

set -e

SRC="/tmp/brother_drv/ql820nwbpdrv-3.1.5-0.i386"

if [ ! -d "$SRC" ]; then
    echo "FEHLER: $SRC nicht gefunden."
    echo "Zuerst entpacken:"
    echo "  cd /tmp && mkdir -p brother_drv && cd brother_drv"
    echo "  unzip ql820nwbpdrv-3_1_5-0_i386_20260325_1115.zip"
    exit 1
fi

BASE="$SRC/opt/brother/PTouch/ql820nwb"

echo "==> Installiere Brother QL-820NWB Treiber..."

# 1. CUPS-Filter
echo "  -> /usr/lib/cups/filter/brother_lpdwrapper_ql820nwb"
cp "$BASE/cupswrapper/brother_lpdwrapper_ql820nwb" \
   /usr/lib/cups/filter/brother_lpdwrapper_ql820nwb
chmod 755 /usr/lib/cups/filter/brother_lpdwrapper_ql820nwb

# 2. Treiber-Verzeichnis
echo "  -> /opt/brother/PTouch/ql820nwb/"
mkdir -p /opt/brother/PTouch/ql820nwb/cupswrapper
mkdir -p /opt/brother/PTouch/ql820nwb/inf
mkdir -p /opt/brother/PTouch/ql820nwb/lpd/x86_64

cp "$BASE/cupswrapper/"* /opt/brother/PTouch/ql820nwb/cupswrapper/
cp "$BASE/inf/"*         /opt/brother/PTouch/ql820nwb/inf/
cp "$BASE/lpd/filter_ql820nwb" \
   /opt/brother/PTouch/ql820nwb/lpd/
cp "$BASE/lpd/x86_64/"* /opt/brother/PTouch/ql820nwb/lpd/x86_64/

chmod 755 /opt/brother/PTouch/ql820nwb/cupswrapper/*
chmod 755 /opt/brother/PTouch/ql820nwb/lpd/filter_ql820nwb
chmod 755 /opt/brother/PTouch/ql820nwb/lpd/x86_64/*

# 3. brpapertoollpr symlink
echo "  -> /usr/bin/brpapertoollpr_ql820nwb"
ln -sf /opt/brother/PTouch/ql820nwb/lpd/x86_64/brpapertoollpr_ql820nwb \
       /usr/bin/brpapertoollpr_ql820nwb

# 4. CUPS neu starten
echo "==> CUPS neu starten..."
systemctl restart cups
sleep 2

echo "==> Prüfe Filter..."
ls -la /usr/lib/cups/filter/brother_lpdwrapper_ql820nwb

echo ""
echo "==> Fertig! Jetzt testen:"
echo "  lp -d Brother_QL-820NWB_IPP -o PageSize=54mm -o MediaType=Tape etikett.png"
echo ""
echo "==> brpapertoollpr verfügbar:"
echo "  brpapertoollpr_ql820nwb -P Brother_QL-820NWB_IPP -n custom54x56 -w 54 -h 56"
