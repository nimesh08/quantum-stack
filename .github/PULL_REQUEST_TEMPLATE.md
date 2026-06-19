# Pull request

## Summary

<!-- One paragraph: what does this change, and why? -->

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] New chip (YAML only) or new submission adapter
- [ ] New optimiser pass in Phonon
- [ ] Documentation
- [ ] Refactor / housekeeping

## Test plan

<!-- Which tests did you add or run? What did you check manually? -->

- [ ] `pytest` is green for the affected packages.
- [ ] `cmake --build && ctest` is green if the C++ engine changed.
- [ ] `npm test` is green if the playground changed.
- [ ] `mkdocs build --strict` is green if the docs changed.
- [ ] `python scripts/check_docs.py` is green.
- [ ] `bash scripts/check_no_cursor.sh` is green.

## Rule check

- [ ] No new optimisation in Spinor
      ([RULE 2](https://nimesh08.github.io/quantum-stack/internals/seven_rules/)).
- [ ] No reimplementation of compilation outside the C++ engine
      ([RULE 3](https://nimesh08.github.io/quantum-stack/internals/seven_rules/)).
- [ ] No silent provider transpilation
      ([RULE 5](https://nimesh08.github.io/quantum-stack/internals/seven_rules/)).
- [ ] All third-party version pins are unchanged or have been
      re-verified upstream
      ([RULE 4](https://nimesh08.github.io/quantum-stack/internals/seven_rules/)).

## Screenshots / logs (optional)

<!-- For UI changes or interesting log output. -->
