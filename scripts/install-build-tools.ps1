$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$toolsDir = Join-Path $root 'venv\tools'
$compiler = Join-Path $toolsDir 'zig-x86_64-windows-0.15.2\zig.exe'
if (Test-Path -LiteralPath $compiler) {
    & "$PSScriptRoot\verify-build-tools.ps1" -Tool Zig
    return
}

# Verify the official archive before extracting or executing its contents.
$archive = Join-Path $root 'build\zig-x86_64-windows-0.15.2.zip'
New-Item -ItemType Directory -Path (Split-Path $archive -Parent), $toolsDir -Force | Out-Null
Invoke-WebRequest -Uri 'https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip' -OutFile $archive
$expected = '3A0ED1E8799A2F8CE2A6E6290A9FF22E6906F8227865911FB7DDEDC3CC14CB0C'
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    throw 'Official Zig archive SHA-256 mismatch; extraction blocked.'
}
Expand-Archive -LiteralPath $archive -DestinationPath $toolsDir -Force
& "$PSScriptRoot\verify-build-tools.ps1" -Tool Zig
Remove-Item -LiteralPath $archive
