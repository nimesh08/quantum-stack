# M0 — Bootstrap

End: a CI workflow runs against a fresh checkout, the platform/ tree
exists with placeholder packages, all versions are pinned, the doc
journal is opened.

## Layout created

```
platform/
  jobsvc/      pyproject.toml + src/jobsvc/ + tests/{unit,integration,regression}
  calibration/ pyproject.toml + src/calibration/ + tests/{unit,integration}
  playground/  package.json + src/ + tests/{unit,e2e}
  deploy/
docs/build/phaseD/
.github/workflows/phase-d-ci.yml
```

## Pins (re-verified 2026-06-16)

See `phaseD_platform_guide.md` §Pinned versions and the per-package
manifest files. The four hero pins:

- FastAPI **0.137.1** (released 2026-06-15)
- PostgreSQL **17.10** (2026-05-14)
- React **19.2.7** (avoids the 19.2.0–19.2.6 CVEs)
- @monaco-editor/react **^4.7.0** (first stable with React 19 peer)

## Definition of done (M0)

- [x] `platform/jobsvc/pyproject.toml` parses, declares all pinned deps.
- [x] `platform/calibration/pyproject.toml` parses.
- [x] `platform/playground/package.json` parses, lists pinned deps.
- [x] `.github/workflows/phase-d-ci.yml` is committed.
- [x] `docs/build/phaseD/README.md` and this file exist.
- [x] `docs/build/phaseD_progress.md` opened.
