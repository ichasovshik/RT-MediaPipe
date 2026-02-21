param(
  [ValidateSet("2024.6","2023.3")]
  [string]$Version = "2024.6",

  [string]$DropDir = "Z:\MediaPipe\Working-dir\_drop\Spout-v04-real",
  [string]$DestBase = "C:\Deps\openvino"
)

$ErrorActionPreference = "Stop"

$zip = switch ($Version) {
  "2024.6" { Join-Path $DropDir "w_openvino_toolkit_windows_2024.6.0.17404.4c0f47d2335_x86_64.zip" }
  "2023.3" { Join-Path $DropDir "w_openvino_toolkit_windows_2023.3.0.13775.ceeafaf64f3_x86_64.zip" }
}

if (!(Test-Path $zip)) { throw "Zip not found: $zip" }

New-Item -ItemType Directory -Force -Path $DestBase | Out-Null

Write-Host "=== Unzip OpenVINO $Version ==="
Write-Host "ZIP : $zip"
Write-Host "DEST: $DestBase"

# Expand-Archive creates the top-level folder from the zip (e.g., w_openvino_toolkit_windows_2024.6.0....)
Expand-Archive -Path $zip -DestinationPath $DestBase -Force

# Find the extracted root (contains setupvars.ps1)
$root = Get-ChildItem $DestBase -Directory |
  Where-Object { Test-Path (Join-Path $_.FullName "setupvars.ps1") } |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 1

if (-not $root) { throw "Could not find extracted OpenVINO root with setupvars.ps1 under $DestBase" }

Write-Host "OK: OpenVINO root: $($root.FullName)"
Write-Host "Try build with:"
Write-Host "  -OVRoot `"$($root.FullName)`""