"""M4 translator tests: AST -> Phonon text."""
from __future__ import annotations
import os, sys, unittest

PY_PKG_DIR = os.environ.get("PHOTON_PY_PKG_DIR", "")
if PY_PKG_DIR: sys.path.insert(0, PY_PKG_DIR)

import photon  # noqa: E402  (imported for the kernel bodies below)


class M4Translator(unittest.TestCase):
    def setUp(self) -> None:
        try:
            from photon._translator import translate  # noqa: F401
        except ImportError as e:
            self.skipTest(f"photon translator unavailable: {e}")

    def test_bell(self) -> None:
        from photon._translator import translate
        def bell():
            q = photon.QReg(2)
            q.h(0)
            q.cx(0, 1)
            return q.measure_int()
        text = translate(bell)
        self.assertIn("qubit q[2]", text)
        self.assertIn("h q[0]", text)
        self.assertIn("cx q[0], q[1]", text)
        # measure_int return should produce per-slot measure stmts.
        self.assertIn("measure q[0]", text)
        self.assertIn("measure q[1]", text)

    def test_ghz_via_lib(self) -> None:
        from photon._translator import translate
        def ghz3():
            q = photon.QReg(3)
            q.ghz()
            return q.measure_int()
        text = translate(ghz3)
        self.assertIn("h q[0]", text)
        self.assertIn("cx q[0], q[1]", text)
        self.assertIn("cx q[1], q[2]", text)

    def test_for_loop(self) -> None:
        from photon._translator import translate
        def f():
            q = photon.QReg(4)
            for i in range(0, 4):
                q.h(0)
        text = translate(f)
        self.assertIn("for i in 0..4 {", text)

    def test_target_propagated(self) -> None:
        from photon._translator import translate
        def f():
            q = photon.QReg(1)
        text = translate(f, target="ibm_heron_r2")
        self.assertTrue(text.startswith("target ibm_heron_r2"))


class M4TranslatorRejection(unittest.TestCase):
    def setUp(self) -> None:
        try:
            from photon._translator import translate  # noqa: F401
            from photon._errors import UnsupportedConstructError  # noqa: F401
        except ImportError as e:
            self.skipTest(f"photon translator unavailable: {e}")

    def _assert_rejects(self, fn, *substrs: str) -> None:
        from photon._translator import translate
        from photon._errors import UnsupportedConstructError
        try:
            translate(fn)
            self.fail("expected UnsupportedConstructError")
        except UnsupportedConstructError as e:
            for s in substrs:
                self.assertIn(s, str(e))

    def test_while_rejected(self) -> None:
        def f():
            q = photon.QReg(1)
            while True:  # type: ignore[no-untyped-def]
                q.h(0)
        self._assert_rejects(f, "while")

    def test_import_rejected(self) -> None:
        def f():
            q = photon.QReg(1)
            import os  # noqa: F401
        self._assert_rejects(f, "import")

    def test_unknown_method_rejected(self) -> None:
        def f():
            q = photon.QReg(1)
            q.foo(0)
        self._assert_rejects(f, "unknown method", "foo")

    def test_try_rejected(self) -> None:
        def f():
            q = photon.QReg(1)
            try:
                q.h(0)
            except Exception:
                pass
        self._assert_rejects(f, "try")


if __name__ == "__main__":
    unittest.main()
