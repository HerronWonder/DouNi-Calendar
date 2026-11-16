param(
    [string]$InputDirectory = '',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:[-.][A-Za-z0-9.-]+)?$')]
    [string]$Version = '1.0.0',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $InputDirectory) { $InputDirectory = Join-Path $projectRoot 'dist\豆泥日历' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'release' }
$InputDirectory = [IO.Path]::GetFullPath($InputDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$executable = Join-Path $InputDirectory 'DouNiCalendar.exe'
$help = Join-Path $projectRoot 'docs\使用说明-Windows.txt'
foreach ($file in @($executable, $help)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required package file is missing: $file" }
}

$stage = Join-Path $env:TEMP ('douni-calendar-package-' + [Guid]::NewGuid().ToString('N'))
$packageFolder = Join-Path $stage '豆泥日历'
New-Item -ItemType Directory -Path $packageFolder -Force | Out-Null
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$archive = Join-Path $OutputDirectory ("DouNiCalendar-$Version-Windows-x64.zip")
try {
    # An explicit allowlist excludes every user setting, note and backup.
    Copy-Item -LiteralPath $executable -Destination (Join-Path $packageFolder 'DouNiCalendar.exe')
    Copy-Item -LiteralPath $help -Destination (Join-Path $packageFolder '使用说明.txt')
    Compress-Archive -LiteralPath $packageFolder -DestinationPath $archive -CompressionLevel Optimal -Force
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $actual = @($zip.Entries | Where-Object { $_.Name } | ForEach-Object { $_.FullName.Replace('\', '/') } | Sort-Object)
        $expected = @('豆泥日历/DouNiCalendar.exe', '豆泥日历/使用说明.txt') | Sort-Object
        if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
            throw 'The release archive contains unexpected files.'
        }
    }
    finally { $zip.Dispose() }
    Get-Item -LiteralPath $archive | Select-Object FullName, Length
    Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Select-Object Algorithm, Hash
}
finally {
    $resolved = [IO.Path]::GetFullPath($stage)
    $temporaryRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if ($resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved).StartsWith('douni-calendar-package-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
