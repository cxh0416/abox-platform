[CmdletBinding()]
param(
    [string] $Workspace = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)
)

$ErrorActionPreference = 'Stop'
$expectedMcu = 'STM32F105R(8-B-C)Tx'
$expectedPackage = 'LQFP64'
$products = @(
    @{ Name = 'Top_Flying_Wing'; Uart5 = 115200; Can1 = 9; Can2 = 4; Boot = 'platform/targets/stm32f105_abox_boot/STM32F105xx_FLASH.ld'; App = 'app/STM32F105xx_FLASH.ld'; ConfigRequired = $false; BootV2 = $true },
    @{ Name = 'Airport_vehicle'; Uart5 = 115200; Can1 = 9; Can2 = 4; Boot = 'boot/STM32F105XX_FLASH.ld'; App = 'app/STM32F105XX_FLASH.ld'; BootV2 = $false },
    @{ Name = 'CageDumper'; Uart5 = 115200; Can1 = 4; Can2 = 4; Boot = 'boot/STM32F105XX_FLASH.ld'; App = 'app/STM32F105XX_FLASH.ld'; BootV2 = $false },
    @{ Name = 'LockCtrlBoard_cheweishi'; Uart5 = 115200; Can1 = 9; Can2 = 4; Boot = 'platform/targets/stm32f105_abox_boot/STM32F105xx_FLASH.ld'; App = 'app/STM32F105XX_FLASH.ld'; BootV2 = $true },
    @{ Name = 'Meal_Delivery_Vehicle'; Uart5 = 115200; Can1 = 9; Can2 = 4; Boot = 'platform/targets/stm32f105_abox_boot/STM32F105xx_FLASH.ld'; App = 'app/STM32F105XX_FLASH.ld'; BootV2 = $true },
    @{ Name = 'Sweeper_VCU'; Uart5 = 115200; Can1 = 4; Can2 = 4; Boot = 'platform/targets/stm32f105_abox_boot/STM32F105xx_FLASH.ld'; App = 'app/STM32F105xx_FLASH.ld'; BootV2 = $true }
)

function Assert-Contains([string] $Path, [string] $Pattern, [string] $Label) {
    if (-not (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)) { throw "$Label missing in $Path" }
}

$flashScript = Join-Path $PSScriptRoot 'validate_flash_layout.ps1'
foreach ($product in $products) {
    $root = Join-Path $Workspace $product.Name
    $ioc = Get-ChildItem (Join-Path $root 'app') -Filter *.ioc -File | Select-Object -First 1
    if ($null -eq $ioc) { throw "No App IOC found for $($product.Name)" }
    Assert-Contains $ioc.FullName "^Mcu.Name=$([regex]::Escape($expectedMcu))$" "$($product.Name) MCU"
    Assert-Contains $ioc.FullName "^Mcu.Package=$([regex]::Escape($expectedPackage))$" "$($product.Name) package"

    $cfg = Join-Path $root 'platform_config/abox_product_config.h'
    if (Test-Path -LiteralPath $cfg) {
        Assert-Contains $cfg 'ABOX_PRODUCT_CONFIG' "$($product.Name) platform config"
    } elseif ($product.ConfigRequired -ne $false) {
        throw "Missing product config: $cfg"
    }

    $uart = Join-Path $root 'app/Core/Src/usart.c'
    Assert-Contains $uart "huart5\.Init\.BaudRate\s*=\s*$($product.Uart5)" "$($product.Name) UART5 base baud"
    Assert-Contains $ioc.FullName "^CAN1\.Prescaler=$($product.Can1)$" "$($product.Name) CAN1 prescaler"
    Assert-Contains $ioc.FullName "^CAN2\.Prescaler=$($product.Can2)$" "$($product.Name) CAN2 prescaler"

    $bootLinker = Join-Path $root $product.Boot
    $appLinker = Join-Path $root $product.App
    if ($product.BootV2) {
        $layout = @{}
        if ($product.Name -in @('Top_Flying_Wing', 'LockCtrlBoard_cheweishi', 'Sweeper_VCU')) {
            $layout.NoConfigRecovery = $true
        } elseif ($product.Name -eq 'Meal_Delivery_Vehicle') {
            $layout.NoConfigRecovery = $true
            $layout.ExpectedAppEnd = 0x0803E000
        }
        & $flashScript -BootLinker $bootLinker -AppLinker $appLinker @layout | Out-Host
    } else {
        Assert-Contains $bootLinker 'ORIGIN\s*=\s*0x0?8000000\s*,\s*LENGTH\s*=\s*(0x8000|32K)' "$($product.Name) legacy Boot region"
        Assert-Contains $appLinker 'ORIGIN\s*=\s*0x0?8008000\s*,\s*LENGTH\s*=\s*(0x37800|222K)' "$($product.Name) legacy App region"
        Write-Host "PASS $($product.Name) legacy Flash layout (no Boot State pages)"
    }
    Write-Host "PASS $($product.Name) hardware/config matrix"
}
