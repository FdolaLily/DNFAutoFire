param([Parameter(Mandatory = $true)][string]$Source)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$info = Get-Content -LiteralPath (Join-Path $root 'Version') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($info.tag_name -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' -or [string]::IsNullOrWhiteSpace($info.body)) {
    throw 'Version must contain a four-part v-prefixed tag and release notes.'
}
$tag = $info.tag_name
$version = $tag.Substring(1)
if ($env:GITHUB_REF_TYPE -eq 'tag' -and $env:GITHUB_REF_NAME -ne $tag) {
    throw 'Pushed tag does not match Version.'
}
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$binary = (Get-Item -LiteralPath $sourcePath).VersionInfo
if ($binary.FileVersion -ne $version -or $binary.ProductVersion -ne $version) {
    throw 'EXE version does not match Version.'
}
[xml]$manifest = Get-Content -LiteralPath (Join-Path $root 'native\client.manifest') -Raw
if ($manifest.assembly.assemblyIdentity.version -ne $version) {
    throw 'Manifest version does not match Version.'
}
& "$PSScriptRoot\assert-release-security.ps1" -Source $sourcePath

# Explicit paths keep personal configuration, debug symbols and build tools out of releases.
$output = Join-Path $root 'build\github-release'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe = Join-Path $output 'DNFAutoFire.exe'
$zip = Join-Path $output "DNFAutoFire-$tag-windows-x64.zip"
Copy-Item -LiteralPath $sourcePath -Destination $exe -Force
Compress-Archive -LiteralPath $exe -DestinationPath $zip -Force
$checksums = foreach ($file in @($exe, $zip)) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $file -Leaf)
}
$checksums | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
$notes = $info.body + "`n`nWindows x64 · 单 EXE，无需安装。`n`n构建已通过自动回归与已知检出样本黑名单检查；这不代表通过杀毒软件主动扫描。"
[IO.File]::WriteAllText((Join-Path $output 'release-notes.md'), $notes, [Text.UTF8Encoding]::new($false))
if ($env:GITHUB_OUTPUT) { "tag=$tag" | Out-File -LiteralPath $env:GITHUB_OUTPUT -Append -Encoding utf8 }
Write-Output "Packaged $tag in $output"
