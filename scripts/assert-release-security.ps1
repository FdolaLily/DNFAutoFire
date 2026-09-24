[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$PolicyFile = (Join-Path $PSScriptRoot 'release-security.json')
)
$ErrorActionPreference = 'Stop'
$policy = Get-Content -LiteralPath $PolicyFile -Raw | ConvertFrom-Json
if ($policy.schemaVersion -ne 1 -or !$policy.PSObject.Properties['blockedArtifacts']) {
    throw 'Invalid release security policy'
}
$hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
foreach ($artifact in $policy.blockedArtifacts) {
    if ($artifact.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'Invalid SHA256 in release security policy'
    }
    if ($artifact.sha256 -eq $hash) {
        throw ("Release blocked: {0} detected this exact artifact as {1}. Status: {2}. Evidence: {3}." -f
            $artifact.vendor, $artifact.detection, $artifact.status, $artifact.evidence)
    }
}
# This check prevents re-publishing a known detected artifact. Passing it is not
# an antivirus scan or a statement that a new, different binary is safe.
Write-Output ('Release denylist check passed: ' + $hash)
