"""photon._engine round-trip test (M3 Python side).

Skipped automatically if the C++ extension was not built (no
nanobind on the build host); CMake tells us via env var.
"""

from __future__ import annotations

import os
import sys
import unittest


PY_PKG_DIR = os.environ.get("PHOTON_PY_PKG_DIR", "")
if PY_PKG_DIR:
    sys.path.insert(0, PY_PKG_DIR)


class M3PyEngine(unittest.TestCase):
    def setUp(self) -> None:
        try:
            import photon  # noqa: F401
        except ImportError as e:
            self.skipTest(f"photon package not importable: {e}")

    def test_import(self) -> None:
        import photon
        self.assertEqual(hasattr(photon, "compile_phonon"), True)
        self.assertEqual(hasattr(photon, "kernel"), True)

    def test_compile_phonon(self) -> None:
        import photon
        try:
            prog = photon.compile_phonon(
                "target generic\n"
                "qubit q[2]\nbit c[2]\n"
                "h q[0]\ncx q[0], q[1]\n"
                "c[0] = measure q[0]\nc[1] = measure q[1]\n")
        except ImportError:
            self.skipTest("photon._engine extension not built")
        self.assertEqual(prog.ok, True)
        s = prog.dump_spinor()
        self.assertEqual(isinstance(s, str), True)
        self.assertEqual(len(s) > 0, True)

    def test_estimate(self) -> None:
        import photon
        try:
            prog = photon.compile_phonon(
                "target generic\n"
                "qubit q[2]\nbit c[2]\n"
                "h q[0]\ncx q[0], q[1]\n"
                "c[0] = measure q[0]\nc[1] = measure q[1]\n")
        except ImportError:
            self.skipTest("photon._engine extension not built")
        est = prog.estimate()
        self.assertEqual(est.num_qubits, 2)
        self.assertEqual(est.two_qubit_count, 1)


if __name__ == "__main__":
    unittest.main()
