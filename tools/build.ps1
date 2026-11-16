param(
    [string]$ZigPath = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $projectRoot 'src\windows'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'dist\豆泥日历' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$toolRoot = Join-Path $env:TEMP 'desktop-calendar-toolchain'

if (-not $ZigPath) {
    $zigCommand = Get-Command zig.exe -ErrorAction SilentlyContinue
    if ($zigCommand) { $ZigPath = $zigCommand.Source }
    else { $ZigPath = Join-Path $toolRoot 'zig-x86_64-windows-0.15.2\zig.exe' }
}

if (-not (Test-Path -LiteralPath $ZigPath -PathType Leaf)) {
    New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
    Write-Host 'Downloading the portable Zig compiler from ziglang.org ...'
    $index = Invoke-RestMethod -Uri 'https://ziglang.org/download/index.json'
    $package = $index.'0.15.2'.'x86_64-windows'
    if (-not $package.tarball -or -not $package.shasum) { throw 'Official Zig package metadata is missing.' }
    $archivePath = Join-Path $toolRoot 'zig-x86_64-windows-0.15.2.zip'
    Invoke-WebRequest -Uri $package.tarball -OutFile $archivePath
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $package.shasum) {
        throw 'Zig archive SHA-256 mismatch. The downloaded compiler will not be used.'
    }
    Expand-Archive -LiteralPath $archivePath -DestinationPath $toolRoot -Force
    $ZigPath = Join-Path $toolRoot 'zig-x86_64-windows-0.15.2\zig.exe'
}

$buildDirectory = Join-Path $env:TEMP ('desktop-calendar-build-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$resourcePath = Join-Path $buildDirectory 'app.res'
$executablePath = Join-Path $OutputDirectory 'DouNiCalendar.exe'
$compiledExecutable = Join-Path $buildDirectory 'DouNiCalendar.exe'
$stagedExecutable = Join-Path $OutputDirectory ('DouNiCalendar.exe.tmp-' + [Guid]::NewGuid().ToString('N'))
$previousGlobalCache = $env:ZIG_GLOBAL_CACHE_DIR
$previousLocalCache = $env:ZIG_LOCAL_CACHE_DIR
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $toolRoot 'cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $buildDirectory 'cache'

Push-Location $sourceDirectory
try {
    & $ZigPath rc /c65001 /fo $resourcePath app.rc
    if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed ($LASTEXITCODE)." }
    $arguments = @(
        'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-Os', '-s',
        '-static', '-Wl,--subsystem,windows', '-municode', '-DUNICODE', '-D_UNICODE',
        '-DWINVER=0x0a00', '-D_WIN32_WINNT=0x0a00',
        'main.cpp', 'storage.cpp', 'dates.cpp', 'inline_editor.cpp', $resourcePath,
        '-luser32', '-lgdi32', '-lgdiplus', '-lshell32', '-ladvapi32',
        '-lole32', '-limm32', '-o', $compiledExecutable
    )
    & $ZigPath @arguments
    if ($LASTEXITCODE -ne 0) { throw "C++ compilation failed ($LASTEXITCODE)." }
    $executableBytes = [IO.File]::ReadAllBytes($compiledExecutable)
    $peOffset = [BitConverter]::ToInt32($executableBytes, 0x3c)
    $subsystem = [BitConverter]::ToUInt16($executableBytes, $peOffset + 24 + 68)
    if ($subsystem -ne 2) { throw 'The executable must use the Windows GUI subsystem.' }
    try {
        [IO.File]::Copy($compiledExecutable, $stagedExecutable, $false)
        if (Test-Path -LiteralPath $executablePath -PathType Leaf) {
            [IO.File]::Replace($stagedExecutable, $executablePath, [NullString]::Value)
        }
        else {
            [IO.File]::Move($stagedExecutable, $executablePath)
        }
    }
    catch {
        throw ("The new executable compiled successfully, but could not replace '$executablePath'. " +
               "Exit the calendar from its tray menu, check folder write permissions, and run the build again. " +
               "The existing executable was not deliberately deleted. Details: " + $_.Exception.Message)
    }
    Get-Item -LiteralPath $executablePath | Select-Object FullName, Length
}
finally {
    Pop-Location
    $env:ZIG_GLOBAL_CACHE_DIR = $previousGlobalCache
    $env:ZIG_LOCAL_CACHE_DIR = $previousLocalCache
    if (Test-Path -LiteralPath $stagedExecutable -PathType Leaf) {
        Remove-Item -LiteralPath $stagedExecutable -Force
    }
    $resolvedBuild = [IO.Path]::GetFullPath($buildDirectory)
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if ($resolvedBuild.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedBuild).StartsWith('desktop-calendar-build-')) {
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
}
