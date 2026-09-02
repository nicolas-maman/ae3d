#!/usr/bin/env bash
# Build and run everything: the native layer, every module, every test suite and
# every example.
#
# Examples need a window, so they are driven for a bounded number of frames via
# AETHER3D_FRAMES and are skipped where no display is available.
#
#   ./ci.sh

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

FRAMES="${AETHER3D_CI_FRAMES:-30}"
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
for module in src/a3d/*/; do
    name="$(basename "$module")"
    if [ ! -f "$module/module.ae" ]; then
        fail "a3d.$name (no module.ae; aether-lang-dev/aether#1858 makes the import silently succeed)"
        continue
    fi
    probe="$(mktemp -t ae3d_probe).ae"
    printf 'import a3d.%s\nmain() { println("ok") }\n' "$name" > "$probe"
    if aetherc "$probe" "${probe%.ae}.c" >/tmp/ae3d_mod.log 2>&1; then
        pass "a3d.$name"
    else
        fail "a3d.$name"
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
    grep -q "a3d.engine" "$suite" && needs_window=1
    if [ "$needs_window" = 1 ] && ! have_display; then
        skip "$name" "no display"
        continue
    fi
    if output="$(AETHER3D_FRAMES="$FRAMES" ./build/"$name" 2>&1)" && \
       printf '%s' "$output" | grep -q "all checks passed"; then
        pass "$name"
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
    if AETHER3D_FRAMES="$FRAMES" ./build/"$name" >/tmp/ae3d_run.log 2>&1; then
        pass "$name"
    else
        fail "$name"
        sed 's/^/        /' /tmp/ae3d_run.log | head -20
    fi
done

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

if command -v leaks >/dev/null 2>&1; then
    step "leak check, headless suites"
    for suite in tests/test_*.ae benchmarks/bench_*.ae; do
        [ -e "$suite" ] || continue
        name="$(basename "$suite" .ae)"
        grep -q "a3d.engine" "$suite" && continue
        [ -x "build/$name" ] || continue
        report="$(MallocStackLogging=1 leaks --atExit -- "./build/$name" 2>&1 |
                  grep -o '[0-9]* leaks for [0-9]* total leaked bytes' | tail -1)"
        case "$report" in
            "0 leaks"*) pass "$name" ;;
            "") skip "$name" "no leak report" ;;
            *) fail "$name ($report)" ;;
        esac
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
