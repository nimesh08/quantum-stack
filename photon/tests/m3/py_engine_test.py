"""photon._engine round-trip test (M3 Python side).

CMake registers this test only when it builds the nanobind extension.
An import or staging failure must therefore fail CI, rather than skip.
"""

from __future__ import annotations

import os
import sys
import unittest


# Python 3.8+ uses explicit DLL search directories for extension imports.
# CMake supplies this only for source-build tests; repaired wheels carry
# their own runtime dependencies and do not depend on this test hook.
_DLL_DIRECTORY_HANDLE = None
if os.name == "nt" and os.environ.get("PHOTON_TEST_DLL_DIR"):
    _DLL_DIRECTORY_HANDLE = os.add_dll_directory(os.environ["PHOTON_TEST_DLL_DIR"])

PY_PKG_DIR = os.environ.get("PHOTON_PY_PKG_DIR", "")
if PY_PKG_DIR:
    sys.path.insert(0, PY_PKG_DIR)


class M3PyEngine(unittest.TestCase):
    def setUp(self) -> None:
        import photon._engine  # noqa: F401

    def test_import(self) -> None:
        import photon
        self.assertEqual(hasattr(photon, "compile_phonon"), True)
        self.assertEqual(hasattr(photon, "kernel"), True)

    def test_compile_phonon(self) -> None:
        import photon
        prog = photon.compile_phonon(
            "target generic\n"
            "qubit q[2]\nbit c[2]\n"
            "h q[0]\ncx q[0], q[1]\n"
            "c[0] = measure q[0]\nc[1] = measure q[1]\n")
        self.assertEqual(prog.ok, True)
        s = prog.dump_spinor()
        self.assertEqual(isinstance(s, str), True)
        self.assertEqual(len(s) > 0, True)

    def test_estimate(self) -> None:
        import photon
        prog = photon.compile_phonon(
            "target generic\n"
            "qubit q[2]\nbit c[2]\n"
            "h q[0]\ncx q[0], q[1]\n"
            "c[0] = measure q[0]\nc[1] = measure q[1]\n")
        est = prog.estimate()
        self.assertEqual(est.num_qubits, 2)
        self.assertEqual(est.two_qubit_count, 1)


if __name__ == "__main__":
    unittest.main()
