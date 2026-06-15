// Tulpar Async Runtime — cooperative event loop + stackful coroutines.
//
// This is the engine behind `async func` / `await`. It is deliberately a
// *stackful* coroutine design: an async function compiles to an ordinary
// native function, and `await` simply swaps the running coroutine's stack
// back to the scheduler. That keeps the compiler changes tiny — there is no
// CPS / state-machine transform — and pushes all the cleverness here.
//
// Model:
//   - Every `async` call spawns a Task (a coroutine) and returns an
//     ObjPromise immediately. The task is queued, not run, until the loop
//     pumps it.
//   - `await p` on a coroutine yields until `p` settles; on the main thread
//     it drives the loop until `p` settles ("block-on").
//   - Timers (`sleep_async`) settle their promise after a delay; the loop
//     sleeps until the earliest deadline when nothing else is runnable.
//
// Single-threaded: tasks never run nested — a task either runs to completion
// or yields control back to the scheduler, so one shared scheduler context
// is enough. See runtime/tulpar_async.cpp for the mechanics.

#ifndef TULPAR_ASYNC_H
#define TULPAR_ASYNC_H

#include "../src/vm/vm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Allocate a fresh pending promise.
ObjPromise *aot_promise_new(void);

// Settle a promise (state 1 = fulfilled, 2 = rejected) and wake its waiters.
void aot_promise_settle(ObjPromise *p, VMValue value, int state);

// Spawn `fn` (a top-level user-function pointer using the AOT
// `void(VMValue* ret, VMValue* args...)` ABI) as a coroutine. `args` is
// copied; ownership of object args is retained for the task's lifetime.
// Returns the promise the coroutine fulfils on return.
ObjPromise *aot_async_spawn(void *fn, VMValue *args, int argc);

// Suspend until `awaited` settles, then yield its value. A non-promise
// argument is returned unchanged (so `await 5` == 5).
VMValue aot_await(VMValue awaited);

// A promise that fulfils with void after `ms` milliseconds.
ObjPromise *aot_sleep_async(long long ms);

// Await every value in `args` concurrently and fulfil with an array of their
// results, in argument order. Non-promise args pass through unchanged. The
// children were already spawned (each `async` call queues its own task), so
// awaiting them in sequence inside one coroutine still runs them in parallel —
// total time is max(children), not the sum. Returns the gather promise.
ObjPromise *aot_gather(VMValue *args, int argc);

// Drive the loop until no tasks and no timers remain (call at program exit so
// spawned-but-unawaited tasks still complete, Node-style).
void aot_event_loop_run(void);

// 1 if the value is a promise.
int aot_is_promise(VMValue v);

#ifdef __cplusplus
}
#endif

#endif // TULPAR_ASYNC_H
