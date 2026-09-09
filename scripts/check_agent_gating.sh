#!/usr/bin/env bash
# The agent channel must be reached from the frame loop only through the gate.
#
# tests/test_agent_cost measures what the channel costs when it is open. It
# cannot measure what an ungated call would cost, because an ungated call is a
# change to the source rather than a state at run time. This checks the shape
# instead: inside engine_loop, every line that mentions the agent must also
# mention the flag that decides whether the agent is there at all.
#
# That is the property the whole design rests on. A hook added outside the gate
# -- worse, one added per model rather than per frame -- would be paid by every
# program that never asked for the channel, and nothing else would notice.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/src/ae3d/engine/module.ae"

body="$(awk '/^engine_loop\(/ {inside=1} inside {print} inside && /^}/ && !/^engine_loop\(/ {exit}' "$ENGINE")"
if [ -z "$body" ]; then
    echo "check_agent_gating: could not find engine_loop in $ENGINE" >&2
    exit 1
fi

# Comments do not run.
offenders="$(printf '%s\n' "$body" \
    | sed 's|//.*||' \
    | grep -nE 'agent' \
    | grep -vE 'e\.agent_on' || true)"

if [ -n "$offenders" ]; then
    echo "check_agent_gating: the frame loop reaches the agent outside the gate:" >&2
    printf '%s\n' "$offenders" | sed 's/^/    /' >&2
    echo "    every such line must sit under 'if e.agent_on'" >&2
    exit 1
fi
exit 0
