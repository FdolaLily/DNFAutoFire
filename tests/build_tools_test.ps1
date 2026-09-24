$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$verify = Join-Path $root 'scripts\verify-build-tools.ps1'
$buildRoot = [IO.Path]::GetFullPath((Join-Path $root 'build'))
$testRoot = Join-Path $buildRoot ('tool-verification-test-' + [Guid]::NewGuid().ToString('N'))
$zigPath = Join-Path $root 'venv\tools\zig-x86_64-windows-0.15.2\zig.exe'
$trustedZigHash = 'D408DD38EED3E5204AF841BCEBF70502A4DBBB8399A3A3262BE55059370BC018'
$checks = New-Object 'System.Collections.Generic.List[string]'

function Assert-Rejected([scriptblock]$Action, [string]$Expected, [string]$Name) {
    try { & $Action | Out-Null }
    catch {
        if ($_.Exception.Message -like $Expected) {
            $checks.Add($Name)
            return
        }
        throw
    }
    throw "Expected rejection: $Name"
}

function Flip-FirstByte([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $original = $stream.ReadByte()
        if ($original -lt 0) { throw "Empty test file: $Path" }
        $stream.Position = 0
        $stream.WriteByte($original -bxor 1)
    } finally { $stream.Dispose() }
}

try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    & $verify | Out-Null
    $checks.Add('pinned Zig compiler accepted')

    $customCompiler = Join-Path $testRoot 'CustomZig.exe'
    Copy-Item -LiteralPath $zigPath -Destination $customCompiler
    Assert-Rejected { & $verify -Tool Zig -Compiler $customCompiler } `
        '*requires an explicit*' 'custom compiler without expected hash rejected'
    & $verify -Tool Zig -Compiler $customCompiler -ExpectedCompilerHash $trustedZigHash | Out-Null
    $checks.Add('custom compiler with explicit matching hash accepted')
    Flip-FirstByte $customCompiler
    Assert-Rejected { & $verify -Tool Zig -Compiler $customCompiler -ExpectedCompilerHash $trustedZigHash } `
        '*SHA-256 mismatch*' 'modified Zig copy rejected'
    Assert-Rejected { & $verify -Tool Zig -ExpectedCompilerHash ('0' * 64) } `
        '*cannot be overridden*' 'default compiler pin cannot be overridden'
    Assert-Rejected { & $verify -Tool Zig -Compiler $customCompiler -ExpectedCompilerHash 'invalid' } `
        '*64-character*' 'invalid custom hash rejected'
    Write-Output "PASS: $($checks.Count) build tool integrity checks; no compiler or client was executed."
} finally {
    # Delete only this invocation's generated directory, after checking its boundary.
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $allowedPrefix = $buildRoot + [IO.Path]::DirectorySeparatorChar
    if (!$resolvedTestRoot.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase) `
        -or (Split-Path $resolvedTestRoot -Leaf) -notmatch '^tool-verification-test-[0-9a-f]{32}$') {
        throw "Unsafe test cleanup path: $resolvedTestRoot"
    }
    if (Test-Path -LiteralPath $resolvedTestRoot) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
