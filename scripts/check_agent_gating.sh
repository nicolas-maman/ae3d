#!/usr/bin/env bash
# Inside engine_loop, the agent must only be reached under `if e.agent_on`.
#
# That gate is what makes the channel free in a build that did not ask for it,
# and it is the one property tests/test_agent_cost cannot check: an ungated
# hook is a change to the source, not a state at run time.
#
# Exit 0 when the loop is gated, 1 when it is not, and anything else means the
# check could not run.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/src/ae3d/engine/module.ae"

[ -f "$ENGINE" ] || {
    echo "check_agent_gating: $ENGINE is not there" >&2
    exit 2
}

offenders="$(awk '
    /^engine_loop\(/ { inside = 1 }
    !inside { next }
    {
        line = $0
        sub(/\/\/.*/, "", line)

        if (guard_depth == 0 && line ~ /if[ \t]+e\.agent_on/) {
            guard_depth = depth + 1
        } else if (line ~ /agent/ && line !~ /e\.agent_on/ && guard_depth == 0) {
            printf "    %d: %s\n", NR, $0
        }

        opens = gsub(/\{/, "{", line)
        closes = gsub(/\}/, "}", line)
        depth += opens - closes
        if (guard_depth > 0 && depth < guard_depth) { guard_depth = 0 }
    }
    inside && /^\}/ { exit }
' "$ENGINE")"

if [ -n "$offenders" ]; then
    echo "check_agent_gating: the frame loop reaches the agent outside the gate:" >&2
    printf '%s\n' "$offenders" >&2
    echo "    every such line must sit under 'if e.agent_on'" >&2
    exit 1
fi
exit 0
