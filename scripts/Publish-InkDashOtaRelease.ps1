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
    [string]$FirmwarePath = '',
    [string]$PrivateKeyPath = '',
    [string]$OutputRoot = '',
    [string]$PythonPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($FirmwarePath)) {
    $FirmwarePath = Join-Path $projectRoot '.pio\build\esp32c3-ink-dashboard\firmware.bin'
}
if ([string]::IsNullOrWhiteSpace($PrivateKeyPath)) {
    $PrivateKeyPath = Join-Path $projectRoot 'artifacts\private\inkdash-ota-p256-private.pk8'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot 'artifacts\ota\repository'
}
if ([string]::IsNullOrWhiteSpace($PythonPath)) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($null -eq $pythonCommand) {
        throw 'Python is required to validate the ESP32-C3 firmware image.'
    }
    $PythonPath = $pythonCommand.Source
}
$FirmwarePath = (Resolve-Path -LiteralPath $FirmwarePath).Path
$PrivateKeyPath = (Resolve-Path -LiteralPath $PrivateKeyPath).Path
$PythonPath = (Resolve-Path -LiteralPath $PythonPath).Path
$firmware = Get-Item -LiteralPath $FirmwarePath
if ($firmware.Length -lt 65536 -or $firmware.Length -gt 0x150000) {
    throw "Firmware size $($firmware.Length) is outside the OTA slot limits."
}
$expectedReleaseMarker =
    "INKDASH_RELEASE_V1|inkdash-esp32c3-75-bwr-v1|$VersionCode|$Version"
$firmwareBytes = [IO.File]::ReadAllBytes($FirmwarePath)
try {
    $firmwareText = [Text.Encoding]::ASCII.GetString($firmwareBytes)
    $firstMarker = $firmwareText.IndexOf($expectedReleaseMarker, [StringComparison]::Ordinal)
    $secondMarker = if ($firstMarker -ge 0) {
        $firmwareText.IndexOf(
            $expectedReleaseMarker,
            $firstMarker + $expectedReleaseMarker.Length,
            [StringComparison]::Ordinal
        )
    } else { -1 }
    if ($firstMarker -lt 0 -or $secondMarker -ge 0) {
        throw 'Firmware release identity does not exactly match the requested version.'
    }
}
finally {
    [Array]::Clear($firmwareBytes, 0, $firmwareBytes.Length)
}
$esptoolPath = @(
    (Join-Path $projectRoot '.pio-home\packages\tool-esptoolpy\esptool.py'),
    (Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($esptoolPath)) {
    throw 'esptool.py is missing. Build the project with PlatformIO first.'
}
$esptoolPath = (Resolve-Path -LiteralPath $esptoolPath).Path
$imageInfo = & $PythonPath $esptoolPath --chip esp32c3 image_info $FirmwarePath 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $imageInfo -notmatch 'Checksum:.*valid' -or
    $imageInfo -notmatch 'Validation Hash:.*valid') {
    throw 'Firmware is not a valid ESP32-C3 application image.'
}

$channelDirectory = Join-Path $OutputRoot $Channel
$releaseDirectory = Join-Path $channelDirectory $VersionCode.ToString()
[IO.Directory]::CreateDirectory($releaseDirectory) | Out-Null
$releaseFirmware = Join-Path $releaseDirectory 'firmware.bin'
if (Test-Path -LiteralPath $releaseFirmware -PathType Leaf) {
    $existingHash = (Get-FileHash -LiteralPath $releaseFirmware -Algorithm SHA256).Hash
    $candidateHash = (Get-FileHash -LiteralPath $FirmwarePath -Algorithm SHA256).Hash
    if ($existingHash -ne $candidateHash) {
        throw "Version code $VersionCode already identifies a different immutable binary."
    }
}
Copy-Item -LiteralPath $FirmwarePath -Destination $releaseFirmware -Force
$sha256 = (Get-FileHash -LiteralPath $releaseFirmware -Algorithm SHA256).Hash.ToLowerInvariant()
$url = "$($BaseUrl.TrimEnd('/'))/$Channel/$VersionCode/firmware.bin"

$payloadObject = [ordered]@{
    schema_version = 1
    hardware = 'inkdash-esp32c3-75-bwr-v1'
    channel = $Channel
    version_code = $VersionCode
    version = $Version
    size = $firmware.Length
    sha256 = $sha256
    url = $url
}
$payloadJson = $payloadObject | ConvertTo-Json -Compress
$payloadBytes = [Text.UTF8Encoding]::new($false).GetBytes($payloadJson)
$privateBytes = [IO.File]::ReadAllBytes($PrivateKeyPath)
$key = [Security.Cryptography.ECDsa]::Create()
try {
    $bytesRead = 0
    $key.ImportPkcs8PrivateKey($privateBytes, [ref]$bytesRead)
    if ($bytesRead -ne $privateBytes.Length) {
        throw 'OTA private key contains trailing data.'
    }
    $publicBytes = $key.ExportSubjectPublicKeyInfo()
    $keyId = ([Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($publicBytes)
    )).Substring(0, 16).ToLowerInvariant()
    $signature = $key.SignData(
        $payloadBytes,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.DSASignatureFormat]::Rfc3279DerSequence
    )
    $manifest = [ordered]@{
        schema_version = 1
        algorithm = 'ecdsa-p256-sha256'
        key_id = $keyId
        payload = [Convert]::ToBase64String($payloadBytes)
        signature = [Convert]::ToBase64String($signature)
    } | ConvertTo-Json -Compress
    $manifestPath = Join-Path $channelDirectory 'manifest.json'
    $temporaryManifest = "$manifestPath.new"
    [IO.Directory]::CreateDirectory($channelDirectory) | Out-Null
    [IO.File]::WriteAllText(
        $temporaryManifest,
        $manifest + "`n",
        [Text.UTF8Encoding]::new($false)
    )
    Move-Item -LiteralPath $temporaryManifest -Destination $manifestPath -Force
    $releaseRecord = [ordered]@{
        schema_version = 1
        version_code = $VersionCode
        size = $firmware.Length
        sha256 = $sha256
    } | ConvertTo-Json -Compress
    [IO.File]::WriteAllText(
        (Join-Path $releaseDirectory 'release.json'),
        $releaseRecord + "`n",
        [Text.UTF8Encoding]::new($false)
    )
    $releaseMetadata = [ordered]@{
        version_code = $VersionCode
        version = $Version
        channel = $Channel
        bytes = $firmware.Length
        sha256 = $sha256
        key_id = $keyId
        manifest = $manifestPath
        firmware = $releaseFirmware
    }
    $releaseMetadata | ConvertTo-Json | Write-Output
}
finally {
    [Array]::Clear($privateBytes, 0, $privateBytes.Length)
    $key.Dispose()
}
