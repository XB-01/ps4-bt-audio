$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$workspace = Split-Path -Parent $root
$sdk = Join-Path $workspace 'toolchain/OpenOrbis/PS4Toolchain'
$clang = Join-Path $workspace 'toolchain/llvm/bin/clang.exe'
$link = Join-Path $workspace 'toolchain/llvm/bin/ld.lld.exe'
$bin = Join-Path $sdk 'bin/windows'
$out = Join-Path $root 'build/native'
$pkg = Join-Path $out 'package'
$env:DOTNET_ROLL_FORWARD = 'Major'
$env:OO_PS4_TOOLCHAIN = $sdk
python (Join-Path $root 'native/make_assets.py')
if ($LASTEXITCODE) { throw 'Asset generation failed' }
foreach ($folder in @('sce_sys/about','sce_module','assets')) { New-Item -ItemType Directory -Force -Path (Join-Path $pkg $folder) | Out-Null }
& $clang --target=x86_64-pc-freebsd12-elf -O2 -fPIC -funwind-tables -isysroot $sdk -isystem (Join-Path $sdk 'include') -I $out -Wall -Wextra -Wno-misleading-indentation -c (Join-Path $root 'native/main.c') -o (Join-Path $out 'main.o')
if ($LASTEXITCODE) { throw 'Native compile failed' }
& $link (Join-Path $out 'main.o') (Join-Path $sdk 'lib/crt1.o') -m elf_x86_64 -pie --script (Join-Path $sdk 'link.x') --eh-frame-hdr -L (Join-Path $sdk 'lib') -lc -lkernel -lSceVideoOut -lScePad -lSceUserService -lSceSystemService -o (Join-Path $out 'BluetoothAudio.elf')
if ($LASTEXITCODE) { throw 'Native link failed' }
# Relative paths: create-fself records the -in path inside eboot.bin, so an absolute one would embed the build machine's folder names.
Push-Location $out
try { & (Join-Path $bin 'create-fself.exe') '-in=BluetoothAudio.elf' '-out=BluetoothAudio.oelf' --eboot 'package/eboot.bin' --paid 0x3800000000000011 } finally { Pop-Location }
if ($LASTEXITCODE) { throw 'Native fself failed' }
Copy-Item -LiteralPath (Join-Path $out 'icon0.png') -Destination (Join-Path $pkg 'sce_sys/icon0.png') -Force
Copy-Item -LiteralPath (Join-Path $sdk 'samples/hello_world/sce_sys/about/right.sprx') -Destination (Join-Path $pkg 'sce_sys/about/right.sprx') -Force
foreach ($module in @('libc.prx','libSceFios2.prx')) { Copy-Item -LiteralPath (Join-Path $sdk "samples/hello_world/sce_module/$module") -Destination (Join-Path $pkg "sce_module/$module") -Force }
Copy-Item -LiteralPath (Join-Path $root 'build/BTA-Audio.bin') -Destination (Join-Path $pkg 'assets/BTA-Audio.bin') -Force
$sfo = Join-Path $pkg 'sce_sys/param.sfo'
$tool = Join-Path $bin 'PkgTool.Core.exe'
& $tool sfo_new $sfo
if ($LASTEXITCODE) { throw 'SFO create failed' }
$entries = @(
    @('APP_TYPE','Integer','4','1'), @('APP_VER','Utf8','8','02.07'),
    @('ATTRIBUTE','Integer','4','0'), @('CATEGORY','Utf8','4','gd'),
    @('CONTENT_ID','Utf8','48','IV0000-BTAD00001_00-BTAUDIO000000001'),
    @('DOWNLOAD_DATA_SIZE','Integer','4','0'), @('SYSTEM_VER','Integer','4','0'),
    @('TITLE','Utf8','128','Bluetooth Audio'), @('TITLE_ID','Utf8','12','BTAD00001'),
    @('VERSION','Utf8','8','02.07')
)
foreach ($entry in $entries) {
    & $tool sfo_setentry $sfo $entry[0] --type $entry[1] --maxsize $entry[2] --value $entry[3]
    if ($LASTEXITCODE) { throw "SFO entry failed: $($entry[0])" }
}
Push-Location $pkg
try {
    & (Join-Path $bin 'create-gp4.exe') -out package.gp4 --content-id=IV0000-BTAD00001_00-BTAUDIO000000001 --files 'eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx sce_module/libc.prx sce_module/libSceFios2.prx assets/BTA-Audio.bin'
    if ($LASTEXITCODE) { throw 'GP4 generation failed' }
    & $tool pkg_build package.gp4 $out
    if ($LASTEXITCODE) { throw 'PKG build failed' }
} finally { Pop-Location }
Copy-Item -LiteralPath (Join-Path $out 'IV0000-BTAD00001_00-BTAUDIO000000001.pkg') -Destination (Join-Path $root 'dist/Bluetooth-Audio-2.7.pkg') -Force
& $tool pkg_validate (Join-Path $root 'dist/Bluetooth-Audio-2.7.pkg')
if ($LASTEXITCODE) { throw 'PKG validation failed' }
Write-Output 'Built and validated Bluetooth-Audio-2.7.pkg'
