@echo off
rem Installs the ChaosGrain VST3 plugins into the standard Windows VST3 folder.
rem Right-click this file and choose "Run as administrator".

net session >nul 2>&1
if errorlevel 1 (
  echo.
  echo Please right-click Install-ChaosGrain.bat and choose "Run as administrator".
  echo.
  pause
  exit /b 1
)

set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"

echo Installing to "%DEST%" ...
xcopy /E /I /Y /Q "%~dp0ChaosGrain.vst3" "%DEST%\ChaosGrain.vst3" >nul
xcopy /E /I /Y /Q "%~dp0ChaosGrain FX.vst3" "%DEST%\ChaosGrain FX.vst3" >nul

if exist "%DEST%\ChaosGrain.vst3\Contents\x86_64-win\ChaosGrain.vst3" (
  echo   OK  ChaosGrain
) else (
  echo   FAILED  ChaosGrain
)
if exist "%DEST%\ChaosGrain FX.vst3\Contents\x86_64-win\ChaosGrain FX.vst3" (
  echo   OK  ChaosGrain FX
) else (
  echo   FAILED  ChaosGrain FX
)
echo.
echo Done. In FL Studio: Options ^> Manage plugins ^> Find installed plugins,
echo then tick the star next to ChaosGrain and ChaosGrain FX.
echo.
pause
