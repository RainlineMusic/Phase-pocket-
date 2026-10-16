"""Independent verification of the interleaved dry/output fixtures from DSP tests.
Requires numpy/scipy. Does not reuse the plugin's interpolation coefficients.
Run PhaseDSPTests first, then this script in the same working directory.
"""
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly
for name in ["phase_fixture.f32", "phase_stream.f32"]:
    path = Path(name)
    if not path.exists():
        raise SystemExit(f"Missing {name}; run PhaseDSPTests first")
    x = np.fromfile(path, dtype=np.float32).reshape(-1, 2)
    y = resample_poly(x, 16, 1, axis=0, window=("kaiser", 12.0))
    peaks = 20 * np.log10(np.max(np.abs(y), axis=0))
    rms_delta = 10 * np.log10(np.mean(x[:, 1] ** 2) / np.mean(x[:, 0] ** 2))
    print(f"{name}: dry={peaks[0]:.6f}, wet={peaks[1]:.6f} dBTP; "
          f"saved={peaks[0]-peaks[1]:.6f} dB; RMS delta={rms_delta:.6f} dB")
    assert peaks[1] <= peaks[0] - 0.19
    assert abs(rms_delta) < 0.1
