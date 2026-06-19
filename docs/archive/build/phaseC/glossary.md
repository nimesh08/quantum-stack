# Phase C — glossary

Phase C terms only. For Phase A/B terms see
[`../phaseA/glossary.md`](../phaseA/glossary.md) and
[`../phaseB/glossary.md`](../phaseB/glossary.md).

| Term | Definition |
| --- | --- |
| **Photon** | The top layer of the stack; an OO quantum language + its standard algorithm library `photon.lib`. Translates to Phonon. |
| **`.pho`** | File extension of Photon source (the OO surface; not the same as `.phn` Phonon source). |
| **`QReg(n)`** | The Photon quantum-register object — `n` qubits with gate methods. Lowers to Phonon `qubit q[n]`. |
| **gate method** | A method on `QReg` corresponding to a quantum gate, e.g. `q.h(0)` lowers to Phonon `h q[0]`. |
| **`measure_int()`** | Method on `QReg` that measures every qubit and returns the joint outcome as an unsigned integer. Lowers to a Phonon `measure` followed by a bit-pack. |
| **`photon.lib`** | The standard algorithm library: `bell_pair`, `ghz`, `qft`, `iqft`, `grover`, `teleport`, `vqe_ansatz`. Each is an inline-able C++ template plus a Python facade. |
| **frontend** | A translator from a host language (Python, C++) into Phonon. There are exactly two: Python (`@photon.kernel`) and C++ (Clang LibTooling). |
| **`@photon.kernel`** | The Python decorator. Reads the function via `inspect.getsource`, parses with `ast.parse`, walks the tree, emits Phonon, hands off to the engine. |
| **AST visitor (Python)** | A subclass of `ast.NodeVisitor` whose `visit_*` methods map Python AST nodes to Phonon Builder calls. Lives in `photon/frontends/python/photon/_decorator/`. |
| **LibTooling** | Clang's standalone-tool API: `clang::FrontendAction`, `ASTConsumer`, `RecursiveASTVisitor`, `LibASTMatchers`. |
| **`PHOTON_KERNEL`** | The C++ macro / attribute that marks a function as a quantum kernel for the C++ ingester. Default: `[[photon::kernel]]`. |
| **build config** | The YAML file driving the C++ frontend: `sources`, `entry`, `target`, `shots`. Schema in `M5_cpp_frontend.md`. |
| **nanobind** | Wenzel Jakob's BSD-licensed C++/Python binding library. Used in `photon/bindings/` to expose the engine to Python. |
| **engine** | Synonym for the Phase A + Phase B C++ pipeline: parse → typecheck → optimize → lower → place → route → decompose → emit. |
| **convergence** | The architectural property tested in M6: the same logical program written in Photon, decorated Python, and marked C++ produces equivalent compiled Spinor. |
| **histogram** | The shot-count outcome of a quantum execution: `{outcome: count, …}`. Same shape across all three doors. |
| **kernel** | A function whose body is the quantum computation. In Python, `@photon.kernel def f()`; in C++, `[[photon::kernel]] void f()`; in Photon, `kernel f(...)`. |
