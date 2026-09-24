[CmdletBinding()]
param(
    [switch]$Benchmark,
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 30,
    [ValidateRange(1, 300)]
    [int]$BenchmarkTimeoutSeconds = 120,
    [string]$AutoHotkeyPath = ''
)

$ErrorActionPreference = 'Stop'
$projectDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$AutoHotkeyPath) {
    $AutoHotkeyPath = Join-Path $projectDir 'venv\tools\autohotkey-v1.1.37.02\AutoHotkeyU64.exe'
}
$AutoHotkeyPath = [System.IO.Path]::GetFullPath($AutoHotkeyPath)
$runName = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
$outputDir = Join-Path $projectDir ('build\test-results\' + $runName)
$null = New-Item -ItemType Directory -Path $outputDir -Force
$tests = @()
foreach ($name in @('one_key_run_test', 'numpad_validation_test', 'combo_parse_test',
        'autofire_mode_test', 'autofire_rules_test', 'autofire_input_test', 'native_engine_test',
        'native_payload_test')) {
    $scriptPath = Join-Path $PSScriptRoot ($name + '.ahk')
    $tests += [pscustomobject]@{
        Name = $name
        Kind = 'AutoHotkey'
        Executable = $AutoHotkeyPath
        RequiredFile = $scriptPath
        Arguments = @('/CP65001', '/ErrorStdOut', ('"{0}"' -f $scriptPath))
        TimeoutSeconds = $TimeoutSeconds
    }
}
$powerShellExecutable = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh.exe' } else { 'powershell.exe' }
foreach ($name in @('build_tools_test', 'release_security_test')) {
    $scriptPath = Join-Path $PSScriptRoot ($name + '.ps1')
    $tests += [pscustomobject]@{
        Name = $name
        Kind = 'Build and release preflight'
        Executable = Join-Path $PSHOME $powerShellExecutable
        RequiredFile = $scriptPath
        Arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-File', ('"{0}"' -f $scriptPath))
        TimeoutSeconds = [Math]::Max(30, $TimeoutSeconds)
    }
}
$tests += [pscustomobject]@{
    Name = 'single_instance_test'
    Kind = 'PowerShell process integration'
    Executable = Join-Path $PSHOME $powerShellExecutable
    RequiredFile = Join-Path $PSScriptRoot 'single_instance_test.ps1'
    Arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
        ('"{0}"' -f (Join-Path $PSScriptRoot 'single_instance_test.ps1')),
        '-AutoHotkeyPath', ('"{0}"' -f $AutoHotkeyPath))
    TimeoutSeconds = [Math]::Max(30, $TimeoutSeconds)
}
$tests += [pscustomobject]@{
    Name = 'native_schedule_test'
    Kind = 'Native'
    Executable = Join-Path $projectDir 'build\native_schedule_test.exe'
    RequiredFile = Join-Path $projectDir 'build\native_schedule_test.exe'
    Arguments = @()
    TimeoutSeconds = $TimeoutSeconds
}
if ($Benchmark) {
    $tests += [pscustomobject]@{
        Name = 'native_benchmark'
        Kind = 'Native benchmark'
        Executable = Join-Path $projectDir 'build\native_benchmark.exe'
        RequiredFile = Join-Path $projectDir 'build\native_benchmark.exe'
        Arguments = @()
        TimeoutSeconds = $BenchmarkTimeoutSeconds
    }
}

# The runner never compiles or starts the application. The existing one-key-run
# regression consumes its synthetic events with a lower InputHook. The input
# layout test and native scheduler/benchmark do not inject keyboard input. The
# singleton regression uses isolated named objects and no GUI or keyboard input.
$suiteStarted = [DateTimeOffset]::Now
$results = @()
foreach ($test in $tests) {
    $stdoutPath = Join-Path $outputDir ($test.Name + '.stdout')
    $stderrPath = Join-Path $outputDir ($test.Name + '.stderr')
    $result = [ordered]@{
        Test = $test.Name
        Kind = $test.Kind
        Executable = $test.Executable
        Arguments = $test.Arguments
        ExitCode = $null
        Passed = $false
        TimedOut = $false
        DurationMs = 0
        Error = ''
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
        Stdout = ''
        Stderr = ''
    }
    $process = $null
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        if (!(Test-Path -LiteralPath $test.RequiredFile -PathType Leaf)) {
            if ($test.Kind -like 'Native*') {
                throw ('Missing {0}; run scripts/build-native.ps1 first. This runner does not compile.' -f $test.RequiredFile)
            }
            throw ('Missing test script: ' + $test.RequiredFile)
        }
        if (!(Test-Path -LiteralPath $test.Executable -PathType Leaf)) {
            throw ('Missing test executable: ' + $test.Executable)
        }
        $startOptions = @{
            FilePath = $test.Executable
            WorkingDirectory = $projectDir
            PassThru = $true
            WindowStyle = 'Hidden'
            RedirectStandardOutput = $stdoutPath
            RedirectStandardError = $stderrPath
        }
        if ($test.Arguments.Count) {
            $startOptions.ArgumentList = $test.Arguments
        }
        $process = Start-Process @startOptions
        # Retain the handle of the exact process this runner created.
        $null = $process.Handle
        if (!$process.WaitForExit($test.TimeoutSeconds * 1000)) {
            $result.TimedOut = $true
            $process.Refresh()
            if (!$process.HasExited) {
                $actualPath = [System.IO.Path]::GetFullPath($process.MainModule.FileName)
                $expectedPath = [System.IO.Path]::GetFullPath($test.Executable)
                if (![StringComparer]::OrdinalIgnoreCase.Equals($actualPath, $expectedPath)) {
                    throw ('Timed out; refusing to stop PID {0}: executable path does not match this test.' -f $process.Id)
                }
                # Kill only this retained process, never by image name or a scan.
                $process.Kill()
                if (!$process.WaitForExit(5000)) {
                    throw ('Timed out; PID {0} did not exit after termination.' -f $process.Id)
                }
            }
            $result.ExitCode = $process.ExitCode
            throw ('Timed out after {0}s; the matching test process has exited.' -f $test.TimeoutSeconds)
        }
        $result.ExitCode = $process.ExitCode
        $result.Passed = $process.ExitCode -eq 0
        if (!$result.Passed) {
            $result.Error = 'Test returned exit code ' + $process.ExitCode
        }
    } catch {
        $result.Error = $_.Exception.Message
    } finally {
        $watch.Stop()
        $result.DurationMs = [Math]::Round($watch.Elapsed.TotalMilliseconds, 2)
        if ($process) {
            $process.Dispose()
        }
        if (Test-Path -LiteralPath $stdoutPath) {
            $result.Stdout = [string](Get-Content -LiteralPath $stdoutPath -Raw)
        }
        if (Test-Path -LiteralPath $stderrPath) {
            $result.Stderr = [string](Get-Content -LiteralPath $stderrPath -Raw)
        }
        $results += [pscustomobject]$result
    }
    $status = if ($result.Passed) { 'PASS' } else { 'FAIL' }
    Write-Output ('{0} {1}: exit={2}, {3}ms' -f $status, $test.Name, $result.ExitCode, $result.DurationMs)
    if (([string]$result.Stdout).Trim()) { Write-Output ([string]$result.Stdout).TrimEnd() }
    if (([string]$result.Stderr).Trim()) { Write-Output ([string]$result.Stderr).TrimEnd() }
    if ($result.Error) { Write-Output $result.Error }
}

$failed = @($results | Where-Object { !$_.Passed }).Count
$report = [ordered]@{
    StartedAt = $suiteStarted.ToString('o')
    FinishedAt = [DateTimeOffset]::Now.ToString('o')
    BenchmarkIncluded = [bool]$Benchmark
    Passed = $failed -eq 0
    FailedCount = $failed
    Results = $results
}
$reportPath = Join-Path $outputDir 'results.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output ('Report: ' + $reportPath)
if ($failed) { exit 1 }
exit 0
