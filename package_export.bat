@echo off
setlocal

set "ROOT=%~dp0"
set "APP_SOURCE=%ROOT%build_config\release_export4"
set "EXPORT_ROOT=%ROOT%WonderVisionStudio_Export"
set "APP_ROOT=%EXPORT_ROOT%\WonderVisionStudio"
set "ZIP_PATH=%ROOT%WonderVisionStudio_Export.zip"

if not exist "%APP_SOURCE%\WonderVisionStudio.exe" (
    echo Missing built app: %APP_SOURCE%\WonderVisionStudio.exe
    exit /b 1
)

if not exist "%APP_ROOT%" mkdir "%APP_ROOT%"
copy /Y "%APP_SOURCE%\*.exe" "%APP_ROOT%\" >nul || goto :error
copy /Y "%APP_SOURCE%\*.dll" "%APP_ROOT%\" >nul || goto :error

for %%D in (iconengines imageformats platforms styles) do (
    if exist "%APP_SOURCE%\%%D" (
        xcopy "%APP_SOURCE%\%%D" "%APP_ROOT%\%%D\" /E /I /Y >nul
        if errorlevel 1 goto :error
    )
)

if exist "%ROOT%build_config\release\vc_redist.x64.exe" copy /Y "%ROOT%build_config\release\vc_redist.x64.exe" "%APP_ROOT%\" >nul
copy /Y "%ROOT%EXPORT_README.txt" "%EXPORT_ROOT%\README.txt" >nul || goto :error
if exist "%ZIP_PATH%" del /Q "%ZIP_PATH%"
pushd "%ROOT%"
tar -a -c -f "%ZIP_PATH%" WonderVisionStudio_Export || goto :error
popd

echo Export folder: %EXPORT_ROOT%
echo Export ZIP: %ZIP_PATH%
exit /b 0

:error
echo Export packaging failed.
exit /b 1
