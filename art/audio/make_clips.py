# Cuts the CC0 ambience, weather, footstep and settlement clips from their downloaded sources
# into 16-bit mono 32 kHz WAVs next to this script. Sources and licences: art/audio_sources.md.
#
#   python art/audio/make_clips.py SOURCES_DIR
#
# SOURCES_DIR holds one folder per source (beach, vistula, ...), exactly as art/audio_sources.md
# names them, with any archive already unpacked. Needs numpy, scipy and soundfile.
#
# A loop is cut a little long and its tail is cross-faded over its head, so the end meets the
# start without a click. Every clip is scaled to the RMS its kind asks for, with the peak held
# under PEAK_LIMIT, which is the band tests/test_audio_clips.cpp checks.

import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

RATE = 32000
HERE = os.path.dirname(os.path.abspath(__file__))
PEAK_LIMIT = 0.89
FADE = 0.75

# kind -> target RMS in dBFS
RMS_DB = {"step": -18.0, "bed": -26.0, "layer": -28.0, "shot": -18.0}

# name: (kind, loop seconds or 0 for a one-shot, [(source file, start s, length s), ...] summed in order,
#        optional second layer [(file, start, length, gain)])
CLIPS = {
    "A_Step_Grass": ("step", 0, [("fantozzi/Fantozzi-footsteps/flac/Fantozzi-SandL1.flac", 0, 1)], []),
    "A_Step_Rock": ("step", 0, [("fantozzi/Fantozzi-footsteps/flac/Fantozzi-StoneL1.flac", 0, 1)], []),
    "A_Step_Road": ("step", 0, [("fantozzi/Fantozzi-footsteps/flac/Fantozzi-StoneR2.flac", 0, 1)], []),
    "A_Step_Snow": ("step", 0, [("snow42/Corsica_S-Walking_in_Snow/Corsica_S-Walking_on_snow_covered_gravel_01.flac", 0, 1)], []),
    "A_Bed_Surf": ("bed", 8, [("beach/wave_01_cc0-18363__jasinski__alkaibeach.flac", 0, 4), ("beach/wave_02_cc0-18363__jasinski__alkaibeach.flac", 0, 2.5), ("beach/wave_03_cc0-18363__jasinski__alkaibeach.flac", 0, 2.2)], []),
    "A_Bed_Lake": ("bed", 8, [("vistula/Vistula.mp3", 1200, 9)], []),
    "A_Bed_Taiga": ("bed", 8, [("windy/windy_ambiance_0.ogg", 30, 9)], []),
    "A_Bed_Forest": ("bed", 8, [("outdoor1/amb_outdoor1_loop.wav", 0, 9)], []),
    "A_Bed_Grassland": ("bed", 8, [("windbg/wind background noise 2.wav", 10, 9)], []),
    "A_Bed_Marsh": ("bed", 8, [("swamp/swamp.ogg", 40, 9)], []),
    "A_Bed_Rainforest": ("bed", 8, [("drip/atmosbasement.mp3_.flac", 0, 9)], []),
    "A_Bed_Savanna": ("bed", 8, [("cicada/082526-cicadasounds_0.ogg", 5, 9)], []),
    "A_Bed_Blight": ("bed", 4, [("dark/dark_ambiences/ambience-1.wav", 0, 4.6)], []),
    "A_Bed_Farmland": ("bed", 8, [("morning/amb_morning.wav", 10, 9)], []),
    "A_Dawn_Birds": ("layer", 8, [("parkbirds/park_ambience_birds.wav", 60, 9)], []),
    "A_Day_Insects": ("layer", 8, [("cicada/082526-cicadasounds2_0.ogg", 10, 9)], []),
    "A_Dusk_Crickets": ("layer", 8, [("crickets/crickets_1.mp3", 0, 9)], []),
    "A_Night_Insects": ("layer", 3, [("birdscrickets/birdsCrickets/cricket2.wav", 0, 3.3)], []),
    "A_Night_Wolves": ("shot", 0, [("wolf/wolf_monster_6.mp3", 0, 2)], []),
    "A_Settlement_Chatter": ("bed", 8, [("crowd/crowd_shouting_0.ogg", 5, 9)], []),
    "A_Settlement_Bell": ("shot", 0, [("bell/bell_ding3.wav", 0, 3)], []),
    "A_Settlement_Mill": ("bed", 8, [("parkriver/park_ambience_river.wav", 100, 9)], []),
    "A_Settlement_Surf": ("bed", 8, [("beach/wave_01_cc0-18363__jasinski__alkaibeach.flac", 0, 4), ("beach/wave_02_cc0-18363__jasinski__alkaibeach.flac", 0, 2.5), ("beach/wave_03_cc0-18363__jasinski__alkaibeach.flac", 0, 2.2)],
                          [("seagull/Seagull Ambient 1.wav", 0, 3.9, 0.6)]),
    "A_Settlement_Machinery": ("bed", 8, [("generator/generator_loop.wav", 4, 9)], []),
}


def load(root, name, start, length):
    info = sf.info(os.path.join(root, name))
    first = int(start * info.samplerate)
    data, rate = sf.read(os.path.join(root, name), start=first, frames=int(length * info.samplerate), dtype="float64", always_2d=True)
    mono = data.mean(axis=1)
    if rate != RATE:
        g = np.gcd(rate, RATE)
        mono = resample_poly(mono, RATE // g, rate // g)
    return mono


def rms(x):
    return float(np.sqrt(np.mean(x * x)))


def loop(x, seconds):
    n = int(seconds * RATE)
    f = int(FADE * RATE)
    f = min(f, max(1, (len(x) - n)))
    if len(x) < n + f:
        n = len(x) - f
    t = np.linspace(0.0, np.pi / 2, f)
    body = x[:n].copy()
    body[:f] = x[:f] * np.sin(t) + x[n:n + f] * np.cos(t)
    return body


def shot(x):
    edge = int(0.005 * RATE)
    x = x.copy()
    if len(x) > 2 * edge:
        x[:edge] *= np.linspace(0, 1, edge)
        x[-edge:] *= np.linspace(1, 0, edge)
    return x


def build(root, kind, seconds, parts, layers):
    x = np.concatenate([load(root, *p) for p in parts])
    for name, start, length, gain in layers:
        y = load(root, name, start, length) * gain
        x[:len(y)] += y[:len(x)]
    x = x - x.mean()
    if seconds:
        x = loop(x, seconds)
    else:
        x = shot(x[: int(2.5 * RATE)] if kind == "shot" else x)
        peak_at = np.argmax(np.abs(x))
        x = x[max(0, peak_at - int(0.02 * RATE)):]
        x = shot(x)
    target = 10 ** (RMS_DB[kind] / 20)
    x = x * (target / rms(x))
    peak = float(np.max(np.abs(x)))
    if peak > PEAK_LIMIT:
        x = x * (PEAK_LIMIT / peak)
    return x


def main():
    root = sys.argv[1]
    for name, (kind, seconds, parts, layers) in CLIPS.items():
        x = build(root, kind, seconds, parts, layers)
        pcm = np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)
        sf.write(os.path.join(HERE, name + ".wav"), pcm, RATE, subtype="PCM_16")
        db = 20 * np.log10(rms(pcm / 32768.0))
        print("%-26s %5.2fs  rms %6.1f dBFS" % (name, len(pcm) / RATE, db))


if __name__ == "__main__":
    main()
