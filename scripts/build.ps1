param([switch]$SkipTests, [switch]$Benchmark, [string]$OutputFile = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$compiler = Join-Path $root 'venv\tools\zig-x86_64-windows-0.15.2\zig.exe'
& "$PSScriptRoot\verify-build-tools.ps1" -Tool Zig
New-Item -ItemType Directory -Path build -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-local-cache'
if (!$OutputFile) { $OutputFile = Join-Path $root 'build\release-0.2.0.0\DNFAutoFire.exe' }
$OutputFile = [System.IO.Path]::GetFullPath($OutputFile)
New-Item -ItemType Directory -Path (Split-Path $OutputFile -Parent) -Force | Out-Null
$common = @('c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
    '-static', '-target', 'x86_64-windows-gnu')
# Native code and ordinary Windows resources only: no interpreter or extracted DLL.
Push-Location (Join-Path $root 'native')
try {
    & $compiler rc /fo '..\build\client-res.o' 'client.rc'
    if ($LASTEXITCODE) { throw 'Windows resource compilation failed' }
} finally { Pop-Location }
# Zig 0.15.2 ignores the Clang driver -mwindows option. Pass the PE subsystem
# explicitly to the linker so Windows never allocates a startup console.
& $compiler @common '-DDAF_STATIC_ENGINE' '-municode' '-Wl,--subsystem,windows' `
    'native/client_main.cpp' 'native/client_ui.cpp' 'native/client_gfx.cpp' 'native/client_config.cpp' `
    'native/client_input.cpp' 'native/engine.cpp' 'build/client-res.o' `
    '-luser32' '-lwinmm' '-lcomctl32' '-lshell32' '-lgdi32' '-lole32' '-luuid' '-ladvapi32' '-ld2d1' '-ldwrite' '-ldwmapi' '-o' $OutputFile
if ($LASTEXITCODE) { throw 'Native client compilation failed' }
& "$PSScriptRoot\inspect-native-dependencies.ps1" -Dll $OutputFile -Client
if (!$SkipTests) {
    & $compiler @common 'native/client_config.cpp' 'tests/client_config_test.cpp' '-luser32' '-o' 'build/client_config_test.exe'
    if ($LASTEXITCODE) { throw 'Configuration test compilation failed' }
    & $compiler @common 'tests/client_input_test.cpp' '-o' 'build/client_input_test.exe'
    if ($LASTEXITCODE) { throw 'Input model test compilation failed' }
    & $compiler @common 'native/client_ui.cpp' 'native/client_gfx.cpp' 'native/client_config.cpp' 'tests/client_ui_test.cpp' 'build/client-res.o' `
        '-luser32' '-lcomctl32' '-lshell32' '-lgdi32' '-lole32' '-ld2d1' '-ldwrite' '-ldwmapi' '-o' 'build/client_ui_test.exe'
    if ($LASTEXITCODE) { throw 'UI test compilation failed' }
    & $compiler @common 'tests/native_schedule_test.cpp' '-o' 'build/native_schedule_test.exe'
    if ($LASTEXITCODE) { throw 'Scheduler test compilation failed' }
    foreach ($test in @('client_config_test', 'client_input_test', 'client_ui_test', 'native_schedule_test')) {
        & (Join-Path $root "build\$test.exe")
        if ($LASTEXITCODE) { throw "$test failed" }
    }
    foreach ($mode in @('--self-test', '--ui-self-test')) {
        $process = Start-Process -FilePath $OutputFile -ArgumentList $mode -WindowStyle Hidden -PassThru
        $null = $process.Handle
        if (!$process.WaitForExit(20000)) {
            if ($process.MainModule.FileName -eq $OutputFile) { $process.Kill(); $process.WaitForExit() }
            throw "$mode timed out"
        }
        if ($process.ExitCode) { throw "$mode failed with exit code $($process.ExitCode)" }
        Write-Output "PASS $mode"
    }
    foreach ($test in @('build_tools_test', 'release_security_test')) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tests\$test.ps1")
        if ($LASTEXITCODE) { throw "$test failed" }
    }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tests\client_singleton_test.ps1') -Client $OutputFile
    if ($LASTEXITCODE) { throw 'Native singleton test failed' }
}
if ($Benchmark) {
    # Scheduler-only measurement (no SendInput); see native/BENCHMARK.md.
    & $compiler @common 'tests/native_benchmark.cpp' '-lwinmm' '-o' 'build/native_benchmark.exe'
    if ($LASTEXITCODE) { throw 'Native benchmark compilation failed' }
    & (Join-Path $root 'build\native_benchmark.exe') | Set-Content -LiteralPath (Join-Path $root 'build\native-benchmark.csv') -Encoding UTF8
    if ($LASTEXITCODE) { throw 'Native benchmark failed' }
    Write-Output 'Benchmark written to build/native-benchmark.csv'
}
Get-FileHash -LiteralPath $OutputFile -Algorithm SHA256
