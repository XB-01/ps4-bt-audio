"""Swap the running audio worker for any payload .bin, in RAM, with no reboot.

  python tools/deploy_variant.py payload/BTA-Audio-2.7.5.bin --host <PS4_IP> --connect

A swap cuts live audio for ~6-15 s, so this refuses while the worker reports
streaming=1 unless you pass --yes. It never touches the boot payload on disk: a
reboot returns to whatever tools/install_bta.py last installed. The payload goes
to the GoldHEN payloader (9090) in ONE connection - never probe that port, a bare
connection spends its slot. After the final status read it does no polling.
"""
import argparse
import ftplib
import hashlib
import io
import json
from pathlib import Path
import re
import socket
import sys
import time


def port_open(host, port, timeout=2):
    try:
        socket.create_connection((host, port), timeout=timeout).close()
        return True
    except OSError:
        return False


def http(host, method, path, timeout=3):
    s = socket.create_connection((host, 8097), timeout=timeout)
    s.settimeout(timeout)
    control = 'X-BTA-Control: 1\r\nContent-Length: 0\r\n' if method == 'POST' else ''
    s.sendall(f'{method} {path} HTTP/1.1\r\nHost: {host}:8097\r\n{control}Connection: close\r\n\r\n'.encode())
    buf = b''
    while True:
        try:
            chunk = s.recv(65536)
        except OSError:
            break
        if not chunk:
            break
        buf += chunk
    s.close()
    return buf.split(b'\r\n\r\n', 1)[-1].decode('latin1')


def status(host):
    try:
        return json.loads(http(host, 'GET', '/api/status'))
    except (OSError, ValueError):
        return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('payload')
    p.add_argument('--host', required=True, help='PS4 LAN IP address')
    p.add_argument('--connect', action='store_true', help='press Connect once the new worker is up')
    p.add_argument('--yes', action='store_true', help='swap even though audio is streaming')
    p.add_argument('--wait', type=int, default=30, help='seconds to keep retrying a refused payloader connect')
    args = p.parse_args()
    data = Path(args.payload).read_bytes()
    version = re.search(rb'"version":"([0-9a-z.]+)"', data)
    if not version or not 10000 < len(data) < 100000:
        sys.exit('Not an audio worker payload: ' + args.payload)
    print(f'payload {args.payload}: version {version[1].decode()}, {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()[:16]}')

    now = status(args.host) if port_open(args.host, 8097) else None
    if now:
        print(f"running: version {now.get('version')}, streaming={now.get('streaming')}, bt_state={now.get('bt_state')}")
        if now.get('streaming') and not args.yes:
            sys.exit('Audio is streaming - a swap cuts it for ~6-15 s. Tell the listener, then rerun with --yes.')

    ftp = ftplib.FTP()
    ftp.connect(args.host, 2121, timeout=8)
    ftp.login()
    if now is not None:
        ftp.storbinary('STOR /data/bta_audio/stop', io.BytesIO(b'stop\n'))
        print('stop sentinel written; waiting for the old worker to exit...')
        for _ in range(15):
            if not port_open(args.host, 8097):
                break
            time.sleep(1)
        else:
            sys.exit('Old worker is still up after 15 s; not sending the new payload.')
    try:
        ftp.delete('/data/bta_audio/no_widen')
        print('cleared no_widen')
    except ftplib.all_errors:
        pass
    ftp.quit()
    time.sleep(1)

    # A refused connect has no side effects, so retry it; the connection that finally
    # opens is the one that carries the payload (never open one without sending).
    link = None
    for attempt in range(max(1, args.wait // 2)):
        try:
            link = socket.create_connection((args.host, 9090), timeout=6)
            break
        except OSError as exc:
            if not attempt:
                print(f'payloader not accepting ({exc}); retrying for up to {args.wait} s')
            time.sleep(2)
    if link is None:
        sys.exit('Payloader (9090) is not accepting connections. The old worker is already stopped '
                 '(TV sound is back). Launch BTA-Audio from the GoldHEN payload menu, or reboot and let AutoRun start it.')
    link.sendall(data)
    link.shutdown(socket.SHUT_WR)
    try:
        link.recv(64)
    except OSError:
        pass
    link.close()
    print(f'sent {len(data)} bytes to the payloader')

    up = None
    for _ in range(14):
        up = status(args.host)
        if up:
            break
        time.sleep(0.5)
    if not up:
        sys.exit('New worker did not come up on 8097.')
    print(f"up: version {up.get('version')}, idle={not up.get('wanted')}, message={up.get('message')!r}")
    if args.connect:
        http(args.host, 'POST', '/api/connect')
        print('pressed Connect; checking once in 8 s (then no more polling)')
        time.sleep(8)
        s = status(args.host) or {}
        print({k: s.get(k) for k in ('version', 'message', 'bt_state', 'streaming', 'audio_rc', 'packets', 'errors', 'resyncs')})


if __name__ == '__main__':
    main()
