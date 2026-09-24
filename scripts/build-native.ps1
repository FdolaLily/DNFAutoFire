param([string]$Compiler = '', [string]$ExpectedCompilerHash = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
if (!$Compiler) { $Compiler = Join-Path $root 'venv\tools\zig-x86_64-windows-0.15.2\zig.exe' }
if (!(Test-Path -LiteralPath $Compiler)) { throw 'Set -Compiler to a portable Zig 0.15.2 zig.exe. See native/README.md.' }
& "$PSScriptRoot\verify-build-tools.ps1" -Tool Zig -Compiler $Compiler -ExpectedCompilerHash $ExpectedCompilerHash
New-Item -ItemType Directory -Force -Path build | Out-Null
# Keep compiler caches in the workspace; statically link the C/C++ runtime.
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-local-cache'
$common = @('c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-target', 'x86_64-windows-gnu')
& $Compiler @common '-shared' 'native/engine.cpp' '-luser32' '-lwinmm' '-o' 'build/DNFAutoFireNative.dll'
if ($LASTEXITCODE) { throw 'Native engine compilation failed' }
& $Compiler @common 'tests/native_schedule_test.cpp' '-o' 'build/native_schedule_test.exe'
if ($LASTEXITCODE) { throw 'Native scheduler test compilation failed' }
& $Compiler @common 'tests/native_benchmark.cpp' '-lwinmm' '-o' 'build/native_benchmark.exe'
if ($LASTEXITCODE) { throw 'Native benchmark compilation failed' }
Get-FileHash -LiteralPath 'build/DNFAutoFireNative.dll' -Algorithm SHA256
