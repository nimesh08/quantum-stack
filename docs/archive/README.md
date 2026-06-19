# Heisenberg — historical archive

This directory holds the original build journals, per-milestone
specifications, and the engineering-handoff documents that were used
*while we were building* the Heisenberg quantum stack. They are kept
verbatim because they capture decisions and rationale, but they are
**not the user-facing documentation**.

If you are new to the project, start at the live site:
<https://nimesh08.github.io/quantum-stack/>.

## What is in here

| Directory | What it contains | Why it is here |
|---|---|---|
| [`build/`](build/) | Per-milestone progress journals (`phaseA_progress.md` ... `phaseD_progress.md`), the original decision logs, and the milestone-level specs (`phaseA/M*.md`, `phaseB/M*.md`, ...). | These were authored while building. They use phase- and milestone-level vocabulary that does not surface in the live docs. |
| [`specs/`](specs/) | The six engineering-handoff specifications that bootstrapped each phase. | Historical scope statements; superseded by the live language and SDK references. |

## Why we archived them

The user-facing site needs to be human-first: short ledes, runnable
examples, no internal milestone numbers. The archive keeps the
build-time material lossless so anyone reading a commit, decision, or
old milestone document can still find the original prose.

Author: Nimesh Cheedella.
