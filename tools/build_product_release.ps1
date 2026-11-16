[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $ProductRoot,
    [Parameter(Mandatory = $true)] [string] $AppVersion,
    [ValidateSet('Release')] [string] $Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$product = (Resolve-Path -LiteralPath $ProductRoot).Path
$entryPath = Join-Path $product 'tools/release_entry.json'
$contractPath = Join-Path $product 'tools/release_contract.json'
$entry = Get-Content -LiteralPath $entryPath -Raw | ConvertFrom-Json
$contract = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
if ($entry.product -ne $contract.product) { throw 'Release entry identity disagrees with contract' }
if ($entry.script -notmatch '^tools/[a-z_]+\.ps1$' -or
    $entry.version_parameter -notmatch '^(AppVersion|FirmwareVersion)$') {
    throw 'Invalid declared product release entry'
}
$variants = @($entry.variants)
if ($null -eq $entry.variants) {
    $variants = @([pscustomobject]@{
        directory = ''; contract = 'tools/release_contract.json'
        boot = 'platform/targets/stm32f105_abox_boot/dist/ABox_Boot.bin'
    })
}
if ($variants.Count -eq 0) { throw 'No declared release variants' }
$seen = @{}
foreach ($variant in $variants) {
    if (($variant.directory -ne '' -and $variant.directory -notmatch '^[a-zA-Z0-9_-]+$') -or
        $variant.contract -notmatch '^tools/[a-zA-Z0-9_]+\.json$' -or
        $variant.boot -notmatch '^platform/targets/[a-zA-Z0-9_]+/dist/ABox_Boot\.bin$' -or
        $seen.ContainsKey([string]$variant.directory)) { throw 'Invalid release variant declaration' }
    $seen[[string]$variant.directory] = $true
    $variantContract = Get-Content -LiteralPath (Join-Path $product $variant.contract) -Raw | ConvertFrom-Json
    if ($variantContract.product -ne $entry.product -or
        (Get-FileHash -LiteralPath (Join-Path $product $variant.boot) -Algorithm SHA256).Hash -ne $variantContract.frozen_boot_sha256) {
        throw "Frozen Boot or product mismatch for $($variant.directory)"
    }
}
# Product packagers retain their manifest ABI and extra gates. They must stage
# every output and call the common publisher; this entry never writes dist.
$arguments = @{ Configuration = $Configuration }
$arguments[$entry.version_parameter] = $AppVersion
& (Join-Path $product $entry.script) @arguments
if (-not $?) { throw 'Product release failed' }
if ($null -ne $entry.variants) {
    $actual = @(Get-ChildItem -LiteralPath (Join-Path $product 'dist') -Force)
    if ($actual.Count -ne $variants.Count -or @($actual | Where-Object { -not $_.PSIsContainer -or -not $seen.ContainsKey($_.Name) }).Count) {
        throw 'Published release root does not match declared variants'
    }
}
foreach ($variant in $variants) {
    $stage = if ($variant.directory) { Join-Path $product "dist/$($variant.directory)" } else { Join-Path $product 'dist' }
    & python (Join-Path $PSScriptRoot 'publish_release.py') --contract (Join-Path $product $variant.contract) --stage $stage --product-root $product --validate-only | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Published release failed validation: $($variant.directory)" }
}
Write-Host "Formal release completed for $($entry.product): $AppVersion"
