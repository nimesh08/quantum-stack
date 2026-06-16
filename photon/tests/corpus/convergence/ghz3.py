from __future__ import annotations
import photon


@photon.kernel
def ghz3():
    q = photon.QReg(3)
    q.h(0)
    q.cx(0, 1)
    q.cx(1, 2)
    return q.measure_int()


if __name__ == "__main__":
    print(ghz3.phonon_text)
