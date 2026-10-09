"""M4 translator tests: AST -> Phonon text."""
from __future__ import annotations
import os, sys, unittest

PY_PKG_DIR = os.environ.get("PHOTON_PY_PKG_DIR", "")
if PY_PKG_DIR: sys.path.insert(0, PY_PKG_DIR)

import photon  # noqa: E402  (imported for the kernel bodies below)


class M4Translator(unittest.TestCase):
    def test_controller_snapshots_uint_and_branch_assignments(self):
        from photon._translator import translate
        def program():
            q = photon.QReg(2)
            measured = q.measure()
            saved = measured[0]
            counter = photon.uint(8, 250)
            if saved:
                counter = counter + 10
            else:
                counter = counter - 10
            photon.output(counter)
            return q.measure_int()
        text = translate(program)
        self.assertIn("bool saved = __c_q[0]",text)
        self.assertIn("uint[8] counter = uint[8](250)",text)
        self.assertIn("counter = (counter + 10)",text)
        self.assertIn("output counter",text)

    def test_bounded_device_while_keeps_measurement_and_counter_in_device_program(self):
        from photon._translator import translate
        def program():
            q=photon.QReg(1)
            measured=q.measure()
            counter=photon.uint(8,0)
            while photon.bounded(counter<3,max_iterations=4):
                q.x(0)
                measured=q.measure()
                counter=counter+1
            photon.output(counter)
            return q.measure_int()
        text=translate(program)
        self.assertIn("while (counter < 3) max_iterations 4",text)
        self.assertIn("counter = (counter + 1)",text)

    def test_bounded_return_break_continue_and_unsigned_complement(self):
        from photon._translator import translate
        def program():
            q=photon.QReg(1)
            count=photon.uint(8,0)
            while photon.bounded(count<4,max_iterations=4):
                count=count+1
                if count==1:
                    continue
                if count==2:
                    return ~count
                q.x(0)
            return count
        text=translate(program)
        self.assertIn("continue",text)
        self.assertIn("break",text)
        self.assertIn("__qstack_return_value = ~(count)",text)
        self.assertIn("output __qstack_return_value",text)

    def test_same_width_return_registers_share_one_projection(self):
        from photon._translator import Translator
        def program():
            q=photon.QReg(1)
            r=photon.QReg(1)
            flag=q.measure()
            if flag[0]:
                return r.measure_int()
            return q.measure_int()
        translator=Translator();text=translator.translate(program)
        self.assertIn("__c_r[0] = measure r[0]",text)
        self.assertIn("__c_r[0] = measure q[0]",text)
        self.assertEqual(translator.return_bits,[1])

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

    def test_concrete_target_identifiers_are_quoted_when_needed(self) -> None:
        from photon._translator import translate
        def sample():
            q = photon.QReg(1)
            q.reset(0)
        self.assertTrue(translate(sample, target="processor-with-hyphens").startswith('target "processor-with-hyphens"'))

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
        self.assertEqual(text.count("h q[0]"), 4)

    def test_explicit_reset_and_saved_measurements_keep_distinct_destinations(self) -> None:
        from photon._translator import Translator
        def sample():
            q = photon.QReg(2)
            first = q.measure()
            for i in range(2):
                q.reset(i)
            second = q.measure()
            if first[0] == 1:
                q.x(1)
            if second[0] == 1:
                q.z(1)
            return q.measure_int()
        translator = Translator()
        text = translator.translate(sample)
        self.assertIn("reset q[0]", text)
        self.assertIn("reset q[1]", text)
        self.assertIn("bit __qstack_measure_1[2]", text)
        self.assertIn("if (__c_q[0] == 1)", text)
        self.assertIn("if (__qstack_measure_1[0] == 1)", text)
        self.assertEqual(translator.return_bits, [0, 1])

    def test_nested_strided_ranges_bind_indices_and_angles(self) -> None:
        from photon._translator import translate
        def sample():
            q = photon.QReg(6)
            stop = 6
            for i in range(5, -1, -2):
                for j in range(i, i + 1):
                    q.rx(j / stop, j)
        text = translate(sample)
        self.assertIn("rx(0.8333333333333334) q[5]", text)
        self.assertIn("rx(0.5) q[3]", text)
        self.assertIn("rx(0.16666666666666666) q[1]", text)
        self.assertEqual(text.count("rx("), 3)

    def test_range_induction_and_scalar_updates_match_python(self) -> None:
        from photon._translator import translate
        def sample():
            q = photon.QReg(4)
            count = 0
            for i in range(0, 4, 2):
                count = count + 1
                q.x(i)
            if count >= 2:
                q.h(i)
            else:
                q.z(1)
        text = translate(sample)
        self.assertIn("x q[0]", text)
        self.assertIn("x q[2]", text)
        self.assertIn("h q[2]", text)
        self.assertNotIn("z q[1]", text)

    def test_measured_comparisons_and_loop_indices_are_preserved(self) -> None:
        from photon._translator import translate
        def sample():
            q = photon.QReg(2)
            output = photon.QReg(2)
            c = q.measure()
            for i in range(2):
                if c[i] != 0:
                    output.x(1 - i)
                if 0 < c[i]:
                    output.z(1 - i)
        text = translate(sample)
        self.assertIn("if (__c_q[0] != 0)", text)
        self.assertIn("if (__c_q[1] != 0)", text)
        self.assertIn("if (0 < __c_q[0])", text)
        self.assertIn("if (0 < __c_q[1])", text)

    def test_measurement_rebinding_does_not_reuse_stale_constant(self) -> None:
        from photon._translator import translate
        def sample():
            q = photon.QReg(1)
            c = 0
            c = q.measure()
            if c == 1:
                q.x(0)
        self.assertIn("if (__c_q[0] == 1)", translate(sample))

    def test_invalid_and_unbounded_ranges_are_rejected(self) -> None:
        def zero():
            q = photon.QReg(1)
            for i in range(0, 3, 0):
                q.h(0)
        def fractional():
            q = photon.QReg(1)
            for i in range(0, 3, 0.5):
                q.h(0)
        def excessive():
            q = photon.QReg(1)
            for i in range(100001):
                pass
        from photon._translator import translate
        from photon._errors import UnsupportedConstructError
        for sample, message in ((zero, "nonzero"), (fractional, "compile-time integers"), (excessive, "100000")):
            with self.assertRaisesRegex(UnsupportedConstructError, message):
                translate(sample)

    def test_runtime_scalar_assignment_cannot_change_following_gate(self) -> None:
        def sample():
            q = photon.QReg(1)
            c = q.measure()
            theta = 0.5
            if c[0] == 1:
                theta = 1.0
            q.rx(theta, 0)
        from photon._translator import translate
        from photon._errors import UnsupportedConstructError
        with self.assertRaisesRegex(UnsupportedConstructError, "runtime branches cannot change classical bindings"):
            translate(sample)

    def test_target_propagated(self) -> None:
        from photon._translator import translate
        def f():
            q = photon.QReg(1)
        text = translate(f, target="ibm_heron_r2")
        self.assertTrue(text.startswith("target ibm_heron_r2"))

    def test_all_declared_registers_are_preserved(self) -> None:
        from photon._translator import translate
        def sample():
            first = photon.QReg(1)
            second = photon.QReg(2)
            second.x(1)
            return second.measure_int()
        text = translate(sample)
        self.assertIn("qubit first[1]", text)
        self.assertIn("qubit second[2]", text)
        self.assertIn("__c_second[1] = measure second[1]", text)

    def test_library_defaults_and_explicit_integer_expressions(self) -> None:
        from photon._translator import translate
        def sample():
            q = photon.QReg(3)
            q.bell_pair()
            q.bell_pair(0, 1 + 1)
            q.vqe_ansatz(1 + 1)
            q.teleport(2, 0, 1)
        text = translate(sample)
        self.assertIn("cx q[0], q[1]", text)
        self.assertIn("cx q[0], q[2]", text)
        self.assertEqual(text.count("ry("), 6)
        self.assertIn("__c_q[2] = measure q[2]", text)


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

    def test_multibit_measurement_is_not_truncated_to_bit_zero(self) -> None:
        def sample():
            q = photon.QReg(2)
            c = q.measure()
            if c == 1:
                q.x(0)
        self._assert_rejects(sample, "explicit bit index")

    def test_missing_oracle_is_not_replaced_by_comment(self) -> None:
        def sample():
            q = photon.QReg(3)
            q.grover()
        self._assert_rejects(sample, "bound oracle")

    def test_library_argument_counts_are_not_ignored(self) -> None:
        def bell():
            q = photon.QReg(3)
            q.bell_pair(1)
        def bell_extra():
            q = photon.QReg(3)
            q.bell_pair(0, 1, 2)
        def ghz():
            q = photon.QReg(3)
            q.ghz(1)
        def qft():
            q = photon.QReg(3)
            q.qft(1)
        def iqft():
            q = photon.QReg(3)
            q.iqft(1)
        def teleport():
            q = photon.QReg(3)
            q.teleport(0, 1)
        def teleport_extra():
            q = photon.QReg(3)
            q.teleport(0, 1, 2, 3)
        def vqe():
            q = photon.QReg(3)
            q.vqe_ansatz(1, 2)
        for sample in (bell, bell_extra, ghz, qft, iqft, teleport, teleport_extra, vqe):
            with self.subTest(sample=sample.__name__):
                self._assert_rejects(sample, "expects argument count")

    def test_library_indices_require_distinct_in_range_integers(self) -> None:
        def duplicate():
            q = photon.QReg(3)
            q.bell_pair(1, 1)
        def negative():
            q = photon.QReg(3)
            q.bell_pair(-1, 1)
        def outside():
            q = photon.QReg(3)
            q.teleport(0, 1, 3)
        def incomplete_register():
            q = photon.QReg(2)
            q.teleport()
        def fractional():
            q = photon.QReg(3)
            q.bell_pair(0.5, 1)
        def boolean():
            q = photon.QReg(3)
            q.bell_pair(False, 1)
        for sample in (duplicate, negative, outside, incomplete_register):
            self._assert_rejects(sample, "distinct in-range")
        for sample in (fractional, boolean):
            self._assert_rejects(sample, "compile-time integers")

    def test_vqe_depth_is_positive_integral_and_bounded(self) -> None:
        def negative():
            q = photon.QReg(3)
            q.vqe_ansatz(-1)
        def zero():
            q = photon.QReg(3)
            q.vqe_ansatz(0)
        def fractional():
            q = photon.QReg(3)
            q.vqe_ansatz(0.5)
        def excessive():
            q = photon.QReg(3)
            q.vqe_ansatz(100001)
        for sample in (negative, zero, fractional, excessive):
            self._assert_rejects(sample, "positive integer", "100000")

    def test_numeric_grover_arguments_do_not_create_placeholder_oracle(self) -> None:
        def sample():
            q = photon.QReg(3)
            q.grover(1)
        self._assert_rejects(sample, "bound oracle", "unsupported")

    def test_qft_tracks_each_controlled_phase(self) -> None:
        import math
        import re
        from photon._translator import translate
        def forward():
            q = photon.QReg(3)
            q.qft()
        def inverse():
            q = photon.QReg(3)
            q.iqft()
        for sample, sign in ((forward, 1), (inverse, -1)):
            text = translate(sample)
            phases = [float(value) for value in re.findall(r"gphase\(([^)]+)\)", text)]
            self.assertEqual(len(phases), 3)
            self.assertAlmostEqual(sum(phases), sign * 5 * math.pi / 16)

    def test_measurement_arguments_are_never_ignored(self) -> None:
        def sample():
            q = photon.QReg(2)
            c = q.measure(1)
        self._assert_rejects(sample, "accepts no arguments")
        def returned():
            q = photon.QReg(2)
            return q.measure_int(1)
        self._assert_rejects(returned, "accepts no arguments")

    def test_early_returns_normalize_and_incompatible_implicit_returns_reject(self) -> None:
        from photon._translator import translate
        def early():
            q = photon.QReg(1)
            return q.measure_int()
            q.x(0)
        self.assertNotIn("x q[0]",translate(early))
        def conditional():
            q = photon.QReg(1)
            c = q.measure()
            if c[0] == 1:
                return q.measure_int()
        self._assert_rejects(conditional, "conditional")
        def static_conditional():
            q = photon.QReg(1)
            if 1 == 1:
                return q.measure_int()
            q.x(0)
        self.assertNotIn("x q[0]",translate(static_conditional))
        def both_paths():
            q=photon.QReg(1)
            measured=q.measure()
            if measured[0]==1:
                return q.measure_int()
            q.x(0)
            return q.measure_int()
        text=translate(both_paths)
        self.assertIn("else {",text)
        self.assertEqual(text.count("x q[0]"),1)


if __name__ == "__main__":
    unittest.main()
