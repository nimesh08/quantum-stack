# Security policy

## Reporting a vulnerability

If you discover a security vulnerability in the Heisenberg Quantum
Stack, please do **not** open a public GitHub issue. Instead, send a
private report.

### Preferred channels

1. **GitHub private vulnerability reports**:
   <https://github.com/nimesh08/quantum-stack/security/advisories/new>
2. **Email**: <chnimesh0808@gmail.com>

Please include:

- A description of the vulnerability and the affected component
  (e.g. `jobsvc.auth`, the C++ parser, the Spinor verifier).
- Steps to reproduce or a proof-of-concept.
- The version (or commit SHA) you tested against.
- Optional: a suggested fix.

We will acknowledge your report within **3 business days** and aim to
have an initial assessment within **14 days**.

## Supported versions

| Version | Supported |
|---------|-----------|
| 0.5.x | yes |
| < 0.5 | no  |

The pre-0.5 series predates the OSS release and is not maintained.

## Disclosure policy

We follow coordinated disclosure:

1. The vulnerability is reported privately.
2. We confirm the issue and assess severity (CVSS 3.1).
3. We develop a fix and prepare a public advisory.
4. We disclose the issue together with the fix release.

Reporters will be credited in the advisory unless they ask
otherwise.

## Out of scope

- Vulnerabilities in third-party dependencies that we do not yet
  expose. Please report those upstream and let us know if a
  user-facing surface is affected.
- Issues in cassette-recorded provider responses (those are static
  test fixtures by design).
- Anything in `docs/archive/` (historical content kept verbatim).
