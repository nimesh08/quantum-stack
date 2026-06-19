# Phonon — install

Phonon ships in the same `spinorc` binary as Spinor; there is no
separate `phononc` to install. If you have `spinorc` working, you
have Phonon.

## With `heisenberg`

```bash
pip install heisenberg
heisenberg version              # confirms spinorc is wired
```

This installs the launcher plus the signed `spinorc` binary that
parses and lowers Phonon source.

## From source

The full from-source build is documented in
[Spinor / install / from source](../spinor/install.md#from-source).
Phonon's parser, lowering, types, and optimizer all live under
[`phonon/`](https://github.com/nimesh08/quantum-stack/tree/main/phonon)
and are built by the same `cmake --build build` command.

## Verify

```bash
spinorc parse /path/to/program.pho     # any .pho file from the cookbook
spinorc verify -t generic /path/to/program.pho
spinorc compile -t ibm_heron_r2 /path/to/program.pho
```

The same `spinorc` subcommands work — Phonon files are detected by
content (presence of `def`, `for`, `if` constructs), not by file
extension.

## File extension

By convention `.pho`. The compiler does not care about the
extension; it reads the source and dispatches on what it sees.

---

!!! quote "Credits"
    **Phonon** was designed and implemented by **Nimesh Cheedella**.
