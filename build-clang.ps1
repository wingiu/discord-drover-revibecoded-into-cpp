param(
    [string]$BuildDir = "build-clang-msvc",
    [string]$MinHookRoot = "C:\Libraries\MinHook",
    [string]$MinHookLib = ""
)

$ErrorActionPreference = "Stop"

$SourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildPath = Join-Path $SourceDir $BuildDir
$MsvcLink = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\link.exe"
$VsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $MsvcLink)) {
    $MsvcLink = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe"
    $VsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
}
if (Test-Path $VsDevCmd) {
    cmd /c "`"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
        if ($_ -match "^(.*?)=(.*)$") {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
}
if ($MinHookLib -eq "") {
    $MinHookLib = Join-Path $MinHookRoot "lib\libMinHook-x64-v100-md.lib"
}

cmake -S $SourceDir -B $BuildPath -G "Ninja" `
    -DCMAKE_C_COMPILER=clang-cl `
    -DCMAKE_CXX_COMPILER=clang-cl `
    -DCMAKE_LINKER="$MsvcLink" `
    -DMINHOOK_ROOT="$MinHookRoot" `
    -DMINHOOK_LIB="$MinHookLib" `
    -DCMAKE_BUILD_TYPE=Release

cmake --build $BuildPath --config Release
