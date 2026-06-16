#!/usr/bin/env bash
# Convenience wrapper for compose. Run from the deploy/ folder.

set -euo pipefail
cd "$(dirname "$0")"

case "${1:-up}" in
  up)
    docker compose --project-directory ../.. -f docker-compose.yml up --build "$@" ;;
  down)
    docker compose --project-directory ../.. -f docker-compose.yml down -v ;;
  smoke)
    docker compose --project-directory ../.. -f docker-compose.yml up -d --build
    for i in {1..30}; do
      if curl -fsS http://localhost:8000/healthz > /dev/null; then
        echo "OK"; exit 0
      fi
      sleep 2
    done
    echo "FAIL"; docker compose ps; exit 1 ;;
  *)
    docker compose --project-directory ../.. -f docker-compose.yml "$@" ;;
esac
