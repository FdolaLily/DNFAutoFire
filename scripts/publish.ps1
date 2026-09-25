#Requires -RunAsAdministrator
param([string]$ExpectedHash, [string]$Version = '', [string]$Source = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
# Default to the current version (root Version file, kept in step by build.ps1 / bump-version.ps1).
if (!$Version) { $Version = @(& (Join-Path $PSScriptRoot 'bump-version.ps1') -Check)[-1] }
$distExe = Join-Path $root 'dist\DNFAutoFire.exe'
if (!$Source) { $Source = $distExe }
$source = [IO.Path]::GetFullPath($Source)
$deploy = 'E:\autokill\DNFAutoFire.exe'
$managerDir = 'D:\workspace\AutoManagerProcess'
$managerExe = Join-Path $managerDir 'DNFAutoFire.exe'
$reportPath = Join-Path $root "build\publish-$Version-result.json"
$result = [ordered]@{ Success = $false; Version = $Version; SelfTestExitCode = $null; ConfigUnchanged = $false; ConfigMigrated = $false; Service = @{}; Stopped = @(); Restarted = @(); Copies = @(); SingletonChecks = @(); Commit = ''; Error = '' }
$restart = $false
$replaced = $false
$serviceRestart = $false
# The unified EXE also runs as the "DNFAutoFire" Windows service ("<exe>" --service).
# A service registered for the deployed path locks the file, so it is stopped by the
# service manager (never by killing the process) and started again afterwards.
function Get-DeployedService {
    Get-CimInstance Win32_Service -Filter "Name='DNFAutoFire'" | Where-Object {
        $_.PathName -and ($_.PathName -replace '^\s*"([^"]+)".*$', '$1') -eq $deploy
    }
}
$backup = Join-Path $root "build\autokill-before-$Version.exe"
try {
    if (!$ExpectedHash -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $ExpectedHash) { throw 'Source hash mismatch' }
    if ((Get-Item -LiteralPath $source).VersionInfo.FileVersion -ne $Version) { throw 'Source version mismatch' }
    # A known detected build must not be re-published as a successful release.
    # Run before examining/stopping clients or writing any installed copy.
    & (Join-Path $PSScriptRoot 'assert-release-security.ps1') -Source $source
    if (!(Test-Path -LiteralPath $deploy) -or !(Test-Path -LiteralPath $managerExe)) { throw 'Expected existing release paths are missing' }
    $configPath = Join-Path (Split-Path $deploy -Parent) 'config.json'
    $legacyConfigs = @('config.ini', 'appsettings.json') | ForEach-Object { Join-Path (Split-Path $deploy -Parent) $_ } | Where-Object { Test-Path -LiteralPath $_ }
    $configHash = if (Test-Path -LiteralPath $configPath) { (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash } else { '' }
    # Validate the exact static artifact with the same elevated token used by
    # the installed client, before touching an existing process or binary.
    $selfTest = Start-Process -FilePath $source -ArgumentList '--ui-self-test' -WindowStyle Hidden -PassThru
    $null = $selfTest.Handle
    if (!$selfTest.WaitForExit(20000)) {
        if ($selfTest.MainModule.FileName -eq $source) { $selfTest.Kill(); $selfTest.WaitForExit() }
        throw 'Elevated native self-test timed out'
    }
    $result.SelfTestExitCode = $selfTest.ExitCode
    if ($selfTest.ExitCode) { throw 'Elevated native self-test failed' }
    $tracked = @(& git -c "safe.directory=$managerDir" -C $managerDir ls-files -- DNFAutoFire.exe)
    if ($LASTEXITCODE -or $tracked -notcontains 'DNFAutoFire.exe') { throw 'Manager EXE must already be tracked' }
    $candidates = @(Get-CimInstance Win32_Process -Filter "Name='DNFAutoFire.exe'")
    if (@($candidates | Where-Object { !$_.ExecutablePath }).Count) { throw 'Cannot verify executable paths; no processes were stopped' }
    # Known copies named by this project's release workflow; never stop by image
    # name alone. Old versions may be running from E or the workspace dist copy.
    $allowedPaths = @($deploy, $distExe, $managerExe)
    $targets = @($candidates | Where-Object { $_.ExecutablePath -in $allowedPaths })
    if (@($candidates | Where-Object { $_.ExecutablePath -notin $allowedPaths }).Count) { throw 'An unknown DNFAutoFire path is running; deployment not started' }
    $service = Get-DeployedService
    if ($service -and $service.State -ne 'Stopped') {
        Stop-Service -Name $service.Name -ErrorAction Stop
        (Get-Service -Name $service.Name).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(20))
        $serviceRestart = $true
        $result.Service = @{ Name = $service.Name; StoppedPid = $service.ProcessId }
    }
    $candidates = @(Get-CimInstance Win32_Process -Filter "Name='DNFAutoFire.exe'")
    $targets = @($candidates | Where-Object { $_.ExecutablePath -in $allowedPaths })
    $restart = $targets.Count -gt 0
    Copy-Item -LiteralPath $deploy -Destination $backup -Force
    Copy-Item -LiteralPath $managerExe -Destination (Join-Path $root "build\manager-before-$Version.exe") -Force
    foreach ($candidate in ($targets | Sort-Object { $_.CommandLine -notmatch '/Run=' })) {
        $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($candidate.ProcessId)"
        if (!$current) { continue }
        if ($current.ExecutablePath -ne $candidate.ExecutablePath -or $current.ExecutablePath -notin $allowedPaths) { throw 'Process identity changed' }
        Stop-Process -Id $current.ProcessId -Force
        Wait-Process -Id $current.ProcessId -Timeout 10 -ErrorAction SilentlyContinue
        $result.Stopped += @{ Pid = $current.ProcessId; Path = $current.ExecutablePath }
    }
    if (@(Get-CimInstance Win32_Process -Filter "Name='DNFAutoFire.exe'" | Where-Object { $_.ExecutablePath -in $allowedPaths }).Count) { throw 'Target process did not exit' }
    Copy-Item -LiteralPath $source -Destination $deploy -Force
    $replaced = $true
    if ((Get-FileHash -LiteralPath $deploy -Algorithm SHA256).Hash -ne $ExpectedHash) { throw 'E drive copy hash mismatch' }
    $result.Copies += @{ Path = $deploy; SHA256 = $ExpectedHash }
    if ($restart) {
        $new = Start-Process -FilePath $deploy -WorkingDirectory (Split-Path $deploy -Parent) -WindowStyle Hidden -PassThru
        $null = $new.Handle
        Start-Sleep -Seconds 3
        if ($new.HasExited) { throw 'New process exited during startup' }
        $started = Get-CimInstance Win32_Process -Filter "ProcessId=$($new.Id)"
        if ($started.ExecutablePath -ne $deploy) { throw 'Restarted process path mismatch' }
        # The client statically links its engine, with no extracted payload.
        $modules = @(($new.Modules | Where-Object { $_.ModuleName -match '^(AutoHotkey.*|DNFAutoFireNative|engine)\.dll$' }).FileName)
        if ($modules.Count) { throw 'Unexpected payload module in static client' }
        $result.Restarted = @(@{ Pid = $new.Id; Path = $started.ExecutablePath; NativeModules = $modules })
    }
    # Restore the client before the service so a running game cannot make the service
    # launch another client while the explicit restart is still being verified.
    if ($serviceRestart) {
        Start-Service -Name 'DNFAutoFire' -ErrorAction Stop
        (Get-Service -Name 'DNFAutoFire').WaitForStatus('Running', [TimeSpan]::FromSeconds(15))
        $running = Get-DeployedService
        if (!$running -or $running.State -ne 'Running') { throw 'Service did not restart from the deployed path' }
        $result.Service.RestartedPid = $running.ProcessId
    }
    if ($source -ne $distExe) { Copy-Item -LiteralPath $source -Destination $distExe -Force }
    Copy-Item -LiteralPath $source -Destination $managerExe -Force
    foreach ($path in @($deploy, $distExe, $managerExe)) {
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $ExpectedHash) { throw "Final hash mismatch: $path" }
    }
    $afterConfigHash = if (Test-Path -LiteralPath $configPath) { (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash } else { '' }
    $migrated = !$configHash -and $afterConfigHash -and $legacyConfigs.Count -and
        !@($legacyConfigs | Where-Object { Test-Path -LiteralPath $_ }).Count
    if ($migrated) {
        # First start of a 0.3 build merges config.ini / appsettings.json into config.json once.
        $result.ConfigMigrated = $true
    } elseif ($afterConfigHash -ne $configHash) { throw 'Installed configuration unexpectedly changed during deployment' }
    else { $result.ConfigUnchanged = $true }
    $result.Copies += @{ Path = $distExe; SHA256 = $ExpectedHash }
    $result.Copies += @{ Path = $managerExe; SHA256 = $ExpectedHash }
    if ($restart) {
        $renamedDir = Join-Path $root "build\singleton-client-$Version"
        New-Item -ItemType Directory -Path $renamedDir -Force | Out-Null
        $renamedExe = Join-Path $renamedDir 'RenamedClient.exe'
        Copy-Item -LiteralPath $source -Destination $renamedExe -Force
        foreach ($otherPath in @($distExe, $managerExe, $renamedExe)) {
            $duplicate = Start-Process -FilePath $otherPath -WorkingDirectory (Split-Path $otherPath -Parent) -WindowStyle Hidden -PassThru
            $null = $duplicate.Handle
            if (!$duplicate.WaitForExit(5000)) {
                $identity = Get-CimInstance Win32_Process -Filter "ProcessId=$($duplicate.Id)"
                if ($identity.ExecutablePath -eq $otherPath) { Stop-Process -Id $duplicate.Id -Force; $duplicate.WaitForExit() }
                throw "Duplicate client did not exit: $otherPath"
            }
            if ($duplicate.ExitCode) { throw "Duplicate client exited abnormally: $otherPath" }
            $result.SingletonChecks += @{ Path = $otherPath; ExitCode = $duplicate.ExitCode }
        }
        $new.Refresh()
        if ($new.HasExited) { throw 'Original E drive client was interrupted by duplicate launches' }
    }
    & git -c "safe.directory=$managerDir" -C $managerDir commit --only -m "release: update DNFAutoFire to v$Version" -- DNFAutoFire.exe 2>&1 |
        Out-File -LiteralPath (Join-Path $root "build\publish-$Version-git.log") -Encoding UTF8
    if ($LASTEXITCODE) { throw 'Manager local Git commit failed' }
    $result.Commit = (& git -c "safe.directory=$managerDir" -C $managerDir rev-parse HEAD).Trim()
    $files = @(& git -c "safe.directory=$managerDir" -C $managerDir diff-tree --no-commit-id --name-only -r HEAD)
    if ($LASTEXITCODE -or $files.Count -ne 1 -or $files[0] -ne 'DNFAutoFire.exe') { throw 'Release commit included unexpected files' }
    $result.Success = $true
} catch {
    $result.Error = $_.Exception.Message
    # Preserve a usable installed EXE if replacement/start fails before the
    # manager copy/commit. Never conceal an incomplete release as success.
    if ($replaced -and $serviceRestart -and !$result.Service.RestartedPid) {
        try { Start-Service -Name 'DNFAutoFire' -ErrorAction Stop; $result.Error += '; requested service restart' }
        catch { $result.Error += '; service restart failed: ' + $_.Exception.Message }
    }
    if ($replaced -and !$result.Restarted.Count -and $restart) {
        try {
            if (!(Get-CimInstance Win32_Process -Filter "Name='DNFAutoFire.exe'" | Where-Object { $_.ExecutablePath -eq $deploy })) {
                & (Join-Path $PSScriptRoot 'assert-release-security.ps1') -Source $backup
                Copy-Item -LiteralPath $backup -Destination $deploy -Force
                Start-Process -FilePath $deploy -WorkingDirectory (Split-Path $deploy -Parent) -WindowStyle Hidden | Out-Null
                $result.Error += '; restored previous E drive binary and requested restart'
            }
        } catch { $result.Error += '; rollback failed: ' + $_.Exception.Message }
    }
} finally {
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
}
if (!$result.Success) { exit 1 }
