@echo off
setlocal

rem builds rave in release mode and puts the two exe files into builds\test and builds\installer

cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe was not found, install Visual Studio Build Tools 2022
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo MSBuild.exe was not found
    exit /b 1
)

"%MSBUILD%" Rave.sln -t:Build -p:Configuration=Release -p:Platform=x64 -m -nologo -v:minimal
if errorlevel 1 (
    echo build failed
    exit /b 1
)

if not exist builds\test mkdir builds\test
if not exist builds\installer mkdir builds\installer

copy /y out\Release\Rave.exe builds\test\Test-Rave.exe >nul || exit /b 1
copy /y out\Release\Rave-Installer.exe builds\installer\Rave-Installer.exe >nul || exit /b 1

echo.
echo builds\test\Test-Rave.exe
echo builds\installer\Rave-Installer.exe
exit /b 0
