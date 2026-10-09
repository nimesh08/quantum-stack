"""Install the provider wheel before testing a companion compiler wheel."""
from pathlib import Path
import subprocess
import sys

directory = Path(sys.argv[1])
wheels = sorted(directory.glob("heisenberg_spinor_submit-*.whl"))
if len(wheels) != 1:
    raise SystemExit(f"Expected one provider wheel in {directory}, found {len(wheels)}")
subprocess.run([sys.executable, "-m", "pip", "install", str(wheels[0])], check=True)
