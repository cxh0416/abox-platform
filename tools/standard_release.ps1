# Shared App-only build and bundle primitives. Products supply metadata for
# their existing manifest ABI and run their own extra gates before publishing.
function Invoke-ABoxAppBuild {
    param([string] $ProductRoot, [string] $BuildDirectory, [string] $Target,
          [ValidateSet('Release', 'Debug')] [string] $Configuration,
          [string[]] $Definitions = @(), [string] $ServiceStackBudget = '')
    $source = Join-Path $ProductRoot 'app'
    $toolchain = Join-Path $source 'cmake/gcc-arm-none-eabi.cmake'
    & cmake -S $source -B $BuildDirectory -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_C_FLAGS=-fstack-usage" @Definitions | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'App configure failed' }
    & cmake --build $BuildDirectory --target $Target --parallel 4 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'App build failed' }
    $budget = if ($ServiceStackBudget) { $ServiceStackBudget } else { Join-Path $ProductRoot 'tools/service_stack_budget.json' }
    if (Test-Path -LiteralPath $budget) {
        & python (Join-Path $PSScriptRoot 'verify_service_stack.py') --build $BuildDirectory --budget $budget | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'App service stack budget failed' }
    }
    $bin = Join-Path $BuildDirectory "$Target.bin"
    $elf = Join-Path $BuildDirectory "$Target.elf"
    if (!(Test-Path -LiteralPath $bin -PathType Leaf) -or !(Test-Path -LiteralPath $elf -PathType Leaf)) {
        throw 'Missing App build output'
    }
    return [ordered]@{ bin = $bin; elf = $elf }
}

function Get-ABoxReleaseArtifact {
    param([string] $Path, [string] $LoadAddress,
          [ValidateSet('length', 'size')] [string] $SizeField = 'length')
    $bytes = [IO.File]::ReadAllBytes($Path)
    [uint32] $crc = 0xFFFFFFFFu
    foreach ($value in $bytes) {
        $crc = $crc -bxor [uint32]$value
        for ($bit = 0; $bit -lt 8; $bit++) {
            $crc = ($crc -shr 1) -bxor $(if ($crc -band 1) { [uint32]0xEDB88320u } else { [uint32]0 })
        }
    }
    $record = [ordered]@{ name = [IO.Path]::GetFileName($Path) }
    $record[$SizeField] = $bytes.Length
    $record.load_address = $LoadAddress
    $record.crc32 = ('{0:X8}' -f ($crc -bxor [uint32]0xFFFFFFFFu))
    $record.sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    $record.includes_device_config = $false
    $record.includes_boot_state = $false
    return $record
}

function New-ABoxReleaseImages {
    param([string] $AppPath, [string] $BootPath, [string] $Stage,
          [string] $ContractPath, [string] $Version)
    $contract = Get-Content -LiteralPath $ContractPath -Raw | ConvertFrom-Json
    $boot = [IO.File]::ReadAllBytes($BootPath)
    $app = [IO.File]::ReadAllBytes($AppPath)
    if ($boot.Length -ne 0x8000 -or
        (Get-FileHash -LiteralPath $BootPath -Algorithm SHA256).Hash -ne $contract.frozen_boot_sha256) {
        throw 'Frozen Boot does not match release contract'
    }
    if ($app.Length -gt $contract.app_max_size -or
        ![Text.Encoding]::ASCII.GetString($app).Contains($contract.version_marker_prefix + $Version + [char]0)) {
        throw 'App size or version marker does not match release contract'
    }
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null
    $appOut = Join-Path $Stage $contract.app_name
    $bootOut = Join-Path $Stage $contract.boot_name
    $fullOut = Join-Path $Stage $contract.full_name
    [IO.File]::WriteAllBytes($appOut, $app)
    [IO.File]::WriteAllBytes($bootOut, $boot)
    $full = [byte[]]::new($boot.Length + $app.Length)
    [Array]::Copy($boot, 0, $full, 0, $boot.Length)
    [Array]::Copy($app, 0, $full, $boot.Length, $app.Length)
    [IO.File]::WriteAllBytes($fullOut, $full)
    return [ordered]@{ app = $appOut; boot = $bootOut; full = $fullOut }
}
