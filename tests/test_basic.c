/*
 * test_basic.c — unit tests for core coroutine behaviour
 *
 * Covers: co_create, co_run, co_yield, co_id, co_state, co_await, co_free.
 */

#include "../include/coroutine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────────*/

#define PASS(name) fprintf(stderr, "  PASS  %s\n", name)
#define FAIL(name) do { fprintf(stderr, "  FAIL  %s\n", name); _failures++; } while(0)
#define CHECK(cond, name) do { if (cond) PASS(name); else FAIL(name); } while(0)

static int _failures = 0;

/* ── test bodies ──────────────────────────────────────────────────────────*/

static int ran_once;

static void body_run_once(void *arg) {
    (void)arg;
    ran_once = 1;
}

static void test_run_once(void) {
    ran_once = 0;
    Coroutine *c = co_create(body_run_once, NULL, 0);
    co_run();
    CHECK(ran_once == 1, "run_once: body executed");
    CHECK(co_state(c) == CO_DONE, "run_once: state is CO_DONE");
    co_free(c);
}

static int id_a, id_b;

static void body_id(void *arg) { *(int *)arg = co_id(); }

static void test_co_id(void) {
    Coroutine *a = co_create(body_id, &id_a, 0);
    Coroutine *b = co_create(body_id, &id_b, 0);
    co_run();
    CHECK(id_a > 0,       "co_id: a has positive id");
    CHECK(id_b > 0,       "co_id: b has positive id");
    CHECK(id_a != id_b,   "co_id: a and b have distinct ids");
    co_free(a);
    co_free(b);
}

static void test_id_outside(void) {
    CHECK(co_id() == 0, "co_id outside coroutine returns 0");
}

#define SEQ_LEN 6
static int seq[SEQ_LEN];
static int seq_pos;

static void body_even(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        seq[seq_pos++] = 0;
        co_yield();
    }
}

static void body_odd(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        seq[seq_pos++] = 1;
        co_yield();
    }
}

static void test_yield_interleave(void) {
    seq_pos = 0;
    memset(seq, -1, sizeof(seq));
    Coroutine *e = co_create(body_even, NULL, 0);
    Coroutine *o = co_create(body_odd,  NULL, 0);
    co_run();
    int expected[] = {0, 1, 0, 1, 0, 1};
    int ok = (memcmp(seq, expected, sizeof(expected)) == 0);
    CHECK(ok, "yield_interleave: round-robin order");
    CHECK(seq_pos == SEQ_LEN, "yield_interleave: all steps executed");
    co_free(e);
    co_free(o);
}

static int await_result;

static void body_producer(void *arg) {
    (void)arg;
    co_yield();
    await_result = 42;
}

static void body_consumer(void *arg) {
    Coroutine *prod = (Coroutine *)arg;
    co_await(prod);
}

static void test_await(void) {
    await_result = 0;
    Coroutine *prod = co_create(body_producer, NULL, 0);
    Coroutine *cons = co_create(body_consumer, prod, 0);
    co_run();
    CHECK(await_result == 42, "co_await: consumer sees producer's result");
    CHECK(co_state(prod) == CO_DONE, "co_await: producer is DONE");
    CHECK(co_state(cons) == CO_DONE, "co_await: consumer is DONE");
    co_free(prod);
    co_free(cons);
}

static int already_done_count;

static void body_quick(void *arg) { (void)arg; }

static void body_waits_on_done(void *arg) {
    Coroutine *done = (Coroutine *)arg;
    co_await(done);
    already_done_count++;
}

static void test_await_already_done(void) {
    already_done_count = 0;
    Coroutine *quick  = co_create(body_quick, NULL, 0);
    Coroutine *waiter = co_create(body_waits_on_done, quick, 0);
    co_run();
    CHECK(already_done_count == 1, "await_already_done: waiter ran");
    CHECK(co_state(waiter) == CO_DONE, "await_already_done: waiter done");
    co_free(quick);
    co_free(waiter);
}

static int arg_received;

static void body_arg(void *arg) {
    arg_received = *(int *)arg;
}

static void test_arg_passing(void) {
    int val = 1337;
    Coroutine *c = co_create(body_arg, &val, 0);
    co_run();
    CHECK(arg_received == 1337, "arg_passing: correct value received");
    co_free(c);
}

#define MANY 64
static int many_count;

static void body_many(void *arg) {
    (void)arg;
    co_yield();
    many_count++;
}

static void test_many(void) {
    many_count = 0;
    Coroutine *cos[MANY];
    for (int i = 0; i < MANY; i++)
        cos[i] = co_create(body_many, NULL, 0);
    co_run();
    CHECK(many_count == MANY, "many: all coroutines completed");
    for (int i = 0; i < MANY; i++) co_free(cos[i]);
}

/* ── main ─────────────────────────────────────────────────────────────────*/
int main(void) {
    fprintf(stderr, "=== test_basic ===\n");
    test_id_outside();
    test_run_once();
    test_co_id();
    test_yield_interleave();
    test_arg_passing();
    test_await();
    test_await_already_done();
    test_many();

    if (_failures == 0)
        fprintf(stderr, "All tests passed.\n");
    else
        fprintf(stderr, "%d test(s) FAILED.\n", _failures);

    return _failures ? 1 : 0;
}
