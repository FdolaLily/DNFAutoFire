# Keeps the product version in step with the EXE sources.
#
#   bump-version.ps1                    raise the fourth part (0.3.0.1 -> 0.3.0.2)
#   bump-version.ps1 -IfSourcesChanged  raise it only when the EXE sources differ from the
#                                       fingerprint recorded at the last bump (build.ps1 uses this)
#   bump-version.ps1 -Set 0.4.0.0       set an explicit version (new minor / major release)
#   bump-version.ps1 -Check             verify only; never writes
#
# The root Version file (tag_name) is the release version. native/version.h (resource,
# kProductVersion, UI) and native/client.manifest must agree with it; this script writes all three.
# The fingerprint covers what is compiled into the EXE (native/*.cpp, .h, .rc, .manifest, icons,
# fonts); text files are hashed without CR, so a CRLF or LF checkout gives the same value. version.h and the
# manifest's own version attribute are excluded, otherwise every bump would count as a change.
# Writes the resulting version (for example 0.3.0.2) to the output pipeline.
param([switch]$IfSourcesChanged, [string]$Set = '', [switch]$Check)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$native = Join-Path $root 'native'
$versionFile = Join-Path $root 'Version'
$header = Join-Path $native 'version.h'
$manifest = Join-Path $native 'client.manifest'
$versionPattern = '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'
$manifestPattern = '(<assemblyIdentity\s+version=")([^"]*)("[^>]*\sname="DNFAutoFire\.Client")'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path, $utf8) }
function Write-Text([string]$Path, [string]$Text) {
    # Temporary file + replace, so an interrupted build never leaves a half-written file.
    $temp = "$Path.bump.tmp"
    [IO.File]::WriteAllText($temp, $Text, $utf8)
    [IO.File]::Replace($temp, $Path, [NullString]::Value)
}
function Get-Sha256Hex([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes)) -replace '-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Get-SourceFingerprint {
    $extensions = @('.cpp', '.h', '.rc', '.manifest', '.ico', '.ttf')
    $prefix = $native.TrimEnd('\', '/').Length + 1
    $files = @{}
    foreach ($item in Get-ChildItem -LiteralPath $native -Recurse -File) {
        if ($extensions -notcontains $item.Extension.ToLowerInvariant()) { continue }
        $relative = $item.FullName.Substring($prefix).Replace('\', '/')
        if ($relative -eq 'version.h') { continue }
        $files[$relative] = $item.FullName
    }
    $names = [string[]]@($files.Keys)
    [Array]::Sort($names, [StringComparer]::Ordinal)
    $listing = New-Object System.Text.StringBuilder
    foreach ($name in $names) {
        $bytes = [IO.File]::ReadAllBytes($files[$name])
        if ($name -notmatch '\.(ico|ttf)$') {
            # Text sources are UTF-8; Git may check them out with CRLF or LF.
            $text = $utf8.GetString($bytes).Replace("`r", '')
            if ($name -eq 'client.manifest') { $text = [regex]::Replace($text, $manifestPattern, '$1$3') }
            $bytes = $utf8.GetBytes($text)
        }
        [void]$listing.Append($name).Append("`n").Append((Get-Sha256Hex $bytes)).Append("`n")
    }
    return Get-Sha256Hex ($utf8.GetBytes($listing.ToString()))
}
function Read-State {
    $info = Read-Text $versionFile
    $tag = [regex]::Match($info, '"tag_name"\s*:\s*"v([^"]*)"')
    if (!$tag.Success -or $tag.Groups[1].Value -notmatch $versionPattern) { throw 'Version: tag_name must look like "v0.3.0.1".' }
    $state = [ordered]@{ Version = $tag.Groups[1].Value; Header = ''; Stamp = ''; Manifest = '' }
    if (Test-Path -LiteralPath $header) {
        $text = Read-Text $header
        $parsed = [regex]::Match($text, '#define DAF_VERSION_STRING "([^"]*)"')
        if ($parsed.Success) { $state.Header = $parsed.Groups[1].Value }
        $stamp = [regex]::Match($text, '// source-sha256: ([0-9a-f]*)')
        if ($stamp.Success) { $state.Stamp = $stamp.Groups[1].Value }
    }
    $identity = [regex]::Match((Read-Text $manifest), $manifestPattern)
    if (!$identity.Success) { throw 'native/client.manifest: DNFAutoFire.Client assemblyIdentity not found.' }
    $state.Manifest = $identity.Groups[2].Value
    return $state
}
function Write-Version([string]$Version, [string]$Stamp) {
    $parts = $Version.Split('.')
    $lines = @(
        '#pragma once',
        "// Product version: the only place the EXE's version lives in source. client.rc (version",
        '// resource), app_ids.h (kProductVersion) and client_ui.cpp (title bar / about) all use it.',
        '// Written by scripts/bump-version.ps1 -- build.ps1 raises DAF_VERSION_BUILD automatically when',
        '// the EXE sources change; do not edit by hand. Plain #defines so the resource compiler reads it.',
        "// source-sha256: $Stamp",
        "#define DAF_VERSION_MAJOR $($parts[0])",
        "#define DAF_VERSION_MINOR $($parts[1])",
        "#define DAF_VERSION_PATCH $($parts[2])",
        "#define DAF_VERSION_BUILD $($parts[3])",
        "#define DAF_VERSION_STRING `"$Version`"",
        "#define DAF_VERSION_SHORT `"$($parts[0]).$($parts[1]).$($parts[2])`"",
        '')
    Write-Text $header ($lines -join "`n")
    Write-Text $manifest ([regex]::Replace((Read-Text $manifest), $manifestPattern, "`${1}$Version`$3"))
    # Only tag_name changes; the release notes (body) keep their text and CRLF escapes.
    $info = Read-Text $versionFile
    Write-Text $versionFile ([regex]::Replace($info, '("tag_name"\s*:\s*"v)[^"]*(")', "`${1}$Version`$2"))
}

$state = Read-State
$fingerprint = Get-SourceFingerprint
$consistent = $state.Header -eq $state.Version -and $state.Manifest -eq $state.Version

if ($Check) {
    if (!$consistent) {
        throw "Version mismatch: Version=$($state.Version), native/version.h=$($state.Header), client.manifest=$($state.Manifest). Run scripts/bump-version.ps1."
    }
    if ($state.Stamp -ne $fingerprint) {
        Write-Warning "EXE sources changed since v$($state.Version) was assigned; build locally (build.ps1 bumps the version) before committing."
        if ($env:GITHUB_ACTIONS -eq 'true') { Write-Output "::warning::EXE sources changed without a version bump (still v$($state.Version))." }
    }
    Write-Output $state.Version
    return
}

if ($Set) {
    if ($Set -notmatch $versionPattern) { throw "-Set expects four numbers such as 0.4.0.0, got '$Set'." }
    $next = $Set
} elseif ($IfSourcesChanged -and $consistent -and $state.Stamp -eq $fingerprint) {
    Write-Host "Version v$($state.Version) (EXE sources unchanged)"
    Write-Output $state.Version
    return
} else {
    $parts = $state.Version.Split('.')
    $next = '{0}.{1}.{2}.{3}' -f $parts[0], $parts[1], $parts[2], ([int64]$parts[3] + 1)
}
Write-Version $next $fingerprint
$after = Read-State
if ($after.Version -ne $next -or $after.Header -ne $next -or $after.Manifest -ne $next -or $after.Stamp -ne $fingerprint) {
    throw 'Version files were not updated consistently.'
}
Write-Host "Version v$($state.Version) -> v$next"
Write-Output $next
