/*
 * task_graph.c — DAG-based task execution using c-coroutine
 *
 * Demonstrates how to model a directed acyclic graph (DAG) of tasks where
 * each task waits for all its dependencies to finish before running.
 * The scheduler runs all ready tasks concurrently as coroutines; once a
 * task's dependencies are done, it is enqueued automatically.
 *
 * Build:
 *   gcc -O2 -o task_graph task_graph.c -I../include -L../build -lcoroutine
 *
 * Usage:
 *   ./task_graph
 *
 * Expected output (order of independent tasks may vary):
 *   [fetch_data]     starting
 *   [parse_json]     starting
 *   [fetch_config]   starting
 *   [validate]       starting (deps: fetch_data, parse_json done)
 *   [enrich]         starting (deps: fetch_config, validate done)
 *   [write_output]   starting (deps: enrich done)
 *   All tasks complete.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "coroutine.h"

/* -------------------------------------------------------------------------
 * Task graph definition
 * ------------------------------------------------------------------------- */

#define MAX_TASKS    16
#define MAX_DEPS     8
#define MAX_NAME     32

typedef struct Task Task;

struct Task {
    char         name[MAX_NAME];
    int          dep_ids[MAX_DEPS]; /* indices into g_tasks[] */
    int          dep_count;
    atomic_int   deps_remaining;   /* decremented when each dep finishes */
    int          id;
    void       (*work)(Task *self); /* the actual work function */
};

/* Global task registry */
static Task  g_tasks[MAX_TASKS];
static int   g_task_count = 0;

/* -------------------------------------------------------------------------
 * Helper: register a task
 * ------------------------------------------------------------------------- */
static int task_register(const char *name, void (*work)(Task *))
{
    int id = g_task_count++;
    Task *t = &g_tasks[id];
    strncpy(t->name, name, MAX_NAME - 1);
    t->dep_count      = 0;
    t->deps_remaining = 0; /* set after all deps are added */
    t->id             = id;
    t->work           = work;
    return id;
}

static void task_add_dep(int task_id, int dep_id)
{
    Task *t = &g_tasks[task_id];
    t->dep_ids[t->dep_count++] = dep_id;
}

/* Call this after all deps have been added for all tasks */
static void task_graph_finalize(void)
{
    for (int i = 0; i < g_task_count; i++) {
        atomic_store(&g_tasks[i].deps_remaining, g_tasks[i].dep_count);
    }
}

/* -------------------------------------------------------------------------
 * Work functions (simulated with a busy loop + print)
 * ------------------------------------------------------------------------- */

static void work_fetch_data(Task *t)
{
    printf("  [%-16s] fetching remote data...\n", t->name);
    /* Simulate I/O by yielding a few times */
    for (int i = 0; i < 3; i++) co_yield();
    printf("  [%-16s] data fetched\n", t->name);
}

static void work_parse_json(Task *t)
{
    printf("  [%-16s] parsing JSON payload...\n", t->name);
    for (int i = 0; i < 2; i++) co_yield();
    printf("  [%-16s] JSON parsed\n", t->name);
}

static void work_fetch_config(Task *t)
{
    printf("  [%-16s] reading config file...\n", t->name);
    co_yield();
    printf("  [%-16s] config loaded\n", t->name);
}

static void work_validate(Task *t)
{
    printf("  [%-16s] validating data against schema...\n", t->name);
    for (int i = 0; i < 2; i++) co_yield();
    printf("  [%-16s] validation passed\n", t->name);
}

static void work_enrich(Task *t)
{
    printf("  [%-16s] enriching records with config...\n", t->name);
    for (int i = 0; i < 3; i++) co_yield();
    printf("  [%-16s] enrichment done\n", t->name);
}

static void work_write_output(Task *t)
{
    printf("  [%-16s] writing results to disk...\n", t->name);
    for (int i = 0; i < 2; i++) co_yield();
    printf("  [%-16s] output written\n", t->name);
}

/* -------------------------------------------------------------------------
 * Coroutine entry: run one task, then notify dependents
 * ------------------------------------------------------------------------- */

static void task_coroutine(void *arg)
{
    Task *self = (Task *)arg;

    printf("  [%-16s] starting\n", self->name);
    self->work(self);
    printf("  [%-16s] finished\n\n", self->name);

    /*
     * Notify every task that depends on us.
     * When a task's deps_remaining drops to 0, it can be scheduled.
     * In a real runtime we'd enqueue it; here we spawn it directly.
     */
    for (int i = 0; i < g_task_count; i++) {
        Task *other = &g_tasks[i];
        for (int d = 0; d < other->dep_count; d++) {
            if (other->dep_ids[d] == self->id) {
                int remaining = atomic_fetch_sub(&other->deps_remaining, 1) - 1;
                if (remaining == 0) {
                    /* All deps satisfied — spawn this task */
                    co_create(task_coroutine, other);
                }
                break;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void)
{
    printf("=== Task Graph Execution Demo ===\n\n");

    /* Build the DAG:
     *
     *   fetch_data ──┐
     *                ├──► validate ──► enrich ──► write_output
     *   parse_json ──┘
     *
     *   fetch_config ────────────────► enrich
     */

    int fetch_data   = task_register("fetch_data",   work_fetch_data);
    int parse_json   = task_register("parse_json",   work_parse_json);
    int fetch_config = task_register("fetch_config", work_fetch_config);
    int validate     = task_register("validate",     work_validate);
    int enrich       = task_register("enrich",       work_enrich);
    int write_output = task_register("write_output", work_write_output);

    task_add_dep(validate,     fetch_data);
    task_add_dep(validate,     parse_json);
    task_add_dep(enrich,       validate);
    task_add_dep(enrich,       fetch_config);
    task_add_dep(write_output, enrich);

    task_graph_finalize();

    /* Seed the scheduler with the root tasks (those with no deps) */
    for (int i = 0; i < g_task_count; i++) {
        if (g_tasks[i].dep_count == 0) {
            co_create(task_coroutine, &g_tasks[i]);
        }
    }

    /* Run until all coroutines complete */
    co_run();

    printf("=== All tasks complete ===\n");
    return 0;
}
