#!/usr/bin/env bash
# install-qiskit-venv.sh — create the Qiskit venv that `spinorc run`
# spawns when compiling IBM-targeted circuits.
#
# Location: <build-dir>/.qiskit-venv (looked up by spinorc via
# $SPINOR_REGISTRY_ROOT -> ../build/.qiskit-venv).
set -euo pipefail

QSROOT="${QSROOT:-/home/ubuntu/heisenberg-research_fullstack-compiler/quantum-stack}"
VENV="$QSROOT/build/.qiskit-venv"

if [ -x "$VENV/bin/python3" ] && [ "${FORCE:-0}" != "1" ]; then
  # Already installed — skip unless caller forces.
  if "$VENV/bin/python3" -c "import qiskit, qiskit_ibm_runtime" >/dev/null 2>&1; then
    echo "qiskit venv already present at $VENV"
    exit 0
  fi
fi

echo "creating qiskit venv at $VENV"
python3 -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip
# Pin to known-good range. Qiskit 1.x is the stable v2 primitive series.
"$VENV/bin/pip" install --quiet \
  "qiskit>=1.0,<3.0" \
  "qiskit-ibm-runtime>=0.20" \
  "qiskit-qasm3-import>=0.5" \
  "numpy>=1.26"

echo "qiskit venv ready:"
"$VENV/bin/pip" list 2>/dev/null | grep -iE "qiskit|numpy"
