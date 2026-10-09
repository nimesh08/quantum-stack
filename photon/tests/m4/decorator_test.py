"""M4 decorator + e2e tests."""
from __future__ import annotations
import os, sys, unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

PY_PKG_DIR = os.environ.get("PHOTON_PY_PKG_DIR", "")
if PY_PKG_DIR: sys.path.insert(0, PY_PKG_DIR)


class M4Decorator(unittest.TestCase):
    def test_typed_controller_return_projects_actual_output_histogram(self):
        import photon
        with patch.object(photon,"_engine",None):
            @photon.kernel
            def sample():
                q=photon.QReg(1)
                value=photon.uint(8,255)
                return value
        service=Mock(return_value=SimpleNamespace(counts={"111000":7},metadata={"classical_counts":{"__qstack_return_value":{"255":7}}}))
        with patch.dict(sys.modules,{"qstack":SimpleNamespace(run_source=service)}):
            self.assertEqual(sample.run(shots=7),{"11111111":7})
        service.return_value=SimpleNamespace(counts={"111000":7},metadata={})
        with patch.dict(sys.modules,{"qstack":SimpleNamespace(run_source=service)}):
            with self.assertRaisesRegex(Exception,"missing the declared classical return"):
                sample.run(shots=7)

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

    def test_run_delegates_and_returns_actual_counts(self) -> None:
        import photon

        @photon.kernel
        def bell():
            q = photon.QReg(2)
            q.h(0)
            q.cx(0, 1)
            return q.measure_int()

        service = Mock(return_value=SimpleNamespace(counts={"00": 47, "11": 53}))
        with patch.dict(sys.modules, {"qstack": SimpleNamespace(run_source=service)}):
            h = bell.run(shots=100, target="ibm_fez", env_file="test.env")
        self.assertEqual(h, {"00": 47, "11": 53})
        source = service.call_args.args[0]
        self.assertTrue(source.startswith("target ibm_fez"))
        self.assertIn("cx q[0], q[1]", source)
        self.assertEqual(service.call_args.kwargs,
                         dict(language="phonon", target="ibm_fez", mode="local",
                              shots=100, env_file="test.env"))

    def test_runtime_error_is_not_replaced_with_fake_counts(self) -> None:
        import photon
        @photon.kernel
        def sample():
            q = photon.QReg(1)
            return q.measure_int()
        service = Mock(side_effect=RuntimeError("backend rejected the request"))
        with patch.dict(sys.modules, {"qstack": SimpleNamespace(run_source=service)}):
            with self.assertRaisesRegex(RuntimeError, "backend rejected"):
                sample.run()

    def test_numeric_parameters_bind_before_execution(self) -> None:
        # Numeric arguments are compile-time source bindings.
        import photon
        @photon.kernel
        def rotate(theta, width=2):
            q = photon.QReg(width)
            q.rx(theta, 1)
            return q.measure_int()
        service = Mock(return_value=SimpleNamespace(counts={"10": 8}))
        with patch.dict(sys.modules, {"qstack": SimpleNamespace(run_source=service)}):
            result = rotate.run(shots=8, parameters={"theta": 0.75})
        self.assertEqual(result, {"10": 8})
        self.assertIn("rx(0.75) q[1]", service.call_args.args[0])
        self.assertIn("qubit q[2]", service.call_args.args[0])
        from photon._errors import CompilationError
        with self.assertRaisesRegex(CompilationError, "theta"):
            rotate.run()

    def test_unverified_or_exhausted_application_keeps_result_on_error(self) -> None:
        import photon
        from photon._errors import PhotonKernelError
        @photon.kernel
        def sample():
            q = photon.QReg(1)
            return q.measure_int()
        for status in ("loop_exhausted", "not_checked"):
            result = SimpleNamespace(job_id="saved-job", counts={"0": 3, "1": 4}, metadata={"application_status": status})
            with patch.dict(sys.modules, {"qstack": SimpleNamespace(run_source=Mock(return_value=result))}):
                with self.assertRaises(PhotonKernelError) as failure:
                    sample.run(shots=7)
            self.assertIs(failure.exception.result, result)
            self.assertIn("saved-job", str(failure.exception))

    def test_call_uses_execution_service_not_python_qreg_stub(self) -> None:
        import photon
        @photon.kernel
        def sample():
            q = photon.QReg(1)
            q.x(0)
            return q.measure_int()
        service = Mock(return_value=SimpleNamespace(counts={"1": 1}))
        with patch.dict(sys.modules, {"qstack": SimpleNamespace(run_source=service)}):
            self.assertEqual(sample(), {"1": 1})
        self.assertEqual(service.call_args.kwargs["shots"], 1)

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
