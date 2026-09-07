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

have_display() {
    case "$(uname -s)" in
        Darwin) return 0 ;;
        *) [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] ;;
    esac
}

step "native layer, warnings as errors"
CC="${CC:-cc}"
GLFW_CFLAGS="$(pkg-config --cflags glfw3 2>/dev/null || true)"
VULKAN_CFLAGS=""
if pkg-config --exists vulkan 2>/dev/null; then
    VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
elif [ -d /opt/homebrew/include/vulkan ]; then
    VULKAN_CFLAGS="-I/opt/homebrew/include"
fi
for src in native/*.c; do
    if "$CC" -c -O2 -Wall -Wextra -Werror $GLFW_CFLAGS $VULKAN_CFLAGS "$src" -o /dev/null 2>/tmp/ae3d_cc.log; then
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
    probe="$(mktemp -t ae3d_probe).ae"
    printf 'import ae3d.%s\nmain() { println("ok") }\n' "$name" > "$probe"
    if aetherc "$probe" "${probe%.ae}.c" >/tmp/ae3d_mod.log 2>&1; then
        pass "ae3d.$name"
    else
        fail "ae3d.$name"
        sed 's/^/        /' /tmp/ae3d_mod.log | head -10
    fi
    rm -f "$probe" "${probe%.ae}.c"
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
    if ! output="$(AE3D_FRAMES="$FRAMES" ./build/"$name" 2>&1)"; then
        fail "$name"
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
    if AE3D_FRAMES="$FRAMES" ./build/"$name" >/tmp/ae3d_run.log 2>&1; then
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
    name="ae3d_editor ($editor_backend)"
    report="$(mktemp)"
    snapshot="$(mktemp -t ae3d_shot).png"
    log="$(mktemp)"
    # A bounded run ends itself; the timeout is only a backstop so a hang
    # fails the step rather than blocking it.
    AE3D_EDITOR_BACKEND="$editor_backend" \
    AE3D_EDITOR_FRAMES=30 \
    AE3D_EDITOR_SCENE=components \
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
UI_ROOT="${AETHER_UI_ROOT:-$ROOT/../aether-ui}"
if [ ! -f "$UI_ROOT/ui/module.ae" ]; then
    skip "ae3d_editor" "aether-ui not found at $UI_ROOT"
elif ! have_display; then
    skip "ae3d_editor" "no display"
else
    if ./editor/build_editor.sh >/tmp/ae3d_build.log 2>&1; then
        for editor_backend in opengl vulkan; do
            check_editor_run "$editor_backend"
        done
    else
        fail "ae3d_editor (build)"
        sed 's/^/        /' /tmp/ae3d_build.log | head -20
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
    if output="$(./build/"$name" 2>&1)"; then
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
        grep -q "ae3d.engine" "$suite" && continue
        [ -x "build/$name" ] || continue
        # Only allocations this code lost count. A program that creates a GPU
        # context also produces NSXPCConnection retain cycles inside the window
        # server's own machinery, which leaks reports as ROOT CYCLE rather than
        # ROOT LEAK and which nothing here can release.
        output="$(MallocStackLogging=1 leaks --atExit -- "./build/$name" 2>&1)"
        report="$(printf '%s' "$output" | grep -o '[0-9]* leaks for [0-9]* total leaked bytes' | tail -1)"
        lost="$(printf '%s' "$output" | grep -c 'ROOT LEAK')"
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
