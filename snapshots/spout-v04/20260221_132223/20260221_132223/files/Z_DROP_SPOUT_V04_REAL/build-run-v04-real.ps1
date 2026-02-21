param(
  [string]$CRepoRoot = "C:\Temp\Spout-v04",
  [string]$BuildDir  = "C:\Temp\Spout-v04\build\SpoutMaskSenderDX11_vs",
  [string]$Config    = "Release",
  [string]$Platform  = "x64",

  # Change this to an older OpenVINO root when you install it.
  [string]$OVRoot    = "C:\Deps\openvino\openvino_toolkit_windows_2025.4.1.20426.82bbf0292c5_x86_64"
)

$ErrorActionPreference = "Stop"

function Assert-Ok($cond, $msg) { if (-not $cond) { throw $msg } }

# Make CMakeLists.txt pick up OpenVINO from an explicit env var
$env:OPENVINO_ROOT_DIR = $OVRoot

$OVSetupVars = Join-Path $OVRoot "setupvars.ps1"
Assert-Ok (Test-Path $OVSetupVars) "OpenVINO setupvars not found: $OVSetupVars"
. $OVSetupVars

$srcDir = Join-Path $CRepoRoot "cpp\SpoutMaskSenderDX11"
Assert-Ok (Test-Path $srcDir) "Source dir not found: $srcDir"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host "=== CMake configure ==="
cmake -S $srcDir -B $BuildDir -G "Visual Studio 17 2022" -A $Platform
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

$msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
Assert-Ok (Test-Path $msbuild) "MSBuild not found: $msbuild"

Write-Host "=== Build ($Config|$Platform) ==="
& $msbuild (Join-Path $BuildDir "SpoutMaskSenderDX11.sln") /t:Rebuild /p:Configuration=$Config /p:Platform=$Platform /m
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }

$exeDir = Join-Path $BuildDir $Config
$exe    = Join-Path $exeDir "SpoutMaskSenderDX11.exe"
Assert-Ok (Test-Path $exe) "EXE not found: $exe"

# ---- Stage OpenVINO runtime DLLs next to the EXE ----
$ovBin = Join-Path $OVRoot "runtime\bin\intel64\$Config"
Assert-Ok (Test-Path $ovBin) "OpenVINO runtime bin not found: $ovBin"

Write-Host "=== Stage OpenVINO DLLs ==="
Copy-Item "$ovBin\openvino*.dll" $exeDir -Force
Copy-Item "$ovBin\openvino_*_plugin.dll" $exeDir -Force -ErrorAction SilentlyContinue
Copy-Item "$ovBin\plugins.xml" $exeDir -Force -ErrorAction SilentlyContinue

$tbbBin = Join-Path $OVRoot "runtime\3rdparty\tbb\bin"
if (Test-Path $tbbBin) {
  Copy-Item "$tbbBin\tbb12.dll" $exeDir -Force -ErrorAction SilentlyContinue
  Copy-Item "$tbbBin\tbbmalloc.dll" $exeDir -Force -ErrorAction SilentlyContinue
  Copy-Item "$tbbBin\tbbmalloc_proxy.dll" $exeDir -Force -ErrorAction SilentlyContinue
  Copy-Item "$tbbBin\tbbbind_2_5.dll" $exeDir -Force -ErrorAction SilentlyContinue
}

Write-Host "=== Run ==="
Push-Location $exeDir
& $exe
Pop-Location