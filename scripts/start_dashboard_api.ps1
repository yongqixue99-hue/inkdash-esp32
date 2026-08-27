[CmdletBinding()]
param(
    [string]$Bind = '0.0.0.0',
    [ValidateRange(1, 65535)]
    [int]$Port = 8767,
    [string]$Python = 'python',
    [switch]$DisableAccountProfile
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$arguments = @(
    (Join-Path $projectRoot 'host\dashboard_server.py'),
    '--bind', $Bind,
    '--port', $Port.ToString()
)
if ($DisableAccountProfile) {
    $arguments += '--disable-account-profile'
}
& $Python @arguments
exit $LASTEXITCODE
