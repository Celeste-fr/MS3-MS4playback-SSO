"""envelope.py <out.npz> <audio files...>: per file the 50 ms mean-square envelope (dB re full scale),
after a 4th-order 40 Hz Butterworth high pass; plus the sample rate. Audio decoded by ffmpeg."""
import sys, subprocess, numpy as np
from scipy.signal import butter, sosfilt
out = {}
for f in sys.argv[2:]:
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', f, '-f', 'f32le', '-ac', '1', '-'], capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
    sr = 48000
    p = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries', 'stream=sample_rate', '-of', 'csv=p=0', f], capture_output=True, text=True).stdout.strip()
    sr = int(p.strip(","))
    x = sosfilt(butter(4, 40, 'highpass', fs=sr, output='sos'), x)
    n = int(sr * 0.05)
    m = len(x) // n
    ms = (x[:m * n].reshape(m, n) ** 2).mean(axis=1)
    key = f.rsplit('/', 1)[-1].rsplit('.', 1)[0]
    out[key] = 10 * np.log10(ms + 1e-20)
    print(key, sr, '%.1f s' % (len(x) / sr), 'p10 %.1f p50 %.1f p99 %.1f max %.1f dBFS' % tuple(np.percentile(out[key], [10, 50, 99, 100])), flush=True)
np.savez(sys.argv[1], **out)
