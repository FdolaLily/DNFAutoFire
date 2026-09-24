param([Parameter(Mandatory=$true)][string]$Client)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clientPath = (Resolve-Path -LiteralPath $Client).Path
$testDir = Join-Path $root ('build\singleton-native-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testDir
$copy = Join-Path $testDir 'RenamedClient.exe'
Copy-Item -LiteralPath $clientPath -Destination $copy
$mutex = $null
try {
    # Same product identity as the legacy and native clients. This guard creates
    # no client window/hook and holds no ownership lock; it models a live client.
    $mutex = [Threading.Mutex]::new($false, 'Global\DNFAutoFire.Client.{B797BFB2-305A-44DC-9E06-76D6CC424419}')
    foreach ($path in @($clientPath, $copy)) {
        $process = Start-Process -FilePath $path -WindowStyle Hidden -PassThru
        $null = $process.Handle
        if (!$process.WaitForExit(5000)) {
            if ($process.MainModule.FileName -eq $path) { $process.Kill(); $process.WaitForExit() }
            throw "Duplicate native client did not exit: $path"
        }
        if ($process.ExitCode -ne 0) { throw "Duplicate exit code: $($process.ExitCode)" }
    }
    Write-Output 'PASS: native original and renamed cross-directory copies exit before elevation when product mutex exists.'
} finally {
    if ($mutex) { $mutex.Dispose() }
    if (Test-Path -LiteralPath $copy) { Remove-Item -LiteralPath $copy -Force }
    Remove-Item -LiteralPath $testDir
}
