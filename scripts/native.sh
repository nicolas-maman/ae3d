# The engine's C, built once into a library that every program and every
# script shares.
#
# It is shared rather than a pile of objects linked into each program because a
# script is a library the host opens while it is running. On macOS and Linux a
# script can leave the engine's calls undefined and have the host resolve them;
# on Windows a DLL cannot, and the link fails on every native call the engine
# code it imported makes. Linking the same library from both sides is what the
# platform offers, and doing it on all three means one copy of the GL loader's
# state at run time rather than one per script.
#
# Sourced by build.sh, editor/build_editor.sh and scripts/build_script.sh.

ae3d_native_suffix() {
    case "$(uname -s)" in
        Darwin) echo ".dylib" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT) echo ".dll" ;;
        *) echo ".so" ;;
    esac
}

# Position-independent code is what a shared library is made of, and it is the
# default on macOS and on Windows. Naming it there is not harmless: MinGW warns
# that the flag is ignored, and ci.sh reads a warning in a build log as a
# failure.
ae3d_native_pic_flag() {
    case "$(uname -s)" in
        Darwin|MINGW*|MSYS*|CYGWIN*|Windows_NT) echo "" ;;
        *) echo "-fPIC" ;;
    esac
}

# Where a program or a script says the library will be, relative to itself. The
# programs sit beside it in build/; a script sits one directory down, in
# build/scripts/. Windows has no equivalent and needs none: the loader finds a
# DLL beside the executable that asked for it, and a script opened by a running
# host binds to the copy that host already has open.
ae3d_native_rpath() {
    case "$(uname -s)" in
        Darwin) echo "-Wl,-rpath,@loader_path/$1" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT) echo "" ;;
        *) echo "-Wl,-rpath,\$ORIGIN/$1" ;;
    esac
}

# What to put on a link line to use it. $1 is the path from the linked file back
# to build/, empty for a program.
ae3d_native_link_flags() {
    printf '%s' "-Lbuild -lae3d_native $(ae3d_native_rpath "${1:-.}")"
}

ae3d_native_library() {
    printf '%s' "build/libae3d_native$(ae3d_native_suffix)"
}

# Link the objects the caller has just compiled into that library. The engine's
# own dependencies are named here rather than taken from the caller: the
# editor's link line carries aether-ui's libraries, which are nothing to do
# with this, and on Windows a DLL has to resolve everything it calls.
#
#   ae3d_native_build <cc> <object directory> <cflags> <extra link libraries>
ae3d_native_build() {
    ae3d_cc="$1"
    ae3d_obj_dir="$2"
    ae3d_cflags="$3"
    ae3d_extra_libs="${4:-}"
    ae3d_lib="$(ae3d_native_library)"

    ae3d_stale=0
    [ -f "$ae3d_lib" ] || ae3d_stale=1
    for ae3d_obj in "$ae3d_obj_dir"/*.o; do
        [ -e "$ae3d_obj" ] || continue
        [ "$ae3d_obj" -nt "$ae3d_lib" ] && ae3d_stale=1
    done
    [ "$ae3d_stale" = 0 ] && return 0

    ae3d_soname=""
    case "$(uname -s)" in
        Darwin) ae3d_soname="-Wl,-install_name,@rpath/$(basename "$ae3d_lib")" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT)
            # The import library is what a program links against; the DLL
            # itself is only ever loaded.
            ae3d_soname="-Wl,--out-implib,$ae3d_lib.a" ;;
        *) ae3d_soname="-Wl,-soname,$(basename "$ae3d_lib")" ;;
    esac

    # shellcheck disable=SC2086
    "$ae3d_cc" -shared $ae3d_cflags "$ae3d_obj_dir"/*.o $ae3d_soname \
        $ae3d_extra_libs $(ae3d_platform_libs "$(uname -s)") -o "$ae3d_lib"
}
