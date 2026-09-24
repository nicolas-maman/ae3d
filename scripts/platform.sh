# Platform link libraries, keyed on `uname -s`.
#
# Its own file so ci.sh can check every arm on any host. The Windows arm was
# missing for a long time and nothing said so: `uname -s` under MinGW/MSYS
# reports MINGW64_NT-... , which fell through to the catch-all and linked -lm
# alone, and an OpenGL link on Windows cannot succeed without opengl32 and
# gdi32 however complete the install is (#80).

# The native layer opens no socket and makes no thread: the agent channel
# (ae3d.channel, over std.tcp) and the job pool (ae3d.jobs, over std.worker)
# are Aether, and the Aether toolchain links what its own libraries call.
ae3d_platform_libs() {
    case "$1" in
        Darwin)
            echo "-framework Cocoa -framework IOKit -framework CoreVideo -framework QuartzCore -framework Metal -framework OpenGL" ;;
        Linux)
            echo "-ldl -lm" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT)
            echo "-lopengl32 -lgdi32 -luser32 -lshell32 -lm" ;;
        *)
            echo "-lm" ;;
    esac
}
