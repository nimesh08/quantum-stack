# Operation-specific placement and routing

The compiler checks each operation against active physical qubits and the
operation's own ordered locus list. Different logical wires can use different
single-qubit bases, and a wire can move to a compatible gate or readout site.
Every inserted SWAP is synthesized and checked against the native operations on
its physical locus. Disabled components, directed gates and reserved resonators
remain constraints throughout compilation and final validation.

`placement_strategy=auto` first retains a legal uniform compilation. If that
cannot satisfy the target, it tries the heterogeneous search. Explicit
`heterogeneous` also tries an alternative to a legal uniform incumbent. An
alternative is accepted only when both native two-qubit count and total native
gate count are no greater than that incumbent. Search limits never invalidate a
previously validated incumbent.

| Optimization level | Initial layouts | Beam width | Expanded-state budget |
|---|---:|---:|---:|
| O0 / O1 | 1 | 16 | 10,000 |
| O2 | 1 | 32 | 50,000 |
| O3 | 8 | 64 | 200,000 shared across layouts |

The default seed is 42. CLI/configuration fields `placement_strategy`,
`placement_max_states`, `placement_beam_width`, `placement_max_layouts` and
`placement_max_swaps` override these choices. The report records the actual seed,
budgets, visited candidates, selected routing SWAP count and incumbent decision.
The SWAP cap concerns inserted routing operations, including remaining branch
compensation, rather than source-level SWAP gates.

Branch joins consider the entry map, up to 4,096 recorded routing-prefix maps
per arm, and each final map. Common maps are ranked by the native cost of the
remaining compensation suffixes; both native two-qubit and total gate counts
must remain no greater than the entry-map fallback. The chosen prefix propagates
to enclosing branches. Complete compensation groups include their scoped phase
instructions. Candidate counts and any truncated enumeration are reported.
Measurement destinations remain their original classical indices after physical
remapping. Classical operations and control boundaries remain rewrite fences.

An exhausted state budget reports `PLACEMENT_SEARCH_LIMIT`; a requested SWAP cap
can report `PLACEMENT_SWAP_LIMIT`. `PLACEMENT_SEARCH_INCOMPLETE` means this finite
search found no legal candidate. These diagnostics do not prove that no possible
layout exists. Explicit capacity or disconnected-component contradictions can
instead report `TARGET_INCOMPATIBLE`.

IQM MOVE recipes reserve resonators, move one computational state into a vacant
resonator, apply a permitted interaction and return that state. The compiler
validates occupation and return assumptions. This is a restricted-subspace
gate-level contract; it is not pulse calibration, a full-space MOVE equivalence
claim, or an experimental fidelity result.

Search coverage is bounded. Candidate ranking uses legal native gate counts and
SWAPs; supplied quality data can influence the existing uniform incumbent, but
the heterogeneous search does not claim an optimal fidelity or duration layout.
Native recipe residuals are explicitly unmeasured in the compilation report;
independent operator/instrument tests and `qstack verify` provide separate finite
evidence. Existing YAML families and unavailable vendors retain their readiness
restrictions and require current concrete capability contracts for submission.
