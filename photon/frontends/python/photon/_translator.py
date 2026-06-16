"""@photon.kernel AST translator (M4) - Part 1: framework and gate set."""
from __future__ import annotations
import ast
import inspect
import textwrap
from typing import Any, Callable, List, Optional
from ._errors import UnsupportedConstructError

# Photon gate methods recognised on a QReg; mirrors photon/lang's set.
_GATE_METHODS = {
    "h", "x", "y", "z", "s", "sdg", "t", "tdg",
    "rx", "ry", "rz", "cx", "cz", "swap",
    "sx", "sxdg", "ecr", "ms", "rzz",
    "gpi", "gpi2", "u1q",
    "cnot", "hadamard", "phase",
}
_LIB_ROUTINES = {"bell_pair", "ghz", "qft", "iqft",
                 "grover", "teleport", "vqe_ansatz"}
_MEASURE_METHODS = {"measure", "measure_int"}


def _const_value(node: ast.AST) -> Optional[Any]:
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        v = _const_value(node.operand)
        return -v if v is not None else None
    return None


def _expr_text(node: ast.AST) -> str:
    """Render a small numeric/identifier expression as Phonon text."""
    if isinstance(node, ast.Constant):
        return repr(node.value)
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return "-" + _expr_text(node.operand)
    if isinstance(node, ast.BinOp):
        ops = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/"}
        sym = ops.get(type(node.op))
        if sym is None:
            raise UnsupportedConstructError(
                f"binary operator {type(node.op).__name__}",
                getattr(node, "lineno", 0), getattr(node, "col_offset", 0))
        return f"({_expr_text(node.left)} {sym} {_expr_text(node.right)})"
    if isinstance(node, ast.Attribute):
        # `photon.QReg` etc.
        return _expr_text(node.value) + "." + node.attr
    raise UnsupportedConstructError(
        f"expression of kind {type(node).__name__}",
        getattr(node, "lineno", 0), getattr(node, "col_offset", 0))


class Translator(ast.NodeVisitor):
    """Walks a Python `def` body and emits Phonon source text.

    Output goes into `self.lines` (a list of strings; each becomes
    a line in the final Phonon source).
    """

    def __init__(self, target: str = "generic") -> None:
        self.target = target
        self.lines: List[str] = []
        self.indent: int = 1  # inside a `def { ... }`.
        self.qregs: dict[str, int] = {}
        self.measured_bits: dict[str, str] = {}  # python var -> phonon ref
        self.func_name: str = ""

    # ----- output helpers -------------------------------------------------
    def _emit(self, s: str) -> None:
        self.lines.append("  " * self.indent + s)

    def _err(self, what: str, node: ast.AST) -> "UnsupportedConstructError":
        return UnsupportedConstructError(
            what,
            getattr(node, "lineno", 0),
            getattr(node, "col_offset", 0))

    # ----- entry ----------------------------------------------------------
    def translate(self, fn: Callable[..., Any]) -> str:
        """Pull the function source, parse, walk, return Phonon text."""
        try:
            src = inspect.getsource(fn)
        except (OSError, TypeError) as e:
            raise UnsupportedConstructError(
                f"cannot read source of '{fn.__name__}': {e}")
        src = textwrap.dedent(src)
        tree = ast.parse(src, filename=getattr(fn, "__code__",
                                               type("X", (), {"co_filename":
                                                              "<inline>"})())
                         .co_filename)
        if not isinstance(tree, ast.Module) or not tree.body:
            raise UnsupportedConstructError("empty source")
        # Find the FunctionDef (skipping the @photon.kernel decorator
        # already applied; ast still shows it).
        func: Optional[ast.FunctionDef] = None
        for stmt in tree.body:
            if isinstance(stmt, ast.FunctionDef):
                func = stmt
                break
        if func is None:
            raise UnsupportedConstructError("no `def` in kernel source")
        self.func_name = func.name
        self.lines = [f"target {self.target}"]
        # Render the kernel as a phonon `def` (the engine wraps it in a
        # module with the right target attr).
        self._emit_def(func)
        return "\n".join(self.lines) + "\n"

    def _emit_def(self, fn: ast.FunctionDef) -> None:
        # We don't currently propagate Python parameter types into the
        # Phonon def (M5 handles classical-typed kernels). The function
        # body is rendered into a series of top-level Phonon statements
        # because the engine accepts that form.
        # Pre-pass: identify the QReg declaration so we can emit `qubit q[N]`
        # at the top of the program before any gate stmts.
        qreg_size = self._find_qreg(fn.body)
        if qreg_size is None:
            raise UnsupportedConstructError(
                "kernel must contain `q = photon.QReg(N)` exactly once")
        self.qreg_var, n = qreg_size
        self.qregs[self.qreg_var] = n
        # Reset to top-level (no `def { ... }` wrapper at M4; the engine
        # accepts a flat Phonon program).
        self.indent = 0
        self._emit(f"qubit {self.qreg_var}[{n}]")
        self._emit(f"bit __c_{self.qreg_var}[{n}]")
        for stmt in fn.body:
            self._visit_stmt(stmt)

    def _find_qreg(self, body: List[ast.stmt]) -> Optional[tuple[str, int]]:
        for stmt in body:
            if (isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and
                    isinstance(stmt.targets[0], ast.Name) and
                    isinstance(stmt.value, ast.Call)):
                fn = stmt.value.func
                # Match photon.QReg(...) or QReg(...).
                is_qreg = (
                    (isinstance(fn, ast.Attribute) and fn.attr == "QReg") or
                    (isinstance(fn, ast.Name) and fn.id == "QReg")
                )
                if is_qreg and stmt.value.args:
                    n = _const_value(stmt.value.args[0])
                    if isinstance(n, int) and n > 0:
                        return (stmt.targets[0].id, n)
        return None

    # ----- statement visitors --------------------------------------------
    def _visit_stmt(self, node: ast.stmt) -> None:
        if isinstance(node, ast.Assign):
            self._visit_assign(node); return
        if isinstance(node, ast.Expr):
            self._visit_expr_stmt(node); return
        if isinstance(node, ast.For):
            self._visit_for(node); return
        if isinstance(node, ast.If):
            self._visit_if(node); return
        if isinstance(node, ast.Return):
            self._visit_return(node); return
        if isinstance(node, ast.Pass):
            return
        if isinstance(node, ast.Import) or isinstance(node, ast.ImportFrom):
            raise self._err("`import` inside a kernel", node)
        if isinstance(node, ast.While):
            raise self._err("`while` (use a counted `for` loop)", node)
        if isinstance(node, ast.Try):
            raise self._err("`try` / exception handling", node)
        if isinstance(node, ast.With):
            raise self._err("`with` / context managers", node)
        if isinstance(node, ast.AsyncFunctionDef):
            raise self._err("`async def`", node)
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            raise self._err("nested function / class definition", node)
        if isinstance(node, ast.Raise):
            raise self._err("`raise`", node)
        raise self._err(f"statement of kind {type(node).__name__}", node)

    def _visit_assign(self, node: ast.Assign) -> None:
        if (len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)):
            target = node.targets[0].id
            # QReg declaration already handled; skip.
            if (isinstance(node.value, ast.Call) and
                    self._is_qreg_call(node.value)):
                return
            # measure() returning a bit list is recorded so future
            # `if c == 1:` can resolve to a phonon bit reference.
            if (isinstance(node.value, ast.Call) and
                isinstance(node.value.func, ast.Attribute) and
                isinstance(node.value.func.value, ast.Name) and
                node.value.func.value.id in self.qregs and
                node.value.func.attr == "measure"):
                qname = node.value.func.value.id
                # Emit per-slot measure into __c_q[i] bits.
                for i in range(self.qregs[qname]):
                    self._emit(
                        f"__c_{qname}[{i}] = measure {qname}[{i}]")
                self.measured_bits[target] = f"__c_{qname}"
                return
            # Plain int/float assignment becomes a Phonon `int` decl.
            v = _const_value(node.value)
            if isinstance(v, int):
                self._emit(f"int {target} = {v}")
                return
            if isinstance(v, float):
                self._emit(f"angle {target} = {v}")
                return
            raise self._err("assignment of unsupported value", node)
        raise self._err("multi-target or destructuring assignment", node)

    def _is_qreg_call(self, call: ast.Call) -> bool:
        f = call.func
        return ((isinstance(f, ast.Attribute) and f.attr == "QReg") or
                (isinstance(f, ast.Name) and f.id == "QReg"))

    def _visit_expr_stmt(self, node: ast.Expr) -> None:
        v = node.value
        if isinstance(v, ast.Call):
            self._emit_call(v); return
        raise self._err("standalone expression", node)

    def _emit_call(self, call: ast.Call) -> None:
        f = call.func
        if not isinstance(f, ast.Attribute):
            raise self._err(f"free-function call '{ast.dump(f)}'", call)
        if not isinstance(f.value, ast.Name):
            raise self._err("non-trivial receiver in method call", call)
        recv = f.value.id
        method = f.attr
        if recv not in self.qregs:
            raise self._err(f"call on unknown receiver '{recv}'", call)
        # Compile-time fold each arg.
        args_text = []
        for a in call.args:
            try:
                args_text.append(_expr_text(a))
            except UnsupportedConstructError as e:
                raise self._err(f"call argument: {e}", call)
        if method in _GATE_METHODS:
            # Phonon gate stmt form: `<gate> q[i]` or `<gate> q[i], q[j]`,
            # with rotations rendered as `rx(angle) q[i]` etc.
            if method in ("rx", "ry", "rz", "gpi", "gpi2"):
                if len(args_text) != 2:
                    raise self._err(f"{method} expects (angle, idx)", call)
                angle, idx = args_text
                self._emit(f"{method}({angle}) {recv}[{idx}]")
                return
            if method == "u1q":
                if len(args_text) != 3:
                    raise self._err("u1q expects (theta, phi, idx)", call)
                t, p, i = args_text
                self._emit(f"u1q({t}, {p}) {recv}[{i}]")
                return
            if method == "rzz":
                if len(args_text) != 3:
                    raise self._err("rzz expects (angle, a, b)", call)
                a, ia, ib = args_text
                self._emit(f"rzz({a}) {recv}[{ia}], {recv}[{ib}]")
                return
            if method in ("cx", "cnot", "cz", "swap", "ecr", "ms"):
                if len(args_text) != 2:
                    raise self._err(f"{method} expects (a, b)", call)
                a, b = args_text
                gname = "cx" if method == "cnot" else method
                self._emit(f"{gname} {recv}[{a}], {recv}[{b}]")
                return
            # Single-qubit, no angle.
            if len(args_text) != 1:
                raise self._err(f"{method} expects (idx)", call)
            gname = {"hadamard": "h", "phase": "s"}.get(method, method)
            self._emit(f"{gname} {recv}[{args_text[0]}]")
            return
        if method in _LIB_ROUTINES:
            # Photon source surface: `q.<routine>(args)`. Phonon doesn't
            # have library calls natively; we emit a phonon.call op via
            # the textual `call lib.<name>` placeholder. Phase D's
            # platform driver may inline; M2's Library.cpp handles it
            # only at the .pho parse-and-lower path. For the Python
            # decorator's M4 scope we expand inline here.
            self._inline_lib(recv, method, call.args, call); return
        if method in _MEASURE_METHODS:
            # Bare `q.measure()` / `q.measure_int()` as a statement is
            # only meaningful when its result is consumed (assignment or
            # return). M4 rejects bare measures.
            raise self._err(
                f"`{recv}.{method}()` must be the value of a `return` "
                "or assigned to a variable", call)
        raise self._err(f"unknown method '{method}' on QReg", call)

    def _inline_lib(self, recv: str, name: str,
                    args: List[ast.expr], call: ast.Call) -> None:
        n = self.qregs[recv]
        if name == "bell_pair":
            a = _const_value(args[0]) if args else 0
            b = _const_value(args[1]) if len(args) > 1 else 1
            self._emit(f"h {recv}[{a}]")
            self._emit(f"cx {recv}[{a}], {recv}[{b}]"); return
        if name == "ghz":
            self._emit(f"h {recv}[0]")
            for i in range(n - 1):
                self._emit(f"cx {recv}[{i}], {recv}[{i+1}]")
            return
        if name == "qft":
            import math
            for j in range(n):
                self._emit(f"h {recv}[{j}]")
                for k in range(j + 1, n):
                    theta = math.pi / (2 ** (k - j))
                    self._emit(f"rz({theta/2}) {recv}[{k}]")
                    self._emit(f"cx {recv}[{k}], {recv}[{j}]")
                    self._emit(f"rz({-theta/2}) {recv}[{j}]")
                    self._emit(f"cx {recv}[{k}], {recv}[{j}]")
                    self._emit(f"rz({theta/2}) {recv}[{j}]")
            for i in range(n // 2):
                self._emit(f"swap {recv}[{i}], {recv}[{n - 1 - i}]")
            return
        if name == "vqe_ansatz":
            depth = _const_value(args[0]) if args else 1
            for d in range(depth):
                for i in range(n):
                    theta = 0.1 * (d * n + i)
                    self._emit(f"ry({theta}) {recv}[{i}]")
                for i in range(n - 1):
                    self._emit(f"cx {recv}[{i}], {recv}[{i+1}]")
            return
        if name == "teleport":
            # Slots default 0,1,2; folder allows literal indices.
            src = _const_value(args[0]) if len(args) > 0 else 0
            anc = _const_value(args[1]) if len(args) > 1 else 1
            dst = _const_value(args[2]) if len(args) > 2 else 2
            self._emit(f"h {recv}[{anc}]")
            self._emit(f"cx {recv}[{anc}], {recv}[{dst}]")
            self._emit(f"cx {recv}[{src}], {recv}[{anc}]")
            self._emit(f"h {recv}[{src}]")
            self._emit(f"__c_{recv}[{src}] = measure {recv}[{src}]")
            self._emit(f"__c_{recv}[{anc}] = measure {recv}[{anc}]")
            self._emit(f"if (__c_{recv}[{anc}] == 1) {{")
            self._emit(f"  x {recv}[{dst}]")
            self._emit("}")
            self._emit(f"if (__c_{recv}[{src}] == 1) {{")
            self._emit(f"  z {recv}[{dst}]")
            self._emit("}")
            return
        if name == "grover":
            rounds = 1
            for a in args:
                v = _const_value(a)
                if isinstance(v, int): rounds = v
            for i in range(n): self._emit(f"h {recv}[{i}]")
            for _ in range(rounds):
                self._emit('# oracle call (resolved by Phase D)')
                # Diffusion: H^N X^N CZ-chain X^N H^N.
                for i in range(n): self._emit(f"h {recv}[{i}]")
                for i in range(n): self._emit(f"x {recv}[{i}]")
                for i in range(n - 1):
                    self._emit(f"cz {recv}[{i}], {recv}[{n-1}]")
                for i in range(n): self._emit(f"x {recv}[{i}]")
                for i in range(n): self._emit(f"h {recv}[{i}]")
            return
        if name == "iqft":
            import math
            for i in range(n // 2):
                self._emit(f"swap {recv}[{i}], {recv}[{n - 1 - i}]")
            for j in range(n - 1, -1, -1):
                for k in range(n - 1, j, -1):
                    theta = -math.pi / (2 ** (k - j))
                    self._emit(f"rz({theta/2}) {recv}[{k}]")
                    self._emit(f"cx {recv}[{k}], {recv}[{j}]")
                    self._emit(f"rz({-theta/2}) {recv}[{j}]")
                    self._emit(f"cx {recv}[{k}], {recv}[{j}]")
                    self._emit(f"rz({theta/2}) {recv}[{j}]")
                self._emit(f"h {recv}[{j}]")
            return
        raise self._err(f"library routine '{name}'", call)

    def _visit_for(self, node: ast.For) -> None:
        if not isinstance(node.target, ast.Name):
            raise self._err("for-loop target must be a single name", node)
        if not (isinstance(node.iter, ast.Call) and
                isinstance(node.iter.func, ast.Name) and
                node.iter.func.id == "range"):
            raise self._err("for-loop iterable must be `range(...)`", node)
        args = node.iter.args
        if len(args) == 1:
            lo, hi = 0, _const_value(args[0])
        elif len(args) == 2:
            lo = _const_value(args[0])
            hi = _const_value(args[1])
        else:
            raise self._err("range with step is not supported", node)
        if not (isinstance(lo, int) and isinstance(hi, int)):
            raise self._err("range bounds must be integer literals", node)
        var = node.target.id
        self._emit(f"for {var} in {lo}..{hi} {{")
        self.indent += 1
        for s in node.body:
            self._visit_stmt(s)
        self.indent -= 1
        self._emit("}")
        if node.orelse:
            raise self._err("for/else clause", node)

    def _visit_if(self, node: ast.If) -> None:
        # Only `<measured_bit_var> == 1` shape is supported in M4.
        cmp_ = node.test
        if not (isinstance(cmp_, ast.Compare) and
                len(cmp_.ops) == 1 and isinstance(cmp_.ops[0], ast.Eq)):
            raise self._err(
                "if-predicate must be `<measured_bit> == <int>`", node)
        if not isinstance(cmp_.left, ast.Name):
            raise self._err("if-predicate lhs must be a name", node)
        rhs = _const_value(cmp_.comparators[0])
        if not isinstance(rhs, int):
            raise self._err("if-predicate rhs must be a literal int", node)
        var = cmp_.left.id
        bit_ref = self.measured_bits.get(var, var)
        # If bit_ref is a `__c_q` register, take slot 0 (matches the
        # canonical teleport idiom); the user can also `bit c0 =
        # q.measure()[0]` style which we don't support yet.
        if bit_ref.startswith("__c_"):
            bit_ref = bit_ref + "[0]"
        self._emit(f"if ({bit_ref} == {rhs}) {{")
        self.indent += 1
        for s in node.body:
            self._visit_stmt(s)
        self.indent -= 1
        self._emit("}")
        if node.orelse:
            self._emit("else {")
            self.indent += 1
            for s in node.orelse:
                self._visit_stmt(s)
            self.indent -= 1
            self._emit("}")

    def _visit_return(self, node: ast.Return) -> None:
        if node.value is None:
            return
        v = node.value
        # Allow `return q.measure_int()` and `return q.measure()`.
        if (isinstance(v, ast.Call) and isinstance(v.func, ast.Attribute) and
                isinstance(v.func.value, ast.Name) and
                v.func.value.id in self.qregs and
                v.func.attr in _MEASURE_METHODS):
            qname = v.func.value.id
            for i in range(self.qregs[qname]):
                self._emit(f"__c_{qname}[{i}] = measure {qname}[{i}]")
            return
        raise self._err(
            "`return` value must be `q.measure_int()` or `q.measure()`",
            node)


def translate(fn: Callable[..., Any], target: str = "generic") -> str:
    return Translator(target).translate(fn)
