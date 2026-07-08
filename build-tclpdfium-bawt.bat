@echo off
REM ===========================================================================
REM build-tclpdfium-bawt.bat
REM Build pdfiumtcl.dll for Tcl 8.6 AND Tcl 9.0 (Windows x64) using BAWT's
REM MinGW gcc and BAWT's Tcl/Tk stub libraries. No 'make' required -- this
REM calls gcc directly with the exact flags taken from the project Makefile.
REM
REM Result per version (dropped straight into your project layout):
REM   <PROJ>\libs\windows-tcl9.0\tclpdfium\  pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl
REM   <PROJ>\libs\windows-tcl8.6\tclpdfium\  pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl
REM ===========================================================================

setlocal

REM ===================== CONFIG -- edit these 6 lines =========================
REM Full path to BAWT's MinGW gcc.exe (from gcc14.2.0_x86_64-w64-mingw32.7z,
REM after you extract it somewhere and point here at its bin\gcc.exe):
set "GCC=C:\Bawt\Bawt903\Tools\gcc14.2.0_x86_64-w64-mingw32\mingw64\bin\gcc.exe"

REM tclpdfium C source and your PDFium SDK (include\ lib\ bin\):
set "SRC=C:\proj\app01\tclpdfium\src\pdfiumtcl.c"
set "PDFIUM=C:\proj\app01\tclpdfium\vendor\pdfium"

REM Your project root (the one that contains libs\ and runtimes\):
set "PROJ=C:\proj\app01"

REM The two BAWT trees (each the ...\Development\opt\tcl folder):
set "TCL86=C:\Bawt\Bawt86\Windows\x64\Development\opt\tcl"
set "TCL90=C:\Bawt\Bawt903\Windows\x64\Development\opt\tcl"
REM ===========================================================================

if not exist "%GCC%"  ( echo ERROR: gcc not found: %GCC%  & echo Point GCC at BAWT's MinGW gcc.exe. & goto :end )
if not exist "%SRC%"  ( echo ERROR: source not found: %SRC% & goto :end )
if not exist "%PDFIUM%\lib\pdfium.dll.lib" ( echo ERROR: import lib missing: %PDFIUM%\lib\pdfium.dll.lib & goto :end )
if not exist "%PDFIUM%\bin\pdfium.dll"     ( echo ERROR: engine missing:     %PDFIUM%\bin\pdfium.dll     & goto :end )

REM combo            tcl-tree   tcl-stub          tk-stub
call :build "windows-tcl9.0" "%TCL90%" "libtclstub.a"   "libtkstub.a"
call :build "windows-tcl8.6" "%TCL86%" "libtclstub86.a" "libtkstub86.a"

echo.
echo All done.
goto :end

REM ---------------------------------------------------------------------------
:build
REM %~1=combo  %~2=tcl-tree  %~3=tcl-stub-file  %~4=tk-stub-file
set "COMBO=%~1"
set "TCLROOT=%~2"
set "TCLSTUB=%~2\lib\%~3"
set "TKSTUB=%~2\lib\%~4"
set "OUT=%PROJ%\libs\%COMBO%\tclpdfium"
echo.
echo === %COMBO% ===
if not exist "%TCLROOT%\include\tcl.h" ( echo SKIP: %TCLROOT%\include\tcl.h missing & goto :eof )
if not exist "%TCLSTUB%"               ( echo SKIP: %TCLSTUB% missing & goto :eof )
if not exist "%TKSTUB%"                ( echo SKIP: %TKSTUB% missing & goto :eof )
if not exist "%OUT%" mkdir "%OUT%"

"%GCC%" -shared -O2 -Wall -I"%PDFIUM%\include" -I"%TCLROOT%\include" -DUSE_TCL_STUBS -DUSE_TK_STUBS -DWIN32 -D_WIN32 -DWIN32_LEAN_AND_MEAN -o "%OUT%\pdfiumtcl.dll" "%SRC%" -L"%PDFIUM%\lib" "%PDFIUM%\lib\pdfium.dll.lib" "%TCLSTUB%" "%TKSTUB%" -lws2_32 -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -Wl,--export-all-symbols
if errorlevel 1 ( echo *** BUILD FAILED for %COMBO% *** & goto :eof )

copy /Y "%PDFIUM%\bin\pdfium.dll" "%OUT%\pdfium.dll" >nul
(echo package ifneeded pdfiumtcl 0.5 [list load [file join $dir pdfiumtcl.dll]])>"%OUT%\pkgIndex.tcl"
echo OK: %OUT%
echo     pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl
goto :eof

:end
endlocal
