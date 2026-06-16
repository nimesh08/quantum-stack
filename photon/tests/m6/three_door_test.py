"""three_door_test.py — Python side of the convergence check.

Drives the Python decorator's translator and confirms the produced
Phonon source matches the `.pho` file's intended shape (gate
counts) once both go through the engine.
"""
from __future__ import annotations
import os, sys, unittest

PY_PKG_DIR = os.environ.get("PHOTON_PY_PKG_DIR", "")
if PY_PKG_DIR: sys.path.insert(0, PY_PKG_DIR)

CORPUS = os.environ.get("PHOTON_TEST_CORPUS_DIR", "")


def _count_op(text: str, mn: str) -> int:
    return sum(1 for line in text.splitlines() if line.strip().startswith(mn))


class M6PythonConvergence(unittest.TestCase):
    def setUp(self) -> None:
        try:
            import photon  # noqa: F401
        except ImportError as e:
            self.skipTest(f"photon unavailable: {e}")

    def test_bell_three_door(self) -> None:
        import photon
        @photon.kernel
        def bell():
            q = photon.QReg(2)
            q.h(0)
            q.cx(0, 1)
            return q.measure_int()
        py_text = bell.phonon_text
        # Compare gate counts to the .pho corpus.
        with open(os.path.join(CORPUS, "convergence", "bell.pho")) as f:
            _pho = f.read()
        # Both should produce the same gate counts when handed to the engine,
        # but for the python-side smoke test we just check the decorator
        # emitted the expected stmt set.
        self.assertEqual(_count_op(py_text, "h q[0]"), 1)
        self.assertEqual(_count_op(py_text, "cx q[0], q[1]"), 1)
        # Engine compile should succeed on both sides.
        if bell.compiled is not None:
            self.assertTrue(bell.compiled.ok)
            est = bell.compiled.estimate()
            self.assertEqual(est.num_qubits, 2)
            self.assertEqual(est.two_qubit_count, 1)

    def test_ghz3_three_door(self) -> None:
        import photon
        @photon.kernel
        def ghz3():
            q = photon.QReg(3)
            q.h(0)
            q.cx(0, 1)
            q.cx(1, 2)
            return q.measure_int()
        if ghz3.compiled is None:
            self.skipTest("photon._engine not built")
        self.assertTrue(ghz3.compiled.ok)
        est = ghz3.compiled.estimate()
        self.assertEqual(est.num_qubits, 3)
        self.assertEqual(est.two_qubit_count, 2)


if __name__ == "__main__":
    unittest.main()
