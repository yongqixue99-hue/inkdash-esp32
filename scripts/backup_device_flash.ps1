[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,
    [ValidateSet(115200, 230400, 460800)]
    [int]$Baud = 115200,
    [string]$Python = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$esptool = @(
    (Join-Path $projectRoot '.pio-home\packages\tool-esptoolpy\esptool.py'),
    (Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
$otaInspector = Join-Path $projectRoot 'tools\inspect_ota_boot.py'

if ([string]::IsNullOrWhiteSpace($esptool)) {
    throw 'esptool.py is missing. Install PlatformIO and build the project first.'
}
if (-not (Test-Path -LiteralPath $otaInspector -PathType Leaf)) {
    throw "OTA inspector is missing: $otaInspector"
}
if ([string]::IsNullOrWhiteSpace($Python)) {
    $Python = (Get-Command python -ErrorAction Stop).Source
}

$serialPort = Get-CimInstance Win32_SerialPort |
    Where-Object { $_.DeviceID -ieq $Port } |
    Select-Object -First 1
if ($null -eq $serialPort) {
    throw "$Port is not a currently detected Windows serial port. No read or write was attempted."
}
if ($serialPort.PNPDeviceID -notmatch 'VID_303A') {
    throw "$Port is not identified as the board's native Espressif USB device (VID_303A). No read or write was attempted."
}
Write-Host "Confirmed Espressif device: $($serialPort.Name) [$($serialPort.PNPDeviceID)]"

function Invoke-EsptoolChecked {
    param(
        [Parameter(Mandatory)]
        [string[]]$EsptoolArgs
    )

    $lines = & $Python $esptool --chip esp32c3 --port $Port @EsptoolArgs 2>&1
    $result = $LASTEXITCODE
    $lines | ForEach-Object { Write-Host $_ }
    if ($result -ne 0) {
        throw "esptool failed with exit code $result."
    }
    return ($lines -join "`n")
}

function Get-FileSliceSha256 {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [long]$Offset,
        [Parameter(Mandatory)]
        [int]$Length
    )

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        [void]$stream.Seek($Offset, [System.IO.SeekOrigin]::Begin)
        $buffer = [byte[]]::new($Length)
        $total = 0
        while ($total -lt $Length) {
            $count = $stream.Read($buffer, $total, $Length - $total)
            if ($count -eq 0) {
                throw "Unexpected end of backup while reading offset 0x$($Offset.ToString('X'))."
            }
            $total += $count
        }
    }
    finally {
        $stream.Dispose()
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($buffer)).Replace('-', '')
    }
    finally {
        $sha.Dispose()
    }
}

$chipInfo = Invoke-EsptoolChecked -EsptoolArgs @('--after', 'no_reset', 'chip_id')
$macMatch = [regex]::Match($chipInfo, '(?im)^MAC:\s*([0-9a-f:]{17})\s*$')
if (-not $macMatch.Success) {
    throw 'Could not read a stable ESP32-C3 MAC address. No flash backup was started.'
}
$deviceMac = $macMatch.Groups[1].Value.ToLowerInvariant()

$flashInfo = Invoke-EsptoolChecked -EsptoolArgs @('--after', 'no_reset', 'flash_id')
if ($flashInfo -notmatch '(?im)Detected flash size:\s*4MB') {
    throw 'The detected flash is not the expected 4 MB device. No flash backup was started.'
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDir = Join-Path $projectRoot "backups\$timestamp-$($Port.ToUpperInvariant())"
New-Item -ItemType Directory -Path $backupDir | Out-Null
$partialPath = Join-Path $backupDir 'original-flash-4mb.bin.partial'
$backupPath = Join-Path $backupDir 'original-flash-4mb.bin'
$chunkDir = Join-Path $backupDir 'read-chunks'
New-Item -ItemType Directory -Path $chunkDir | Out-Null
$chunkBytes = 0x10000
$flashBytes = 0x400000
$chunkCount = [int]($flashBytes / $chunkBytes)

# Native USB on this board can occasionally truncate one long SLIP transfer.
# Read independent 64 KiB windows so a transient error retries only that window,
# while every accepted chunk still comes from a successful esptool transaction.
for ($chunkIndex = 0; $chunkIndex -lt $chunkCount; $chunkIndex++) {
    $offset = $chunkIndex * $chunkBytes
    $chunkPath = Join-Path $chunkDir ('chunk-{0:D2}-0x{1:X6}.bin' -f $chunkIndex, $offset)
    $chunkRead = $false
    for ($attempt = 1; $attempt -le 6; $attempt++) {
        $attemptPath = Join-Path $chunkDir `
            ('chunk-{0:D2}-attempt-{1}.partial' -f $chunkIndex, $attempt)
        try {
            Invoke-EsptoolChecked -EsptoolArgs @(
                '--after', 'no_reset',
                '--baud', $Baud.ToString(),
                'read_flash', ('0x{0:X}' -f $offset), ('0x{0:X}' -f $chunkBytes),
                $attemptPath
            ) | Out-Null
            $attemptFile = Get-Item -LiteralPath $attemptPath
            if ($attemptFile.Length -ne $chunkBytes) {
                throw "Chunk length is $($attemptFile.Length), expected $chunkBytes."
            }
            Move-Item -LiteralPath $attemptPath -Destination $chunkPath
            $chunkRead = $true
            Write-Host ('Backup read {0}/{1}: offset 0x{2:X6}' -f `
                    ($chunkIndex + 1), $chunkCount, $offset)
            break
        }
        catch {
            Write-Warning ('Chunk {0}/{1} attempt {2}/6 failed: {3}' -f `
                    ($chunkIndex + 1), $chunkCount, $attempt, $_.Exception.Message)
        }
    }
    if (-not $chunkRead) {
        throw ('Could not read flash chunk {0}/{1} at offset 0x{2:X6}. ' +
            'All completed chunks and failed-attempt artifacts were retained; nothing was written.') -f `
            ($chunkIndex + 1), $chunkCount, $offset
    }
}

$outputStream = [System.IO.File]::Create($partialPath)
try {
    for ($chunkIndex = 0; $chunkIndex -lt $chunkCount; $chunkIndex++) {
        $offset = $chunkIndex * $chunkBytes
        $chunkPath = Join-Path $chunkDir ('chunk-{0:D2}-0x{1:X6}.bin' -f $chunkIndex, $offset)
        $inputStream = [System.IO.File]::OpenRead($chunkPath)
        try {
            $inputStream.CopyTo($outputStream)
        }
        finally {
            $inputStream.Dispose()
        }
    }
}
finally {
    $outputStream.Dispose()
}

$backupFile = Get-Item -LiteralPath $partialPath
if ($backupFile.Length -ne 4194304) {
    throw "Backup length is $($backupFile.Length), expected 4194304 bytes. The partial file was retained for diagnosis."
}
Move-Item -LiteralPath $partialPath -Destination $backupPath

$expectedPartitionHash = '57A85AFF36BEB5896CDEA3E9D7039CE13B97463398D80EDFE799D7EF70B09B01'
$devicePartitionHash = Get-FileSliceSha256 `
    -Path $backupPath -Offset 0x8000 -Length 3072
if ($devicePartitionHash -ine $expectedPartitionHash) {
    throw "Device partition hash $devicePartitionHash does not match the reference layout. The complete backup was retained, but this device will not be flashed."
}
$otaJson = & $Python $otaInspector $backupPath 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Could not determine the device's active OTA slot: $($otaJson -join ' ')"
}
$otaSelection = ($otaJson -join "`n") | ConvertFrom-Json

Invoke-EsptoolChecked -EsptoolArgs @(
    '--after', 'no_reset',
    '--baud', $Baud.ToString(),
    'verify_flash', '0x0', $backupPath
) | Out-Null

$sha256 = (Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash
$metadata = [ordered]@{
    schema_version = 1
    created_at = (Get-Date).ToString('o')
    device_mac = $deviceMac
    serial_port = $Port.ToUpperInvariant()
    pnp_device_id = $serialPort.PNPDeviceID
    flash_bytes = 4194304
    sha256 = $sha256
    backup_file = 'original-flash-4mb.bin'
    verified_against_device = $true
    allowed_app_offsets = @('0x10000', '0x160000')
    reference_partition_sha256 = $devicePartitionHash
    active_ota_slot = $otaSelection.slot
    active_app_offset = $otaSelection.offset
    ota_selection_reason = $otaSelection.reason
}
$metadata | ConvertTo-Json | Set-Content `
    -LiteralPath (Join-Path $backupDir 'metadata.json') `
    -Encoding utf8

Write-Host "Verified full-flash backup: $backupPath"
Write-Host "Device MAC: $deviceMac"
Write-Host "SHA256: $sha256"
[pscustomobject]@{
    BackupPath = $backupPath
    MetadataPath = (Join-Path $backupDir 'metadata.json')
    DeviceMac = $deviceMac
    Sha256 = $sha256
    ActiveOtaSlot = $otaSelection.slot
    ActiveAppOffset = $otaSelection.offset
}
