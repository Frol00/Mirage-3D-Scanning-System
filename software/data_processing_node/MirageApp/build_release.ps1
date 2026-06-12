$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceDir = Join-Path $root "source\InfiniTAM-master\InfiniTAM"
$buildDir = Join-Path $root "build-vs18-rs2-final"
$binDir = Join-Path $root "bin\Release"
$realsenseRoot = Join-Path $root "source\librealsense-2.53.1"
$glutRoot = Join-Path $sourceDir "freeglut"

cmake -S $sourceDir -B $buildDir `
  -DWITH_OPENMP=ON `
  -DWITH_CUDA=OFF `
  -DWITH_REALSENSE2=ON `
  -DWITH_REALSENSE2_NET=ON `
  -DREALSENSE2_ROOT="$realsenseRoot" `
  -DGLUT_ROOT="$glutRoot"

cmake --build $buildDir --config Release --target InfiniTAM --parallel 4

$builtRelease = Join-Path $buildDir "Apps\InfiniTAM\Release"
New-Item -ItemType Directory -Force -Path $binDir | Out-Null

foreach ($file in @("InfiniTAM.exe", "freeglut.dll", "realsense2.dll", "realsense2-net.dll", "rs-enumerate-devices.exe", "imgui.ini")) {
    $src = Join-Path $builtRelease $file
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $binDir $file) -Force
    }
}

Write-Host "Build complete: $binDir"
