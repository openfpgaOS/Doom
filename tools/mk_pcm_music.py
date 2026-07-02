#!/usr/bin/env python3
"""mk_pcm_music.py - build a Doom-core PCM music WAD from a FLAC OST (not upstream).

i_pcmmusic.c streams raw stereo s16le PCM lumps (P-prefixed, e.g. PE1M1 / PRUNNIN)
from a merged music WAD, with a PCMINFO lump giving the sample rate.  This builds
that WAD:

  * resample each track to 48 kHz with soxr -- matches the Pocket mixer's output
    rate so it does NO runtime resampling (its runtime resampler is only linear);
  * loudness-normalize to a uniform target with a true-peak limiter (EBU R128,
    two-pass loudnorm) so every track sits at the same level and transients are
    limited, not hard-clipped -- play back at unity gain (MASTER_GAIN = 1.0);
  * dither to 16-bit.

Tracks reused under several map-music names share one data block (duplicate
directory entries point at the same offset), as in the stock IWAD layout.

Usage:
  tools/mk_pcm_music.py --game doom2 --ost "/path/to/Doom II OST" --out DOOM2MUS.WAD
  tools/mk_pcm_music.py --game doom1 --ost "/path/to/DOOM OST"    --out DOOMMUS.WAD
  [--I -12] [--TP -1] [--LRA 11]
"""
import argparse, json, os, re, struct, subprocess, sys, tempfile

RATE = 48000
CHANNELS = 2

MAPPINGS = {}

# Doom II / Final Doom (DOOM2.WAD, PLUTONIA.WAD, TNT.WAD).  Directory order =
# stock music lumps (D_ prefix swapped for P).  (PCM lump name, source FLAC);
# tracks listed more than once are deduplicated into one data block.  Mappings
# derived by duration-matching the original music WAD against the OST.
MAPPINGS["doom2"] = [
    ("PRUNNIN", "02. Running From Evil.flac"),
    ("PSTALKS", "04. The Healer Stalks.flac"),
    ("PCOUNTD", "05. Countdown To Death.flac"),
    ("PBETWEE", "06. Between Levels.flac"),
    ("PDOOM",   "07. DOOM.flac"),
    ("PTHE_DA", "08. In The Dark.flac"),
    ("PSHAWN",  "10. Shawn's Got The Shotgun.flac"),
    ("PDDTBLU", "11. The Dave D. Taylor Blues.flac"),
    ("PIN_CIT", "12. Into Sandy's City.flac"),
    ("PDEAD",   "13. The Demon's Dead.flac"),
    ("PSTLKS2", "04. The Healer Stalks.flac"),
    ("PTHEDA2", "08. In The Dark.flac"),
    ("PDOOM2",  "07. DOOM.flac"),
    ("PDDTBL2", "11. The Dave D. Taylor Blues.flac"),
    ("PRUNNI2", "02. Running From Evil.flac"),
    ("PDEAD2",  "13. The Demon's Dead.flac"),
    ("PSTLKS3", "04. The Healer Stalks.flac"),
    ("PROMERO", "16. Waiting For Romero To Play.flac"),
    ("PSHAWN2", "10. Shawn's Got The Shotgun.flac"),
    ("PMESSAG", "17. Message For The Archvile.flac"),
    ("PCOUNT2", "05. Countdown To Death.flac"),
    ("PDDTBL3", "11. The Dave D. Taylor Blues.flac"),
    ("PAMPIE",  "18. Bye Bye American Pie.flac"),
    ("PTHEDA3", "08. In The Dark.flac"),
    ("PADRIAN", "19. Adrian's Asleep.flac"),
    ("PMESSG2", "17. Message For The Archvile.flac"),
    ("PROMER2", "16. Waiting For Romero To Play.flac"),
    ("PTENSE",  "20. Getting Too Tense.flac"),
    ("PSHAWN3", "10. Shawn's Got The Shotgun.flac"),
    ("POPENIN", "21. Opening To Hell.flac"),
    ("PEVIL",   "14. Evil Incarnate.flac"),
    ("PULTIMA", "15. The Ultimate Challenge.flac"),
    ("PREAD_M", "09. Read Me.flac"),
    ("PDM2TTL", "01. Intro.flac"),
    ("PDM2INT", "03. Intermission.flac"),
]

# Doom / Ultimate Doom (DOOM.WAD).  Many E?M? maps reuse a track under several
# music names; those share one data block.
MAPPINGS["doom1"] = [
    ("PE1M1",   "At Doom's Gate.flac"),
    ("PE1M2",   "The Imp's Song.flac"),
    ("PE1M3",   "Dark Halls.flac"),
    ("PE1M4",   "Kitchen Ace (and Taking Names).flac"),
    ("PE1M5",   "Suspense.flac"),
    ("PE1M6",   "On the Hunt.flac"),
    ("PE1M7",   "Demons on the Prey.flac"),
    ("PE1M8",   "Sign of Evil.flac"),
    ("PE1M9",   "Hiding the Secrets.flac"),
    ("PE2M1",   "I Sawed the Demons.flac"),
    ("PE2M2",   "The Demons from Adrian's Pen.flac"),
    ("PE2M3",   "Intermission from DOOM.flac"),
    ("PE2M4",   "They're Going To Get You.flac"),
    ("PE2M5",   "Demons on the Prey.flac"),
    ("PE2M6",   "Sinister.flac"),
    ("PE2M7",   "Waltz of the Demons.flac"),
    ("PE2M8",   "Nobody Told Me About id.flac"),
    ("PE3M1",   "Hell Keep.flac"),
    ("PE3M2",   "Donna to the Rescue.flac"),
    ("PE3M3",   "Deep Into the Code.flac"),
    ("PE3M4",   "Sign of Evil.flac"),
    ("PE3M5",   "Demons on the Prey.flac"),
    ("PE3M6",   "On the Hunt.flac"),
    ("PE3M7",   "Waltz of the Demons.flac"),
    ("PE3M8",   "Facing the Spider.flac"),
    ("PE3M9",   "Hiding the Secrets.flac"),
    ("PE4M1",   "Sign of Evil.flac"),
    ("PE4M2",   "Donna to the Rescue.flac"),
    ("PE4M3",   "Deep Into the Code.flac"),
    ("PE4M4",   "Suspense.flac"),
    ("PE4M5",   "Waltz of the Demons.flac"),
    ("PE4M6",   "They're Going To Get You.flac"),
    ("PE4M7",   "Sinister.flac"),
    ("PE4M8",   "Demons on the Prey.flac"),
    ("PE4M9",   "Hiding the Secrets.flac"),
    ("PINTRO",  "Introduction.flac"),
    ("PINTROA", "Introduction.flac"),
    ("PINTER",  "Intermission from DOOM.flac"),
    ("PVICTOR", "Victory.flac"),
    ("PBUNNY",  "Sweet Little Dead Bunny.flac"),
]


def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def measure(flac, I, TP, LRA):
    """Pass 1: measure loudness."""
    p = run(["ffmpeg", "-hide_banner", "-i", flac, "-af",
             f"loudnorm=I={I}:TP={TP}:LRA={LRA}:print_format=json", "-f", "null", "-"])
    err = p.stderr.decode("utf-8", "replace")
    blocks = re.findall(r"\{[^{}]*\}", err)
    if not blocks:
        raise RuntimeError(f"loudnorm measure failed for {flac}\n{err[-1200:]}")
    return json.loads(blocks[-1])


def convert(flac, out, I, TP, LRA, st):
    """Pass 2: apply measured loudnorm, resample (soxr) to 48 kHz, dither to s16le."""
    af = (f"loudnorm=I={I}:TP={TP}:LRA={LRA}:"
          f"measured_I={st['input_i']}:measured_TP={st['input_tp']}:"
          f"measured_LRA={st['input_lra']}:measured_thresh={st['input_thresh']}:"
          f"offset={st['target_offset']}:print_format=summary,"
          f"aresample={RATE}:resampler=soxr:precision=28:dither_method=triangular")
    p = run(["ffmpeg", "-hide_banner", "-y", "-i", flac, "-af", af,
             "-ar", str(RATE), "-ac", str(CHANNELS),
             "-f", "s16le", "-c:a", "pcm_s16le", out])
    if p.returncode != 0:
        raise RuntimeError(f"convert failed for {flac}\n{p.stderr.decode('utf-8','replace')[-1200:]}")


def build(ost, outwad, mapping, I, TP, LRA):
    uniq = []
    for _, flac in mapping:
        if flac not in uniq:
            uniq.append(flac)
    for flac in uniq:
        if not os.path.exists(os.path.join(ost, flac)):
            sys.exit(f"missing source: {os.path.join(ost, flac)}")

    tmp = tempfile.mkdtemp(prefix="pcmmus_")
    pcm = {}
    for i, flac in enumerate(uniq, 1):
        print(f"[{i:2d}/{len(uniq)}] {flac}", flush=True)
        st = measure(os.path.join(ost, flac), I, TP, LRA)
        dst = os.path.join(tmp, f"{i:02d}.pcm")
        convert(os.path.join(ost, flac), dst, I, TP, LRA, st)
        pcm[flac] = dst

    entries = []
    with open(outwad, "wb") as w:
        w.write(b"PWAD")
        w.write(struct.pack("<II", 0, 0))                 # header backfilled below
        off = w.tell()
        w.write(struct.pack("<II", RATE, CHANNELS))       # PCMINFO
        entries.append(("PCMINFO", off, w.tell() - off))
        block = {}
        for flac in uniq:                                 # one data block per track
            off = w.tell()
            with open(pcm[flac], "rb") as r:
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    w.write(b)
            block[flac] = (off, w.tell() - off)
        for name, flac in mapping:                        # directory (dedup: shared offsets)
            entries.append((name, *block[flac]))
        dirofs = w.tell()
        for name, off, size in entries:
            w.write(struct.pack("<II", off, size))
            w.write(name.encode("ascii")[:8].ljust(8, b"\x00"))
        w.seek(4)
        w.write(struct.pack("<II", len(entries), dirofs))

    for f in pcm.values():
        os.remove(f)
    os.rmdir(tmp)
    total = os.path.getsize(outwad)
    print(f"\nWrote {outwad}")
    print(f"  {len(entries)} lumps, {len(uniq)} unique tracks @ {RATE} Hz, "
          f"loudnorm I={I} TP={TP} LRA={LRA}, {total/1e6:.0f} MB")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Build a Doom PCM music WAD from a FLAC OST.")
    ap.add_argument("--game", choices=sorted(MAPPINGS), default="doom2", help="which lump map to use")
    ap.add_argument("--ost", required=True, help="folder of source FLACs")
    ap.add_argument("--out", required=True, help="output WAD path")
    ap.add_argument("--I", type=float, default=-12.0, help="integrated loudness target (LUFS)")
    ap.add_argument("--TP", type=float, default=-1.0, help="true-peak ceiling (dBTP)")
    ap.add_argument("--LRA", type=float, default=11.0, help="loudness range target (LU)")
    a = ap.parse_args()
    build(a.ost, a.out, MAPPINGS[a.game], a.I, a.TP, a.LRA)
