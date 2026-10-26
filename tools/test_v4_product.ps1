[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ProductRoot,
    [Parameter(Mandatory)] [string] $ServiceRoot,
    [Parameter(Mandatory)] [string] $Product,
    [Parameter(Mandatory)] [string] $PacketTest,
    [Parameter(Mandatory)] [string] $IdentityTest,
    [Parameter(Mandatory)] [string] $LifecycleTest,
    [string] $Compiler = 'D:/MinGW/bin/gcc.exe'
)
$ErrorActionPreference = 'Stop'
$platform = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$productPath = (Resolve-Path -LiteralPath $ProductRoot).Path
$servicePath = (Resolve-Path -LiteralPath $ServiceRoot).Path
foreach ($source in @($platform, (Join-Path $productPath 'tests'))) {
    $build = if ($source -eq $platform) { Join-Path $platform 'build/host' } else { Join-Path $productPath 'build/host' }
    & cmake -S $source -B $build -G Ninja "-DCMAKE_C_COMPILER=$Compiler" '-DBUILD_TESTING=ON' '-DCMAKE_BUILD_TYPE=Debug'
    if ($LASTEXITCODE -ne 0) { throw 'V4 host configure failed' }
    & cmake --build $build --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'V4 host build failed' }
    & ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'V4 host regression failed' }
}
& python (Join-Path $PSScriptRoot 'verify_v4_product.py') --product $Product --service-root $servicePath `
    --packet-test (Join-Path $productPath "build/host/$PacketTest") `
    --identity-test (Join-Path $productPath "build/host/$IdentityTest") `
    --lifecycle-test (Join-Path $productPath "build/host/$LifecycleTest")
if ($LASTEXITCODE -ne 0) { throw 'Compiled product/service V4 contract failed' }
