# Experimental PS4 12.52 audio payload

Work in progress. Not a stable release.

## Included
- audio/BTA-Audio-12.52-clockfix.bin
- SHA256SUMS.csv

This is the exact candidate deployed and tested in the latest
recorded session, not a newly rebuilt replacement.

Source changes: commit deb4560.
Build command:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./build_variant.ps1 -Worker app/worker.c -Out out/BTA-Audio-12.52-clockfix.bin

## Test results
- Firmware: PS4 12.52.
- Headphones: Samsung Galaxy Buds3 Pro.
- Game audio played.
- Audible dropouts and noticeable latency remained.
- The tester perceived improvement, without controlled measurement.
- An earlier session froze the console. The freeze did not recur in
  the latest reported session, but its cause remains unresolved.
- One automatic recovery was recorded in the latest session.

## Related research materials not included
A local dumper-fixed3 tool and earlier diagnostic payloads were used
or investigated during the work, but are not included in this commit.
The dumper source/binary correspondence and redistribution permissions
have not been verified.

Local dumper provenance under investigation:
Scene-Collective/ps4-kernel-dumper, base commit 42fce7e.
The upstream README credits eversion as the original creator.

No kernel dump or raw memory capture is included.
