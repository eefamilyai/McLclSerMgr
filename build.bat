@echo off
rem Builds Voxual + its installer with MSVC + Ninja.
rem Output: dist\Voxual.exe and dist\VoxualSetup.exe
rem (if Voxual.exe is locked because the app is running, it is saved as dist\Voxual-new.exe)
rem Code signing is optional and off unless configured: see "Code signing (optional)" in README.md.
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" for /f "delims=" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath') do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || exit /b 1
set "BUILD=%LOCALAPPDATA%\Voxual-build"
cmake -S "%~dp0." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul || exit /b 1
cmake --build "%BUILD%" || exit /b 1

rem Optional code signing. The installer carries Voxual.exe as a resource, so the app has to be
rem signed before the installer is linked, and the installer then re-linked around the signed app
rem - otherwise the copy that lands in Program Files is unsigned. sign.ps1 exits 3 when signing is
rem not configured, which leaves this script doing exactly what it always did.
set "SIGN=%~dp0tools\sign.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SIGN%" "%BUILD%\Voxual.exe"
set "SIGN_RC=%ERRORLEVEL%"
if "%SIGN_RC%"=="0" (
  cmake --build "%BUILD%" || exit /b 1
  powershell -NoProfile -ExecutionPolicy Bypass -File "%SIGN%" "%BUILD%\VoxualSetup.exe" || exit /b 1
  powershell -NoProfile -ExecutionPolicy Bypass -File "%SIGN%" -VerifyEmbedded "%BUILD%\Voxual.exe" "%BUILD%\VoxualSetup.exe" || exit /b 1
) else (
  if not "%SIGN_RC%"=="3" exit /b 1
)

if not exist "%~dp0dist" mkdir "%~dp0dist"
copy /Y "%BUILD%\VoxualSetup.exe" "%~dp0dist\VoxualSetup.exe" >nul
copy /Y "%BUILD%\Voxual.exe" "%~dp0dist\Voxual.exe" >nul 2>&1
if errorlevel 1 (
  copy /Y "%BUILD%\Voxual.exe" "%~dp0dist\Voxual-new.exe" >nul
  echo.
  echo Voxual.exe is in use - saved the new build as: %~dp0dist\Voxual-new.exe
) else (
  echo.
  echo Built: %~dp0dist\Voxual.exe
)
echo Installer: %~dp0dist\VoxualSetup.exe
echo.
echo SHA256, for the release notes:
for %%f in ("%~dp0dist\Voxual.exe" "%~dp0dist\VoxualSetup.exe") do (
  for /f "delims=" %%h in ('certutil -hashfile %%f SHA256 ^| findstr /r "^[0-9a-fA-F][0-9a-fA-F]*$"') do echo   %%~nxf  %%h
)
