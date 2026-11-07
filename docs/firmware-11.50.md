# PS4 11.50 port

This checkout adds a separate 11.50 payload build. The default build remains
13.520.001. Do not send the original bundled 13.52 payload to an 11.50 console.

## Build and use

Python 3, Clang and LLD are sufficient; no OpenOrbis SDK is needed for the flat
payload. Run from the repository root:

```sh
python tools/build_payload.py --firmware 11.50 --out build/BTA-Audio-11.50.bin
python tools/deploy_variant.py build/BTA-Audio-11.50.bin --host <PS4_IP>
```

Open `http://<PS4_IP>:8097/`, scan, select the headphones and connect. The
existing home-screen app can control the running service. Its original bundled
payload still targets 13.52; build a new package with the 11.50 payload or use
GoldHEN AutoRun to start the service on future jailbreaks:

```sh
python tools/install_bta.py --host <PS4_IP> --payload build/BTA-Audio-11.50.bin
```

The distributable payload is `payload/BTA-Audio-11.50.bin` (44,290 bytes), SHA-256
`037a364725a5bcf2413cc962083cce48106a52e208416813f997de825c41b43d`.

To build a package, produce `build/BTA-Audio.bin` using the Python builder with
`--firmware 11.50`, then run the existing `build_native.ps1` with its documented
Windows/OpenOrbis prerequisites. That packages the already-built payload.

## Address provenance

Addresses below were checked against kernel memory read from a jailbroken
test console on 2026-11-07 using the official GoldHEN ps4debug payload.
The captured first 16 MiB has SHA-256
`b096193e5e631eeae797bd60fad7285a3c790c104da70bd4e81edf9362eb24ce`.
The private capture is in the ignored `build/fw1150/` directory and is not
included in the distributable port.

| Purpose | Kernel-relative offset |
| --- | --- |
| Bluetooth acceptance check | `0x5a0844` |
| `copyout` | `0x2bd4a0` |
| Device cache lookup | `0x5b6a80` |
| HCI command function | `0x5ad8c0` |
| Bluetooth lock | `0x5a6900` |
| Bluetooth unlock | `0x5a6980` |
| Bluetooth context pointer | `0x2612518` |
| HCI packet-type format (`22\0`) | `0xad4082` |

The acceptance sequence is exactly `83 78 20 40 75 2a 49 8b`, matching the
upstream check's semantics. Its replacement and atomic compare/exchange are
unchanged. The patch also checks the cache/HCI function fingerprints before
writing. The 11.50 profile additionally fingerprints lock/unlock, `copyout`,
cache bounds/stride and the HCI format string before using them.

Disassembly confirms the existing cache bounds `0x18da8..0x20ea8`, stride
`0x408`, name offset `0x80`, connection stride `0x1588`, cached-device pointer
at `0x638`, and packet-type state at `0x1560`/`0x1564`. A live cache read
returned the wireless controller and surrounding Bluetooth device names at
those positions. The primary context pointer at `0x2612518` and the driver's
controller-slot pointer at `0x2612550` pointed to the same context during this
session.

The console reports software version `11.508.011` (`0x11508011`). The 11.50
build checks the upper 16 version bits, then requires the kernel fingerprints;
it does not assume the low build bits equal `0001`. The default 13.52 build
retains its exact version check.

## Validation

- Both firmware profiles compile with the original warning-as-error flags.
- The 13.52 profile has build validation only; it was not tested on a 13.52
  console during this port.
- All 11.50 instruction and format fingerprints match the captured kernel.
- The service starts on 11.50 and returns its web UI and status API.
- Scanning finds Nothing Ear (a); selection and the in-memory acceptance patch
  succeed.
- Connections reached driver state 6, including a continuous session with more
  than 14,000 packets, zero reported errors and no automatic reconnects.
- The test-tone send returned 768 bytes. The listener subsequently confirmed
  audible playback in games and in the PS4 Spotify app through Nothing Ear (a).
- The listener reports occasional small audio glitches. Their frequency and
  cause have not been measured; successful send counters do not establish
  glitch-free playback.
- The exact payload was uploaded to `/data/payloads/BTA-Audio.bin`, read back
  and verified, and enabled in GoldHEN AutoRun. Replaced-file backups and the
  install manifest are under the ignored `install_backups/` directory.

These checks cover one console. Reboot, rest mode, other headphones and other
11.50 builds have not been validated. The upstream experimental limitations
still apply.
