[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,
    [Parameter(Mandatory)]
    [string]$BackupPath,
    [string]$FirmwarePath = '',
    [ValidateSet(115200, 230400, 460800)]
    [int]$Baud = 115200,
    [string]$Python = ''
)

$ErrorActionPreference = 'Stop'
$shouldProcessContext = $PSCmdlet
if ($null -eq $shouldProcessContext) {
    throw 'PowerShell did not provide the required ShouldProcess context. Nothing was written.'
}
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$esptool = @(
    (Join-Path $projectRoot '.pio-home\packages\tool-esptoolpy\esptool.py'),
    (Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
$otaInspector = Join-Path $projectRoot 'tools\inspect_ota_boot.py'
$builtPartition = Join-Path $projectRoot '.pio\build\esp32c3-ink-dashboard\partitions.bin'
$expectedPartitionHash = '57A85AFF36BEB5896CDEA3E9D7039CE13B97463398D80EDFE799D7EF70B09B01'

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

if ([string]::IsNullOrWhiteSpace($FirmwarePath)) {
    $FirmwarePath = Join-Path $projectRoot '.pio\build\esp32c3-ink-dashboard\firmware.bin'
}
$BackupPath = (Resolve-Path -LiteralPath $BackupPath).Path
$FirmwarePath = (Resolve-Path -LiteralPath $FirmwarePath).Path
if ([string]::IsNullOrWhiteSpace($esptool)) {
    throw 'esptool.py is missing. Install PlatformIO and build the project first.'
}
if (-not (Test-Path -LiteralPath $otaInspector -PathType Leaf)) {
    throw "OTA inspector is missing: $otaInspector"
}
if (-not (Test-Path -LiteralPath $builtPartition -PathType Leaf)) {
    throw 'The generated reference partition table is missing. Build the project first.'
}
if ([string]::IsNullOrWhiteSpace($Python)) {
    $Python = (Get-Command python -ErrorAction Stop).Source
}

$serialPort = Get-CimInstance Win32_SerialPort |
    Where-Object { $_.DeviceID -ieq $Port } |
    Select-Object -First 1
if ($null -eq $serialPort) {
    throw "$Port is not a currently detected Windows serial port. Nothing was written."
}
if ($serialPort.PNPDeviceID -notmatch 'VID_303A') {
    throw "$Port is not identified as the board's native Espressif USB device (VID_303A). Nothing was written."
}

$backupFile = Get-Item -LiteralPath $BackupPath
if ($backupFile.Length -ne 4194304) {
    throw "The required original-flash backup is not exactly 4194304 bytes. Nothing was written."
}
$metadataPath = Join-Path $backupFile.DirectoryName 'metadata.json'
if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
    throw 'Backup metadata.json is missing. Nothing was written.'
}
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$backupHash = (Get-FileHash -LiteralPath $BackupPath -Algorithm SHA256).Hash
if ($backupHash -ine $metadata.sha256 -or -not $metadata.verified_against_device) {
    throw 'The full-flash backup hash/verification metadata is invalid. Nothing was written.'
}
$backupPartitionHash = Get-FileSliceSha256 `
    -Path $BackupPath -Offset 0x8000 -Length 3072
if ($backupPartitionHash -ine $expectedPartitionHash -or
    $backupPartitionHash -ine $metadata.reference_partition_sha256) {
    throw 'The physical-device backup does not contain the exact reference partition table. Nothing was written.'
}
$otaJson = & $Python $otaInspector $BackupPath 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Could not determine the active OTA slot from the verified backup. Nothing was written. $($otaJson -join ' ')"
}
$otaSelection = ($otaJson -join "`n") | ConvertFrom-Json
$appOffset = [string]$otaSelection.offset
$appSlot = [string]$otaSelection.slot
if ($appOffset -notin @('0x10000', '0x160000') -or
    $appSlot -notin @('ota_0', 'ota_1')) {
    throw 'OTA inspector returned an unsupported application target. Nothing was written.'
}

$partitionHash = (Get-FileHash -LiteralPath $builtPartition -Algorithm SHA256).Hash
if ($partitionHash -ine $expectedPartitionHash) {
    throw 'The build partition table is not byte-identical to the reference table. Nothing was written.'
}
$firmwareFile = Get-Item -LiteralPath $FirmwarePath
$maxFirmwareLength = 0x14F000
if ($firmwareFile.Length -le 0 -or $firmwareFile.Length -gt $maxFirmwareLength) {
    throw "Firmware length $($firmwareFile.Length) overlaps the reserved 4 KiB Wi-Fi configuration sector. Nothing was written."
}

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

$chipInfo = Invoke-EsptoolChecked -EsptoolArgs @(
    '--after', 'no_reset', '--no-stub', 'chip_id'
)
$macMatch = [regex]::Match($chipInfo, '(?im)^MAC:\s*([0-9a-f:]{17})\s*$')
if (-not $macMatch.Success) {
    throw 'Could not read the ESP32-C3 MAC address. Nothing was written.'
}
$deviceMac = $macMatch.Groups[1].Value.ToLowerInvariant()
if ($deviceMac -ine $metadata.device_mac) {
    throw "Connected device MAC $deviceMac does not match backup MAC $($metadata.device_mac). Nothing was written."
}

$flashInfo = Invoke-EsptoolChecked -EsptoolArgs @(
    '--after', 'no_reset', '--no-stub', 'flash_id'
)
if ($flashInfo -notmatch '(?im)Detected flash size:\s*4MB') {
    throw 'The detected flash is not the expected 4 MB device. Nothing was written.'
}

$imageInfo = & $Python $esptool --chip esp32c3 image_info --version 2 $FirmwarePath 2>&1
$imageInfoResult = $LASTEXITCODE
$imageInfo | ForEach-Object { Write-Host $_ }
if ($imageInfoResult -ne 0 -or
    ($imageInfo -join "`n") -notmatch '(?s)Flash size:\s*4MB.*Flash freq:\s*80m.*Flash mode:\s*DIO') {
    throw 'Firmware image header is not ESP32-C3 / 4 MB / 80 MHz / DIO. Nothing was written.'
}

$target = "$Port device $deviceMac, active $appSlot only at $appOffset"
$action = "Write and verify $($firmwareFile.Name); preserve bootloader, partition table, NVS, Wi-Fi configuration sector, inactive OTA slot, and SPIFFS"
if (-not $shouldProcessContext.ShouldProcess($target, $action)) {
    return
}

Invoke-EsptoolChecked -EsptoolArgs @(
    '--after', 'no_reset',
    '--no-stub',
    '--baud', $Baud.ToString(),
    'write_flash',
    '--flash_mode', 'dio',
    '--flash_freq', '80m',
    '--flash_size', '4MB',
    $appOffset, $FirmwarePath
) | Out-Null

Invoke-EsptoolChecked -EsptoolArgs @(
    '--after', 'hard_reset',
    '--no-stub',
    '--baud', $Baud.ToString(),
    'verify_flash', $appOffset, $FirmwarePath
) | Out-Null

Write-Host 'Application write and on-device verification succeeded.'
Write-Host "Updated active slot: $appSlot at $appOffset."
Write-Host 'Preserved regions: bootloader, reference partition table, NVS, reserved Wi-Fi configuration sector, inactive OTA slot, and SPIFFS.'
