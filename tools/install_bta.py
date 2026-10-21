"""Install Bluetooth Audio and manage its single GoldHEN AutoRun entry over FTP.

Backs up every replaced remote file locally. Does not launch or reboot the PS4.
"""
import argparse
from datetime import datetime
import ftplib
import hashlib
import io
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = '/data/payloads/BTA-Audio.bin'
INI = '/data/GoldHEN/payloads.ini'
ENTRY = '/user/data/payloads/BTA-Audio.bin'


def retrieve(ftp, path):
    data = bytearray()
    try:
        ftp.retrbinary('RETR ' + path, data.extend)
    except ftplib.error_perm as exc:
        if str(exc).startswith('550'):
            return None
        raise
    return bytes(data)


def edit_autorun(data, enabled):
    text = (data or b'').decode('utf-8-sig')
    lines = text.splitlines(keepends=True)
    newline = '\r\n' if '\r\n' in text else '\n'
    output, section, found, inserted = [], '', False, False
    entry_line = ENTRY + (' = 1' if enabled else ' = 0') + newline
    for line in lines:
        match = re.match(r'^\s*\[([^]]+)\]', line)
        if match:
            if section == 'AutoRun' and not inserted:
                if output and not output[-1].endswith(('\n', '\r')):
                    output[-1] += newline
                output.append(entry_line)
                inserted = True
            section = match[1]
            if section == 'AutoRun':
                found = True
        if section == 'AutoRun' and line.split('=', 1)[0].strip() in (ENTRY, PAYLOAD):
            if not inserted:
                output.append(entry_line)
                inserted = True
        else:
            output.append(line)
    if not inserted:
        if output and not output[-1].endswith(('\n', '\r')):
            output[-1] += newline
        if not found:
            output.append(newline + '[AutoRun]' + newline)
        output.append(entry_line)
    return ''.join(output).encode('utf-8')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True, help='PS4 LAN IP address')
    p.add_argument('--disable-autorun', action='store_true')
    p.add_argument('--payload', default=str(ROOT / 'payload/BTA-Audio-2.6.bin'),
                   help='payload to install as the boot payload, default: the shipped payload/BTA-Audio-2.6.bin')
    args = p.parse_args()
    backup = ROOT / 'install_backups' / datetime.now().strftime('%Y%m%d-%H%M%S')
    backup.mkdir(parents=True, exist_ok=False)
    report = {'host': args.host, 'files': []}
    with ftplib.FTP() as ftp:
        ftp.connect(args.host, 2121, timeout=8)
        ftp.login()

        def replace(path, data):
            previous = retrieve(ftp, path)
            if previous == data:
                print('Already current:', path)
                return
            if previous is not None:
                (backup / path.strip('/').replace('/', '__')).write_bytes(previous)
            # Keep staging out of the loader's .bin list.
            staging = path + '.bta-staging'
            ftp.storbinary('STOR ' + staging, io.BytesIO(data))
            if retrieve(ftp, staging) != data:
                raise RuntimeError('Upload verification failed: ' + staging)
            ftp.rename(staging, path)
            if retrieve(ftp, path) != data:
                raise RuntimeError('Installed file verification failed: ' + path)
            report['files'].append({'path': path, 'existed': previous is not None,
                                    'sha256': hashlib.sha256(data).hexdigest()})
            print('Installed and verified:', path)

        if not args.disable_autorun:
            replace(PAYLOAD, Path(args.payload).read_bytes())
        previous = retrieve(ftp, INI)
        replace(INI, edit_autorun(previous, not args.disable_autorun))
    (backup / 'manifest.json').write_text(json.dumps(report, indent=2))
    print('AutoRun disabled.' if args.disable_autorun else 'GoldHEN AutoRun entry enabled.')
    print('Backup:', backup)


if __name__ == '__main__':
    main()
