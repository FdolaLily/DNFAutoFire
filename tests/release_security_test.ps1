[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$check = Join-Path $root 'scripts\assert-release-security.ps1'
$testDir = Join-Path $root ('build\test-results\release-security-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$sample = Join-Path $testDir 'sample.txt'
[IO.File]::WriteAllText($sample, 'release security test fixture; not executable')
$hash = (Get-FileHash -LiteralPath $sample -Algorithm SHA256).Hash
$policy = Join-Path $testDir 'policy.json'

function Assert-Rejected([string]$Expected) {
    try {
        & $check -Source $sample -PolicyFile $policy | Out-Null
    } catch {
        if ($_.Exception.Message -notlike $Expected) { throw }
        return
    }
    throw 'Expected preflight rejection'
}

@{ schemaVersion = 1; blockedArtifacts = @(@{
    sha256 = $hash; vendor = 'Test vendor'; detection = 'Test detection';
    status = 'pending'; evidence = 'test fixture'
}) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $policy -Encoding UTF8
Assert-Rejected 'Release blocked:*'
Write-Output 'PASS: known detected artifact is rejected'

[IO.File]::WriteAllText($sample, 'different fixture; not executable')
& $check -Source $sample -PolicyFile $policy | Out-Null
Write-Output 'PASS: different artifact is not attributed to the old detection'

'{"schemaVersion":1,"blockedArtifacts":[{"sha256":"bad"}]}' | Set-Content -LiteralPath $policy -Encoding UTF8
Assert-Rejected 'Invalid SHA256*'
Write-Output 'PASS: invalid policy hash is rejected'
'{"schemaVersion":2}' | Set-Content -LiteralPath $policy -Encoding UTF8
Assert-Rejected 'Invalid release security policy'
Write-Output 'PASS: invalid policy schema is rejected'

$release = Join-Path $root 'dist\DNFAutoFire.exe'
if ((Test-Path -LiteralPath $release) -and
    (Get-FileHash -LiteralPath $release -Algorithm SHA256).Hash -eq
    'B7A493CA497C0896F188613627692B3C7C1FBBA9BBE690E31C983F741C07DC51') {
    try {
        & $check -Source $release | Out-Null
        throw 'Expected current detected release to be rejected'
    } catch {
        if ($_.Exception.Message -notlike 'Release blocked:*') { throw }
    }
    Write-Output 'PASS: actual detected 0.1.4.1 binary is rejected without executing it'
}
