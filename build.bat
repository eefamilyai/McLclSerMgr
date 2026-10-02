@echo off
rem Builds Voxual + its installer with MSVC + Ninja.
rem Output: dist\Voxual.exe and dist\VoxualSetup.exe
rem (if Voxual.exe is locked because the app is running, it is saved as dist\Voxual-new.exe)
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" for /f "delims=" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath') do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || exit /b 1
set "BUILD=%LOCALAPPDATA%\Voxual-build"
cmake -S "%~dp0." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul || exit /b 1
cmake --build "%BUILD%" || exit /b 1
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
