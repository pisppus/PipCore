#Requires -Version 5.1
param(
  [switch]$Force,
  [switch]$EnsureBuildTools
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step ([string]$msg) { Write-Host "  >> $msg" -ForegroundColor Cyan }
function Write-Ok ([string]$msg) { Write-Host "  OK $msg" -ForegroundColor Green }
function Write-Warn ([string]$msg) { Write-Host "  !! $msg" -ForegroundColor Yellow }
function Write-Fatal ([string]$msg) { Write-Host "`n  FAIL $msg`n" -ForegroundColor Red; exit 1 }

function Invoke-WingetInstall([string]$id, [string[]]$extraArgs = @()) {
  $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
  if (!$winget) { return $false }
  & $winget.Source install --id $id --exact --silent --accept-package-agreements --accept-source-agreements @extraArgs
  return ($LASTEXITCODE -eq 0)
}

if ($EnsureBuildTools) {
  if (!(Get-Command git.exe -ErrorAction SilentlyContinue)) {
    Write-Step "Installing Git via winget..."
    if (!(Invoke-WingetInstall "Git.Git")) {
      Write-Fatal "Git install failed. Install Git for Windows manually."
    }
  }

  if (!(Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    Write-Step "Installing CMake via winget..."
    if (!(Invoke-WingetInstall "Kitware.CMake")) {
      Write-Warn "CMake install failed. Install CMake manually if configure fails."
    }
  }

  $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
  $hasVcTools = $false
  if (Test-Path $vswhere) {
    $hasVcTools = [bool](& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
  }
  if (!$hasVcTools) {
    Write-Step "Installing Visual Studio C++ Build Tools via winget..."
    $override = "--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    if (!(Invoke-WingetInstall "Microsoft.VisualStudio.2022.BuildTools" @("--override", $override))) {
      Write-Warn "Visual Studio Build Tools install failed. Install the C++ workload manually if vcvars64.bat is missing."
    }
  }
}

Write-Ok "Simulator dependencies are ready."
Write-Host "  The simulator builds natively (Win32 + MSVC); no external packages are required." -ForegroundColor DarkGray
Write-Host "  Optional: drop miniaudio.h into Simulator\miniaudio\ for real audio output." -ForegroundColor DarkGray
