#!/usr/bin/env bash
# Regenerate docs/agent.md from the engine's own command table.
#
#   ./scripts/gen_agent_docs.sh            write docs/agent.md
#   ./scripts/gen_agent_docs.sh --check    fail if it is out of date
#
# The schema comes from build/agent_schema, which prints the same table `help`
# answers with, and the page is written by build/ae3d_agent, the channel's own
# client, around the text in tools/agent_doc_text.md. A doc written by hand
# beside a protocol is a doc that describes last month's protocol.
#
# Exits 2 when it could not check, distinct from 0 "current" and 1 "out of
# date": exiting 0 here would have told a caller the documentation had been
# verified when nothing had looked at it.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Always rebuilt. Reusing whatever binaries were lying around let the check
# pass against a schema older than the source it is meant to describe, which
# is the one failure mode a documentation check must not have.
./build.sh tools/agent_schema.ae agent_schema >/dev/null 2>&1 || {
    echo "gen_agent_docs: could not build agent_schema" >&2
    exit 2
}
./build.sh tools/ae3d_agent.ae ae3d_agent >/dev/null 2>&1 || {
    echo "gen_agent_docs: could not build ae3d_agent" >&2
    exit 2
}

generated="$(mktemp)"
./build/agent_schema | ./build/ae3d_agent --docs > "$generated" || {
    echo "gen_agent_docs: the generator failed" >&2
    rm -f "$generated"
    exit 2
}

if [ "${1:-}" = "--check" ]; then
    # Compared by the shell, byte for byte: a minimal MSYS2 ships neither
    # diff nor cmp, and a missing tool read as "out of date" and reported a
    # change nobody had made.
    if [ "$(cat docs/agent.md)" = "$(cat "$generated")" ]; then
        rm -f "$generated"
        exit 0
    fi
    echo "gen_agent_docs: docs/agent.md is out of date; run ./scripts/gen_agent_docs.sh" >&2
    if command -v diff >/dev/null 2>&1; then
        diff -u docs/agent.md "$generated" | head -20 >&2
    fi
    rm -f "$generated"
    exit 1
fi

mv "$generated" docs/agent.md
echo "gen_agent_docs: wrote docs/agent.md"
