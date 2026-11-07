"""Build the freestanding payload with Clang/LLD on Linux, macOS or Windows."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--linker', default='ld.lld')
    parser.add_argument('--worker', type=Path, default=root / 'app/worker.c')
    parser.add_argument('--out', type=Path, default=root / 'build/BTA-Audio.bin')
    parser.add_argument('--firmware', choices=['13.52', '11.50'], default='13.52')
    args = parser.parse_args()
    for tool in (args.clang, args.linker):
        if not shutil.which(tool):
            parser.error(f'Tool not found: {tool}')
    build = root / 'build/bta-app'
    build.mkdir(parents=True, exist_ok=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    html = (root / 'app/web.html').read_bytes()
    if args.firmware == '11.50':
        html = html.replace(b'PS4 13.52', b'PS4 11.50')
    (build / 'web_page.h').write_text(
        'static const char web_page[] = {' + ','.join(map(str, html)) + ',0};\n')
    flags = ['--target=x86_64-pc-freebsd12-elf', '-O2', '-fPIE',
             '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
             '-mno-red-zone', '-fno-asynchronous-unwind-tables',
             '-Wall', '-Wextra', '-Werror', '-I', str(build)]
    if args.firmware == '11.50':
        flags.append('-DBTA_FW_1150')
    sources = [(root / 'app/launcher.c', []), (args.worker, []),
               (root / 'probe/bt_accept_patch.c',
                ['-mgeneral-regs-only', '-D_start=bta_apply_patch']),
               (root / 'app/bt_devices.c', ['-mgeneral-regs-only'])]
    objects = []
    for index, (source, extra) in enumerate(sources):
        obj = build / f'{index}-{source.stem}.o'
        subprocess.run([args.clang, *flags, *extra, '-c', str(source),
                        '-o', str(obj)], check=True)
        objects.append(str(obj))
    link = [args.linker, '-T', str(root / 'app/payload.ld'), *objects]
    subprocess.run([*link, '-o', str(build / 'BTA-Audio.debug.elf')], check=True)
    subprocess.run([*link, '--oformat=binary', '-o', str(args.out)], check=True)
    data = args.out.read_bytes()
    print(f'{args.out}: {len(data)} bytes; SHA-256 {hashlib.sha256(data).hexdigest()}')


if __name__ == '__main__':
    main()
