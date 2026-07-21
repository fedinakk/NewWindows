@echo off
rem Builds InfiniteCanvas (Release, x64) from the command line without opening
rem Visual Studio. Requires VS 2022 (any edition) or Build Tools 2022 with the
rem "Desktop development with C++" workload.

setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [!] vswhere.exe not found. Install Visual Studio 2022 or Build Tools 2022.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo [!] MSBuild not found. Install the C++ workload for VS 2022.
    exit /b 1
)

echo Using: !MSBUILD!
"!MSBUILD!" "%~dp0InfiniteCanvas.sln" /m /nologo /p:Configuration=Release /p:Platform=x64
if errorlevel 1 (
    echo [!] Build failed.
    exit /b 1
)

echo.
echo Done: %~dp0bin\x64\Release\InfiniteCanvas.exe
endlocal
