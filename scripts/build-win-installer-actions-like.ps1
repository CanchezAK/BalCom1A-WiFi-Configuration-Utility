[CmdletBinding()]
param(
  [string]$Msys2Root = "C:\msys64",
  [string]$EsptoolZipUrl = "https://github.com/espressif/esptool/releases/download/v5.1.0/esptool-v5.1.0-windows-amd64.zip",
  [string]$DownloadUrl = "",
  [string]$CpackTempDir = "",
  [switch]$Clean
)

$ErrorActionPreference = 'Stop'

function Require-Command([string]$Name, [string]$Hint) {
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "Missing required command: $Name. $Hint"
  }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $bash)) {
  throw "MSYS2 bash not found at: $bash. Install MSYS2 or pass -Msys2Root."
}

# NSIS is required to produce the installer (makensis).
Require-Command 'makensis.exe' 'Install NSIS (e.g. winget install --id NSIS.NSIS -e) and ensure it is on PATH.'

# Prepare esptool.exe asset (same source as Actions: official zip release).
$assetDir = Join-Path $repoRoot 'build\esptool-asset'
$tmpRoot = Join-Path $env:TEMP 'balcom-esptool-local'
$zipPath = Join-Path $tmpRoot 'esptool.zip'
$extractDir = Join-Path $tmpRoot 'unzipped'

New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
if (Test-Path -LiteralPath $extractDir) { Remove-Item -LiteralPath $extractDir -Recurse -Force }

Write-Host "Downloading esptool zip: $EsptoolZipUrl"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -UseBasicParsing -Uri $EsptoolZipUrl -OutFile $zipPath -Headers @{ 'User-Agent'='BalComLocalBuilder' }
Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force

$exe = Get-ChildItem -Path $extractDir -Recurse -File -Filter 'esptool.exe' | Select-Object -First 1
if (-not $exe) {
  throw "esptool.exe not found in extracted zip: $EsptoolZipUrl"
}

New-Item -ItemType Directory -Force -Path $assetDir | Out-Null
Copy-Item -LiteralPath $exe.FullName -Destination (Join-Path $assetDir 'esptool.exe') -Force

$esptoolPath = Join-Path $assetDir 'esptool.exe'
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $esptoolPath).Hash.ToLower()

# Default DownloadUrl: file:// URL pointing to the locally prepared esptool.exe.
# This lets you test the installer download logic without creating a GitHub Release first.
if ([string]::IsNullOrWhiteSpace($DownloadUrl)) {
  $fileUri = (New-Object System.Uri($esptoolPath)).AbsoluteUri
  $DownloadUrl = $fileUri
}

if ([string]::IsNullOrWhiteSpace($CpackTempDir)) {
  $CpackTempDir = Join-Path $repoRoot 'build\cpack-tmp'
}

# Match the Actions workflow: pass these values via env, consumed by the preset.
$env:BALCOM_FLASH_TOOL_DOWNLOAD_URL = $DownloadUrl
$env:BALCOM_FLASH_TOOL_SHA256 = $sha
$env:CPACK_TEMPORARY_DIRECTORY = $CpackTempDir
$env:MSYS2_MINGW64_PREFIX = (Join-Path $Msys2Root 'mingw64')

Write-Host "Using preset: win-release-installer-actions"
Write-Host "MSYS2_MINGW64_PREFIX=$($env:MSYS2_MINGW64_PREFIX)"
Write-Host "CPACK_TEMPORARY_DIRECTORY=$($env:CPACK_TEMPORARY_DIRECTORY)"
Write-Host "BALCOM_FLASH_TOOL_DOWNLOAD_URL=$($env:BALCOM_FLASH_TOOL_DOWNLOAD_URL)"
Write-Host "BALCOM_FLASH_TOOL_SHA256=$($env:BALCOM_FLASH_TOOL_SHA256)"

$buildDir = Join-Path $repoRoot 'build\win-release-installer-actions'
if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
  Remove-Item -LiteralPath $buildDir -Recurse -Force
}

# Run CMake inside MSYS2 to ensure the same toolchain (MinGW Makefiles + pkg-config GTK4).
# We rely on the preset for cache vars; env provides URL/SHA/MSYS2 prefix/tempdir.
$cmdConfigure = "cd '$repoRoot' && cmake --preset win-release-installer-actions"
$cmdBuild = "cd '$repoRoot' && cmake --build --preset win-installer-actions"

Write-Host "Configuring..."
& $bash -lc $cmdConfigure

Write-Host "Building installer (make_installer)..."
& $bash -lc $cmdBuild

# Locate produced installer.
$installer = Get-ChildItem -Path $buildDir -Recurse -File -Filter 'BalCom1A Configuration Utility-*.exe' |
  Sort-Object LastWriteTime -Descending |
  Select-Object -First 1

if (-not $installer) {
  throw "Installer not found under: $buildDir"
}

Write-Host "Installer built: $($installer.FullName)"
Write-Host "(Tip) If you want to match Actions even closer, set -DownloadUrl to the GitHub Release asset URL for your tag."
