---
title: Authors and credits
---

# Authors and credits

Heisenberg, Spinor, Phonon and Photon were designed and implemented
by **Nimesh Cheedella**.

| | |
|---|---|
| Email | <chnimesh0808@gmail.com> |
| GitHub | [@nimesh08](https://github.com/nimesh08) |
| Repository | [github.com/nimesh08/quantum-stack](https://github.com/nimesh08/quantum-stack) |

## Scope of authorship

- Spinor — the chip-locking C++ compiler. Parser, verifier,
  placement, SABRE routing, KAK + Euler-ZYZ decomposition, OpenQASM
  3 / QIR / Quil emitters.
- Phonon — the structured IR with linear types and the optimizer
  (cancellation, rotation merging, ZX, scheduling).
- Photon — the OO user-facing language with three frontends
  converging on the same C++ engine via nanobind.
- Platform — FastAPI jobsvc, the worker queue, the calibration
  scheduler, the React 19 + Monaco playground, the
  `heisenberg` launcher.
- Chip registry — 27 verified chips across 8 vendors, plus the
  topology library.
- Documentation — every page on this site, plus the AUTHORS.md
  and CONTRIBUTING.md files.

## How to cite

Until a peer-reviewed paper exists, cite the GitHub repository:

```
Cheedella, N. (2026). Heisenberg Quantum Stack
(version 0.5.0) [Computer software].
https://github.com/nimesh08/quantum-stack
```

## License

Heisenberg is released under the
[Apache License 2.0](https://github.com/nimesh08/quantum-stack/blob/main/LICENSE).
You may use, modify, and redistribute the project provided you
retain the LICENSE notice. The trademarks "Photon", "Phonon", and
"Spinor" are working names; please do not use them in derivative
projects without first checking trademark status — see RULE 7 in
[the seven critical rules](internals/seven_rules.md).
