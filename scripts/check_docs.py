#!/usr/bin/env python3
"""Lint the user-facing documentation.

Three checks, run in CI:

1. **No phase-talk** in user-facing pages. ``Phase A/B/C/D`` and
   capitalised milestone identifiers like ``M1``..``M11`` belong in
   the archive, not in the live site. The regex is **case
   sensitive** so ordinary identifiers like ``m1`` (a classical bit
   in code samples) do not match.

2. **First non-blank line after H1 is not a fenced code block.**
   Pages should open with at least one prose sentence so a reader
   knows what they are reading. If the page starts with code, the
   reader gets no orientation. (We allow `## sub-heading` or a
   list as the lede; only fenced code is rejected.)

3. **Every internal link target exists.** mkdocs `--strict` already
   catches most cases; this script adds path-relative checks for
   things mkdocs misses.

Pages that include a snippet via ``--8<-- "..."`` are exempt from
the lede check (the lede lives in the included file).

Author: Nimesh Cheedella <chnimesh0808@gmail.com>
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "docs" / "site" / "content"
ARCHIVE = ROOT / "docs" / "archive"

# Case-sensitive: matches "Phase A" / "M11" but not "m1" (bit name).
PHASE_TALK = re.compile(
    r"\b(Phase\s+[A-D]\b|M(?:1[0-1]|[1-9])\b)"
)

# Allow-listed: pages that legitimately discuss phases / milestones
# (decisions, history, internals, the changelog, faqs).
ALLOWED_PHASE_TALK_PATHS = (
    "internals/",
    "changelog.md",
    "faq.md",
)

LINK = re.compile(r"\]\((?!https?://|mailto:|#)([^)]+?)\)")
SNIPPET = re.compile(r"^--8<--\s", re.MULTILINE)


def find_pages() -> list[Path]:
    return sorted(p for p in CONTENT.rglob("*.md") if "_includes" not in p.parts)


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def is_in_allowlist(p: Path) -> bool:
    rel = p.relative_to(CONTENT).as_posix()
    return any(rel.startswith(prefix) for prefix in ALLOWED_PHASE_TALK_PATHS)


def check_phase_talk(p: Path, body: str) -> list[str]:
    if is_in_allowlist(p):
        return []
    out = []
    in_code = False
    for lineno, line in enumerate(body.splitlines(), 1):
        if line.lstrip().startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        m = PHASE_TALK.search(line)
        if m:
            out.append(
                f"{p.relative_to(ROOT)}:{lineno}: phase-talk match: {m.group(0)!r}"
            )
    return out


def check_lede(p: Path, body: str) -> list[str]:
    """First non-blank line after the H1 must not be a fenced code
    block. Pages that include a snippet are exempt."""
    text = body
    if text.startswith("---\n"):
        end = text.find("\n---\n", 4)
        if end != -1:
            text = text[end + 5 :]
    if SNIPPET.search(text):
        # Snippet pages get their H1 + body from the included file.
        return []
    lines = text.splitlines()
    i = 0
    while i < len(lines) and not lines[i].startswith("# "):
        i += 1
    if i == len(lines):
        return [f"{p.relative_to(ROOT)}: no H1 heading found"]
    i += 1
    while i < len(lines) and not lines[i].strip():
        i += 1
    if i == len(lines):
        return [f"{p.relative_to(ROOT)}: no body after H1"]
    if lines[i].lstrip().startswith("```"):
        return [
            f"{p.relative_to(ROOT)}: page starts with a code block; "
            f"add a one-paragraph lede first"
        ]
    return []


def check_links(p: Path, body: str) -> list[str]:
    out = []
    for m in LINK.finditer(body):
        target = m.group(1).split("#")[0].split("?")[0]
        if not target or target.endswith("/"):
            continue
        if target.endswith(".html"):
            # HTML targets (Redoc embed) are produced by CI; skip.
            continue
        candidate = (p.parent / target).resolve()
        if candidate.exists():
            continue
        out.append(f"{p.relative_to(ROOT)}: broken link to {target}")
    return out


def main() -> int:
    pages = find_pages()
    errors: list[str] = []
    for p in pages:
        body = read(p)
        errors.extend(check_phase_talk(p, body))
        errors.extend(check_lede(p, body))
        errors.extend(check_links(p, body))
    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        print(f"\n{len(errors)} doc-lint error(s).", file=sys.stderr)
        return 1
    print(f"docs/site: {len(pages)} pages OK.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
