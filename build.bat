@echo off
setlocal

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "QTDIR=C:\Qt\5.15.0\msvc2019_64"
set "BUILD_DIR=build_config"
set "APP_DIR=release_export4"

call "%VCVARS%" || goto :error
set "PATH=%QTDIR%\bin;%PATH%"
cd /d "%~dp0"

REM Keep previous build folders intact; Windows may have locked them.
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

if not exist Makefile.Release (
    qmake "CONFIG+=release" ..\WonderVisionStudio.pro || goto :error
)
if not exist "%APP_DIR%" mkdir "%APP_DIR%"
nmake -f Makefile.Release release\WonderVisionStudio.exe DESTDIR_TARGET=%APP_DIR%\WonderVisionStudio.exe || goto :error
"%QTDIR%\bin\windeployqt.exe" --release --no-translations %APP_DIR%\WonderVisionStudio.exe || goto :error

cd /d "%~dp0"
call .\package_export.bat || goto :error

echo.
echo ==== BUILD OK ====
echo App: %~dp0%BUILD_DIR%\%APP_DIR%\WonderVisionStudio.exe
echo ZIP: %~dp0WonderVisionStudio_Export.zip
goto :eof

:error
echo.
echo ==== BUILD FAILED (errorlevel %errorlevel%) ====
exit /b %errorlevel%
