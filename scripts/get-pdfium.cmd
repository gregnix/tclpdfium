@echo off
rem ---------------------------------------------------------------------------
rem get-pdfium.cmd -- PDFium herunterladen und in vendor\ einrichten.
rem
rem Das Gegenstueck zu scripts/setup.sh, fuer cmd.exe. Kein bash, kein
rem PowerShell: curl.exe und tar.exe liegen seit Windows 10 (1803) im System.
rem
rem Aufruf:
rem   scripts\get-pdfium.cmd              -> vendor\pdfium-win-x64  (Default)
rem   scripts\get-pdfium.cmd win-arm64    -> vendor\pdfium-win-arm64
rem ---------------------------------------------------------------------------
setlocal

set "PLATFORM=%~1"
if "%PLATFORM%"=="" set "PLATFORM=win-x64"

rem %~dp0 endet mit einem Backslash; ".." dahinter ergaebe Pfade wie
rem "...\scripts\..\vendor\...". Ueber ein pushd/popd wird der Pfad aufgeloest.
pushd "%~dp0.."
set "ROOT=%CD%"
popd
set "VENDOR=%ROOT%\vendor\pdfium-%PLATFORM%"
set "ARCHIVE=pdfium-%PLATFORM%.tgz"
set "URL=https://github.com/bblanchon/pdfium-binaries/releases/latest/download/%ARCHIVE%"
set "TMP_TGZ=%TEMP%\%ARCHIVE%"

echo ==^> pdfiumtcl setup
echo     Plattform: %PLATFORM%
echo     Ziel:      %VENDOR%
echo.

if exist "%VENDOR%\bin\pdfium.dll" (
    echo OK: PDFium bereits vorhanden -- ueberspringe Download.
    echo     Zum Neuinstallieren: rmdir /S /Q "%VENDOR%"
    exit /b 0
)

where curl.exe >nul 2>&1 || (
    echo FEHLER: curl.exe nicht gefunden.
    echo         Ab Windows 10 1803 im System enthalten. Aeltere Systeme:
    echo         Archiv von Hand laden und nach %VENDOR% entpacken:
    echo         %URL%
    exit /b 1
)
where tar.exe >nul 2>&1 || (
    echo FEHLER: tar.exe nicht gefunden ^(ab Windows 10 1803 im System^).
    exit /b 1
)

echo ==^> Lade PDFium herunter...
echo     %URL%
curl -L --fail --progress-bar -o "%TMP_TGZ%" "%URL%" || (
    echo FEHLER: Download fehlgeschlagen.
    exit /b 1
)

echo ==^> Entpacke nach %VENDOR% ...
if not exist "%VENDOR%" mkdir "%VENDOR%"
tar -xzf "%TMP_TGZ%" -C "%VENDOR%" || (
    echo FEHLER: Entpacken fehlgeschlagen.
    exit /b 1
)
del "%TMP_TGZ%"

if not exist "%VENDOR%\bin\pdfium.dll" (
    echo FEHLER: bin\pdfium.dll fehlt nach dem Entpacken.
    exit /b 1
)
if not exist "%VENDOR%\lib\pdfium.dll.lib" (
    echo WARNUNG: lib\pdfium.dll.lib fehlt -- zum Linken erforderlich.
)

echo.
echo OK: PDFium eingerichtet.
echo     %VENDOR%\bin\pdfium.dll        ^(Laufzeit, gehoert neben pdfiumtcl.dll^)
echo     %VENDOR%\lib\pdfium.dll.lib    ^(Importbibliothek, zum Linken^)
echo     %VENDOR%\include\
echo.
echo ==^> Bauen:
echo     mingw32-make windows-bawt       rem Tcl 8.6, BAWT
echo     mingw32-make windows-bawt90     rem Tcl 9.0, BAWT
echo.
echo     Hinweis: pdfium.dll muss neben pdfiumtcl.dll liegen.
endlocal
