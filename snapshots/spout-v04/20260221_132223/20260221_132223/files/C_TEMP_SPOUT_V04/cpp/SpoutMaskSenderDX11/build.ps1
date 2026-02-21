param(
  [string]$BuildDir = (Join-Path $PSScriptRoot "..\..\build\SpoutMaskSenderDX11"),
  [string]$Config = "Release"
)

$src = $PSScriptRoot
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

cmake -S $src -B $BuildDir -G "Ninja" -DCMAKE_BUILD_TYPE=$Config
cmake --build $BuildDir
Write-Host "Built in $BuildDir"