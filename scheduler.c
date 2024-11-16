/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

#define SZ_STACK 1048576

/* Thread structure declaration */

struct thread {
    jmp_buf ctx;  /* Thread context for saving/restoring execution state */

    struct {
        char *memory;   /* Aligned stack memory */
        char *memory_;  /* Original unaligned stack memory */
    } stack;

    struct {
        void *arg;          /* Argument for the thread function */
        scheduler_fnc_t fnc;  /* Function to execute within the thread */
    } code;

    enum {
        INIT, RUNNING, SLEEPING, TERMINATED
    } status;

    struct thread *link;  /* Pointer to the next thread in the linked list */
};

struct thread *head = NULL;         /* Head of the thread linked list */
struct thread *currThread = NULL;   /* Currently executing thread */
jmp_buf ctx;                        /* Scheduler context */

/**
 * destroy
 * Frees the memory of all threads and their stacks. Used during scheduler cleanup.
 */
static void destroy (void) {
    struct thread *t, *t_;
    t = head;

    while (t) {
        t_ = t;
        t = t->link;
        FREE(t_->stack.memory_);
        FREE(t_);
    }
}

/**
 * scheduler_create
 * Initializes a new thread with the provided function and argument, adds it to the list of threads.
 * Returns 0 on success, -1 on failure.
 */
int scheduler_create(scheduler_fnc_t fnc, void *arg) {
    struct thread *t = (struct thread*) malloc(sizeof(struct thread));

    if (!t) {
        TRACE("Memory allocation error for thread");
        return -1;
    }

    t->status = INIT;
    t->code.fnc = fnc;
    t->code.arg = arg;

    t->stack.memory_ = malloc(SZ_STACK + page_size());
    if (!t->stack.memory_) {
        TRACE("Memory allocation error for stack");
        FREE(t);
        return -1;
    }

    t->stack.memory = memory_align(t->stack.memory_, page_size());
    if (!t->stack.memory) {
        TRACE("Memory alignment error");
        FREE(t->stack.memory_);
        FREE(t);
        return -1;
    }

    t->link = head;
    head = t;
    currThread = head;

    return 0;
}

/**
 * candidate
 * Selects the next eligible thread for execution by scanning the linked list of threads.
 * Returns a pointer to the next thread if available, or NULL if no candidate is found.
 */
static struct thread *candidate(void) {
    struct thread *t = currThread;

    if (t == NULL || t->link == NULL) t = head;
    else t = currThread->link;

    while (t) {
        if (t->status == INIT || t->status == SLEEPING) {
            return t;
        }
        t = t->link;
    }
    
    return NULL;
}

/**
 * handler
 * Signal handler for SIGALRM, re-issues the alarm every 1 second and yields to the next thread.
 */
void handler(int s) {
    if (s == SIGALRM) {
        signal(SIGALRM, handler);
        alarm(1);
        scheduler_yield();
    }
}

/**
 * schedule
 * Switches to the next eligible thread by restoring its context or initializing it if it is new.
 * Returns if no eligible thread is found.
 */
static void schedule(void) {
    struct thread *t = candidate();
    if (t == NULL) {
        return;
    }

    currThread = t;
    if (t->status == INIT) {

        /* Inline Assembly Code to reorient the stack pointer*/
        uint64_t rsp = (uint64_t)t->stack.memory + SZ_STACK;
        __asm__ volatile("mov %[rs], %%rsp \n" : [rs] "+r" (rsp) ::);

        t->status = RUNNING;
        t->code.fnc(t->code.arg);
        t->status = TERMINATED;
        longjmp(ctx, 1);
    } else {
        t->status = RUNNING;
        longjmp(t->ctx, 1);
    }
}

/**
 * scheduler_execute
 * Starts the scheduler, sets up signal handling for automatic thread switching, and runs the scheduler loop.
 */
void scheduler_execute(void) {

    signal(SIGALRM, handler);
    alarm(1);
    setjmp(ctx);
    schedule();
    destroy();
}

/**
 * scheduler_yield
 * Saves the current thread's context and status, then switches back to the scheduler's context.
 */
void scheduler_yield(void) {
    if (!setjmp(currThread->ctx)) {
        currThread->status = SLEEPING;
        longjmp(ctx, 1);
    }
}
