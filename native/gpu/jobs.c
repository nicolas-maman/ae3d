/* The native loops' door to the engine's job pool.
 *
 * Three loops in this directory are worth every core: the instance
 * matrices a crowd's positions and headings make (geometry/mesh.c), the
 * Vulkan crowd's per-frame state and the packed instance stream
 * (gpu/vulkan.c). They used to run on a pool of their own, threads made
 * here beside the ones ae3d.jobs makes, so a frame had two sets of
 * workers competing for the same cores. The pool is Aether's now: the
 * engine installs a runner that takes the range, the loop's body and its
 * context and runs them over ae3d.jobs (aephysics's scheduler), calling
 * each block back through ae3d_job_call. Without a runner -- a program
 * that never started the engine, a test of this layer alone -- the loop
 * runs on the calling thread, run by run, the same runs it would get.
 */
#include "ae3d.h"

static ae3d_jobs_runner_fn g_runner;

void ae3d_jobs_set_runner(ae3d_jobs_runner_fn runner) { g_runner = runner; }

void ae3d_job_call(ae3d_job_fn fn, void *ctx, int start, int end) { fn(ctx, start, end); }

void ae3d_jobs_for(int count, int grain, ae3d_job_fn fn, void *ctx) {
    int start;
    if (!fn || count <= 0) return;
    if (grain <= 0) grain = 1;
    if (g_runner && count > grain) {
        g_runner(count, grain, fn, ctx);
        return;
    }
    for (start = 0; start < count; start += grain) {
        int end = start + grain < count ? start + grain : count;
        fn(ctx, start, end);
    }
}
