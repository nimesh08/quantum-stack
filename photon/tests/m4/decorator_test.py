"""M4 decorator + e2e tests."""
from __future__ import annotations
import os, sys, unittest

PY_PKG_DIR = os.environ.get("PHOTON_PY_PKG_DIR", "")
if PY_PKG_DIR: sys.path.insert(0, PY_PKG_DIR)


class M4Decorator(unittest.TestCase):
    def setUp(self) -> None:
        try:
            import photon  # noqa: F401
        except ImportError as e:
            self.skipTest(f"photon unavailable: {e}")

    def test_bell_compiles(self) -> None:
        import photon

        @photon.kernel
        def bell():
            q = photon.QReg(2)
            q.h(0)
            q.cx(0, 1)
            return q.measure_int()

        self.assertIn("qubit q[2]", bell.phonon_text)
        if bell.compiled is None:
            self.skipTest("photon._engine extension not built")
        self.assertEqual(bell.compiled.ok, True,
                         msg=f"compile error: {bell.compiled.error}")
        self.assertEqual(bell.compiled.estimate().num_qubits, 2)
        self.assertEqual(bell.compiled.estimate().two_qubit_count, 1)

    def test_ghz_compiles(self) -> None:
        import photon

        @photon.kernel
        def ghz3():
            q = photon.QReg(3)
            q.h(0)
            q.cx(0, 1)
            q.cx(1, 2)
            return q.measure_int()

        if ghz3.compiled is None:
            self.skipTest("photon._engine extension not built")
        self.assertEqual(ghz3.compiled.ok, True)
        e = ghz3.compiled.estimate()
        self.assertEqual(e.num_qubits, 3)
        self.assertEqual(e.two_qubit_count, 2)

    def test_run_returns_histogram_shape(self) -> None:
        import photon

        @photon.kernel
        def bell():
            q = photon.QReg(2)
            q.h(0)
            q.cx(0, 1)
            return q.measure_int()

        h = bell.run(shots=100)
        self.assertEqual(isinstance(h, dict), True)
        self.assertEqual(sum(h.values()), 100)

    def test_target_kwarg(self) -> None:
        import photon

        @photon.kernel(target="ibm_heron_r2")
        def f():
            q = photon.QReg(1)
            q.h(0)

        self.assertEqual(f.target, "ibm_heron_r2")
        self.assertTrue(f.phonon_text.startswith("target ibm_heron_r2"))

    def test_lib_ghz_method(self) -> None:
        import photon

        @photon.kernel
        def k():
            q = photon.QReg(3)
            q.ghz()
            return q.measure_int()

        if k.compiled is None:
            self.skipTest("photon._engine not built")
        self.assertEqual(k.compiled.ok, True)


if __name__ == "__main__":
    unittest.main()
