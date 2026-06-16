# How the `@photon.kernel` decorator works

`@photon.kernel` reads the function's source via `inspect.getsource`,
parses it with `ast.parse`, walks the AST with a custom visitor, and
emits Phonon source text. Then it calls `photon._engine.compile_phonon`
to produce a `CompiledProgram`.

## Stage 1 — `inspect.getsource`

The decorator captures the function's source text at decoration time
(when the module loads). This requires the function's source to be on
disk (it doesn't work in the REPL or in `eval`'d code).

## Stage 2 — `ast.parse`

Standard Python AST parse. Yields a `FunctionDef` node.

## Stage 3 — `ast.NodeVisitor`

The visitor walks the body. For each node:

- `Assign(targets=[Name('q')], value=Call(QReg, [N]))` → `qubit q[N]`
  + `bit c[N]` declaration.
- `Expr(value=Call(Attribute(Name('q'), 'h'), [k]))` → `h q[k]`.
- `For(Name('i'), Call(Name('range'), [lo, hi]))` → `for i in lo..hi`.
- `If(Compare(...))` → `if (cond) { ... }`.
- `Return(Call(Attribute(Name('q'), 'measure_int')))` → kernel return.
- Anything else → raise [`UnsupportedConstructError`](unsupported_constructs.md).

## Stage 4 — engine call

The emitted Phonon text is passed to
`photon._engine.compile_phonon(text, target)`. The result is a
`CompiledProgram` exposing `.ok`, `.error`, `.estimate()`,
`.dump_phonon()`, `.dump_spinor()`.

## Why translate to Phonon, not directly to Spinor?

Phonon is the IR the optimizer works on. Going Photon → Phonon →
Spinor lets the optimizer benefit Photon kernels too. And the same
nanobind entry (`compile_phonon`) is used by the C++ frontend and the
.pho file frontend — three doors, one engine.

## Source

[`photon/frontends/python/photon/_decorator.py`](https://github.com/nimesh08/quantum-stack/blob/main/photon/frontends/python/photon/_decorator.py),
[`_translator.py`](https://github.com/nimesh08/quantum-stack/blob/main/photon/frontends/python/photon/_translator.py).

## See also

[Unsupported constructs](unsupported_constructs.md), [Photon language `@photon.kernel`](../../languages/photon/reference/frontends/photon_kernel.md)
