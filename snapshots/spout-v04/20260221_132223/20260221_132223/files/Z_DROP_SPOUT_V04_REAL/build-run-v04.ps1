param(
  [string]$CRepoRoot   = "C:\Temp\Spout-v04",
  [string]$BuildDir    = "C:\Temp\Spout-v04\build\SpoutMaskSenderDX11_vs",
  [string]$Config      = "Release",
  [string]$Platform    = "x64",
  [string]$OVSetupVars = "C:\Deps\openvino\openvino_toolkit_windows_2025.4.1.20426.82bbf0292c5_x86_64\setupvars.ps1"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path $OVSetupVars)) { throw "OpenVINO setupvars not found: $OVSetupVars" }
. $OVSetupVars

$srcDir = Join-Path $CRepoRoot "cpp\SpoutMaskSenderDX11"
if (!(Test-Path $srcDir)) { throw "Source dir not found: $srcDir" }

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host "=== CMake configure ==="
cmake -S $srcDir -B $BuildDir -G "Visual Studio 17 2022" -A $Platform

$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
if (!(Test-Path $msbuild)) { throw "MSBuild not found: $msbuild" }

Write-Host "=== Build ($Config|$Platform) ==="
& $msbuild (Join-Path $BuildDir "SpoutMaskSenderDX11.sln") /t:Rebuild /p:Configuration=$Config /p:Platform=$Platform /m

$exe = Join-Path $BuildDir "$Config\SpoutMaskSenderDX11.exe"
if (!(Test-Path $exe)) { throw "EXE not found: $exe" }

Write-Host "=== Run ==="
& $exe