/* A pool of worker threads and a parallel for over a range.
 *
 * The crowd's frame is a handful of loops over half a million figures --
 * the heading, the shove, the step, the sort by distance, the matrices --
 * and each is the same small function on every element with nothing shared
 * but the arrays. One core ran them in twenty-five milliseconds; the frame
 * has eight or sixteen. ae3d_jobs_for splits the range into runs of `grain`
 * elements, hands them to the workers and the calling thread alike, each
 * taking the next run off an atomic counter until the range is done, and
 * returns when every worker has said so. No queue, no futures: a frame's
 * work is a sequence of such loops, and a loop is done before the next one
 * starts, so the calling thread never waits on anything but the loop it is
 * in.
 *
 * The workers sleep between loops on a condition variable and wake to a
 * generation count; the caller bumps it, they take runs, count themselves
 * out, and the last one wakes the caller. A worker that wakes late finds
 * the counter past the end, takes nothing and counts itself out the same,
 * so the caller's wait is for every worker every time and no generation is
 * ever missed. Win32 threads on Windows, pthreads elsewhere; the atomics
 * are the compiler's.
 */
#include "ae3d.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef CRITICAL_SECTION ae3d_mutex;
typedef CONDITION_VARIABLE ae3d_cond;
typedef HANDLE ae3d_thread;
#define ae3d_mutex_init(m) InitializeCriticalSection(m)
#define ae3d_mutex_free(m) DeleteCriticalSection(m)
#define ae3d_mutex_lock(m) EnterCriticalSection(m)
#define ae3d_mutex_unlock(m) LeaveCriticalSection(m)
#define ae3d_cond_init(c) InitializeConditionVariable(c)
#define ae3d_cond_free(c) ((void)0)
#define ae3d_cond_wait(c, m) SleepConditionVariableCS(c, m, INFINITE)
#define ae3d_cond_broadcast(c) WakeAllConditionVariable(c)
#define ae3d_cond_signal(c) WakeConditionVariable(c)
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_mutex_t ae3d_mutex;
typedef pthread_cond_t ae3d_cond;
typedef pthread_t ae3d_thread;
#define ae3d_mutex_init(m) pthread_mutex_init(m, NULL)
#define ae3d_mutex_free(m) pthread_mutex_destroy(m)
#define ae3d_mutex_lock(m) pthread_mutex_lock(m)
#define ae3d_mutex_unlock(m) pthread_mutex_unlock(m)
#define ae3d_cond_init(c) pthread_cond_init(c, NULL)
#define ae3d_cond_free(c) pthread_cond_destroy(c)
#define ae3d_cond_wait(c, m) pthread_cond_wait(c, m)
#define ae3d_cond_broadcast(c) pthread_cond_broadcast(c)
#define ae3d_cond_signal(c) pthread_cond_signal(c)
#endif

#define AE3D_JOBS_MAX 63

static struct {
    int started;
    int workers;
    ae3d_thread threads[AE3D_JOBS_MAX];
    ae3d_mutex lock;
    ae3d_cond wake;      /* a new generation, or the end */
    ae3d_cond done;      /* the last worker of a generation */
    unsigned generation;
    int stopping;
    /* The loop of the current generation. */
    ae3d_job_fn fn;
    void *ctx;
    int count;
    int grain;
    int next;            /* the next run's start, taken atomically */
    int finished;        /* workers done with this generation */
    /* How many loops were run and how many runs they were split into: the
       numbers a benchmark reads back. */
    long long loops;
    long long runs;
} jobs;

static int ae3d_jobs_hardware_threads(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

/* The runs of the current generation, taken until none are left. Returns
   how many this thread took. */
static int ae3d_jobs_take_runs(void) {
    int taken = 0;
    for (;;) {
        int start = __atomic_fetch_add(&jobs.next, jobs.grain, __ATOMIC_ACQ_REL);
        int end;
        if (start >= jobs.count) break;
        end = start + jobs.grain;
        if (end > jobs.count) end = jobs.count;
        jobs.fn(jobs.ctx, start, end);
        __atomic_fetch_add(&jobs.runs, 1, __ATOMIC_RELAXED);
        taken++;
    }
    return taken;
}

#ifdef _WIN32
static DWORD WINAPI ae3d_jobs_worker(LPVOID arg)
#else
static void *ae3d_jobs_worker(void *arg)
#endif
{
    unsigned seen = 0;
    (void)arg;
    for (;;) {
        ae3d_mutex_lock(&jobs.lock);
        while (jobs.generation == seen && !jobs.stopping) ae3d_cond_wait(&jobs.wake, &jobs.lock);
        if (jobs.stopping) { ae3d_mutex_unlock(&jobs.lock); break; }
        seen = jobs.generation;
        ae3d_mutex_unlock(&jobs.lock);

        ae3d_jobs_take_runs();

        ae3d_mutex_lock(&jobs.lock);
        jobs.finished++;
        if (jobs.finished == jobs.workers) ae3d_cond_signal(&jobs.done);
        ae3d_mutex_unlock(&jobs.lock);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int ae3d_jobs_start(int workers) {
    int i;
    if (jobs.started) return jobs.workers;
    if (workers < 0) {
        /* One thread for every hardware thread but the caller's own, and
           never more than the pool holds. Zero is a pool with no workers:
           every loop on the calling thread, the measure the rest is
           against. */
        workers = ae3d_jobs_hardware_threads() - 1;
    }
    if (workers > AE3D_JOBS_MAX) workers = AE3D_JOBS_MAX;
    memset(&jobs, 0, sizeof(jobs));
    ae3d_mutex_init(&jobs.lock);
    ae3d_cond_init(&jobs.wake);
    ae3d_cond_init(&jobs.done);
    jobs.started = 1;
    for (i = 0; i < workers; i++) {
#ifdef _WIN32
        jobs.threads[i] = CreateThread(NULL, 0, ae3d_jobs_worker, NULL, 0, NULL);
        if (!jobs.threads[i]) break;
#else
        if (pthread_create(&jobs.threads[i], NULL, ae3d_jobs_worker, NULL) != 0) break;
#endif
        jobs.workers = i + 1;
    }
    return jobs.workers;
}

void ae3d_jobs_stop(void) {
    int i;
    if (!jobs.started) return;
    ae3d_mutex_lock(&jobs.lock);
    jobs.stopping = 1;
    ae3d_cond_broadcast(&jobs.wake);
    ae3d_mutex_unlock(&jobs.lock);
    for (i = 0; i < jobs.workers; i++) {
#ifdef _WIN32
        WaitForSingleObject(jobs.threads[i], INFINITE);
        CloseHandle(jobs.threads[i]);
#else
        pthread_join(jobs.threads[i], NULL);
#endif
    }
    ae3d_cond_free(&jobs.done);
    ae3d_cond_free(&jobs.wake);
    ae3d_mutex_free(&jobs.lock);
    memset(&jobs, 0, sizeof(jobs));
}

int ae3d_jobs_workers(void) { return jobs.started ? jobs.workers : 0; }

long long ae3d_jobs_loops(void) { return jobs.loops; }
long long ae3d_jobs_runs(void) { return jobs.runs; }

void ae3d_jobs_for(int count, int grain, ae3d_job_fn fn, void *ctx) {
    if (!fn || count <= 0) return;
    if (grain <= 0) grain = 1;
    /* No pool, or a range too small to split: the calling thread alone,
       run by run all the same, so a loop that keeps something a run -- the
       bucket's counts -- sees the same runs it would over the pool. */
    if (!jobs.started || jobs.workers == 0 || count <= grain) {
        int start;
        for (start = 0; start < count; start += grain) {
            int end = start + grain < count ? start + grain : count;
            fn(ctx, start, end);
            jobs.runs++;
        }
        jobs.loops++;
        return;
    }
    ae3d_mutex_lock(&jobs.lock);
    jobs.fn = fn;
    jobs.ctx = ctx;
    jobs.count = count;
    jobs.grain = grain;
    jobs.next = 0;
    jobs.finished = 0;
    jobs.generation++;
    ae3d_cond_broadcast(&jobs.wake);
    ae3d_mutex_unlock(&jobs.lock);

    ae3d_jobs_take_runs();
    jobs.loops++;

    ae3d_mutex_lock(&jobs.lock);
    while (jobs.finished < jobs.workers) ae3d_cond_wait(&jobs.done, &jobs.lock);
    ae3d_mutex_unlock(&jobs.lock);
}
