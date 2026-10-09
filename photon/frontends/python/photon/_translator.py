"""@photon.kernel AST translator (M4) - Part 1: framework and gate set."""
from __future__ import annotations
import ast
import copy
import inspect
import json
import math
import os
import re
import textwrap
from typing import Any, Callable, List, Optional
from ._errors import UnsupportedConstructError

# Photon gate methods recognised on a QReg; mirrors photon/lang's set.
_GATE_METHODS = {
    "h", "x", "y", "z", "s", "sdg", "t", "tdg",
    "rx", "ry", "rz", "cx", "cz", "swap",
    "sx", "sxdg", "ecr", "ms", "rzz", "rxx",
    "gpi", "gpi2", "u1q", "reset",
    "cnot", "hadamard", "phase",
}
_LIB_ROUTINES = {"bell_pair", "ghz", "qft", "iqft",
                 "grover", "teleport", "vqe_ansatz"}
_MEASURE_METHODS = {"measure", "measure_int"}


def _const_value(node: ast.AST, values: Optional[dict[str, Any]] = None) -> Optional[Any]:
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return (values or {}).get(node.id)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        v = _const_value(node.operand, values)
        return -v if type(v) in (int, float) else None
    if isinstance(node, ast.BinOp):
        lhs, rhs = _const_value(node.left, values), _const_value(node.right, values)
        if not isinstance(lhs, (int, float)) or not isinstance(rhs, (int, float)):
            return None
        if isinstance(node.op, ast.Add): return lhs + rhs
        if isinstance(node.op, ast.Sub): return lhs - rhs
        if isinstance(node.op, ast.Mult): return lhs * rhs
        if isinstance(node.op, ast.Div) and rhs != 0: return lhs / rhs
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
        self.bit_widths: dict[str, int] = {}
        self.saved_measurement_registers: set[str] = set()
        self.measurement_sequence = 0
        self.return_bits: Optional[list[int]] = None
        self.declaration_end = 0
        self.func_name: str = ""
        self.constants: dict[str, Any] = {}
        self.expanded_iterations = 0
        self.runtime_depth = 0
        self.runtime_values: dict[str, tuple[str, int]] = {}
        self.operation_budget = int(os.environ.get("QSTACK_EXPANDED_OPERATION_BUDGET", "100000"))
        self.normalized_returns = False
        self.return_signature = None
        self.return_register = None
        self.return_value: Optional[tuple[str,int]]=None
        self.loop_depth=0
        self.loop_return_mode=False
        self.may_return=False
        if self.operation_budget <= 0:
            raise UnsupportedConstructError("expanded operation budget must be positive")

    def _value(self, node: ast.AST) -> Optional[Any]:
        return _const_value(node, self.constants)

    # ----- output helpers -------------------------------------------------
    def _emit(self, s: str) -> None:
        if len(self.lines) >= self.operation_budget:
            raise UnsupportedConstructError("expanded operation budget exceeded; raise QSTACK_EXPANDED_OPERATION_BUDGET or reduce the program")
        self.lines.append("  " * self.indent + s)

    @staticmethod
    def _helper_call(node: ast.AST, name: str) -> bool:
        return isinstance(node, ast.Call) and ((isinstance(node.func, ast.Name) and node.func.id == name) or
            (isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name) and node.func.value.id == "photon" and node.func.attr == name))

    def _controller(self, node: ast.AST) -> tuple[str, tuple[str, int]]:
        """Render finite device-controller expressions without evaluating shots on the host."""
        if isinstance(node, ast.Name) and node.id in self.runtime_values:
            return node.id, self.runtime_values[node.id]
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name) and node.value.id in self.measured_bits:
            register = self.measured_bits[node.value.id]
            index = self._value(node.slice)
            if type(index) is not int or not 0 <= index < self.bit_widths[register]:
                raise self._err("measured bit requires an in-range static index", node)
            return f"{register}[{index}]", ("bool", 1)
        if isinstance(node, ast.Name) and node.id in self.measured_bits:
            register = self.measured_bits[node.id]
            if self.bit_widths[register] != 1:
                raise self._err("measured register requires an explicit bit index", node)
            return f"{register}[0]", ("bool", 1)
        value = self._value(node)
        if type(value) is bool:
            return str(int(value)), ("bool", 1)
        if type(value) in (int, float) and math.isfinite(value):
            return repr(value), ("constant", 0)
        if self._helper_call(node, "uint"):
            if len(node.args) != 2 or node.keywords:
                raise self._err("photon.uint expects (width, value)", node)
            width = self._value(node.args[0])
            if type(width) is not int or not 1 <= width <= 64:
                raise self._err("uint width must be a static integer in [1,64]", node)
            text, _ = self._controller(node.args[1])
            return f"uint[{width}]({text})", ("uint", width)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
            text, type_ = self._controller(node.operand)
            if type_ != ("bool", 1):
                raise self._err("not requires a Boolean controller value", node)
            return f"!({text})", ("bool", 1)
        if isinstance(node,ast.UnaryOp) and isinstance(node.op,ast.Invert):
            text,type_=self._controller(node.operand)
            if type_[0]!="uint":raise self._err("bitwise complement requires photon.uint",node)
            return f"~({text})",type_
        if isinstance(node, ast.Compare) and len(node.ops) == 1:
            operators = {ast.Eq: "==", ast.NotEq: "!=", ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">="}
            if type(node.ops[0]) not in operators:
                raise self._err("unsupported controller comparison", node)
            left, lt = self._controller(node.left); right, rt = self._controller(node.comparators[0])
            if lt[0] != "constant" and rt[0] != "constant" and lt != rt:
                raise self._err("controller comparison requires matching widths; use photon.uint to cast", node)
            return f"{left} {operators[type(node.ops[0])]} {right}", ("bool", 1)
        if isinstance(node, ast.BinOp):
            symbols = {ast.Add: "+", ast.Sub: "-", ast.BitAnd: "&", ast.BitOr: "|", ast.BitXor: "^", ast.LShift: "<<", ast.RShift: ">>"}
            if type(node.op) not in symbols:
                raise self._err("runtime arithmetic supports +, -, bitwise operators and constant shifts", node)
            left, lt = self._controller(node.left); right, rt = self._controller(node.right)
            result = rt if lt[0] == "constant" else lt
            if result[0] not in ("bool", "uint") or (rt[0] != "constant" and lt != rt):
                raise self._err("runtime arithmetic requires explicit uint values of matching widths", node)
            if result[0] == "bool" and type(node.op) not in (ast.BitAnd, ast.BitOr, ast.BitXor):
                raise self._err("Boolean values support only &, | and ^; cast with photon.uint for arithmetic", node)
            return f"({left} {symbols[type(node.op)]} {right})", result
        raise self._err("expression is outside the finite controller language", node)

    def _measurement_register(self, qname: str, *, saved: bool) -> str:
        register = f"__c_{qname}"
        if register in self.saved_measurement_registers:
            self.measurement_sequence += 1
            register = f"__qstack_measure_{self.measurement_sequence}"
            while register in self.qregs or register in self.bit_widths:
                self.measurement_sequence += 1
                register = f"__qstack_measure_{self.measurement_sequence}"
            self.lines.insert(self.declaration_end, f"bit {register}[{self.qregs[qname]}]")
            self.declaration_end += 1
            self.bit_widths[register] = self.qregs[qname]
        if saved:
            self.saved_measurement_registers.add(register)
        return register

    def _err(self, what: str, node: ast.AST) -> "UnsupportedConstructError":
        return UnsupportedConstructError(
            what,
            getattr(node, "lineno", 0),
            getattr(node, "col_offset", 0))

    # ----- entry ----------------------------------------------------------
    def translate(self, fn: Callable[..., Any], bindings: Optional[dict[str, Any]] = None) -> str:
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
        if bindings:
            for name, value in bindings.items():
                if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                    raise UnsupportedConstructError(f"parameter '{name}' must be a finite int or float")
            class BindParameters(ast.NodeTransformer):
                def visit_Name(self, node):
                    if isinstance(node.ctx, ast.Store) and node.id in bindings:
                        raise UnsupportedConstructError(f"rebinding kernel parameter '{node.id}'")
                    if isinstance(node.ctx, ast.Load) and node.id in bindings:
                        return ast.copy_location(ast.Constant(value=bindings[node.id]), node)
                    return node
            func = BindParameters().visit(func)
        self.func_name = func.name
        target = self.target if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", self.target) else json.dumps(self.target)
        self.lines = [f"target {target}"]
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
        declarations = []
        for stmt in fn.body:
            declaration = self._find_qreg([stmt])
            if declaration:
                declarations.append(declaration)
        if not declarations:
            raise UnsupportedConstructError(
                "kernel must contain a QReg with a positive integer size")
        # Reset to top-level (no `def { ... }` wrapper at M4; the engine
        # accepts a flat Phonon program).
        self.indent = 0
        self.declaration_end = len(self.lines)
        def has_return(statements):
            return any(isinstance(child,ast.Return) for statement in statements for child in ast.walk(statement))

        def single_exit(statements):
            if not statements:return []
            first,*rest=statements
            if isinstance(first,ast.Return):return [first]
            if isinstance(first,(ast.For,ast.While)) and has_return(first.body):
                raise self._err("return inside a loop requires loop-exit normalization",first)
            if isinstance(first,ast.If) and has_return(first.body+first.orelse):
                branch=copy.copy(first)
                branch.body=single_exit(first.body+copy.deepcopy(rest))
                branch.orelse=single_exit(first.orelse+copy.deepcopy(rest))
                return [branch]
            return [first]+single_exit(rest)

        self.normalized_returns=any(has_return([statement]) for statement in fn.body[:-1]) or any(isinstance(statement,ast.If) and has_return(statement.body+statement.orelse) for statement in fn.body)
        self.loop_return_mode=any(isinstance(child,(ast.For,ast.While)) and has_return(child.body) for child in ast.walk(fn))
        statements=single_exit(fn.body) if self.normalized_returns and not self.loop_return_mode else fn.body
        def all_paths_return(block):
            if not block:return False
            final=block[-1]
            if isinstance(final,ast.Return):return final.value is not None
            if isinstance(final,ast.If):
                if isinstance(final.test,ast.Compare) and len(final.test.ops)==1:
                    a,b=_const_value(final.test.left),_const_value(final.test.comparators[0])
                    if type(a) in (int,float) and type(b) in (int,float):
                        choices={ast.Eq:a==b,ast.NotEq:a!=b,ast.Lt:a<b,ast.LtE:a<=b,ast.Gt:a>b,ast.GtE:a>=b}
                        if type(final.test.ops[0]) in choices:return all_paths_return(final.body if choices[type(final.test.ops[0])] else final.orelse)
                return all_paths_return(final.body) and all_paths_return(final.orelse)
            return False
        if self.normalized_returns and not all_paths_return(statements):
            raise UnsupportedConstructError("conditional return requires every reachable path to return a compatible value")
        if self.loop_return_mode:
            if any(isinstance(child,ast.Name) and child.id=="__qstack_returned" for child in ast.walk(fn)):raise UnsupportedConstructError("__qstack_returned is reserved for normalized loop returns")
            self._emit("bool __qstack_returned = 0");self.runtime_values["__qstack_returned"]=("bool",1)
        self._visit_sequence(statements)
        if self.return_value is not None:self._emit(f"output {self.return_value[0]}")

    def _visit_sequence(self,statements):
        for index,statement in enumerate(statements):
            if self.loop_return_mode and self.may_return:
                self._emit("if (__qstack_returned == 0) {");self.indent+=1
                self.may_return=False
                self._visit_sequence(statements[index:])
                self.indent-=1;self._emit("}");self.may_return=True
                return
            self._visit_stmt(statement)

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
                if is_qreg and len(stmt.value.args) == 1 and not stmt.value.keywords:
                    n = _const_value(stmt.value.args[0])
                    if isinstance(n, int) and not isinstance(n, bool) and 0 < n <= 1000000:
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
        if isinstance(node,(ast.Break,ast.Continue)):
            if not self.loop_depth:raise self._err("break/continue requires a bounded runtime loop",node)
            self._emit("break" if isinstance(node,ast.Break) else "continue");return
        if isinstance(node, ast.Pass):
            return
        if isinstance(node, ast.Import) or isinstance(node, ast.ImportFrom):
            raise self._err("`import` inside a kernel", node)
        if isinstance(node, ast.While):
            self._visit_bounded_while(node);return
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
                if self.runtime_depth and target not in self.qregs:
                    if target in self.runtime_values or target in self.constants or target in self.measured_bits:
                        raise self._err("branch-local QReg cannot shadow an existing binding",node)
                    size=self._value(node.value.args[0]) if len(node.value.args)==1 and not node.value.keywords else None
                    if type(size) is not int or not 0<size<=1000000:
                        raise self._err("branch-local QReg requires a positive static size",node)
                    self.qregs[target]=size
                    self._emit(f"qubit {target}[{size}]")
                    register=f"__c_{target}"
                    if register not in self.bit_widths:
                        self.lines.insert(self.declaration_end,f"bit {register}[{size}]")
                        self.declaration_end+=1;self.bit_widths[register]=size
                    return
                if target not in self.qregs and not self.indent:
                    size=self._value(node.value.args[0]) if len(node.value.args)==1 and not node.value.keywords else None
                    if type(size) is not int or not 0<size<=1000000:raise self._err("QReg requires a positive static size",node)
                    self.qregs[target]=size;self._emit(f"qubit {target}[{size}]")
                    register=f"__c_{target}"
                    if register not in self.bit_widths:
                        self._emit(f"bit {register}[{size}]");self.bit_widths[register]=size;self.declaration_end=len(self.lines)
                    return
                if target in self.qregs or self.indent:
                    raise self._err("QReg redeclaration is forbidden; branch-local QReg requires a new name and static size", node)
                return
            if target in self.qregs:
                raise self._err("quantum registers cannot be rebound to classical values", node)
            controller_expression = target in self.runtime_values or self._helper_call(node.value, "uint") or isinstance(node.value, (ast.Subscript, ast.Compare, ast.UnaryOp, ast.BinOp)) or (isinstance(node.value, ast.Name) and node.value.id in self.runtime_values | self.measured_bits) or isinstance(node.value, ast.Constant) and type(node.value.value) is bool
            if (controller_expression and (self._value(node.value) is None or type(self._value(node.value)) is bool)) or target in self.runtime_values or self._helper_call(node.value, "uint"):
                text, type_ = self._controller(node.value)
                previous = self.runtime_values.get(target)
                if previous:
                    if type_[0] != "constant" and previous != type_:
                        raise self._err("assignment changes controller type or width; cast explicitly", node)
                    self._emit(f"{target} = {text}")
                else:
                    if type_[0] == "constant":
                        raise self._err("runtime scalar requires bool or explicit photon.uint", node)
                    declaration = "bool" if type_[0] == "bool" else f"uint[{type_[1]}]"
                    self._emit(f"{declaration} {target} = {text}")
                    self.runtime_values[target] = type_
                self.constants.pop(target, None)
                return
            # measure() returning a bit list is recorded so future
            # `if c == 1:` can resolve to a phonon bit reference.
            if (isinstance(node.value, ast.Call) and
                isinstance(node.value.func, ast.Attribute) and
                isinstance(node.value.func.value, ast.Name) and
                node.value.func.value.id in self.qregs and
                node.value.func.attr == "measure"):
                if node.value.args or node.value.keywords:
                    raise self._err("measure() accepts no arguments; index the returned measured bits", node.value)
                qname = node.value.func.value.id
                if self.runtime_depth:
                    if target not in self.measured_bits:
                        raise self._err("declare a measured register before runtime control; saved scalar bits use explicit snapshots",node)
                    register = self.measured_bits[target]
                    if self.bit_widths[register] != self.qregs[qname]:
                        raise self._err("runtime measurement assignment must preserve register width",node)
                else:
                    register = self._measurement_register(qname, saved=True)
                # Emit per-slot measure into __c_q[i] bits.
                for i in range(self.qregs[qname]):
                    self._emit(
                        f"{register}[{i}] = measure {qname}[{i}]")
                self.measured_bits[target] = register
                self.constants.pop(target, None)
                return
            if self.runtime_depth:
                raise self._err("runtime branches cannot change classical bindings of compile-time scalars; use photon.uint", node)
            # Plain int/float assignment becomes a Phonon `int` decl.
            v = self._value(node.value)
            if type(v) is int:
                self.measured_bits.pop(target, None)
                self.constants[target] = v
                self._emit(f"int {target} = {v}")
                return
            if type(v) is float and math.isfinite(v):
                self.measured_bits.pop(target, None)
                self.constants[target] = v
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
        if self._helper_call(v,"discard"):
            if len(v.args)!=1 or v.keywords or not isinstance(v.args[0],ast.Name) or v.args[0].id not in self.qregs:raise self._err("photon.discard expects one live quantum register",v)
            name=v.args[0].id;self._emit(f"discard {name}");self.qregs.pop(name);return
        if self._helper_call(v, "output"):
            if len(v.args)!=1 or v.keywords or not isinstance(v.args[0],ast.Name) or v.args[0].id not in self.runtime_values:
                raise self._err("photon.output expects one named controller value",v)
            self._emit(f"output {v.args[0].id}")
            return
        if isinstance(v, ast.Call):
            self._emit_call(v); return
        raise self._err("standalone expression", node)

    def _emit_call(self, call: ast.Call) -> None:
        if call.keywords:
            raise self._err("keyword gate arguments are not supported", call)
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
                value = self._value(a)
                args_text.append(repr(value) if type(value) in (int, float) else _expr_text(a))
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
            if method in ("rzz", "rxx"):
                if len(args_text) != 3:
                    raise self._err(f"{method} expects (angle, a, b)", call)
                a, ia, ib = args_text
                self._emit(f"{method}({a}) {recv}[{ia}], {recv}[{ib}]")
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
            # Expand supported routines into concrete Phonon instructions.
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
        allowed = {"bell_pair": (0, 2), "teleport": (0, 3),
                   "ghz": (0,), "qft": (0,), "iqft": (0,), "vqe_ansatz": (0, 1)}
        if name in allowed and len(args) not in allowed[name]:
            raise self._err(f"{name} expects argument count in {allowed[name]}", call)

        def constant(arg):
            try:
                return self._value(arg)
            except (TypeError, ValueError, ArithmeticError):
                return None

        def indices(defaults):
            values = [constant(arg) for arg in args] if args else list(defaults)
            if any(type(value) is not int for value in values):
                raise self._err(f"{name}: indices must be compile-time integers", call)
            if any(value < 0 or value >= n for value in values) or len(set(values)) != len(values):
                raise self._err(f"{name} requires distinct in-range qubits", call)
            return values

        if name == "bell_pair":
            a, b = indices((0, 1))
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
                    self._emit(f"gphase({theta/4})")
            for i in range(n // 2):
                self._emit(f"swap {recv}[{i}], {recv}[{n - 1 - i}]")
            return
        if name == "vqe_ansatz":
            depth = constant(args[0]) if args else 1
            if type(depth) is not int or depth < 1 or depth * n > 100000:
                raise self._err("vqe_ansatz: depth must be a positive integer and expansion must not exceed 100000 qubit-layers", call)
            for d in range(depth):
                for i in range(n):
                    theta = 0.1 * (d * n + i)
                    self._emit(f"ry({theta}) {recv}[{i}]")
                for i in range(n - 1):
                    self._emit(f"cx {recv}[{i}], {recv}[{i+1}]")
            return
        if name == "teleport":
            src, anc, dst = indices((0, 1, 2))
            self._emit(f"h {recv}[{anc}]")
            self._emit(f"cx {recv}[{anc}], {recv}[{dst}]")
            self._emit(f"cx {recv}[{src}], {recv}[{anc}]")
            self._emit(f"h {recv}[{src}]")
            register = self._measurement_register(recv, saved=False)
            self._emit(f"{register}[{src}] = measure {recv}[{src}]")
            self._emit(f"{register}[{anc}] = measure {recv}[{anc}]")
            self._emit(f"if ({register}[{anc}] == 1) {{")
            self._emit(f"  x {recv}[{dst}]")
            self._emit("}")
            self._emit(f"if ({register}[{src}] == 1) {{")
            self._emit(f"  z {recv}[{dst}]")
            self._emit("}")
            return
        if name == "grover":
            raise self._err("grover requires a bound oracle and exact diffusion; callable oracle lowering is unsupported", call)
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
                    self._emit(f"gphase({theta/4})")
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
        if node.iter.keywords or not 1 <= len(node.iter.args) <= 3:
            raise self._err("range expects one, two, or three positional integers", node)
        args = [self._value(arg) for arg in node.iter.args]
        if any(type(arg) is not int for arg in args):
            raise self._err("range bounds and step must be compile-time integers", node)
        if len(args) == 3 and args[2] == 0:
            raise self._err("range step must be nonzero", node)
        if node.orelse:
            raise self._err("for/else clause", node)
        var = node.target.id
        # Bind each Python induction value before visiting its body. This keeps
        # descending/strided/nested ranges and library index arguments exact.
        for value in range(*args):
            self.expanded_iterations += 1
            if self.expanded_iterations > 100000:
                raise self._err("static loop expansion exceeds 100000 iterations", node)
            self.constants[var] = value
            self.indent += 1
            self._visit_sequence(node.body)
            self.indent -= 1

    def _visit_if(self, node: ast.If) -> None:
        if isinstance(node.test, ast.Compare) and len(node.test.ops) == 1:
            lhs, rhs = self._value(node.test.left), self._value(node.test.comparators[0])
            if type(lhs) in (int, float) and type(rhs) in (int, float):
                comparisons = {ast.Eq: lhs == rhs, ast.NotEq: lhs != rhs, ast.Lt: lhs < rhs,
                               ast.LtE: lhs <= rhs, ast.Gt: lhs > rhs, ast.GtE: lhs >= rhs}
                if type(node.test.ops[0]) not in comparisons:
                    raise self._err("unsupported static comparison", node)
                self.indent += 1
                self._visit_sequence(node.body if comparisons[type(node.test.ops[0])] else node.orelse)
                self.indent -= 1
                return
        condition, type_ = self._controller(node.test)
        if type_ != ("bool", 1):
            raise self._err("if requires a Boolean condition; compare uint explicitly", node)
        self._emit(f"if ({condition}) {{")
        constants_before, values_before = self.constants.copy(), self.runtime_values.copy()
        qregs_before=self.qregs.copy()
        return_before=self.may_return
        self.runtime_depth += 1
        self.indent += 1
        self._visit_sequence(node.body)
        return_then=self.may_return
        self.indent -= 1
        self._emit("}")
        self.constants, self.runtime_values = constants_before.copy(), values_before.copy()
        self.qregs=qregs_before.copy()
        self.may_return=return_before
        if node.orelse:
            self._emit("else {")
            self.indent += 1
            self._visit_sequence(node.orelse)
            self.indent -= 1
            self._emit("}")
        self.runtime_depth -= 1
        self.constants, self.runtime_values = constants_before, values_before
        self.qregs=qregs_before
        self.may_return=self.may_return or return_then

    def _visit_bounded_while(self,node:ast.While)->None:
        call=node.test
        if not self._helper_call(call,"bounded"):
            raise self._err("`while` requires photon.bounded(condition, max_iterations=N)",node)
        if len(call.args)!=1 or len(call.keywords)!=1 or call.keywords[0].arg!="max_iterations" or node.orelse:
            raise self._err("bounded while expects one condition, max_iterations=N and no else clause",node)
        bound=self._value(call.keywords[0].value)
        if type(bound) is not int or not 0<bound<=self.operation_budget:
            raise self._err("max_iterations must be a positive static integer within the operation budget",node)
        condition,type_=self._controller(call.args[0])
        if type_!=("bool",1):
            raise self._err("bounded while requires a Boolean predicate",node)
        if self.loop_return_mode:condition=f"({condition}) & (__qstack_returned == 0)"
        self._emit(f"while ({condition}) max_iterations {bound} {{")
        constants_before,values_before=self.constants.copy(),self.runtime_values.copy()
        qregs_before=self.qregs.copy()
        self.runtime_depth+=1;self.indent+=1;self.loop_depth+=1
        self._visit_sequence(node.body)
        self.indent-=1;self.runtime_depth-=1;self.loop_depth-=1
        self.constants,self.runtime_values=constants_before,values_before
        self.qregs=qregs_before
        self._emit("}")

    def _visit_return(self, node: ast.Return) -> None:
        if self.indent and not self.normalized_returns and not self.loop_return_mode:
            raise self._err("return inside a loop or conditional is unsupported", node)
        if node.value is None:
            return
        v = node.value
        def mark_return():
            if self.loop_return_mode:
                self._emit("__qstack_returned = 1");self.may_return=True
                if self.loop_depth:self._emit("break")
        # Allow `return q.measure_int()` and `return q.measure()`.
        if (isinstance(v, ast.Call) and isinstance(v.func, ast.Attribute) and
                isinstance(v.func.value, ast.Name) and
                v.func.value.id in self.qregs and
                v.func.attr in _MEASURE_METHODS):
            if v.args or v.keywords:
                raise self._err(f"{v.func.attr}() accepts no arguments", v)
            qname = v.func.value.id
            signature=(self.qregs[qname],v.func.attr)
            if self.return_signature is not None and self.return_signature!=signature:
                raise self._err("all return paths must use the same width and return representation",node)
            self.return_signature=signature
            if self.return_register is None:self.return_register=f"__c_{qname}"
            offset = 0
            for register, width in self.bit_widths.items():
                if register == self.return_register:
                    self.return_bits = list(range(offset, offset + width))
                    break
                offset += width
            for i in range(self.qregs[qname]):
                self._emit(f"{self.return_register}[{i}] = measure {qname}[{i}]")
            mark_return()
            return
        text,type_=self._controller(v)
        if type_[0] not in ("bool","uint"):raise self._err("classical returns require bool or explicit photon.uint",node)
        signature=("controller",type_)
        if self.return_signature is not None and self.return_signature!=signature:raise self._err("all return paths must have the same classical type and width",node)
        self.return_signature=signature
        if self.return_value is None:
            name="__qstack_return_value"
            if name in self.runtime_values or name in self.constants or name in self.qregs:raise self._err("__qstack_return_value is reserved for normalized returns",node)
            declaration="bool" if type_[0]=="bool" else f"uint[{type_[1]}]"
            self.lines.insert(self.declaration_end,f"{declaration} {name} = 0");self.declaration_end+=1
            self.return_value=(name,type_[1])
        self._emit(f"{self.return_value[0]} = {text}")
        mark_return()


def translate(fn: Callable[..., Any], target: str = "generic", *,
              bindings: Optional[dict[str, Any]] = None) -> str:
    return Translator(target).translate(fn, bindings=bindings)
