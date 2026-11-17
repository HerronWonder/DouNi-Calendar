param(
    [string]$IsccPath = '',
    [string]$InputDirectory = '',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string]$Version = '1.0.1',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $InputDirectory) { $InputDirectory = Join-Path $projectRoot 'dist\豆泥日历' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'release' }
$InputDirectory = [IO.Path]::GetFullPath($InputDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if (-not $IsccPath) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compiler) { $IsccPath = $compiler.Source }
    else {
        $candidates = @(
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
        )
        $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }
}
if (-not $IsccPath) { throw 'Inno Setup 6 was not found. Pass -IsccPath with the path to ISCC.exe.' }

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
& $IsccPath '/Qp' "/DAppVersion=$Version" "/DInputDirectory=$InputDirectory" "/DReleaseDirectory=$OutputDirectory" (Join-Path $PSScriptRoot 'windows-installer.iss')
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed ($LASTEXITCODE)." }
Get-Item -LiteralPath (Join-Path $OutputDirectory "DouNiCalendar-$Version-Windows-x64-Setup.exe") | Select-Object FullName, Length
