[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProductRoot)
$ErrorActionPreference = 'Stop'
$product = (Resolve-Path $ProductRoot).Path
$platform = Join-Path $product 'platform'
$build = Join-Path $product 'app/build/host-tests/recovery'
$compiler = (Get-Command gcc -ErrorAction Stop).Source
& cmake -S $platform -B $build -G Ninja "-DCMAKE_C_COMPILER=$compiler" '-DCMAKE_BUILD_TYPE=Debug' '-DBUILD_TESTING=ON' "-DABOX_STAGE2_PRODUCT_ROOT=$product"
if ($LASTEXITCODE -ne 0) { throw 'Recovery host configure failed' }
& cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Recovery host build failed' }
& ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Recovery host tests failed' }
