# conv.py <in.wav> <out.flac>: a kthost render to mono 16-bit FLAC (the probe's analysis needs pitch and level only), on the VM
import sys
import numpy as np, soundfile as sf
x, sr = sf.read(sys.argv[1])
m = x.mean(axis=1) if x.ndim == 2 else x
sf.write(sys.argv[2], np.clip(m, -1, 1), sr, subtype='PCM_16')
print(sys.argv[2], sr, len(m) / sr)
