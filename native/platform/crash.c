/* The native stack on a crash, printed as it happens.
 *
 * A crash on a headless CI runner leaves nothing but "died on signal 11" and
 * a core file nobody can open. This prints the native stack to stderr the
 * instant it happens, so the log names the frame that fell over.
 *
 * C, and installed when the shared library loads, for two reasons the
 * language cannot meet: a signal handler may call only the async-signal-safe
 * (backtrace and backtrace_symbols_fd are on the allowed list, write is the
 * only other call here, and nothing of a runtime may run between them), and
 * it has to be in place before any entry point runs, so an offscreen test
 * that never opens a window is covered too. The handler re-raises the
 * default so the process still dies and the exit status is unchanged. */

#include "ae3d.h"

#if !defined(_WIN32)
#  include <signal.h>
#  include <string.h>
#  include <unistd.h>
#  if defined(__GLIBC__) || defined(__APPLE__)
#    include <execinfo.h>
#    define AE3D_HAVE_BACKTRACE 1
#  endif
#endif

#if defined(AE3D_HAVE_BACKTRACE)
static void ae3d_say(const char *text) {
    /* write() is marked warn_unused_result by glibc and the build is -Werror;
       there is nothing useful to do if writing the crash message itself fails,
       so the result is captured and discarded. */
    ssize_t written = write(2, text, strlen(text));
    (void)written;
}

static void ae3d_crash_handler(int sig) {
    void *frames[64];
    int n = backtrace(frames, 64);
    char digit[2];
    digit[0] = (char)('0' + (sig % 10));
    digit[1] = '\n';
    ae3d_say("\nae3d: native crash, signal ");
    { ssize_t w = write(2, digit, 2); (void)w; }
    backtrace_symbols_fd(frames, n, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

__attribute__((constructor))
static void ae3d_install_crash_handler(void) {
    signal(SIGSEGV, ae3d_crash_handler);
    signal(SIGABRT, ae3d_crash_handler);
    signal(SIGBUS, ae3d_crash_handler);
    signal(SIGFPE, ae3d_crash_handler);
}
#else
/* Windows has no backtrace(3); an empty translation unit is a warning under
   -pedantic, so the file declares one symbol it never uses. */
int ae3d_crash_handler_absent(void);
int ae3d_crash_handler_absent(void) { return 1; }
#endif
