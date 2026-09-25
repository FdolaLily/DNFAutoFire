param([string]$Dll = '', [switch]$Client)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Dll) { $Dll = Join-Path $root 'build\DNFAutoFireNative.dll' }
$dllPath = (Resolve-Path -LiteralPath $Dll).Path
$bytes = [System.IO.File]::ReadAllBytes($dllPath)
$peOffset = [BitConverter]::ToInt32($bytes, 60)
if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550) { throw 'Invalid PE signature' }
$sectionCount = [BitConverter]::ToUInt16($bytes, $peOffset + 6)
$optionalSize = [BitConverter]::ToUInt16($bytes, $peOffset + 20)
$optionalOffset = $peOffset + 24
$magic = [BitConverter]::ToUInt16($bytes, $optionalOffset)
if ($magic -ne 0x20b) { throw 'Expected a 64-bit PE image' }
$subsystem = [BitConverter]::ToUInt16($bytes, $optionalOffset + 68)
if ($Client -and $subsystem -ne 2) { throw "Client must use Windows GUI subsystem (2), found $subsystem; startup would create a console." }
$directoryOffset = $optionalOffset + 112
$sectionTable = $optionalOffset + $optionalSize

function Convert-Rva([uint32]$Rva) {
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $s = $sectionTable + 40 * $i
        $virtualSize = [BitConverter]::ToUInt32($bytes, $s + 8)
        $virtualAddress = [BitConverter]::ToUInt32($bytes, $s + 12)
        $rawSize = [BitConverter]::ToUInt32($bytes, $s + 16)
        $rawOffset = [BitConverter]::ToUInt32($bytes, $s + 20)
        if ($Rva -ge $virtualAddress -and $Rva -lt ($virtualAddress + [Math]::Max($virtualSize, $rawSize))) {
            return [int]($rawOffset + $Rva - $virtualAddress)
        }
    }
    throw "Unmapped RVA $Rva"
}

function Read-AsciiZ([int]$Offset) {
    $end = $Offset
    while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
    if ($end -eq $bytes.Length) { throw 'Unterminated PE import name' }
    return [Text.Encoding]::ASCII.GetString($bytes, $Offset, $end - $Offset)
}

$imports = @()
$descriptor = Convert-Rva ([BitConverter]::ToUInt32($bytes, $directoryOffset + 8))
while ([BitConverter]::ToUInt32($bytes, $descriptor + 12) -ne 0) {
    $imports += Read-AsciiZ (Convert-Rva ([BitConverter]::ToUInt32($bytes, $descriptor + 12)))
    $descriptor += 20
}
$delayRva = [BitConverter]::ToUInt32($bytes, $directoryOffset + 13 * 8)
if ($delayRva) { throw 'Unexpected delay imports require explicit review' }
$unexpected = @($imports | Where-Object {
    $_ -notmatch '^(KERNEL32|USER32|WINMM)\.dll$' -and $_ -notmatch '^api-ms-win-crt-[a-z0-9-]+\.dll$' -and
    # WTSAPI32 / USERENV: the service starts the client in the game's user session
    # (WTSQueryUserToken, CreateEnvironmentBlock). Both ship with every Windows edition.
    !($Client -and $_ -match '^(ADVAPI32|COMCTL32|GDI32|OLE32|SHELL32|D2D1|DWRITE|DWMAPI|WTSAPI32|USERENV)\.dll$')
})
if ($unexpected.Count) { throw "Unexpected runtime dependencies: $($unexpected -join ', ')" }
$result = [ordered]@{
    file = $dllPath
    sha256 = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
    machine = ('0x{0:X}' -f [BitConverter]::ToUInt16($bytes, $peOffset + 4))
    subsystem = $subsystem
    imports = $imports
    delayImportDirectoryRva = $delayRva
    inspectedAt = (Get-Date -Format o)
}
$reportName = if ($Client) { 'build\native-client-dependencies.json' } else { 'build\native-dependencies.json' }
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root $reportName) -Encoding UTF8
$result | ConvertTo-Json
