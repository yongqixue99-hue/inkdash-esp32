[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateRange(1, 4294967295)]
    [uint64]$VersionCode,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z._-]{0,31}$')]
    [string]$Version,
    [ValidateSet('test', 'stable')]
    [string]$Channel = 'test',
    [Parameter(Mandatory)]
    [ValidatePattern('^https?://')]
    [string]$BaseUrl,
    [switch]$RollbackProbe,
    [switch]$CacheProbe,
    [string]$PioPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($RollbackProbe -and $CacheProbe) {
    throw 'RollbackProbe and CacheProbe are mutually exclusive.'
}
if ([string]::IsNullOrWhiteSpace($PioPath)) {
    $command = Get-Command pio -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $PioPath = $command.Source
    } else { throw 'PlatformIO CLI (pio) is required.' }
}
$PioPath = (Resolve-Path -LiteralPath $PioPath).Path
$environment = if ($RollbackProbe) {
    'esp32c3-ink-dashboard-rollback-probe'
} elseif ($CacheProbe) {
    'esp32c3-ink-dashboard-cache-probe'
} else {
    'esp32c3-ink-dashboard-release'
}

$previousCore = $env:PLATFORMIO_CORE_DIR
$previousCode = $env:INKDASH_RELEASE_VERSION_CODE
$previousVersion = $env:INKDASH_RELEASE_VERSION
try {
    $env:PLATFORMIO_CORE_DIR = Join-Path $projectRoot '.pio-home'
    $env:INKDASH_RELEASE_VERSION_CODE = $VersionCode.ToString()
    $env:INKDASH_RELEASE_VERSION = $Version
    & $PioPath run -e $environment -d $projectRoot
    if ($LASTEXITCODE -ne 0) {
        throw "PlatformIO release build failed with exit code $LASTEXITCODE."
    }
}
finally {
    if ($null -eq $previousCore) { Remove-Item Env:PLATFORMIO_CORE_DIR -ErrorAction SilentlyContinue }
    else { $env:PLATFORMIO_CORE_DIR = $previousCore }
    if ($null -eq $previousCode) { Remove-Item Env:INKDASH_RELEASE_VERSION_CODE -ErrorAction SilentlyContinue }
    else { $env:INKDASH_RELEASE_VERSION_CODE = $previousCode }
    if ($null -eq $previousVersion) { Remove-Item Env:INKDASH_RELEASE_VERSION -ErrorAction SilentlyContinue }
    else { $env:INKDASH_RELEASE_VERSION = $previousVersion }
}

$firmwarePath = Join-Path $projectRoot ".pio\build\$environment\firmware.bin"
$publish = Join-Path $PSScriptRoot 'Publish-InkDashOtaRelease.ps1'
& $publish -VersionCode $VersionCode -Version $Version -Channel $Channel `
    -FirmwarePath $firmwarePath -BaseUrl $BaseUrl
if ($LASTEXITCODE -ne 0) {
    throw 'The release was built but its signed repository package failed.'
}
