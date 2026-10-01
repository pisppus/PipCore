# Requires -Version 5.1
param(
  [switch]$NoRun,
  [switch]$Debug,
  [switch]$Clean,
  [switch]$InstallDeps,
  [string]$ProjectRoot = "",
  [int]$Jobs = [Environment]::ProcessorCount
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$simDir = $PSScriptRoot

$buildDir = Join-Path $simDir "Build"
$configName = if ($Debug) { "debug" } else { "release" }
$cmakeConfig = "Release"
$simDebug = if ($Debug) { "ON" } else { "OFF" }
$cmakeBuildDir = Join-Path $buildDir "build-windows-$configName"
$vsEnvFile = Join-Path $buildDir "_vsdev-env-$PID.txt"
$vsEnvCacheFile = Join-Path $buildDir "_vsdev-env-cache.txt"
$vsEnvStampFile = Join-Path $buildDir "_vsdev-env-cache.stamp"
$watchdogFile = Join-Path $buildDir "_sim-watchdog.ps1"
$watchdogLog = Join-Path $buildDir "sim-watchdog.log"
$runtimeOutLog = Join-Path $buildDir "simulator-runtime.out.log"
$runtimeErrLog = Join-Path $buildDir "simulator-runtime.err.log"
$iniFile = Join-Path $buildDir "simulator.ini"
$rebuildCmd = Join-Path $buildDir "_sim-rebuild.cmd"
$exeName = if ($Debug) { "simulator-debug.exe" } else { "simulator.exe" }
$exe = Join-Path $buildDir $exeName

function Write-Step ([string]$msg) { Write-Host "  >> $msg" -ForegroundColor Cyan }
function Write-Ok ([string]$msg) { Write-Host "  OK $msg" -ForegroundColor Green }
function Write-Warn ([string]$msg) { Write-Host "  !! $msg" -ForegroundColor Yellow }
function Write-Fatal ([string]$msg) { Write-Host "`n  FAIL $msg`n" -ForegroundColor Red; exit 1 }

function ConvertTo-CMakePath([string]$path) { return ($path -replace '\\', '/') }

function Get-IniValue([string]$path, [string]$key) {
  if (!(Test-Path $path)) { return "" }
  $pattern = "^\s*$([regex]::Escape($key))\s*=\s*(.*?)\s*$"
  foreach ($line in Get-Content -LiteralPath $path) {
    if ($line -match $pattern) { return $Matches[1] }
  }
  return ""
}

function Set-IniValue([string]$path, [string]$key, [string]$value) {
  $lines = [System.Collections.Generic.List[string]]::new()
  if (Test-Path $path) {
    foreach ($line in Get-Content -LiteralPath $path) { $lines.Add($line) }
  }
  $pattern = "^\s*$([regex]::Escape($key))\s*="
  $found = $false
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match $pattern) { $lines[$i] = "$key=$value"; $found = $true; break }
  }
  if (!$found) { $lines.Add("$key=$value") }
  [IO.File]::WriteAllLines($path, $lines)
}

function Test-PipCoreRoot([string]$path) {
  return ($path -and (Test-Path (Join-Path $path "PipCore\CMakeLists.txt")))
}

function Find-PipCoreRoot([string]$startDir) {
  $dir = [IO.Path]::GetFullPath($startDir)
  while ($dir) {
    if (Test-PipCoreRoot $dir) { return $dir }
    $parent = [IO.Path]::GetDirectoryName($dir)
    if (!$parent -or $parent -eq $dir) { break }
    $dir = $parent
  }
  return ""
}

function Test-AppDir([string]$path) {
  if (!$path -or !(Test-Path $path)) { return $false }
  $found = Get-ChildItem -LiteralPath $path -Recurse -File -Include *.cpp,*.cc,*.cxx,*.c -ErrorAction SilentlyContinue |
    Select-Object -First 1
  return [bool]$found
}

function Pick-Folder([string]$title) {
  try {
    Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
    $form = New-Object System.Windows.Forms.Form
    $form.TopMost = $true
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = $title
    $dialog.ShowNewFolderButton = $false
    $result = $dialog.ShowDialog($form)
    $form.Dispose()
    if ($result -eq [System.Windows.Forms.DialogResult]::OK -and $dialog.SelectedPath) {
      return $dialog.SelectedPath
    }
  } catch {}
  return ""
}

function Resolve-PipCoreRoot {
  $candidates = @($ProjectRoot, $env:PIPSIM_PROJECT_ROOT, (Get-IniValue $iniFile "project_root"))
  foreach ($candidate in $candidates) {
    if ($candidate -and (Test-PipCoreRoot $candidate)) {
      return [IO.Path]::GetFullPath($candidate).TrimEnd('\')
    }
  }

  $found = Find-PipCoreRoot $PSScriptRoot
  if ($found) { return $found }

  Write-Warn "PipCore component (PipCore\) not found around:"
  Write-Host "      $PSScriptRoot" -ForegroundColor DarkGray
  Write-Host "  Pick the project folder in the dialog, or paste the path below." -ForegroundColor Yellow
  while ($true) {
    $picked = Pick-Folder "Select the PipCore project folder (contains the PipCore component folder)"
    if ($picked) {
      if (Test-PipCoreRoot $picked) { return [IO.Path]::GetFullPath($picked).TrimEnd('\') }
      Write-Warn "No PipCore\ component inside: $picked"
    }
    $typed = (Read-Host "  Project folder path (empty = cancel)").Trim()
    if (!$typed) { Write-Fatal "Project folder is required; run this script again when ready." }
    $typed = $typed.Trim('"')
    if (Test-PipCoreRoot $typed) { return [IO.Path]::GetFullPath($typed).TrimEnd('\') }
    Write-Warn "No PipCore\ component inside: $typed"
  }
}

function Test-MsvcIncludeNote([string]$line) {
  return $line -match '^\s*Note:\s+including file:' -or
  $line -match '^\s*[^:]+:\s+[^:]+:\s+[A-Za-z]:\\' -or
  $line -match '^\s*[^:]{4,100}:\s+[A-Za-z]:\\.*\.(h|hh|hpp|hxx|inl|inc)\s*$'
}

function Invoke-CMakeBuild([string]$buildPath, [string]$config, [int]$parallelJobs) {
  $prevEap = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    & cmake.exe --build $buildPath --config $config --parallel $parallelJobs 2>&1 |
      ForEach-Object {
      $line = "$_"
      if ($line -and !(Test-MsvcIncludeNote $line)) { Write-Host $line }
    }
  } finally {
    $ErrorActionPreference = $prevEap
  }
  return $LASTEXITCODE
}

function Invoke-CMakeConfigure([string[]]$cmakeArgs, [string]$buildPath) {
  $prevEap = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    & cmake.exe @cmakeArgs 2>&1 |
      ForEach-Object {
      $line = "$_"
      if ($line) { Write-Host $line }
    }
  } finally {
    $ErrorActionPreference = $prevEap
  }
  return $LASTEXITCODE
}

function Invoke-CMakeConfigureWithRetry([string[]]$cmakeArgs, [string]$buildPath) {
  $exitCode = Invoke-CMakeConfigure $cmakeArgs $buildPath
  if ($exitCode -eq 0) { return 0 }
  if (Test-Path $buildPath) {
    Write-Warn "CMake configure failed; clearing stale build tree and retrying once."
    Remove-Item $buildPath -Recurse -Force
    New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
    return (Invoke-CMakeConfigure $cmakeArgs $buildPath)
  }
  return $exitCode
}

function Resolve-VcVars64 {
  $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    $installPaths = @(
      & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
      & $vswhere -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    ) | Where-Object { $_ } | Select-Object -Unique

    foreach ($ip in $installPaths) {
      $bat = Join-Path $ip "VC\Auxiliary\Build\vcvars64.bat"
      if (Test-Path $bat) { return $bat }
    }
  }

  foreach ($searchRoot in @(
      "C:\Program Files\Microsoft Visual Studio",
      "C:\Program Files (x86)\Microsoft Visual Studio"
    )) {
    if (!(Test-Path $searchRoot)) { continue }
    $bat = Get-ChildItem $searchRoot -Recurse -Filter "vcvars64.bat" -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    if ($bat) { return $bat }
  }
  return $null
}

function Import-VsEnvironment([string]$vcvars) {
  $cleanPath = (@(
      [Environment]::GetEnvironmentVariable("Path", "Machine"),
      [Environment]::GetEnvironmentVariable("Path", "User")
    ) | Where-Object { $_ } | ForEach-Object { $_.Trim(';') }) -join ';'

  $vcvarsItem = Get-Item $vcvars
  $pathHash = ([Security.Cryptography.SHA256]::Create().ComputeHash(
      [Text.Encoding]::UTF8.GetBytes($cleanPath)) |
        ForEach-Object { $_.ToString("x2") }) -join ""
    $stamp = "$vcvars|$($vcvarsItem.LastWriteTimeUtc.Ticks)|$pathHash"
    $cachedStamp = if (Test-Path $vsEnvStampFile) { Get-Content $vsEnvStampFile -Raw } else { "" }

    if ((Test-Path $vsEnvCacheFile) -and ($cachedStamp.Trim() -eq $stamp)) {
      Write-Ok "Using cached VS environment."
      Copy-Item $vsEnvCacheFile $vsEnvFile -Force
    } else {
      $cmdFile = Join-Path $buildDir "_vsdev-bootstrap.cmd"
      $varsToClear = @(
        "INCLUDE","LIB","LIBPATH","DevEnvDir","ExtensionSdkDir",
        "Framework40Version","FrameworkDir","FrameworkDir32",
        "FrameworkVersion","FrameworkVersion32","FrameworkVersion64",
        "UCRTVersion","UniversalCRTSdkDir","VCIDEInstallDir","VCINSTALLDIR",
        "VCToolsInstallDir","VSINSTALLDIR","VisualStudioVersion",
        "WindowsLibPath","WindowsSdkBinPath","WindowsSdkDir",
        "WindowsSdkVerBinPath","WindowsSDKLibVersion","WindowsSDKVersion",
        "__VSCMD_PREINIT_PATH","__VSCMD_ARG_APP_PLAT","__VSCMD_ARG_HOST_ARCH",
        "__VSCMD_ARG_NO_LOGO","__VSCMD_ARG_TGT_ARCH","__VSCMD_VER"
      )

      $lines = [Collections.Generic.List[string]]@(
        "@echo off",
        "setlocal EnableExtensions",
        ('set "PATH=' + ($cleanPath -replace '"', '') + '"')
      )
      foreach ($v in $varsToClear) { $lines.Add("set `"$v=`"") }
      $lines.Add("call `"$vcvars`" >nul 2>&1")
      $lines.Add("if %errorlevel% neq 0 exit /b %errorlevel%")
      $lines.Add("set > `"$vsEnvFile`"")

      [IO.File]::WriteAllLines($cmdFile, $lines, [Text.Encoding]::ASCII)
      $null = & cmd.exe /d /s /c "`"$cmdFile`""
      if ($LASTEXITCODE -ne 0 -or !(Test-Path $vsEnvFile)) {
        Write-Fatal "Failed to initialize VS build environment (exit $LASTEXITCODE)."
      }
      Copy-Item $vsEnvFile $vsEnvCacheFile -Force
      Set-Content -Path $vsEnvStampFile -Value $stamp -NoNewline
      Remove-Item $cmdFile -Force -ErrorAction SilentlyContinue
    }

    foreach ($line in [IO.File]::ReadAllLines($vsEnvFile)) {
      if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
      }
    }
    Remove-Item $vsEnvFile -Force -ErrorAction SilentlyContinue
  }

  $buildStart = [Diagnostics.Stopwatch]::StartNew()
  Write-Host ""
  Write-Host "  simulator" -ForegroundColor DarkGray
  Write-Host "  $configName  jobs=$Jobs" -ForegroundColor DarkGray
  Write-Host ""

  New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

  Write-Step "Locating PipCore project..."
  $root = Resolve-PipCoreRoot
  $storedRoot = (Get-IniValue $iniFile "project_root") -replace '\\', '/'
  $normalizedRoot = $root -replace '\\', '/'
  if ($storedRoot -ne $normalizedRoot) {
    Set-IniValue $iniFile "project_root" $normalizedRoot
    Write-Ok "Project root saved to simulator.ini"
  }
  Write-Ok "Project root: $root"

  $appDir = Get-IniValue $iniFile "app_dir"
  if (!$appDir -or !(Test-AppDir $appDir)) {
    $appDir = ""
    foreach ($candidate in @((Join-Path $root "src"), (Join-Path $root "main"))) {
      if (Test-AppDir $candidate) { $appDir = $candidate; break }
    }
  }
  if ($appDir) {
    Write-Ok "App sources: $appDir"
  } else {
    Write-Warn "App sources not found (src/ or main/) - the simulator will open with the setup panel."
  }

  if ($Clean) {
    Write-Step "Cleaning build outputs..."
    if (Test-Path $cmakeBuildDir) {
      cmd.exe /c "rd /s /q `"\`?\`?$cmakeBuildDir`"" 2>$null
      if (Test-Path $cmakeBuildDir) { Remove-Item $cmakeBuildDir -Recurse -Force -ErrorAction SilentlyContinue }
    }
    if (Test-Path $exe) { Remove-Item $exe -Force }
    Write-Ok "Clean done."
  }

  if ($InstallDeps) {
    & (Join-Path $PSScriptRoot "Sim-Deps.ps1") -EnsureBuildTools
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  }

  Write-Step "Locating Visual Studio C++ tools..."
  $script:vcvars = Resolve-VcVars64
  if (!$script:vcvars -or !(Test-Path $script:vcvars)) {
    Write-Fatal "vcvars64.bat not found. Run with -InstallDeps or install Visual Studio C++ build tools."
  }
  Write-Ok "Found: $script:vcvars"

  Write-Step "Initializing VS build environment..."
  Import-VsEnvironment $script:vcvars

  $cl = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
  if (!$cl) { Write-Fatal "cl.exe not found after VS env init." }
  Write-Ok "Compiler : $cl"

  if (!(Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    Write-Fatal "cmake.exe not found. Run with -InstallDeps or install CMake."
  }

  $exeProcessName = [IO.Path]::GetFileNameWithoutExtension($exe)
  $watchdogs = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
    $_.ProcessId -ne $PID -and
    $_.CommandLine -and
    $_.CommandLine.IndexOf($watchdogFile, [StringComparison]::OrdinalIgnoreCase) -ge 0
  }
  if ($watchdogs) {
    Write-Warn "Terminating simulator watchdog..."
    foreach ($watchdog in $watchdogs) {
      try { Stop-Process -Id $watchdog.ProcessId -Force -ErrorAction Stop } catch {}
    }
  }
  $running = Get-Process -Name $exeProcessName -ErrorAction SilentlyContinue |
    Where-Object {
    try {
      $_.Path -and [StringComparer]::OrdinalIgnoreCase.Equals($_.Path, $exe)
    } catch {
      $false
    }
  }
  if ($running) {
    Write-Warn "Terminating running simulator (PID $($running.Id))..."
    $running | Stop-Process -Force
    try { $running | Wait-Process -Timeout 10 -ErrorAction Stop } catch {}
    Write-Ok "Simulator stopped."
  }

  $ninjaPath = $null
  if (Get-Command ninja.exe -ErrorAction SilentlyContinue) {
    $ninjaPath = (Get-Command ninja.exe).Source
  } else {
    $vsRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $script:vcvars)))
    $vsNinja = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if (Test-Path $vsNinja) { $ninjaPath = $vsNinja }
  }

  $env:VSLANG = "1033"

  $cmakeArgs = @(
    "-S", $simDir,
    "-B", $cmakeBuildDir,
    "-DSIM_DEBUG=$simDebug",
    "-DCMAKE_BUILD_TYPE=$cmakeConfig",
    "-DCMAKE_CXX_COMPILER=$cl"
  )
  if ($ninjaPath) {
    $cmakeArgs += @("-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=$(ConvertTo-CMakePath $ninjaPath)")
  }

  Write-Step "Configuring simulator..."
  Write-Host ("  cmake " + ($cmakeArgs -join ' ')) -ForegroundColor DarkGray
  $configureExitCode = Invoke-CMakeConfigureWithRetry $cmakeArgs $cmakeBuildDir
  if ($configureExitCode -ne 0) { Write-Fatal "CMake configure failed." }

  Write-Step "Building simulator..."
  $buildExitCode = Invoke-CMakeBuild $cmakeBuildDir $cmakeConfig $Jobs
  if ($buildExitCode -ne 0) { Write-Fatal "CMake build failed." }

  $buildStart.Stop()
  $elapsed = $buildStart.Elapsed
  Write-Host ""
  Write-Host "  done in $($elapsed.ToString('mm\:ss\.f'))  ->  $exe" -ForegroundColor DarkGray
  Write-Host ""

  if (!$NoRun) {
    Write-Host "  launching..." -ForegroundColor DarkGray

    $cmakeExe = (Get-Command cmake.exe).Source
    $rebuildLines = [string[]]@(
      "@echo off",
      ('call "' + $script:vcvars + '" >nul 2>&1'),
      ('"' + $cmakeExe + '" --build "' + $cmakeBuildDir + '" --config ' + $cmakeConfig + ' --parallel ' + $Jobs)
    )
    [IO.File]::WriteAllLines($rebuildCmd, $rebuildLines, [Text.Encoding]::ASCII)

    $watchdogLines = [string[]]@(
      '$ErrorActionPreference = "Stop"',
      ('$exe = ' + "'" + ($exe -replace "'", "''") + "'"),
      ('$wd = ' + "'" + ($buildDir -replace "'", "''") + "'"),
      ('$log = ' + "'" + ($watchdogLog -replace "'", "''") + "'"),
      ('$runtimeOutLog = ' + "'" + ($runtimeOutLog -replace "'", "''") + "'"),
      ('$runtimeErrLog = ' + "'" + ($runtimeErrLog -replace "'", "''") + "'"),
      ('$rebuildCmd = ' + "'" + ($rebuildCmd -replace "'", "''") + "'"),
      ('$env:PIPSIM_WORKDIR = ' + "'" + ($buildDir -replace "'", "''") + "'"),
      '$env:PIPSIM_LAST_EXIT = ""',
      '$userExit = Join-Path $wd "_sim-user-exit"',
      '$restart = Join-Path $wd "_sim-restart"',
      '$rebuild = Join-Path $wd "_sim-rebuild"',
      'Remove-Item $userExit, $restart, $rebuild -Force -ErrorAction SilentlyContinue',
      'while ($true) {',
      '    if (Test-Path $rebuild) {',
      '        Remove-Item $rebuild -Force -ErrorAction SilentlyContinue',
      '        Add-Content -Path $log -Value "rebuilding simulator (folders changed)..."',
      '        $null = & cmd.exe /d /s /c "`"$rebuildCmd`""',
      '        if ($LASTEXITCODE -ne 0) { Add-Content -Path $log -Value "rebuild failed with code $LASTEXITCODE" }',
      '    }',
      '    $p = Start-Process -FilePath $exe -WorkingDirectory $wd -WindowStyle Normal -RedirectStandardOutput $runtimeOutLog -RedirectStandardError $runtimeErrLog -PassThru -Wait',
      '    if (Test-Path $userExit) { Remove-Item $userExit -Force -ErrorAction SilentlyContinue; break }',
      '    if (Test-Path $restart) { Remove-Item $restart -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 150; continue }',
      '    if ($p.ExitCode -eq 0) { break }',
      '    $msg = "previous simulator exited with code {0}; restarted at {1:yyyy-MM-dd HH:mm:ss}" -f $p.ExitCode, (Get-Date)',
      '    Add-Content -Path $log -Value $msg',
      '    $env:PIPSIM_LAST_EXIT = $msg',
      '    Start-Sleep -Milliseconds 500',
      '}'
    )
    [IO.File]::WriteAllLines($watchdogFile, $watchdogLines, [Text.Encoding]::ASCII)
    Start-Process -FilePath "powershell.exe" `
      -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $watchdogFile) `
      -WorkingDirectory $buildDir `
      -WindowStyle Hidden | Out-Null
  }
