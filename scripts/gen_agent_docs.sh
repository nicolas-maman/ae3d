#!/usr/bin/env bash
# Regenerate docs/agent.md from the engine's own command table.
#
#   ./scripts/gen_agent_docs.sh            write docs/agent.md
#   ./scripts/gen_agent_docs.sh --check    fail if it is out of date
#
# The schema comes from build/agent_schema, which prints the same table `help`
# answers with. A doc written by hand beside a protocol is a doc that describes
# last month's protocol.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PYTHON="${PYTHON:-}"
if [ -z "$PYTHON" ]; then
    for candidate in python3 python; do
        if command -v "$candidate" >/dev/null 2>&1; then PYTHON="$candidate"; break; fi
    done
fi
# 2 means "could not check", distinct from 0 "current" and 1 "out of date".
# Exiting 0 here would have told a caller the documentation had been verified
# when nothing had looked at it.
if [ -z "$PYTHON" ]; then
    echo "gen_agent_docs: no python3 found (set PYTHON=/path/to/python)"
    exit 2
fi

# Always rebuilt. Reusing whatever binary was lying around let the check pass
# against a schema older than the source it is meant to describe, which is the
# one failure mode a documentation check must not have.
./build.sh tools/agent_schema.ae agent_schema >/dev/null 2>&1 || {
    echo "gen_agent_docs: could not build agent_schema" >&2
    exit 2
}

generated="$(mktemp)"
./build/agent_schema | "$PYTHON" tools/ae3d_agent.py --docs > "$generated" || {
    echo "gen_agent_docs: the generator failed" >&2
    rm -f "$generated"
    exit 2
}

if [ "${1:-}" = "--check" ]; then
    # Compared with the Python that generated it rather than with diff, which a
    # minimal MSYS2 does not ship: a missing tool read as "out of date" and
    # reported a documentation change nobody had made.
    if $PYTHON - "docs/agent.md" "$generated" <<'PYTHON'
import difflib, io, sys
current = io.open(sys.argv[1], encoding="utf-8").read()
fresh = io.open(sys.argv[2], encoding="utf-8").read()
if current == fresh:
    sys.exit(0)
sys.stderr.writelines(list(difflib.unified_diff(
    current.splitlines(True), fresh.splitlines(True),
    "docs/agent.md", "generated"))[:20])
sys.exit(1)
PYTHON
    then
        rm -f "$generated"
        exit 0
    fi
    echo "gen_agent_docs: docs/agent.md is out of date; run ./scripts/gen_agent_docs.sh" >&2
    rm -f "$generated"
    exit 1
fi

mv "$generated" docs/agent.md
echo "gen_agent_docs: wrote docs/agent.md"
