[CmdletBinding()]
param([string]$AutoHotkeyPath = '')

$ErrorActionPreference = 'Stop'
$projectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$AutoHotkeyPath) {
    $AutoHotkeyPath = Join-Path $projectDir 'venv\tools\autohotkey-v1.1.37.02\AutoHotkeyU64.exe'
}
$AutoHotkeyPath = [IO.Path]::GetFullPath($AutoHotkeyPath)
$testId = [guid]::NewGuid().ToString('N')
$mutexName = 'Global\DNFAutoFire.SingleInstance.Test.' + $testId
$outputDir = Join-Path $projectDir ('build\test-results\single-instance-' + $testId)
$script:workers = [Collections.Generic.List[object]]::new()
$script:events = [Collections.Generic.List[object]]::new()
$script:sequence = 0

function Assert-True([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    Write-Output ('PASS: ' + $Message)
}

function New-TestEvent([string]$Suffix) {
    $event = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset,
        ($mutexName + '.' + $Suffix))
    $script:events.Add($event)
    return $event
}

function Start-Worker([string]$Mode, [int]$Copy, [string]$GateName = '', [string]$ReleaseName = '') {
    $script:sequence++
    $resultPath = Join-Path $outputDir ('worker-{0}.result' -f $script:sequence)
    $workerArguments = @('/CP65001', '/ErrorStdOut', ('"{0}"' -f $scriptPaths[$Copy]),
        $Mode, $mutexName, ('"{0}"' -f $resultPath))
    if ($Mode -ne 'probe') { $workerArguments += @($GateName, $ReleaseName) }
    $startOptions = @{
        FilePath = $AutoHotkeyPath
        ArgumentList = $workerArguments
        WorkingDirectory = $outputDir
        WindowStyle = 'Hidden'
        PassThru = $true
        RedirectStandardOutput = $resultPath + '.stdout'
        RedirectStandardError = $resultPath + '.stderr'
    }
    $process = Start-Process @startOptions
    $null = $process.Handle
    $worker = [pscustomobject]@{ Process = $process; ResultPath = $resultPath }
    $script:workers.Add($worker)
    return $worker
}

function Wait-File([string]$Path) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        if ($watch.ElapsedMilliseconds -ge 10000) { throw ('Timed out waiting for ' + $Path) }
        Start-Sleep -Milliseconds 20
    }
    # The writer may have created the file immediately before writing its line.
    do {
        $value = [string](Get-Content -LiteralPath $Path -Raw)
        if ($value) { return $value }
        if ($watch.ElapsedMilliseconds -ge 10000) { throw ('Empty result: ' + $Path) }
        Start-Sleep -Milliseconds 20
    } while ($true)
}

function Wait-Worker($Worker) {
    if (!$Worker.Process.WaitForExit(10000)) { throw ('Worker did not exit: ' + $Worker.Process.Id) }
    if ($Worker.Process.ExitCode -ne 0) {
        $details = Get-Content -LiteralPath ($Worker.ResultPath + '.stdout'), ($Worker.ResultPath + '.stderr') -Raw
        throw ('Worker failed: {0}; {1}' -f $Worker.Process.Id, ($details -join ' '))
    }
}

function Read-Probe {
    $probe = Start-Worker 'probe' 1
    Wait-Worker $probe
    return Wait-File $probe.ResultPath
}

function Stop-OwnedWorker($Worker) {
    $Worker.Process.Refresh()
    if ($Worker.Process.HasExited) { return }
    $actualPath = [IO.Path]::GetFullPath($Worker.Process.MainModule.FileName)
    if (![StringComparer]::OrdinalIgnoreCase.Equals($actualPath, $AutoHotkeyPath)) {
        throw ('Refusing to stop PID {0}: interpreter path differs.' -f $Worker.Process.Id)
    }
    # Kill only the retained process object, never an image-name match or PID scan.
    $Worker.Process.Kill()
    if (!$Worker.Process.WaitForExit(5000)) { throw ('Worker did not terminate: ' + $Worker.Process.Id) }
}

try {
    if (!(Test-Path -LiteralPath $AutoHotkeyPath -PathType Leaf)) { throw 'AutoHotkey executable is missing.' }
    $scriptPaths = @()
    foreach ($copy in 0..1) {
        $copyDir = Join-Path $outputDir ('copy-' + $copy)
        $null = New-Item -ItemType Directory -Path $copyDir -Force
        $scriptName = if ($copy -eq 0) { 'client.ahk' } else { 'renamed-client.ahk' }
        $scriptPath = Join-Path $copyDir $scriptName
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'single_instance_worker.ahk') -Destination $scriptPath
        Copy-Item -LiteralPath (Join-Path $projectDir 'lib\SingleInstance.ahk') -Destination $copyDir
        $scriptPaths += $scriptPath
    }

    Assert-True ((Read-Probe) -eq 'EMPTY') 'No stale instance exists for the isolated test name.'
    $gate = New-TestEvent 'race-gate'
    $release = New-TestEvent 'race-release'
    $contenders = @()
    foreach ($index in 0..7) {
        $contenders += Start-Worker 'acquire' ($index % 2) ($mutexName + '.race-gate') ($mutexName + '.race-release')
    }
    foreach ($worker in $contenders) { $null = Wait-File ($worker.ResultPath + '.ready') }
    $null = $gate.Set()
    $owners = @()
    $duplicates = @()
    foreach ($worker in $contenders) {
        $result = Wait-File $worker.ResultPath
        if ($result -eq 'OWNER') { $owners += $worker }
        elseif ($result -eq 'DUPLICATE') { $duplicates += $worker }
        else { throw ('Unexpected race result: ' + $result) }
    }
    Assert-True ($owners.Count -eq 1 -and $duplicates.Count -eq 7) 'Eight simultaneous copies in different paths/names produce exactly one owner.'
    foreach ($worker in $duplicates) { Wait-Worker $worker }
    Assert-True ((Read-Probe) -eq 'EXISTS') 'A preflight probe detects the running owner.'
    $late = Start-Worker 'acquire' 1 ($mutexName + '.race-gate') ($mutexName + '.race-release')
    Wait-Worker $late
    Assert-True ((Wait-File $late.ResultPath) -eq 'DUPLICATE') 'A later renamed copy cannot start while the owner lives.'
    $null = $release.Set()
    Wait-Worker $owners[0]
    Assert-True ((Read-Probe) -eq 'EMPTY') 'Normal exit removes the mutex without manual cleanup.'

    $crashGate = New-TestEvent 'crash-gate'
    $crashRelease = New-TestEvent 'crash-release'
    $null = $crashGate.Set()
    $crashOwner = Start-Worker 'acquire' 1 ($mutexName + '.crash-gate') ($mutexName + '.crash-release')
    Assert-True ((Wait-File $crashOwner.ResultPath) -eq 'OWNER') 'A renamed copy can restart after normal exit.'
    Stop-OwnedWorker $crashOwner
    Assert-True ((Read-Probe) -eq 'EMPTY') 'Forced termination also removes the mutex.'
    $restart = Start-Worker 'acquire' 0 ($mutexName + '.crash-gate') ($mutexName + '.crash-release')
    Assert-True ((Wait-File $restart.ResultPath) -eq 'OWNER') 'A new owner can start after forced termination.'
    $null = $crashRelease.Set()
    Wait-Worker $restart
    Assert-True ((Read-Probe) -eq 'EMPTY') 'The final owner leaves no stale mutex.'
    Write-Output ('PASS: singleton process integration; artifacts: ' + $outputDir)
} finally {
    foreach ($event in $script:events) { $null = $event.Set() }
    foreach ($worker in $script:workers) {
        try { Stop-OwnedWorker $worker } finally { $worker.Process.Dispose() }
    }
    foreach ($event in $script:events) { $event.Dispose() }
}
