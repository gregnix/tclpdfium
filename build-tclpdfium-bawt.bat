@echo off
REM ===========================================================================
REM build-tclpdfium-bawt.bat
REM
REM Build pdfiumtcl.dll for Tcl 8.6 AND Tcl 9.0 (Windows x64) using BAWT's
REM MinGW gcc and BAWT's Tcl/Tk stub libraries. No 'make' required -- this
REM calls gcc directly with the exact flags taken from the project Makefile.
REM
REM Paths into this repository are derived from the location of this file.
REM Only the machine-specific ones below need editing.
REM
REM Result per version:
REM   <OUTROOT>\windows-tcl9.0\tclpdfium\  pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl
REM   <OUTROOT>\windows-tcl8.6\tclpdfium\  pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl
REM ===========================================================================

setlocal enabledelayedexpansion

REM ===================== CONFIG -- machine specific ===========================
REM BAWT's MinGW gcc.exe (from gcc14.2.0_x86_64-w64-mingw32.7z):
set "GCC=C:\Bawt\Bawt903\Tools\gcc14.2.0_x86_64-w64-mingw32\mingw64\bin\gcc.exe"

REM The two BAWT trees (each the ...\Development\opt\tcl folder):
set "TCL86=C:\Bawt\Bawt86\Windows\x64\Development\opt\tcl"
set "TCL90=C:\Bawt\Bawt903\Windows\x64\Development\opt\tcl"

REM Where the finished packages go. Default: libs\ inside this repository.
REM Point this at your project root to drop them straight into its layout.
set "OUTROOT=%~dp0libs"
REM ===========================================================================

REM ---- Derived from the location of this script -- do not edit --------------
set "REPO=%~dp0"
set "REPO=%REPO:~0,-1%"
set "SRC=%REPO%\src\pdfiumtcl.c"

REM Package version, read from the Makefile so it cannot drift apart from the
REM version the DLL announces via Tcl_PkgProvide. A mismatch here does not warn
REM -- the package simply refuses to load:
REM   attempt to provide package pdfiumtcl 0.5 failed: 0.5.2 provided instead
set "PKGVER="
for /f "tokens=2 delims==" %%v in ('findstr /b /c:"PKGVERSION" "%REPO%\Makefile"') do (
    for /f "tokens=* delims= " %%w in ("%%v") do set "PKGVER=%%w"
)
if "%PKGVER%"=="" (
    echo ERROR: could not read PKGVERSION from %REPO%\Makefile
    goto :end
)

REM PDFium SDK: platform-specific directory first, legacy path as fallback.
set "PDFIUM=%REPO%\vendor\pdfium-win-x64"
if not exist "%PDFIUM%\lib\pdfium.dll.lib" set "PDFIUM=%REPO%\vendor\pdfium"
REM ---------------------------------------------------------------------------

echo build-tclpdfium-bawt
echo   repo    : %REPO%
echo   version : %PKGVER%
echo   pdfium  : %PDFIUM%
echo   output  : %OUTROOT%
echo.

if not exist "%GCC%" (
    echo ERROR: gcc not found: %GCC%
    echo        Point GCC at BAWT's MinGW gcc.exe.
    goto :end
)
if not exist "%SRC%" (
    echo ERROR: source not found: %SRC%
    goto :end
)
if not exist "%PDFIUM%\lib\pdfium.dll.lib" (
    echo ERROR: import lib missing: %PDFIUM%\lib\pdfium.dll.lib
    echo        Run: scripts\get-pdfium.cmd
    goto :end
)
if not exist "%PDFIUM%\bin\pdfium.dll" (
    echo ERROR: engine missing: %PDFIUM%\bin\pdfium.dll
    echo        Run: scripts\get-pdfium.cmd
    goto :end
)

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
set "OUT=%OUTROOT%\%COMBO%\tclpdfium"
echo.
echo === %COMBO% ===
if not exist "%TCLROOT%\include\tcl.h" ( echo SKIP: %TCLROOT%\include\tcl.h missing & goto :eof )
if not exist "%TCLSTUB%"               ( echo SKIP: %TCLSTUB% missing & goto :eof )
if not exist "%TKSTUB%"                ( echo SKIP: %TKSTUB% missing & goto :eof )
if not exist "%OUT%" mkdir "%OUT%"

"%GCC%" -shared -O2 -Wall -I"%PDFIUM%\include" -I"%TCLROOT%\include" -DUSE_TCL_STUBS -DUSE_TK_STUBS -DWIN32 -D_WIN32 -DWIN32_LEAN_AND_MEAN -o "%OUT%\pdfiumtcl.dll" "%SRC%" -L"%PDFIUM%\lib" "%PDFIUM%\lib\pdfium.dll.lib" "%TCLSTUB%" "%TKSTUB%" -lws2_32 -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -Wl,--export-all-symbols
if errorlevel 1 ( echo *** BUILD FAILED for %COMBO% *** & goto :eof )

copy /Y "%PDFIUM%\bin\pdfium.dll" "%OUT%\pdfium.dll" >nul

REM Flat layout: both DLLs in one directory, so a plain load is enough.
REM The prefix is given explicitly -- Tcl 8.6 title-cases a guessed prefix,
REM Tcl 9 takes it verbatim, and relying on that difference is asking for it.
(echo package ifneeded pdfiumtcl %PKGVER% [list load [file join $dir pdfiumtcl.dll] Pdfiumtcl])>"%OUT%\pkgIndex.tcl"

echo OK: %OUT%
echo     pdfiumtcl.dll + pdfium.dll + pkgIndex.tcl  ^(%PKGVER%^)
goto :eof

:end
endlocal
