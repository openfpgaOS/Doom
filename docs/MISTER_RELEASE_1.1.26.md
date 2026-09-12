# Doom for MiSTer v1.1.26

Doom's GPU plane encoder uses native floating-point maximum instructions to
compute Q29 bounds without a branch at every corner. The encoded commands,
game rules, saves and 35 Hz simulation remain unchanged.

The focused comparison produces identical bytes for 200,000 finite plane
encodings under sanitizers, and all 24 smoothness regression scenarios pass.
Normal link-order profiling measures a small 0.2–0.4 ms improvement in the
heavy views. Sustained 60 FPS is not established: the tested heavy views still
take about 20–22 ms at 90 MHz and 18–19.4 ms at 100 MHz.

Install the companion
[MiSTer core v0.9.4](https://github.com/openfpgaOS/openfpgaCore/releases/tag/openfpgaos-mister-v0.9.4)
for the DirectFB audio refill fix. In 60-second SS1 measurements, that core
eliminated the mixer sample deficit at both 90 MHz and 100 MHz without a
measurable frame-rate cost. These measurements track sample progression;
they do not substitute for listening confirmation.

The official SDK container rebuilt the normal executable in a fresh object
directory. It is byte-identical to the normal build already installed and
tested on the SS1, including loading the existing first-slot save. The package
version is 1.1.26; the executable contains no profiling or automated input.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `doom.elf` | 1,291,592 | `408a6ebd56eb2f86fa7e2d88342dfc9eac94c437b92de3cfa5b9a671ceb58bff` |

Extract `doom-v1.1.26.zip` at the SD-card root and run the setup script, or
update through MiSTer Downloader. The ZIP contains 29 menu launchers, the
engine, setup scripts and boot/save templates. Commercial WADs remain
user-supplied and existing working game data and saves are preserved.
Downloader payload URLs are pinned to `doom-mister-v1.1.26`; the stable
database remains `main/releases/mister/doom.json.zip`.

The renderer change is shared with Pocket and builds successfully for it,
but this release publishes MiSTer packages. Pocket hardware performance has
not been measured. Package evidence is under `build/release-mister-1.1.26/`;
the GitHub release includes `SHA256SUMS.txt`.
