"""Host tests use fake GPIO; PlatformIO build remains a separate hardware-target check."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    for calibrated in (False, True):
        target = Path(directory) / ('configured' if calibrated else 'unconfigured')
        args = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I'+str(root/'tests/stubs'), '-I'+str(root/'EE_Pipette/RP2040/include'),
                '-I'+str(root/'EE_Pipette/RP2040/lib/config/src'),
                '-I'+str(root/'common/EE_Standard/src')]
        if calibrated:
            # Synthetic endpoint for interpolation testing, NOT a hardware calibration.
            args.append('-DPIPETTE_PISTON_PULL_US=600')
        subprocess.run([*args, str(root/'tests/test_firmware.cpp'), '-o', str(target)], check=True)
        subprocess.run([str(target)], check=True)
