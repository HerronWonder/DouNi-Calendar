param(
    [string]$ZigPath = '',
    [ValidateSet('all', 'storage_tests', 'ui_tests')][string]$Suite = 'all'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $env:TEMP 'desktop-calendar-toolchain'
if (-not $ZigPath) {
    $zigCommand = Get-Command zig.exe -ErrorAction SilentlyContinue
    if ($zigCommand) { $ZigPath = $zigCommand.Source }
    else { $ZigPath = Join-Path $toolRoot 'zig-x86_64-windows-0.15.2\zig.exe' }
}
if (-not (Test-Path -LiteralPath $ZigPath -PathType Leaf)) {
    throw 'Zig was not found. Run tools/build.ps1 first or pass -ZigPath with the compiler path.'
}

$testDirectory = Join-Path $env:TEMP ('desktop-calendar-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
$previousGlobalCache = $env:ZIG_GLOBAL_CACHE_DIR
$previousLocalCache = $env:ZIG_LOCAL_CACHE_DIR
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $toolRoot 'cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $testDirectory 'cache'
$passed = 0
$skipped = 0

Push-Location $projectRoot
try {
    $resourcePath = Join-Path $testDirectory 'app.res'
    Push-Location (Join-Path $projectRoot 'src\windows')
    try {
        & $ZigPath rc /c65001 /fo $resourcePath app.rc
        if ($LASTEXITCODE -ne 0) { throw "Test resource compilation failed ($LASTEXITCODE)." }
    }
    finally {
        Pop-Location
    }
    $suiteNames = if ($Suite -eq 'all') { @('storage_tests', 'ui_tests') } else { @($Suite) }
    foreach ($testName in $suiteNames) {
        $testSource = Join-Path $projectRoot ('tests\' + $testName + '.cpp')
        if (-not (Test-Path -LiteralPath $testSource -PathType Leaf)) {
            Write-Host "SKIP $testName : source file is not present."
            ++$skipped
            continue
        }

        $testExecutable = Join-Path $testDirectory ($testName + '.exe')
        # ui_tests includes the editor implementation so its test windows can
        # suppress display and activation without changing release code.
        $arguments = @(
            'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-O2', '-s',
            '-static', '-DUNICODE', '-D_UNICODE',
            '-DWINVER=0x0a00', '-D_WIN32_WINNT=0x0a00',
            $testSource, (Join-Path $projectRoot 'src\windows\storage.cpp'),
            (Join-Path $projectRoot 'src\windows\dates.cpp'), $resourcePath,
            '-luser32', '-lgdi32', '-lgdiplus', '-lshell32', '-ladvapi32',
            '-lole32', '-limm32', '-o', $testExecutable
        )
        Write-Host "BUILD $testName"
        & $ZigPath @arguments
        if ($LASTEXITCODE -ne 0) { throw "$testName compilation failed ($LASTEXITCODE)." }
        Write-Host "RUN $testName"
        & $testExecutable
        if ($LASTEXITCODE -ne 0) { throw "$testName failed (exit code $LASTEXITCODE)." }
        Write-Host "PASS $testName"
        ++$passed
    }
    if ($passed -eq 0) { throw 'No test source files were found; no tests were run.' }
    Write-Host "Completed: $passed test suite(s) passed; $skipped suite(s) skipped."
}
finally {
    Pop-Location
    $env:ZIG_GLOBAL_CACHE_DIR = $previousGlobalCache
    $env:ZIG_LOCAL_CACHE_DIR = $previousLocalCache
    $resolvedTest = [IO.Path]::GetFullPath($testDirectory)
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if ($resolvedTest.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedTest).StartsWith('desktop-calendar-tests-')) {
        Remove-Item -LiteralPath $resolvedTest -Recurse -Force
    }
}
