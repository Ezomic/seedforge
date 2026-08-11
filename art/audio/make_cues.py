# Generates the Stage 0 attack telegraph cues as 44.1kHz mono WAVs.
#
#   python art/audio/make_cues.py
#
# Doc 18 names readability as the confound in the hypothesis: a fight that reads as unfair
# when the telegraphs are demonstrably clean is a different finding from one where they
# are not. Audio is half of that, and it is the half that still works when the attacker is
# behind you - which is why doc 18's anti-frustration rules require a sound before any
# attack from off screen.
#
# Synthesised rather than sourced, because the point is that each cue starts exactly when
# its wind-up starts and lasts exactly as long. A library clip would have to be trimmed to
# the frame data anyway, and the frame data is still moving.
#
# No numpy: this runs on whatever Python is to hand, and a second of audio is 44,100
# floats. Pure stdlib is fast enough and has no install step.

import math
import os
import random
import struct
import wave

RATE = 44100
HERE = os.path.dirname(os.path.abspath(__file__))

# Deterministic, like everything else generated in this project.
SEED = 0x1C407


class OnePole(object):
    """Cheap low-pass. Two of these make a band-pass, which is all these cues need."""

    def __init__(self):
        self.y = 0.0

    def step(self, x, cutoff_hz):
        rc = 1.0 / (2.0 * math.pi * max(cutoff_hz, 20.0))
        a = (1.0 / RATE) / (rc + 1.0 / RATE)
        self.y += a * (x - self.y)
        return self.y


def envelope(t, dur, attack, release, curve=2.0):
    """Percussive by default: near-instant onset, long tail.

    The onset is the readable part - the moment a wind-up begins is the information the
    player acts on, and a slow fade-in buries it."""
    if t < attack:
        return (t / attack) if attack > 0 else 1.0
    remaining = dur - t
    if remaining < release:
        return max(0.0, (remaining / release)) ** curve
    return 1.0


def lerp(a, b, t):
    return a + (b - a) * t


def render(dur, fn):
    n = int(RATE * dur)
    return [fn(i / float(RATE), i / float(n)) for i in range(n)]


def tell_cue(rng):
    """A foot scuffing as weight shifts. 0.12s, dry, no pitch - it must not be mistaken
    for the attack itself, only for the moment before it."""
    lp1, lp2 = OnePole(), OnePole()
    dur = 0.12

    def sample(t, p):
        noise = rng.uniform(-1.0, 1.0)
        band = lp1.step(noise, lerp(2600.0, 900.0, p))
        band = band - lp2.step(band, 240.0)
        return band * envelope(t, dur, 0.002, 0.09, 1.6) * 0.55

    return render(dur, sample)


def light_cue(rng):
    """Light wind-up: bright, rising, short. 0.30s to sit inside a 0.35s wind-up.

    Rising pitch reads as "something is coming" far more reliably than volume alone."""
    lp1, lp2 = OnePole(), OnePole()
    dur = 0.30

    def sample(t, p):
        noise = rng.uniform(-1.0, 1.0)
        band = lp1.step(noise, lerp(1400.0, 5200.0, p))
        band = band - lp2.step(band, lerp(500.0, 1600.0, p))

        # A thin metallic edge so it reads as bronze rather than as wind.
        ring = math.sin(2.0 * math.pi * lerp(880.0, 1320.0, p) * t) * 0.18 * (1.0 - p)
        return (band * 0.85 + ring) * envelope(t, dur, 0.004, 0.12) * 0.62

    return render(dur, sample)


def heavy_cue(rng):
    """Heavy wind-up: lower, longer, descending. 0.62s under a 0.70s wind-up.

    Deliberately the opposite contour to the light cue. If the two differed only in volume
    the player would have to judge loudness under pressure, which nobody can do; falling
    against rising is distinguishable even at the edge of hearing."""
    lp1, lp2 = OnePole(), OnePole()
    dur = 0.62

    def sample(t, p):
        noise = rng.uniform(-1.0, 1.0)
        band = lp1.step(noise, lerp(2200.0, 420.0, p))
        band = band - lp2.step(band, lerp(220.0, 90.0, p))

        # A sub thump on the onset, so it lands in the chest as well as the ear.
        thump = math.sin(2.0 * math.pi * lerp(120.0, 62.0, p) * t) * 0.42 * max(0.0, 1.0 - p * 2.2)

        swell = 0.55 + 0.45 * p
        return (band * swell + thump) * envelope(t, dur, 0.006, 0.22) * 0.72

    return render(dur, sample)


def write_wav(name, samples):
    path = os.path.join(HERE, name + ".wav")

    peak = max(1e-6, max(abs(s) for s in samples))
    # Normalise to -3dBFS. Leaving headroom because these are mixed against each other and
    # against hit sounds later; cues clipping at the mix stage is a miserable thing to debug.
    gain = 0.708 / peak

    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = b"".join(
            struct.pack("<h", int(max(-1.0, min(1.0, s * gain)) * 32767)) for s in samples)
        w.writeframes(frames)

    print("[ichor-audio] {:<18} {:.2f}s  {} bytes".format(
        name, len(samples) / float(RATE), os.path.getsize(path)))


def main():
    rng = random.Random(SEED)
    write_wav("A_Tell", tell_cue(rng))
    write_wav("A_WindupLight", light_cue(rng))
    write_wav("A_WindupHeavy", heavy_cue(rng))
    print("[ichor-audio] DONE")


main()
