# PS4 Bluetooth Audio (experimental)

Homebrew for a **jailbroken PS4 on firmware 13.520.001**. It sends game audio from the
console's own Bluetooth radio straight to Bluetooth Classic headphones, with no PC in the
audio path. TV sound can be muted or kept alongside.

Tested and confirmed audible on QCY AilyBuds Pro and Baseus Bass BS1 NC. Other headphones may or may
not work. Output only: no microphone or chat input.

> **Status: experimental, one console, one firmware.** Use it at your own risk and only on a
> console you own. Other firmware versions are unsupported: the payload checks kernel
> instruction fingerprints before patching and refuses to run if they don't match.

## Security note

The control page (port 8097) has no login. Anyone on your local network can scan, change
volume, connect or stop the service. Use it on a trusted home network only.

## Known issues

- **Audio can still break briefly on some headphones.** QCY AilyBuds Pro is smooth; Baseus Bass
  BS1 NC is much better since 2.6 but can still glitch when a game is loading, because the audio
  service is paused for a few tens of milliseconds. Some other models would not connect
  reliably, even from the PS4's own Bluetooth settings. Results vary by headphone model.
- **Silence after a game change (fixed in 2.6).** The console's capture mix could go silent
  mid-session until you reconnected. 2.6 re-attaches to it by itself after a few seconds of silence.
- **Controller drops.** An early build polled the Bluetooth driver while streaming and
  starved the wireless controller's link. The current worker does no driver polling while
  streaming, which fixed it on the test console. Test with your own controller.
- A full reboot plus AutoRun cycle and rest-mode wake have had limited testing.
- Longer numeric PINs (via the browser controls) are untested.
- The patch lives only in RAM and disappears at reboot. Nothing persistent in system
  firmware is modified.

## What's here

| Path | Contents |
|---|---|
| `dist/Bluetooth-Audio-2.6.pkg` | Prebuilt home-screen app (bundles the same no-keepalive payload as `payload/`) |
| `payload/BTA-Audio-2.6.bin` | Prebuilt flat payload for GoldHEN (sha256 `60f134a75c18adcfe1835bfed08fca7af94e4814fc8e057767d358bc5303d3b6`) |
| `app/` | Payload source: launcher, worker (A2DP bridge and web UI), device cache |
| `native/` | Home-screen app source (OpenOrbis) and asset generator |
| `probe/bt_accept_patch.c` | In-RAM Bluetooth acceptance patch |
| `variants/src/` | Alternative worker builds kept for A/B testing |
| `tools/` | Install and deploy scripts |

## Install

You need a jailbroken PS4 on firmware 13.520.001 running GoldHEN, with its FTP server
enabled (port 2121) so you can copy files. Use either option below, or both.

### Option A: home-screen app (PKG)

`dist/Bluetooth-Audio-2.6.pkg` installs a **Bluetooth Audio** app (title ID `BTAD00001`) on the
home screen. It bundles the audio payload: when you open the app and the service isn't
running, it sends the payload to GoldHEN's BinLoader (port 9090) for you. For the service to
also start after every jailbreak without opening the app, add the AutoRun entry from Option B.

1. Copy the PKG over FTP to **`/data/pkg/Bluetooth-Audio-2.6.pkg`** on the PS4 (create the
   `pkg` folder if it doesn't exist).
2. In GoldHEN's settings menu, open Debug Settings, set the package source to **HDD**, then
   run **Package Installer** and install it. If an older version is installed, delete it from
   the home screen first (Options, Delete).
3. Make sure GoldHEN's **BinLoader (port 9090)** is enabled. It accepts one payload per
   jailbreak (any connection uses it up), so either let the app start the service **or**
   send it from your PC with Option B, not both. Then open **Bluetooth Audio** from
   the home screen. Triangle scans, the d-pad selects, X connects (or starts the service if it
   is offline), Square toggles TV sound, Circle closes the app. The audio service keeps running.
4. If the service still doesn't start, check `/data/bta_audio/app.log` on the console, or send the
   payload yourself with Option B below.

### Option B: payload only (no app)

Send it from your PC without touching the PS4's file system. **Never open a bare connection
to port 9090**: the loader treats any connection as a payload and stops listening until you
reboot and re-jailbreak.

```powershell
python tools/deploy_variant.py payload/BTA-Audio-2.6.bin --host <PS4_IP> --connect
```

To start it automatically after each jailbreak, install it as a GoldHEN AutoRun payload:

```powershell
python tools/install_bta.py --host <PS4_IP> --payload payload/BTA-Audio-2.6.bin
python tools/install_bta.py --host <PS4_IP> --disable-autorun   # undo
```

### Where files end up on the PS4

| Path on the PS4 | What it is |
|---|---|
| `/data/pkg/Bluetooth-Audio-2.6.pkg` | PKG you copy there before installing (Option A) |
| `/data/payloads/BTA-Audio.bin` | Boot payload written by `install_bta.py` (Option B) |
| `/data/GoldHEN/payloads.ini` | GoldHEN AutoRun list. The installer adds `/user/data/payloads/BTA-Audio.bin = 1` under `[AutoRun]` and keeps your other entries |
| `/data/bta_audio/` | Runtime state: saved headphones (`device.bin`), volume, `status.json`, logs |
| `/data/bta_audio/stop` | Create this empty file over FTP to request a clean shutdown |
| `/user/app/BTAD00001/` | Where the PKG installs the app (managed by the system) |

`install_bta.py` backs up any file it replaces into an `install_backups/` folder on your PC.

### Using it

Put your headphones in pairing mode, then scan and pick them from the app or from
`http://<PS4_IP>:8097/` in a browser on your network. **Stop audio** restores TV sound. The
selected headphones are remembered across restarts, and AutoRun reconnects them after the
jailbreak, so switch them on first.

## Build from source

Windows, PowerShell. The scripts look for a folder named `toolchain` **beside** this repo
folder (that is, `..\toolchain` relative to the repo root): LLVM in `toolchain/llvm/bin`, and
[OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) in
`toolchain/OpenOrbis/PS4Toolchain`. The home-screen app also needs Python with Pillow and
matplotlib (its bundled DejaVu fonts are used for the UI text) and a .NET runtime for PkgTool.

```powershell
mkdir dist
powershell -ExecutionPolicy Bypass -File build_app.ps1      # build/BTA-Audio.bin (run first)
powershell -ExecutionPolicy Bypass -File build_native.ps1   # dist/Bluetooth-Audio-2.6.pkg
powershell -ExecutionPolicy Bypass -File build_variant.ps1 -Worker variants/src/worker_2.5_nokeepalive.c -Out out/test.bin
```

`build_native.ps1` bundles `build/BTA-Audio.bin`, so run `build_app.ps1` first.

## Credits and licenses

- Font assets: DejaVu, see `native/LICENSE_DEJAVU.txt`.
- AutoRun entry format: documented in the [orbisRPC installer](https://github.com/SirHumza/orbisRPC/tree/main/installer); no code from it is included.
- Requires [GoldHEN](https://github.com/GoldHEN/GoldHEN) (not included).
- The PKG contains OpenOrbis sample modules (`libc.prx`, `libSceFios2.prx`, `right.sprx`)
  from the [OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain). They are
  not covered by this project's license.

This project's own code is released under the [MIT License](LICENSE).
