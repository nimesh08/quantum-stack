# M3 — Cost control (seam 1)

End: every `POST /jobs` is gated by a per-user budget check that
multiplies `shots * chip.pricing.per_shot_usd` and compares to the
remaining daily/monthly window. Over-budget returns 402 with a
structured detail; under-budget proceeds to `Queued`.

## Algorithm

```
cost  = shots * chip.pricing.per_shot_usd  (Decimal, 6 dp)
daily_remaining   = budget.daily_usd   - sum(jobs today,  spending states)
monthly_remaining = budget.monthly_usd - sum(jobs month,  spending states)

if shots > budget.max_shots_per_job: reject
elif cost > daily_remaining:         reject
elif cost > monthly_remaining:       reject
else:                                allow
```

Spending states (count toward spend): `Queued`, `Running`,
`Completed`, `Failed`. `Rejected` is excluded — a rejected job did
not actually spend.

## Pricing source

Read directly from `spinor/registry/chips/<chip>.yaml` `pricing.per_shot_usd`.
Examples (per the YAMLs already shipped):

| Chip | per_shot_usd |
|------|--------------|
| ibm_heron_r2 | 0.00033 |
| ionq_forte | 0.01 |
| quantinuum_helios | 0.025 |
| ionq_aria_proto | 0.01 |
| generic / local | 0.0 |

## Tests landed

- `tests/unit/test_cost.py` — 12 cases on `dollar_cost` + `check_budget`.
- `tests/integration/test_cost_control.py` — 6 end-to-end paths
  including the recent-spend rollover.

67/67 green at end of M3.
