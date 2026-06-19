#!/usr/bin/env bash
# scripts/check_no_cursor.sh
#
# Fail the build if any non-allow-listed match for "cursor"
# (case-insensitive) appears in the repository. Legitimate uses of
# the word — pagination cursors, the Phonon parser's `Cursor`
# struct, CSS `cursor: pointer` — are allow-listed by file path.
#
# We scan only files that git tracks. That dodges node_modules,
# build outputs, and anything else gitignored.
#
# Author: Nimesh Cheedella.

set -euo pipefail

cd "$(dirname "$0")/.."

ALLOW_PATHS=(
  "platform/jobsvc/src/jobsvc/schemas.py"
  "platform/jobsvc/src/jobsvc/_alembic/env.py"
  "platform/jobsvc/src/jobsvc/_alembic/versions/0001_init.py"
  "platform/jobsvc/src/jobsvc/routers/jobs.py"
  "platform/jobsvc/tests/integration/test_get_jobs.py"
  "platform/playground/src/api/jobs.ts"
  "platform/playground/src/styles.css"
  "phonon/dialect/lib/Parse.cpp"
  "sdks/typescript/src/client.ts"
  "sdks/typescript/src/hooks.ts"
  "docs/site/content/sdks/rest/pagination.md"
  "docs/site/content/sdks/rest/index.md"
  "docs/site/content/sdks/typescript/cookbook.md"
  "docs/site/content/sdks/python/cookbook.md"
  "docs/site/content/sdks/python/tutorial.md"
  "docs/site/content/internals/architecture.md"
  "scripts/check_no_cursor.sh"
  ".github/workflows/no-cursor.yml"
  ".github/workflows/docs-lint.yml"
  "README.md"
  "AUTHORS.md"
  "CHANGELOG.md"
  "CONTRIBUTING.md"
)

# Build the allow-list as a regex anchored to the start of each
# filename grep emits.
allow_pattern=""
for p in "${ALLOW_PATHS[@]}"; do
  if [ -z "$allow_pattern" ]; then
    allow_pattern="^${p}:"
  else
    allow_pattern="${allow_pattern}|^${p}:"
  fi
done

# Also allow the entire archive directory (kept verbatim).
allow_pattern="${allow_pattern}|^docs/archive/"

# Use git ls-files so we never walk node_modules / build outputs.
mapfile -t TRACKED < <(git ls-files)
if [ "${#TRACKED[@]}" -eq 0 ]; then
  echo "no tracked files; nothing to scan" >&2
  exit 0
fi

# grep across all tracked files. -I skips binaries, -n shows line
# numbers, -i is case-insensitive, -w forces whole-word match.
matches=$(grep -InHw 'cursor' "${TRACKED[@]}" 2>/dev/null \
            | grep -Ev "${allow_pattern}" || true)

if [ -n "$matches" ]; then
  echo "Found unexpected 'cursor' mentions:" >&2
  echo "$matches" >&2
  echo "" >&2
  echo "If a match is legitimate, add the file to ALLOW_PATHS in" >&2
  echo "scripts/check_no_cursor.sh." >&2
  exit 1
fi

echo "scripts/check_no_cursor.sh: clean."
