"""Builds the sounds (44.1 kHz mono 16 bit PCM wav, what the game's own effects are) from two recordings from
Pixabay (Pixabay Content License), kept outside the repository:

  leaves-rustling-236742.mp3 (soul_serenity_sounds)  a soft, steady rustle of leaves
  stone-54286.mp3 (freesound_community)              stones and pebbles dropped and tumbling

  res/Sound/fx/ResponsiveGrounds/Leaves01-05.wav   slices of the rustle, eased in and out
  res/Sound/fx/ResponsiveGrounds/Pebbles01-08.wav  the stone recording's little pebble rattles (no big stones)

usage: python make_sounds.py [folder with the two mp3s] [preview.png]   (numpy, miniaudio; matplotlib for the preview)
"""
import os
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "res" / "Sound" / "fx" / "ResponsiveGrounds"
SOURCES = Path(sys.argv[1] if len(sys.argv) > 1 else os.environ.get("RG_SOUNDS", r"D:\SkyrimBuild\ResponsiveGrounds\usersounds"))
RATE = 44100

LEAVES = "soul_serenity_sounds-leaves-rustling-236742.mp3"
LEAF_SLICES = [(0.70, 1.80), (1.75, 2.75), (2.70, 3.90), (3.80, 4.85), (4.60, 5.80)]  # seconds

STONES = "freesound_community-stone-54286.mp3"
# (start, length) of pebble rattles: many small ticks, no big stone
PEBBLE_EVENTS = [(1.00, 0.50), (6.00, 0.60), (18.09, 0.28), (42.21, 0.39), (53.92, 0.60), (54.83, 0.37), (58.50, 0.53), (62.42, 0.56)]


def load(name):
    import miniaudio
    decoded = miniaudio.decode_file(str(SOURCES / name), output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=RATE)
    return np.frombuffer(decoded.samples, dtype=np.int16).astype(np.float64) / 32768.0


def cut(a, start, length):
    return a[int(start * RATE):int((start + length) * RATE)].copy()


def ease(a, fade_in, fade_out):
    n_in, n_out = int(fade_in * RATE), int(fade_out * RATE)
    a[:n_in] *= np.sin(np.linspace(0, np.pi / 2, n_in)) ** 2
    a[-n_out:] *= np.cos(np.linspace(0, np.pi / 2, n_out)) ** 2
    return a


def high_pass(a, cutoff):
    spec = np.fft.rfft(a)
    f = np.fft.rfftfreq(len(a), 1.0 / RATE)
    return np.fft.irfft(spec * np.clip((f - cutoff * 0.6) / (cutoff * 0.8), 0, 1), len(a))


def write(path, a):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((np.clip(a, -1, 1) * 32767).astype(np.int16).tobytes())


def main():
    for old in OUT.glob("*.wav"):
        old.unlink()
    made = []
    leaves = load(LEAVES)
    for i, (start, end) in enumerate(LEAF_SLICES):
        a = ease(cut(leaves, start, end - start), 0.15, 0.45)
        a *= 0.03 / np.sqrt((a * a).mean())  # the recording is very quiet
        made.append((f"Leaves{i + 1:02d}", a))
    stones = load(STONES)
    for i, (start, length) in enumerate(PEBBLE_EVENTS):
        a = high_pass(cut(stones, start - 0.01, length + 0.01), 150)  # no rumble
        a = ease(a, 0.005, min(0.15, length * 0.4))
        a *= 0.4 / np.abs(a).max()
        made.append((f"Pebbles{i + 1:02d}", a))
    for name, a in made:
        write(OUT / f"{name}.wav", a)
        print(f"{name}: {len(a) / RATE:.2f} s, peak {np.abs(a).max():.2f}, rms {np.sqrt((a * a).mean()):.3f}")
    if len(sys.argv) > 2:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(len(made), 1, figsize=(10, 1.1 * len(made)))
        for ax, (name, a) in zip(axes, made):
            ax.plot(np.arange(len(a)) / RATE, a, lw=0.4)
            ax.set_ylim(-0.5, 0.5)
            ax.set_title(name, fontsize=7)
            ax.tick_params(labelsize=6)
        plt.tight_layout()
        plt.savefig(sys.argv[2], dpi=55)


if __name__ == "__main__":
    main()
