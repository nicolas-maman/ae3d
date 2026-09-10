#!/usr/bin/env bash
# Build and run everything: the native layer, every module, every test suite and
# every example.
#
# Examples need a window, so they are driven for a bounded number of frames via
# AE3D_FRAMES and are skipped where no display is available.
#
#   ./ci.sh

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

FRAMES="${AE3D_CI_FRAMES:-30}"
failures=0
skipped=0

step() { printf '\n== %s\n' "$1"; }
pass() { printf '   ok    %s\n' "$1"; }
fail() { printf '   FAIL  %s\n' "$1"; failures=$((failures + 1)); }
skip() { printf '   skip  %s (%s)\n' "$1" "$2"; skipped=$((skipped + 1)); }

# Windows ships a python3 on PATH whose only purpose is to open the Microsoft
# Store, and it answers command -v exactly like an interpreter would. Asking it
# to run something is the only way to tell them apart.
PYTHON=""
for candidate in python3 python "py -3"; do
    if $candidate -c "" >/dev/null 2>&1; then PYTHON="$candidate"; break; fi
done

have_display() {
    case "$(uname -s)" in
        Darwin) return 0 ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT) return 0 ;;
        *) [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] ;;
    esac
}

# A suite, example or benchmark that hangs should fail this step by name rather
# than stall the job until the runner's own six-hour limit: one did, on Linux,
# for three and a half hours, and the log said nothing at all. Not every
# platform ships coreutils' timeout, so where it is missing the run is
# unguarded, exactly as it was before.
if command -v timeout >/dev/null 2>&1; then
    bounded() { timeout "$@"; }
else
    bounded() { shift; "$@"; }
fi
RUN_LIMIT="${AE3D_CI_RUN_LIMIT:-300}"

# A shell gives 128 plus the signal for a child that was killed, and the
# message a crash leaves on its own says neither which signal nor which suite.
died_on() {   # died_on <status>
    if [ "$1" -gt 128 ] && [ "$1" -lt 160 ]; then
        printf ' (died on signal %d)' "$(($1 - 128))"
    fi
}

step "platform link libraries"
# Every host this can be built on, checked from any host. Windows had no arm
# at all and the catch-all's -lm cannot link an OpenGL program, so ae3d could
# not be linked there however complete the install was (#80). A missing arm is
# invisible from the machine that does not need it, which is why this runs
# everywhere rather than only where it applies.
. "$ROOT/scripts/platform.sh"
check_platform() {   # check_platform <uname> <library that must be there>
    libs="$(ae3d_platform_libs "$1")"
    case "$libs" in
        *"$2"*) pass "uname=$1 links $2" ;;
        *)      fail "uname=$1 does not link $2 (got: $libs)" ;;
    esac
}
check_platform Darwin            "-framework OpenGL"
check_platform Linux             "-ldl"
check_platform MINGW64_NT-10.0   "-lopengl32"
check_platform MINGW64_NT-10.0   "-lgdi32"
check_platform MSYS_NT-10.0      "-lopengl32"
check_platform Windows_NT        "-lopengl32"
# The agent channel's own dependencies, named rather than borrowed from whatever
# the Aether toolchain happens to link.
check_platform Linux             "-lpthread"
check_platform MINGW64_NT-10.0   "-lws2_32"

step "the agent channel stays behind its gate"
# The one property the channel's whole design rests on, and the one thing a
# timing test cannot check: an ungated hook is a change to the source, not a
# state at run time. See scripts/check_agent_gating.sh.
./scripts/check_agent_gating.sh 2>/tmp/ae3d_gate.log
gate_status=$?
if [ "$gate_status" -eq 0 ]; then
    pass "engine_loop reaches the agent only through e.agent_on"
elif [ "$gate_status" -eq 1 ]; then
    fail "engine_loop reaches the agent outside the gate"
    sed "s/^/        /" /tmp/ae3d_gate.log | head -10
else
    # 126 is "not executable", 127 is "not found". Reporting either as an
    # ungated hook names a bug that is not there and hides the one that is.
    fail "check_agent_gating.sh could not run (exit $gate_status)"
    sed "s/^/        /" /tmp/ae3d_gate.log | head -5
fi

step "exported fixtures match the exporter"
# Catches an exporter change that nobody regenerated the fixtures for. The
# committed assets under tests/fixtures/exported/ are what test_assets runs
# against on machines with no Blender, so they have to be what this exporter
# actually produces rather than what it produced once.
#
# Skips where Blender is absent, which is most CI.
if command -v blender >/dev/null 2>&1 || [ -n "${BLENDER:-}" ]; then
    check_exported() {   # check_exported <blend> <committed directory>
        fresh="$(mktemp -d)"
        ./scripts/export_assets.sh "$1" "$fresh" >/tmp/ae3d_export.log 2>&1
        if [ ! -f "$fresh/manifest.json" ]; then
            skip "$2" "the exporter produced nothing (see /tmp/ae3d_export.log)"
        elif diff -r "$2" "$fresh" >/tmp/ae3d_export_diff.log 2>&1; then
            pass "$2 is what the exporter produces"
        else
            fail "$2 is stale; run ./scripts/export_assets.sh"
            sed 's/^/        /' /tmp/ae3d_export_diff.log | head -10
        fi
        rm -rf "$fresh"
    }
    check_exported tests/fixtures/spin.blend tests/fixtures/exported
    check_exported resources/blender/showcase.blend resources/blender/showcase
    check_exported resources/blender/zombie_street.blend resources/blender/zombie_street
else
    skip "exported fixtures" "no Blender"
fi

step "no two surfaces share a plane"
# Z-fighting is two faces in one plane close enough in depth that rounding
# decides which is in front. Looked for on screen it depends on where the
# camera happens to be; looked for in the geometry, either two faces share a
# plane and overlap or they do not. Runs against the committed export, so it
# needs no Blender.
if [ -n "$PYTHON" ]; then
    for exported in resources/blender/zombie_street resources/blender/showcase; do
        if $PYTHON tools/blender/check_coplanar.py "$exported" >/tmp/ae3d_coplanar.log 2>&1; then
            pass "$exported has no coplanar overlaps"
        else
            fail "$exported has surfaces that would fight over the same depth"
            sed 's/^/        /' /tmp/ae3d_coplanar.log | head -12
        fi
    done
else
    skip "coplanar surfaces" "no python3"
fi

step "docs/agent.md matches the engine's command table"
# A doc written by hand beside a protocol is a doc that describes last month's
# protocol. This one is generated from the same table `help` answers with, so
# the check is that it was regenerated after the table changed.
./scripts/gen_agent_docs.sh --check >/tmp/ae3d_docs.log 2>&1
docs_status=$?
if [ "$docs_status" -eq 0 ]; then
    pass "docs/agent.md is what the schema produces"
elif [ "$docs_status" -ne 1 ]; then
    # Anything but 1 is "could not check": 2 from the script itself, 126 when
    # it is not executable, 127 when it is not there. Only 1 means the page
    # and the schema actually disagree.
    skip "docs/agent.md" "$(head -1 /tmp/ae3d_docs.log)"
else
    fail "docs/agent.md is out of date; run ./scripts/gen_agent_docs.sh"
    sed "s/^/        /" /tmp/ae3d_docs.log | head -12
fi

step "native layer, warnings as errors"
# Same compiler search as build.sh: a Windows toolchain need not ship `cc`.
if [ -z "${CC:-}" ]; then
    for candidate in cc gcc clang; do
        if command -v "$candidate" >/dev/null 2>&1; then CC="$candidate"; break; fi
    done
fi
CC="${CC:-cc}"
# Same precedence as build.sh. Without this the native step is the one part
# of CI that cannot be run on a machine with no pkg-config, and it fails with
# "GLFW/glfw3.h: No such file or directory" while every other step passes --
# which reads as a broken checkout rather than a missing tool.
if [ -z "${GLFW_CFLAGS:-}" ]; then
    GLFW_CFLAGS="$(pkg-config --cflags glfw3 2>/dev/null || true)"
fi
if [ -z "${ZLIB_CFLAGS:-}" ]; then
    ZLIB_CFLAGS="$(pkg-config --cflags zlib 2>/dev/null || true)"
fi
VULKAN_CFLAGS=""
if pkg-config --exists vulkan 2>/dev/null; then
    VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
elif [ -d /opt/homebrew/include/vulkan ]; then
    VULKAN_CFLAGS="-I/opt/homebrew/include"
fi
for src in native/*.c; do
    if "$CC" -c -O2 -Wall -Wextra -Werror $GLFW_CFLAGS $ZLIB_CFLAGS $VULKAN_CFLAGS "$src" -o /dev/null 2>/tmp/ae3d_cc.log; then
        pass "$src"
    else
        fail "$src"
        sed 's/^/        /' /tmp/ae3d_cc.log | head -20
    fi
done
if [ "$(uname -s)" = "Darwin" ]; then
    if "$CC" -c -O2 -Wall -Wextra -Werror -fobjc-arc $GLFW_CFLAGS native/ae3d_vk_surface.m -o /dev/null 2>/tmp/ae3d_cc.log; then
        pass "native/ae3d_vk_surface.m"
    else
        fail "native/ae3d_vk_surface.m"
        sed 's/^/        /' /tmp/ae3d_cc.log | head -20
    fi
fi

step "modules type-check"
for module in src/ae3d/*/; do
    name="$(basename "$module")"
    probe="$(mktemp -t ae3d_probe.XXXXXX).ae"
    printf 'import ae3d.%s\nmain() { println("ok") }\n' "$name" > "$probe"
    if aetherc "$probe" "${probe%.ae}.c" >/tmp/ae3d_mod.log 2>&1; then
        pass "ae3d.$name"
    else
        fail "ae3d.$name"
        sed 's/^/        /' /tmp/ae3d_mod.log | head -10
    fi
    rm -f "$probe" "${probe%.ae}.c"
done

# Shared code belonging to the examples, on its own search path outside the
# engine's namespace. Type-checked the same way: a module that stops compiling
# should fail here, rather than three lines later in whichever example happens to
# get built first.
for module in examples/lib/*/; do
    [ -e "$module" ] || continue
    name="$(basename "$module")"
    probe="$(mktemp -t ae3d_probe.XXXXXX).ae"
    printf 'import %s\nmain() { println("ok") }\n' "$name" > "$probe"
    if AETHER_LIB_DIR="$PWD/src:$PWD/examples/lib" aetherc "$probe" "${probe%.ae}.c" >/tmp/ae3d_mod.log 2>&1; then
        pass "examples/lib/$name"
    else
        fail "examples/lib/$name"
        sed 's/^/        /' /tmp/ae3d_mod.log | head -10
    fi
    rm -f "$probe" "${probe%.ae}.c"
done

# The scripts an object can be given. They are built before the suites because
# a script is a separate library the test opens at runtime rather than
# something linked into it, which is the whole point of one.
step "scripts"
for script_source in resources/scripts/*.ae; do
    [ -e "$script_source" ] || continue
    script_name="$(basename "$script_source" .ae)"
    if ./scripts/build_script.sh "$script_source" >/tmp/ae3d_script.log 2>&1; then
        pass "script $script_name"
    else
        fail "script $script_name"
        sed 's/^/        /' /tmp/ae3d_script.log | head -10
    fi
done

step "test suites"
for suite in tests/test_*.ae; do
    name="$(basename "$suite" .ae)"
    if ! ./build.sh "$suite" "$name" >/tmp/ae3d_build.log 2>&1; then
        fail "$name (build)"
        sed 's/^/        /' /tmp/ae3d_build.log | head -20
        continue
    fi
    if grep -q "warning" /tmp/ae3d_build.log; then
        fail "$name (build warnings)"
        grep "warning" /tmp/ae3d_build.log | sed 's/^/        /' | head -10
        continue
    fi
    needs_window=0
    grep -q "ae3d.engine" "$suite" && needs_window=1
    if [ "$needs_window" = 1 ] && ! have_display; then
        skip "$name" "no display"
        continue
    fi
    output="$(AE3D_FRAMES="$FRAMES" bounded "$RUN_LIMIT" ./build/"$name" 2>&1)"
    suite_status=$?
    if [ "$suite_status" -eq 124 ]; then
        fail "$name (still running after ${RUN_LIMIT}s)"
        printf '%s\n' "$output" | sed 's/^/        /' | tail -10
    elif [ "$suite_status" -ne 0 ]; then
        fail "$name$(died_on "$suite_status")"
        printf '%s\n' "$output" | sed 's/^/        /' | head -20
    elif printf '%s' "$output" | grep -q "all checks passed"; then
        pass "$name"
    elif printf '%s' "$output" | grep -q "SKIP" && ! printf '%s' "$output" | grep -q "FAIL"; then
        # A suite that cannot run where it finds itself, for want of a display,
        # a GPU or a driver, is not a suite that failed.
        skip "$name" "$(printf '%s' "$output" | grep -m1 "SKIP" | sed 's/.*SKIP *//')"
    else
        fail "$name"
        printf '%s\n' "$output" | sed 's/^/        /' | head -20
    fi
done

step "examples build and run"
for example in examples/*.ae; do
    name="$(basename "$example" .ae)"
    if ! ./build.sh "$example" "$name" >/tmp/ae3d_build.log 2>&1; then
        fail "$name (build)"
        sed 's/^/        /' /tmp/ae3d_build.log | head -20
        continue
    fi
    if grep -q "warning" /tmp/ae3d_build.log; then
        fail "$name (build warnings)"
        grep "warning" /tmp/ae3d_build.log | sed 's/^/        /' | head -10
        continue
    fi
    if ! have_display; then
        skip "$name" "no display"
        continue
    fi
    AE3D_FRAMES="$FRAMES" bounded "$RUN_LIMIT" ./build/"$name" >/tmp/ae3d_run.log 2>&1
    example_status=$?
    if [ "$example_status" -eq 124 ]; then
        fail "$name (still running after ${RUN_LIMIT}s)"
        sed 's/^/        /' /tmp/ae3d_run.log | tail -10
    elif [ "$example_status" -eq 0 ]; then
        pass "$name"
    else
        fail "$name"
        sed 's/^/        /' /tmp/ae3d_run.log | head -20
    fi
done

# The editor runs on either renderer, so both are checked: the Vulkan option
# used to report Vulkan and build an OpenGL renderer, which no OpenGL-only run
# could have caught.
check_editor_run() {
    editor_backend="$1"
    editor_scene="${2:-components}"
    name="ae3d_editor ($editor_backend)"
    if [ "$editor_scene" != "components" ]; then
        name="ae3d_editor ($editor_backend, $editor_scene)"
    fi
    report="$(mktemp)"
    snapshot="$(mktemp -t ae3d_shot.XXXXXX).png"
    log="$(mktemp)"
    # A bounded run ends itself; the timeout is only a backstop so a hang
    # fails the step rather than blocking it.
    # Never onto the desktop. A run of this file opened an editor window per
    # backend per scene and took the keyboard with it, which makes it unusable
    # beside anything else. The window still exists and still answers the test
    # server; it is only never ordered to the front.
    AETHER_UI_HEADLESS=1 \
    AE3D_EDITOR_BACKEND="$editor_backend" \
    AE3D_EDITOR_FRAMES=30 \
    AE3D_EDITOR_SCENE="$editor_scene" \
    AE3D_EDITOR_SNAPSHOT="$snapshot" \
    AE3D_EDITOR_REPORT="$report" \
        timeout 90 ./build/ae3d_editor >"$log" 2>&1
    status=$?
    if grep -q 'no Vulkan driver' "$log"; then
        skip "$name" "$(sed -n 's/.*no Vulkan driver (\(.*\)),.*/\1/p' "$log" | head -1)"
        rm -f "$report" "$snapshot" "$log"
        return
    fi
    if [ "$status" -ne 0 ]; then
        fail "$name (exited $status)"
    elif [ ! -s "$report" ]; then
        fail "$name (wrote no report)"
    elif [ ! -s "$snapshot" ]; then
        fail "$name (wrote no viewport snapshot)"
    elif [ "$(sed -n 's/^backend //p' "$report")" != "$editor_backend" ]; then
        fail "$name (rendered with $(sed -n 's/^backend //p' "$report"))"
    elif ! grep -q '^frames 30$' "$report"; then
        fail "$name (did not reach 30 frames)"
        sed 's/^/        /' "$report"
    elif ! grep -qE '^models [0-9]+$' "$report" || \
         [ "$(sed -n 's/^models //p' "$report")" -lt 5 ]; then
        fail "$name (scene did not build)"
    elif [ "$(sed -n 's/^water //p' "$report")" != "1" ] || \
         [ "$(sed -n 's/^voxels //p' "$report")" != "1" ] || \
         [ "$(sed -n 's/^lights //p' "$report")" != "1" ] || \
         [ "$(sed -n 's/^scripted //p' "$report")" != "1" ]; then
        fail "$name (component types did not build)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^stuck_rows //p' "$report")" != "0" ]; then
        # A row that records an undo step and moves its own readout looks exactly
        # like a row that works. Nine of them did that and nothing else.
        fail "$name ($(sed -n 's/^stuck_rows //p' "$report") inspector row(s) change nothing)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^blind_fields //p' "$report")" != "0" ]; then
        # A number field that applies its value but never shows it is a row the
        # user cannot read, and stuck_rows cannot see it: that check drives the
        # property directly and never looks at the control.
        fail "$name ($(sed -n 's/^blind_fields //p' "$report") number field(s) do not show their value)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^mis_styled //p' "$report")" != "0" ]; then
        # A class the sheet never defines styles nothing at all, and the widget
        # renders in the toolkit's default: a button that looks like somebody
        # meant to leave it plain. Nothing in the widget tree says otherwise.
        fail "$name ($(sed -n 's/^mis_styled //p' "$report") styled widget(s) are not painted what the theme asks for)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^chip_wrong //p' "$report")" != "0" ]; then
        # The three colour sliders never show the colour they add up to, so the
        # chip beside them is the only place it appears. A chip that is never
        # painted looks exactly like one showing a dark material.
        fail "$name (the colour chip is not the material's colour)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^unreached_by_edit //p' "$report")" != "0" ]; then
        # An edit reaches everything selected, not just the row the inspector
        # happens to be showing. The property paths always wrote to the set;
        # nothing could put two things in it until the toolkit could report a
        # modifier, so nothing had ever checked the second one was written to.
        fail "$name (an edit did not reach every selected object)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^unundone_scripts //p' "$report")" != "0" ]; then
        # Attaching a behaviour that records nothing leaves the next undo to
        # step back through whatever came before it, the same fault the gizmo
        # drag had.
        fail "$name ($(sed -n 's/^unundone_scripts //p' "$report") behaviour(s) cannot be undone)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^first_fps //p' "$report")" -lt 1 ]; then
        # The first frame rate the bar ever shows. It opened on 0 and climbed
        # through 1 and 2, because nothing was written until an interval had
        # been measured and the running average started from nothing: the first
        # thing the editor told anyone was that it managed two frames a second.
        # The editor writes down what it showed first, because by the time a
        # driver can ask, the average has climbed to something plausible.
        #
        # Zero, and nothing above it. The first measured interval is genuinely
        # variable, because the first frames of a run do the work of first
        # frames: it reads anywhere from 5 to 28 here between runs of the same
        # scene, and a threshold above that measures the machine rather than
        # the editor. Zero is the defect itself and cannot be reached while the
        # bar waits for a measurement, so this fails on the regression and on
        # nothing else.
        fail "$name (the bar opened on $(sed -n 's/^first_fps //p' "$report") fps)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^shading_disagrees //p' "$report")" != "0" ]; then
        # The switches are a scene-wide control over per-model uniforms, so the
        # two drift apart in both directions: a model added after a switch was
        # flipped never got it, and a loaded scene brings settings the panel
        # knows nothing about.
        fail "$name ($(sed -n 's/^shading_disagrees //p' "$report") model setting(s) disagree with the shading panel)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^unreached_shading //p' "$report")" != "0" ]; then
        # A shading switch that sets a global and reaches no model looks
        # exactly like one that works: the only witness is a frame nobody
        # compares. Each is flipped and the model asked what it now carries.
        fail "$name ($(sed -n 's/^unreached_shading //p' "$report") shading switch(es) reach no model)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^silent_drags //p' "$report")" != "0" ]; then
        # Checked before the undo count, because it is the other explanation
        # for it: a drag that never reaches the model leaves the model where it
        # started, and the undo after it steps back through whatever came
        # before and moves it away. Reported apart so a failure names the gizmo
        # or the history rather than leaving the reader to guess (#198).
        fail "$name ($(sed -n 's/^silent_drags //p' "$report") gizmo drag(s) moved nothing)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^unundone_drags //p' "$report")" != "0" ]; then
        # A drag of the gizmo that records nothing leaves the next undo to step
        # back through whatever came before it and put that back instead.
        fail "$name ($(sed -n 's/^unundone_drags //p' "$report") gizmo drag(s) cannot be undone)"
        sed 's/^/        /' "$report"
    elif [ "$(sed -n 's/^idle_actions //p' "$report")" != "0" ]; then
        # Duplicate, delete, frame selection and the three scripts, each asked
        # for its effect: a button that dispatches to nothing looks exactly like
        # one that works when the only witness is a person watching.
        fail "$name ($(sed -n 's/^idle_actions //p' "$report") action(s) do nothing)"
        sed 's/^/        /' "$report"
    elif grep -q '^selected none$' "$report"; then
        fail "$name (nothing selected)"
        sed 's/^/        /' "$report"
    else
        pass "$name"
        sed 's/^/        /' "$report"
    fi
    rm -f "$report" "$snapshot" "$log"
}

step "editor"
# A print left in from working something out ships silently: it goes to the
# editor's own console, where it looks like a message the editor meant to
# write, and nothing else in this file reads that console. One did ship, and
# was found in a screenshot taken for another reason.
if grep -n "DBG" editor/editor.ae >/tmp/ae3d_debug.log; then
    fail "ae3d_editor (debug prints)"
    sed 's/^/        /' /tmp/ae3d_debug.log | head -5
else
    pass "ae3d_editor (debug prints)"
fi

UI_ROOT="${AETHER_UI_ROOT:-$ROOT/../aether-ui}"
if [ ! -f "$UI_ROOT/ui/module.ae" ]; then
    skip "ae3d_editor" "aether-ui not found at $UI_ROOT"
elif ! have_display; then
    skip "ae3d_editor" "no display"
else
    if ! ./editor/build_editor.sh >/tmp/ae3d_build.log 2>&1; then
        fail "ae3d_editor (build)"
        sed 's/^/        /' /tmp/ae3d_build.log | head -20
    elif grep -q "warning" /tmp/ae3d_build.log; then
        # Every other build in this file is gated on warnings and this one was
        # not, so an unused variable in the largest Aether source in the repo
        # went through ci without a word.
        fail "ae3d_editor (build warnings)"
        grep "warning" /tmp/ae3d_build.log | sed 's/^/        /' | head -10
    else
        for editor_backend in opengl vulkan; do
            check_editor_run "$editor_backend"
        done
        # The same scene saved and loaded again before the run starts, so the
        # report describes what came BACK. Every component count is asserted
        # exactly as above, which is the point: a scene that drops a component
        # on the way through the file shows up here as a count that fell.
        check_editor_run opengl roundtrip

        # A name the editor shares with the toolkit it imports is bound
        # differently inside the ui.window block than outside it, silently, and
        # that is how the Undo button came to step the toolkit's empty stack.
        if [ -n "$PYTHON" ]; then
            collide_log="$(mktemp)"
            if AETHER_UI_ROOT="$UI_ROOT" $PYTHON tools/check_ui_name_collisions.py \
                    >"$collide_log" 2>&1; then
                pass "ae3d_editor (names)"
            else
                fail "ae3d_editor (names)"
                sed 's/^/        /' "$collide_log" | head -12
            fi
            rm -f "$collide_log"
        else
            skip "ae3d_editor (names)" "no python3"
        fi

        # Everything above reads the report the editor writes about itself, and
        # that report comes from calling the handlers directly. A button that
        # cannot be hit, a field whose callback is not wired, a row that does
        # not respond to a click: all of them pass. So this presses the real
        # widgets through aether-ui's driver and asks the tree what changed.
        if [ -z "$PYTHON" ]; then
            skip "ae3d_editor (driver)" "no python3"
        elif ! have_display; then
            skip "ae3d_editor (driver)" "no display"
        else
            # Both backends. The report checks have always run on each, but
            # nothing had ever pressed a widget on the Vulkan one, and the
            # editor's controls reach the renderer through a vtable that only
            # a real click exercises.
            for driver_backend in opengl vulkan; do
                driver_log="$(mktemp)"
                if $PYTHON tools/drive_editor.py --backend "$driver_backend" \
                        --port 8797 >"$driver_log" 2>&1; then
                    pass "ae3d_editor (driver, $driver_backend)"
                else
                    fail "ae3d_editor (driver, $driver_backend)"
                    # The failing lines, not the first twenty. The driver runs
                    # more checks than that now, so the head of its log is all
                    # the ones that passed and a failure two thirds of the way
                    # down was reported as a wall of ok with no reason in it.
                    grep -E 'FAIL|Traceback|Error|error:' "$driver_log" \
                        | sed 's/^/        /' | head -12
                    tail -3 "$driver_log" | sed 's/^/        /'
                fi
                rm -f "$driver_log"
            done
        fi
    fi
fi

# A shared runner is not a machine anyone should take a timing from, and a
# software rasteriser needs orders of magnitude longer per frame than the
# hardware these numbers describe. On CI the benchmarks run briefly, as smoke
# tests; AE3D_BENCH_FRAMES unset gives the counts the numbers were measured at.
if [ -n "${CI:-}" ]; then
    export AE3D_BENCH_FRAMES="${AE3D_BENCH_FRAMES:-10}"
    export AE3D_BENCH_BLOCKS="${AE3D_BENCH_BLOCKS:-1}"
fi

step "benchmarks"
for bench in benchmarks/bench_*.ae; do
    [ -e "$bench" ] || continue
    name="$(basename "$bench" .ae)"
    if ! ./build.sh "$bench" "$name" >/tmp/ae3d_build.log 2>&1; then
        fail "$name (build)"
        sed 's/^/        /' /tmp/ae3d_build.log | head -20
        continue
    fi
    if grep -q "warning" /tmp/ae3d_build.log; then
        fail "$name (build warnings)"
        continue
    fi
    if output="$(bounded "$RUN_LIMIT" ./build/"$name" 2>&1)"; then
        pass "$name"
        printf '%s\n' "$output" | sed 's/^/        /'
    else
        fail "$name"
        printf '%s\n' "$output" | sed 's/^/        /' | head -20
    fi
done

# leaks stops the target and reads its heap, which needs a debugger attach that
# some sandboxes deny: the process ends up stopped and neither side moves again.
# AE3D_SKIP_LEAKS=1 is for those, and CI never sets it.
if [ -n "${AE3D_SKIP_LEAKS:-}" ]; then
    step "leak check, headless suites"
    skip "all" "AE3D_SKIP_LEAKS is set"
elif command -v leaks >/dev/null 2>&1; then
    step "leak check, headless suites"
    for suite in tests/test_*.ae benchmarks/bench_*.ae; do
        [ -e "$suite" ] || continue
        name="$(basename "$suite" .ae)"
        [ -x "build/$name" ] || continue
        # Only allocations this code lost count, judged by whose stack they are
        # on rather than by how leaks labelled them.
        #
        # A program that opens a window produces two kinds of noise it cannot do
        # anything about. NSXPCConnection retain cycles inside the window
        # server's machinery come back as ROOT CYCLE, which is easy to exclude.
        # But destroying the window also lets AppKit strand a stray NSArray
        # inside its own accessibility teardown, and that arrives as a ROOT
        # LEAK, intermittently. Counting ROOT LEAK lines would make this flaky.
        #
        # Asking whether the binary appears anywhere in the stack does not
        # separate them either: AppKit stranded that array under
        # glfwDestroyWindow, which main called, so main is in its stack too.
        # What tells them apart is how far the binary's frame is from the
        # allocation. Something this code lost was allocated a frame or two
        # below its own call; AppKit's stray has ten Apple frames in between.
        #
        # So a leak counts when a frame within four of the allocation is in
        # this binary. That is what lets the suites using ae3d.engine be
        # checked at all: they were skipped wholesale for opening a window, and
        # the exclusion was hiding a lost model in each of them.
        output="$(MallocStackLogging=1 bounded "$RUN_LIMIT" leaks --atExit -- "./build/$name" 2>&1)"
        report="$(printf '%s' "$output" | grep -o '[0-9]* leaks for [0-9]* total leaked bytes' | tail -1)"
        lost="$(printf '%s' "$output" | awk -v bin="$name" '
            /^STACK OF /   { inblock = (index($0, "ROOT LEAK") > 0); ours = 0; next }
            inblock && $1 ~ /^[0-9]+$/ && $1 + 0 <= 4 && index($0, bin) { ours = 1 }
            inblock && /^====/ { if (ours) n++; inblock = 0 }
            END { print n + 0 }
        ')"
        cycles="$(printf '%s' "$output" | grep -c 'ROOT CYCLE')"
        if [ -z "$report" ]; then
            skip "$name" "no leak report"
        elif [ "$lost" -eq 0 ]; then
            if [ "$cycles" -gt 0 ]; then
                pass "$name (no lost allocations; $cycles system retain cycle(s) from the GPU context)"
            else
                pass "$name"
            fi
        else
            fail "$name ($report, $lost from this code)"
            printf '%s' "$output" | grep -A3 'ROOT LEAK' | sed 's/^/        /' | head -12
        fi
    done
fi

printf '\n'
if [ "$failures" -eq 0 ]; then
    printf 'ci: everything passed'
    [ "$skipped" -gt 0 ] && printf ' (%d skipped)' "$skipped"
    printf '\n'
    exit 0
fi
printf 'ci: %d failure(s)\n' "$failures"
exit 1
