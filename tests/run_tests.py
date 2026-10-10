"""Host tests use fake GPIO; PlatformIO build remains a separate hardware-target check."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    for configured in (True, False):
        target = Path(directory) / ('configured' if configured else 'unconfigured')
        args = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I'+str(root/'tests/stubs'), '-I'+str(root/'EE_Pipette/RP2040/include'),
                '-I'+str(root/'EE_Pipette/RP2040/lib/config/src'),
                '-I'+str(root/'EE_Pipette/RP2040/lib/EE_Standard/src')]
        if not configured:
            # Keep coverage of the missing-endpoint guard for future models.
            args.append('-DPIPETTE_PISTON_PULL_US=0')
        subprocess.run([*args, str(root/'tests/test_firmware.cpp'), '-o', str(target)], check=True)
        subprocess.run([str(target)], check=True)
