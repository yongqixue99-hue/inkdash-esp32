[CmdletBinding()]
param(
    [string]$PrivateKeyPath = '',
    [string]$PublicPemPath = '',
    [string]$HeaderPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($PrivateKeyPath)) {
    $PrivateKeyPath = Join-Path $projectRoot 'artifacts\private\inkdash-ota-p256-private.pk8'
}
if ([string]::IsNullOrWhiteSpace($PublicPemPath)) {
    $PublicPemPath = Join-Path $projectRoot 'artifacts\ota\inkdash-ota-p256-public.pem'
}
if ([string]::IsNullOrWhiteSpace($HeaderPath)) {
    $HeaderPath = Join-Path $projectRoot 'include\generated\ota_public_key.h'
}

if (Test-Path -LiteralPath $PrivateKeyPath -PathType Leaf) {
    throw "OTA private key already exists: $PrivateKeyPath"
}

$privateDirectory = Split-Path -Parent $PrivateKeyPath
$publicDirectory = Split-Path -Parent $PublicPemPath
$headerDirectory = Split-Path -Parent $HeaderPath
[IO.Directory]::CreateDirectory($privateDirectory) | Out-Null
[IO.Directory]::CreateDirectory($publicDirectory) | Out-Null
[IO.Directory]::CreateDirectory($headerDirectory) | Out-Null

$key = [Security.Cryptography.ECDsa]::Create(
    [Security.Cryptography.ECCurve+NamedCurves]::nistP256
)
try {
    $privateBytes = $key.ExportPkcs8PrivateKey()
    [IO.File]::WriteAllBytes($PrivateKeyPath, $privateBytes)
    [Array]::Clear($privateBytes, 0, $privateBytes.Length)

    $publicBytes = $key.ExportSubjectPublicKeyInfo()
    $publicBase64 = [Convert]::ToBase64String($publicBytes)
    $publicLines = for ($offset = 0; $offset -lt $publicBase64.Length; $offset += 64) {
        $publicBase64.Substring($offset, [Math]::Min(64, $publicBase64.Length - $offset))
    }
    $publicPem = "-----BEGIN PUBLIC KEY-----`n$($publicLines -join "`n")`n-----END PUBLIC KEY-----`n"
    [IO.File]::WriteAllText(
        $PublicPemPath,
        $publicPem,
        [Text.UTF8Encoding]::new($false)
    )

    $keyIdBytes = [Security.Cryptography.SHA256]::HashData($publicBytes)
    $keyId = ([Convert]::ToHexString($keyIdBytes)).Substring(0, 16).ToLowerInvariant()
    $header = @"
#pragma once

namespace inkdash::ota {

constexpr char kReleaseKeyId[] = "$keyId";
constexpr char kReleasePublicKeyPem[] = R"PEM($($publicPem.TrimEnd())
)PEM";

}  // namespace inkdash::ota
"@
    [IO.File]::WriteAllText(
        $HeaderPath,
        $header,
        [Text.UTF8Encoding]::new($false)
    )
    Write-Host "Created OTA P-256 signing key."
    Write-Host "Private key (ignored): $PrivateKeyPath"
    Write-Host "Public key id: $keyId"
    Write-Host "Firmware header: $HeaderPath"
}
finally {
    $key.Dispose()
}
