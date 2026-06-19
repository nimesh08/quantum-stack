---
name: Chip request
about: Add support for a new quantum chip.
title: "[chip] "
labels: chip-request
---

## Chip details

| Field | Value |
|-------|-------|
| Vendor | |
| Chip name / id | |
| Modality (gate-model SC / TI / neutral / photonic / annealing / topological / other) | |
| Native gate set | |
| Number of qubits | |
| Topology | |
| Public datasheet URL | |
| Known calibration source | |

## Pricing

<!-- per_shot_usd, free-tier rules, anything we need to know to gate
cost-control. -->

## Endpoint

<!-- Is the production submission REST URL public? If so, link it. If
not, the chip belongs on the unsupported-chips ledger; please note the
exact piece of data that would unblock live mode. -->

## Effort I am willing to contribute

- [ ] Open a PR adding the chip YAML to
      `spinor/registry/chips/<id>.yaml`.
- [ ] Add an adapter in
      `spinor/submit/python/spinor_submit/__init__.py`.
- [ ] Just file the request; let someone else implement.

## Verification

<!-- The verifier we ship runs schema + topology + native-gate-vocabulary
checks plus a smoke compile. Have you run
`python scripts/verify_chip_yamls.py` against your YAML? -->
