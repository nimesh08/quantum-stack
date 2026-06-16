"""Quantum register helper used by the Photon Python frontend.

This is the runtime `QReg` callable seen in the `@photon.kernel`
decorator example; the decorator never actually calls these methods
(it walks the AST instead), but having a real class lets users
import and run kernels eagerly for testing.
"""

from __future__ import annotations

from typing import List


class QReg:
    """A list of `n` qubit slots. The real semantics live in the
    compiled Photon kernel; this class is a runtime stub used by
    the decorator's metadata gathering and by tests that exercise
    the Python facade without invoking the C++ engine.
    """

    __slots__ = ("size", "_recorded")

    def __init__(self, n: int) -> None:
        if n < 1:
            raise ValueError("QReg size must be positive")
        self.size = int(n)
        self._recorded: List[str] = []

    # ----- gate methods ----------------------------------------------------
    def h(self, i: int) -> None: self._recorded.append(f"h {i}")
    def x(self, i: int) -> None: self._recorded.append(f"x {i}")
    def y(self, i: int) -> None: self._recorded.append(f"y {i}")
    def z(self, i: int) -> None: self._recorded.append(f"z {i}")
    def s(self, i: int) -> None: self._recorded.append(f"s {i}")
    def t(self, i: int) -> None: self._recorded.append(f"t {i}")
    def cx(self, a: int, b: int) -> None:
        self._recorded.append(f"cx {a},{b}")
    def cz(self, a: int, b: int) -> None:
        self._recorded.append(f"cz {a},{b}")

    def rx(self, theta: float, i: int) -> None:
        self._recorded.append(f"rx({theta}) {i}")
    def ry(self, theta: float, i: int) -> None:
        self._recorded.append(f"ry({theta}) {i}")
    def rz(self, theta: float, i: int) -> None:
        self._recorded.append(f"rz({theta}) {i}")

    # ----- measurement -----------------------------------------------------
    def measure(self) -> List[int]:
        return [0] * self.size  # placeholder for runtime stub

    def measure_int(self) -> int:
        return 0  # placeholder

    # ----- introspection (used by tests) -----------------------------------
    @property
    def recorded(self) -> List[str]:
        return list(self._recorded)
