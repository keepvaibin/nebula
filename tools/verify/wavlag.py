"""wavlag.py A.wav B.wav START_SECONDS LENGTH_SECONDS: the lag of B against A over a window by cross-correlation, then the difference at that lag."""
import sys
import numpy as np
def read(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    return np.frombuffer(d[i + 8:], dtype='<i2').reshape(-1, 2).astype(np.int32)
a, b = read(sys.argv[1]), read(sys.argv[2])
s = int(float(sys.argv[3]) * 48000)
n = int(float(sys.argv[4]) * 48000)
x = a[s:s + n, 0].astype(float)
y = b[s:s + n, 0].astype(float)
c = np.fft.irfft(np.fft.rfft(x, 2 * n) * np.conj(np.fft.rfft(y, 2 * n)))
lag = int(np.argmax(c))
if lag > n:
    lag -= 2 * n
print('lag', lag, 'frames =', round(lag / 800, 3), 'video frames')
m = min(len(a) - s - lag, len(b) - s, n * 6)
d = np.abs(a[s + lag:s + lag + m] - b[s:s + m])
print('frames', m, 'differ', int((d.max(axis=1) > 0).sum()), 'over 8', int((d.max(axis=1) > 8).sum()), 'max', int(d.max()), 'signal peak', int(np.abs(b[s:s + m]).max()))
