param([switch]$SkipTests, [switch]$Benchmark, [switch]$NoVersionBump, [string]$OutputFile = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$compiler = Join-Path $root 'venv\tools\zig-x86_64-windows-0.15.2\zig.exe'
& "$PSScriptRoot\verify-build-tools.ps1" -Tool Zig
New-Item -ItemType Directory -Path build -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-local-cache'
# Product version: the fourth part rises automatically whenever the EXE sources changed since
# the last bump (scripts/bump-version.ps1). CI and -NoVersionBump never write; they only check
# that Version, native/version.h and native/client.manifest agree.
$versionFiles = @('Version', 'native\version.h', 'native\client.manifest') | ForEach-Object { Join-Path $root $_ }
$versionBackup = @{}
foreach ($file in $versionFiles) { $versionBackup[$file] = [IO.File]::ReadAllBytes($file) }
if ($env:GITHUB_ACTIONS -eq 'true' -or $NoVersionBump) {
    $version = @(& "$PSScriptRoot\bump-version.ps1" -Check)[-1]
} else {
    $version = @(& "$PSScriptRoot\bump-version.ps1" -IfSourcesChanged)[-1]
}
$bumped = $false
foreach ($file in $versionFiles) {
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($file)) -ne [Convert]::ToBase64String($versionBackup[$file])) { $bumped = $true }
}
Write-Output "Building DNFAutoFire v$version"
if (!$OutputFile) { $OutputFile = Join-Path $root "build\release-$version\DNFAutoFire.exe" }
$OutputFile = [System.IO.Path]::GetFullPath($OutputFile)
New-Item -ItemType Directory -Path (Split-Path $OutputFile -Parent) -Force | Out-Null
$common = @('c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
    '-static', '-target', 'x86_64-windows-gnu')
# Native code and ordinary Windows resources only: no interpreter or extracted DLL.
try {
Push-Location (Join-Path $root 'native')
try {
    & $compiler rc /fo '..\build\client-res.o' 'client.rc'
    if ($LASTEXITCODE) { throw 'Windows resource compilation failed' }
} finally { Pop-Location }
# Module groups: configuration (config.json + legacy migration), Windows service
# (--service mode and its control from the UI) and the Direct2D client UI.
$core = @('native/json.cpp', 'native/win_fs.cpp', 'native/client_config.cpp', 'native/config_migrate.cpp')
$service = @('native/service_log.cpp', 'native/process_util.cpp', 'native/launcher_monitor.cpp', 'native/game_monitor.cpp',
    'native/service_host.cpp', 'native/service_control.cpp', 'native/app_update.cpp')
$ui = @('native/client_ui.cpp', 'native/client_ui_service.cpp', 'native/client_ui_picker.cpp', 'native/client_ui_toolbox.cpp',
    'native/client_gfx.cpp', 'native/game_toolbox.cpp')
$uiLibs = @('-luser32', '-lcomctl32', '-lshell32', '-lgdi32', '-lole32', '-luuid', '-ld2d1', '-ldwrite', '-ldwmapi', '-ladvapi32', '-lwtsapi32', '-luserenv')
# Zig 0.15.2 ignores the Clang driver -mwindows option. Pass the PE subsystem
# explicitly to the linker so Windows never allocates a startup console.
& $compiler @common '-DDAF_STATIC_ENGINE' '-municode' '-Wl,--subsystem,windows' `
    'native/client_main.cpp' 'native/update_dialog.cpp' @ui @core @service 'native/client_input.cpp' 'native/engine.cpp' 'build/client-res.o' `
    '-luser32' '-lwinmm' '-lcomctl32' '-lshell32' '-lgdi32' '-lole32' '-luuid' '-ladvapi32' '-ld2d1' '-ldwrite' '-ldwmapi' `
    '-lwtsapi32' '-luserenv' '-o' $OutputFile
if ($LASTEXITCODE) { throw 'Native client compilation failed' }
} catch {
    # A build that produced no EXE does not use up a version number: the next successful
    # build of the fixed sources takes the same number.
    if ($bumped) {
        foreach ($file in $versionFiles) { [IO.File]::WriteAllBytes($file, $versionBackup[$file]) }
        Write-Warning "Compilation failed; version files restored to their previous contents."
    }
    throw
}
& "$PSScriptRoot\inspect-native-dependencies.ps1" -Dll $OutputFile -Client
if (!$SkipTests) {
    & $compiler @common 'native/json.cpp' 'tests/json_test.cpp' '-o' 'build/json_test.exe'
    if ($LASTEXITCODE) { throw 'JSON test compilation failed' }
    & $compiler @common '-municode' 'native/game_toolbox.cpp' 'native/process_util.cpp' 'native/win_fs.cpp' 'tests/game_toolbox_test.cpp' `
        '-ladvapi32' '-lshell32' '-lole32' '-luuid' '-lwtsapi32' '-luserenv' '-o' 'build/game_toolbox_test.exe'
    if ($LASTEXITCODE) { throw 'Game toolbox test compilation failed' }
    & $compiler @common @core 'tests/client_config_test.cpp' '-luser32' '-ladvapi32' '-o' 'build/client_config_test.exe'
    if ($LASTEXITCODE) { throw 'Configuration test compilation failed' }
    & $compiler @common @core 'tests/config_migrate_test.cpp' '-luser32' '-ladvapi32' '-o' 'build/config_migrate_test.exe'
    if ($LASTEXITCODE) { throw 'Configuration migration test compilation failed' }
    & $compiler @common '-municode' @core @service 'tests/service_logic_test.cpp' '-luser32' '-ladvapi32' '-lwtsapi32' '-luserenv' '-o' 'build/service_logic_test.exe'
    if ($LASTEXITCODE) { throw 'Service logic test compilation failed' }
    & $compiler @common 'tests/client_input_test.cpp' '-o' 'build/client_input_test.exe'
    if ($LASTEXITCODE) { throw 'Input model test compilation failed' }
    & $compiler @common @ui @core 'native/service_control.cpp' 'native/process_util.cpp' 'native/app_update.cpp' 'tests/client_ui_test.cpp' 'build/client-res.o' `
        @uiLibs '-o' 'build/client_ui_test.exe'
    if ($LASTEXITCODE) { throw 'UI test compilation failed' }
    & $compiler @common 'tests/native_schedule_test.cpp' '-o' 'build/native_schedule_test.exe'
    if ($LASTEXITCODE) { throw 'Scheduler test compilation failed' }
    # In-place update on copies of a test EXE that carries the client's version resource,
    # plus a stand-in for the AHK-era client (old version names, hidden "AutoHotkey" window).
    & $compiler @common '-municode' @core @service 'tests/app_update_test.cpp' 'build/client-res.o' '-luser32' '-ladvapi32' '-lwtsapi32' '-luserenv' '-o' 'build/app_update_test.exe'
    if ($LASTEXITCODE) { throw 'Update test compilation failed' }
    Push-Location (Join-Path $root 'tests')
    try {
        & $compiler rc /fo '..\build\legacy-res.o' 'legacy_client.rc'
        if ($LASTEXITCODE) { throw 'Legacy client resource compilation failed' }
    } finally { Pop-Location }
    & $compiler @common '-municode' 'tests/legacy_client.cpp' 'build/legacy-res.o' '-luser32' '-o' 'build/legacy_client.exe'
    if ($LASTEXITCODE) { throw 'Legacy client stand-in compilation failed' }
    foreach ($test in @('json_test', 'game_toolbox_test', 'client_config_test', 'config_migrate_test', 'service_logic_test', 'client_input_test', 'client_ui_test', 'native_schedule_test', 'app_update_test')) {
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
