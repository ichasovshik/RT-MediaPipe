param(
  [string]$DropDir = "Z:\MediaPipe\Working-dir\_drop\Spout-v04-real",
  [string]$CRepoRoot = "C:\Temp\Spout-v04",
  [string]$BuildRelDir = "C:\Temp\Spout-v04\build\SpoutMaskSenderDX11_vs\Release",
  [switch]$CreateBuildDirs
)

$ErrorActionPreference = "Stop"

# Required payload files in DropDir
$payloadSrcDir = $DropDir
$payloadFiles = @(
  "main.cpp",
  "CMakeLists.txt",
  "SpoutFrameCount_fix.cpp"
)

# Config that must land next to the exe
$configFile = "sender_config.json"

foreach ($f in $payloadFiles + $configFile) {
  $p = Join-Path $payloadSrcDir $f
  if (!(Test-Path $p)) { throw "Missing in drop dir: $p" }
}

$dstSrcDir = Join-Path $CRepoRoot "cpp\SpoutMaskSenderDX11"

if ($CreateBuildDirs) {
  New-Item -ItemType Directory -Force -Path $dstSrcDir | Out-Null
  New-Item -ItemType Directory -Force -Path $BuildRelDir | Out-Null
}

Write-Host "=== Deploy V04 REAL files ==="
Write-Host "DROP: $DropDir"
Write-Host "DST : $dstSrcDir"
Write-Host "CFG : $BuildRelDir"

function RoboCopyOne($srcDir, $dstDir, $fileName) {
  robocopy $srcDir $dstDir $fileName /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
  if ($LASTEXITCODE -gt 7) { throw "robocopy failed ($LASTEXITCODE) for $fileName" }
}

# Copy code payload into source folder (C:)
foreach ($f in $payloadFiles) {
  RoboCopyOne $payloadSrcDir $dstSrcDir $f
}

# Copy runtime config next to exe (Release)
RoboCopyOne $payloadSrcDir $BuildRelDir $configFile

Write-Host "OK: deployed payload: $($payloadFiles -join ', ') and $configFile"