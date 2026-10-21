# Build a worker variant from any source file WITHOUT touching app/worker.c or
# build/BTA-Audio.bin (the installed/boot build). Same flags and object order as
# build_app.ps1, so a given source builds to the same bytes either way.
#   powershell -ExecutionPolicy Bypass -File build_variant.ps1 -Worker variants/src/worker_2.5_nokeepalive.c -Out out/BTA-Audio-2.5.bin
param(
    [Parameter(Mandatory)][string]$Worker,
    [Parameter(Mandatory)][string]$Out
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$workspace = Split-Path -Parent $root
$clang = Join-Path $workspace 'toolchain/llvm/bin/clang.exe'
$linker = Join-Path $workspace 'toolchain/llvm/bin/ld.lld.exe'
$Worker = (Resolve-Path -LiteralPath $Worker).Path
$Out = [IO.Path]::GetFullPath((Join-Path (Get-Location) $Out))
$build = Join-Path $root ('build/variants/' + [IO.Path]::GetFileNameWithoutExtension($Out))
New-Item -ItemType Directory -Force -Path $build | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
$html = [IO.File]::ReadAllBytes((Join-Path $root 'app/web.html'))
$array = ($html | ForEach-Object { $_.ToString() }) -join ','
[IO.File]::WriteAllText((Join-Path $build 'web_page.h'), "static const char web_page[] = {$array,0};")
$app = Join-Path $root 'app'
$flags = @('--target=x86_64-pc-freebsd12-elf','-O2','-fPIE','-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-red-zone','-fno-asynchronous-unwind-tables','-Wall','-Wextra','-Werror','-I', $build, '-I', $app)
$objects = @()
foreach ($item in @(@('launcher', (Join-Path $app 'launcher.c')), @('worker', $Worker))) {
    $obj = Join-Path $build "$($item[0]).o"
    & $clang @flags -c $item[1] -o $obj
    if ($LASTEXITCODE) { throw "Compile failed: $($item[0])" }
    $objects += $obj
}
$obj = Join-Path $build 'bt_accept_patch.o'
& $clang @flags -mgeneral-regs-only '-D_start=bta_apply_patch' -c (Join-Path $root 'probe/bt_accept_patch.c') -o $obj
if ($LASTEXITCODE) { throw 'Compile failed: bt_accept_patch' }
$objects += $obj
$obj = Join-Path $build 'bt_devices.o'
& $clang @flags -mgeneral-regs-only -c (Join-Path $app 'bt_devices.c') -o $obj
if ($LASTEXITCODE) { throw 'Compile failed: bt_devices' }
$objects += $obj
& $linker --oformat=binary -T (Join-Path $app 'payload.ld') @objects -o $Out
if ($LASTEXITCODE) { throw 'Binary link failed' }
$f = Get-Item -LiteralPath $Out
Write-Output ("Built {0} ({1} bytes, sha256 {2})" -f $f.FullName, $f.Length, (Get-FileHash -Algorithm SHA256 -LiteralPath $Out).Hash.Substring(0,16).ToLower())
