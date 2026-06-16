"""bell.py — convergence corpus."""
from __future__ import annotations

import photon


@photon.kernel
def bell():
    q = photon.QReg(2)
    q.h(0)
    q.cx(0, 1)
    return q.measure_int()


if __name__ == "__main__":
    print(bell.phonon_text)
