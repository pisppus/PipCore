param(
    [switch]$NoRun,
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root ".sim"
$objDir = Join-Path $buildDir "obj"
$exe = Join-Path $buildDir "pipgui-sim.exe"
$linkArgFile = Join-Path $buildDir "link-args.rsp"
if ($Debug) {
    $exe = Join-Path $buildDir "pipgui-sim-debug.exe"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$vsDev = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
$cl = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\cl.exe"
if (!(Test-Path $vsDev) -or !(Test-Path $cl)) {
    throw "Visual Studio C++ tools not found. Install Desktop development with C++."
}

$running = Get-Process | Where-Object {
    $_.Path -and ([System.StringComparer]::OrdinalIgnoreCase.Equals($_.Path, $exe))
}
if ($running) {
    $running | Stop-Process -Force
}

$libSources = Get-ChildItem (Join-Path $root "lib\PipKit") -Recurse -Filter *.cpp | Where-Object {
    $_.FullName -notmatch "\\Platforms\\ESP32\\" -and
    $_.FullName -notmatch "\\Displays\\ST7789\\" -and
    $_.FullName -notmatch "\\Displays\\ILI9488\\"
} | Sort-Object FullName

$appSources = Get-ChildItem (Join-Path $root "src") -File -Filter *.cpp | Sort-Object FullName
$simSources = Get-ChildItem (Join-Path $root "simulator\src") -File -Filter *.cpp | Sort-Object FullName
$sources = @($libSources + $appSources + $simSources)

$headerExtensions = @(".h", ".hpp", ".inl")
$headerFiles = Get-ChildItem (Join-Path $root "lib\PipKit"), (Join-Path $root "include"), (Join-Path $root "simulator\include") -Recurse -File |
    Where-Object { $headerExtensions -contains $_.Extension.ToLowerInvariant() }
$globalDeps = @($headerFiles)
$globalDepTicks = 0L
foreach ($dep in $globalDeps) {
    if ($dep.LastWriteTimeUtc.Ticks -gt $globalDepTicks) {
        $globalDepTicks = $dep.LastWriteTimeUtc.Ticks
    }
}

$commonArgs = @(
    "/nologo",
    "/std:c++17",
    "/EHsc",
    "/DWIN32_LEAN_AND_MEAN",
    "/DNOMINMAX",
    "/Isimulator\include",
    "/Iinclude",
    "/Ilib\PipKit"
)

if ($Debug) {
    $commonArgs += @("/Od", "/Zi")
} else {
    $commonArgs += @("/O2")
}

$objectFiles = @()
$rootPrefix = (Resolve-Path $root).Path
$compiledCount = 0
foreach ($src in $sources) {
    $full = $src.FullName
    $relative = $full.Substring($rootPrefix.Length).TrimStart('\')
    $objName = ($relative -replace '[:\\/]', '__' -replace '\.cpp$', '.obj')
    $objPath = Join-Path $objDir $objName
    $objectFiles += $objPath

    $needsCompile = $true
    if (Test-Path $objPath) {
        $objTicks = (Get-Item $objPath).LastWriteTimeUtc.Ticks
        $srcTicks = $src.LastWriteTimeUtc.Ticks
        $needsCompile = ($objTicks -lt $srcTicks) -or ($objTicks -lt $globalDepTicks)
    }

    if ($needsCompile) {
        Write-Host "Compiling $relative"
        $compileCommand = @(
            "call `"$vsDev`" -arch=x64 -host_arch=x64 >nul",
            "&&",
            "`"$cl`"",
            ($commonArgs -join ' '),
            "/c",
            "`"$full`"",
            "/Fo`"$objPath`""
        ) -join ' '
        cmd /c $compileCommand
        if ($LASTEXITCODE -ne 0) {
            throw "Native simulator compile failed."
        }
        $compiledCount++
    }
}

$linkLines = @("/nologo", "/OUT:.sim\pipgui-sim.exe", "/SUBSYSTEM:WINDOWS") + $objectFiles + @("user32.lib", "gdi32.lib", "windowscodecs.lib", "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "ole32.lib")
if ($Debug) {
    $linkLines[1] = "/OUT:.sim\pipgui-sim-debug.exe"
}
Set-Content -Path $linkArgFile -Value ($linkLines | ForEach-Object {
    if ($_ -like '/*') { $_ }
    elseif ($_ -match '\s' -or $_ -match ':') { '"{0}"' -f $_ }
    else { $_ }
})

$needsLink = -not (Test-Path $exe)
if (-not $needsLink) {
    $exeTicks = (Get-Item $exe).LastWriteTimeUtc.Ticks
    foreach ($obj in $objectFiles) {
        if (!(Test-Path $obj) -or ((Get-Item $obj).LastWriteTimeUtc.Ticks -gt $exeTicks)) {
            $needsLink = $true
            break
        }
    }
}

if ($needsLink) {
    Write-Host "Linking $exe"
    $linkCommand = "call `"$vsDev`" -arch=x64 -host_arch=x64 >nul && link @`"$linkArgFile`""
    cmd /c $linkCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Native simulator link failed."
    }
} else {
    Write-Host "Link skipped, executable is up to date."
}

Write-Host "Compiled $compiledCount file(s)."
Write-Host "Built $exe"
if (-not $NoRun) {
    Start-Process -FilePath $exe -WorkingDirectory $buildDir
}
