param(
    [ValidateSet('All', 'Zig')]
    [string]$Tool = 'All',
    [string]$Compiler = '',
    [string]$ExpectedCompilerHash = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

# Source verification completed on 2026-09-24. The compiler hash below also
# matches the corresponding entry in this verified official archive.
# zig-x86_64-windows-0.15.2.zip (0.15.2 / x86_64-windows):
#   https://ziglang.org/download/index.json
#   3A0ED1E8799A2F8CE2A6E6290A9FF22E6906F8227865911FB7DDEDC3CC14CB0C
# These pins detect changed build inputs. They are not an antivirus verdict.
function Assert-BuildToolHash([string]$Path, [string]$ExpectedHash) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Build tool is missing: $Path"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $ExpectedHash) {
        throw "Build tool SHA-256 mismatch: $Path. Expected $ExpectedHash; found $actual. Compilation is blocked."
    }
    [pscustomobject]@{
        Path = (Resolve-Path -LiteralPath $Path).Path
        SHA256 = $actual
        Verified = $true
    }
}

if ($Tool -eq 'All' -or $Tool -eq 'Zig') {
    $defaultCompiler = Join-Path $root 'venv\tools\zig-x86_64-windows-0.15.2\zig.exe'
    $defaultHash = 'D408DD38EED3E5204AF841BCEBF70502A4DBBB8399A3A3262BE55059370BC018'
    if (!$Compiler) { $Compiler = $defaultCompiler }
    $Compiler = [System.IO.Path]::GetFullPath($Compiler)
    if ($ExpectedCompilerHash -and $ExpectedCompilerHash -notmatch '\A[0-9A-Fa-f]{64}\z') {
        throw 'ExpectedCompilerHash must be a 64-character SHA-256 hexadecimal value.'
    }
    if ($Compiler -ieq $defaultCompiler) {
        if ($ExpectedCompilerHash -and $ExpectedCompilerHash -ne $defaultHash) {
            throw 'The default compiler hash is pinned and cannot be overridden. Use a separate -Compiler path for a reviewed custom toolchain.'
        }
        $ExpectedCompilerHash = $defaultHash
    } elseif (!$ExpectedCompilerHash) {
        throw 'A custom -Compiler requires an explicit -ExpectedCompilerHash from a trusted source. Compilation is blocked.'
    }
    Assert-BuildToolHash $Compiler $ExpectedCompilerHash
}
