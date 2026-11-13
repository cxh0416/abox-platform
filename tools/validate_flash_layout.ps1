[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BootLinker,
    [Parameter(Mandatory = $true)] [string] $AppLinker,
    [uint32] $FlashBase = 0x08000000,
    [uint32] $FlashSize = 0x40000,
    [uint32] $BootSize = 0x8000,
    [uint32] $AppStart = 0x08008000,
    [uint32] $ConfigRecoveryStart = 0x0803E000,
    [switch] $NoConfigRecovery,
    [uint32] $ExpectedAppEnd = 0,
    [uint32] $BootStateAStart = 0x0803E800,
    [uint32] $BootStateBStart = 0x0803F000,
    [uint32] $BootStatePageSize = 0x800,
    [uint32] $OtaInfoStart = 0x0803F800,
    [uint32] $OtaInfoSize = 0x800
)

function Read-GnuRegion([string] $Text, [string] $Name) {
    $escaped = [regex]::Escape($Name)
    $match = [regex]::Match($Text, "${escaped}\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+),\s*LENGTH\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+K?)")
    if (-not $match.Success) { return $null }
    [pscustomobject]@{ Origin = [Convert]::ToUInt32($match.Groups[1].Value.Substring(2), 16); Length = Convert-Size $match.Groups[2].Value }
}

function Read-GnuFlash([string] $Path) {
    $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    $flash = Read-GnuRegion $text 'FLASH'
    if ($null -ne $flash) {
        $desc = Read-GnuRegion $text 'DESC'
        if ($null -eq $desc) { return $flash }
        if ($flash.Origin -ne $FlashBase -or $flash.Length -ne 0x7F00 -or
            $desc.Origin -ne ($FlashBase + 0x7F00) -or $desc.Length -ne 0x100) {
            throw "Boot descriptor regions must be FLASH 0x08000000+0x7F00 and DESC 0x08007F00+0x100"
        }
        return [pscustomobject]@{ Origin = $flash.Origin; Length = [uint32]($flash.Length + $desc.Length) }
    }
    $code = Read-GnuRegion $text 'FLASH_CODE'
    $descriptor = Read-GnuRegion $text 'FLASH_DESC'
    if ($null -eq $code -or $null -eq $descriptor) { return $null }
    if ($code.Origin -ne $FlashBase -or $descriptor.Origin -ne ($FlashBase + 0x7F00) -or
        $code.Length -ne 0x7F00 -or $descriptor.Length -ne 0x100) {
        throw "Boot descriptor regions must be FLASH_CODE 0x08000000+0x7F00 and FLASH_DESC 0x08007F00+0x100"
    }
    [pscustomobject]@{ Origin = $code.Origin; Length = [uint32]($code.Length + $descriptor.Length) }
}

function Convert-Size([string] $value) {
    if ($value -match '^0x') { return [Convert]::ToUInt32($value.Substring(2), 16) }
    if ($value.EndsWith('K')) { return [uint32]([uint32]$value.TrimEnd('K') * 1024) }
    return [uint32]$value
}

function Assert-Equal([string] $Name, [uint32] $Actual, [uint32] $Expected) {
    if ($Actual -ne $Expected) { throw "$Name expected 0x$('{0:X8}' -f $Expected), got 0x$('{0:X8}' -f $Actual)" }
    Write-Host "PASS $Name = 0x$('{0:X8}' -f $Actual)"
}

$boot = Read-GnuFlash $BootLinker
$app = Read-GnuFlash $AppLinker
if ($null -eq $boot) { throw "Cannot parse GNU Flash region from $BootLinker" }
if ($null -eq $app) { throw "Cannot parse GNU Flash region from $AppLinker" }

Assert-Equal 'Boot origin' $boot.Origin $FlashBase
Assert-Equal 'Boot length' $boot.Length $BootSize
Assert-Equal 'App origin' $app.Origin $AppStart
$otaEnd = [uint64]$OtaInfoStart + $OtaInfoSize
$appEnd = [uint64]$app.Origin + $app.Length
if ($ExpectedAppEnd -eq 0) {
    $ExpectedAppEnd = if ($NoConfigRecovery) { $BootStateAStart } else { $ConfigRecoveryStart }
}
if ($appEnd -ne $ExpectedAppEnd) { throw 'App Flash region does not match the declared product boundary' }
if (-not $NoConfigRecovery) {
    if ($ExpectedAppEnd -ne $ConfigRecoveryStart) { throw 'App Flash region must end at the dedicated config recovery page' }
    if ($ConfigRecoveryStart + $BootStatePageSize -gt $BootStateAStart) { throw 'Config recovery page overlaps BootState A' }
}
if ($appEnd -gt $OtaInfoStart) { throw 'App Flash region overlaps OTA info page' }
if ($appEnd -gt $BootStateAStart) { throw 'App Flash region overlaps BootState A' }
if ($BootStateAStart + $BootStatePageSize -gt $BootStateBStart) { throw 'BootState A overlaps BootState B' }
if ($BootStateBStart + $BootStatePageSize -gt $OtaInfoStart) { throw 'BootState B overlaps OTA info page' }
if ($otaEnd -gt ([uint64]$FlashBase + $FlashSize)) { throw 'OTA info page is outside declared device Flash capacity' }
Write-Host "PASS BootState A 0x$('{0:X8}' -f $BootStateAStart)-0x$('{0:X8}' -f ($BootStateAStart + $BootStatePageSize - 1))"
if (-not $NoConfigRecovery) {
    Write-Host "PASS config recovery 0x$('{0:X8}' -f $ConfigRecoveryStart)-0x$('{0:X8}' -f ($ConfigRecoveryStart + $BootStatePageSize - 1))"
}
Write-Host "PASS BootState B 0x$('{0:X8}' -f $BootStateBStart)-0x$('{0:X8}' -f ($BootStateBStart + $BootStatePageSize - 1))"
Write-Host "PASS OTA page 0x$('{0:X8}' -f $OtaInfoStart)-0x$('{0:X8}' -f ($otaEnd - 1))"
