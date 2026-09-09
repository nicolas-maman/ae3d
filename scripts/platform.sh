# Platform link libraries, keyed on `uname -s`.
#
# Its own file so ci.sh can check every arm on any host. The Windows arm was
# missing for a long time and nothing said so: `uname -s` under MinGW/MSYS
# reports MINGW64_NT-... , which fell through to the catch-all and linked -lm
# alone, and an OpenGL link on Windows cannot succeed without opengl32 and
# gdi32 however complete the install is (#80).

# native/ae3d_agent.c opens a loopback socket and runs a thread of its own, so
# the sockets and threads libraries are ours to name: ws2_32 on Windows, and
# pthread on Linux, where it was already listed. macOS has both in libSystem.
# The Aether toolchain happens to link ws2_32 for its own networking, and
# leaning on that is how the zlib link broke -- a library this code calls
# directly is named here.
ae3d_platform_libs() {
    case "$1" in
        Darwin)
            echo "-framework Cocoa -framework IOKit -framework CoreVideo -framework QuartzCore -framework Metal -framework OpenGL" ;;
        Linux)
            echo "-ldl -lm -lpthread" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT)
            echo "-lopengl32 -lgdi32 -luser32 -lshell32 -lws2_32 -lm" ;;
        *)
            echo "-lm" ;;
    esac
}
