#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "rtos_api.h"

static char g_log[256];
static int g_log_pos = 0;

static void log_char(char c) {
    if (g_log_pos < (int)sizeof(g_log) - 1) {
        g_log[g_log_pos++] = c;
        g_log[g_log_pos] = '\0';
    }
}

static void reset_log(void) {
    g_log_pos = 0;
    g_log[0] = '\0';
}

static void expect_str(const char *name, const char *got, const char *expected) {
    if (strcmp(got, expected) != 0) {
        printf("[FAIL] %s: got \"%s\", expected \"%s\"\n", name, got, expected);
        exit(1);
    }
    printf("[OK] %s\n", name);
}

static jmp_buf g_fail_jmp;
static char g_fail_msg[128];

static void test_fail_handler(const char *msg) {
    strncpy(g_fail_msg, msg, sizeof(g_fail_msg) - 1);
    g_fail_msg[sizeof(g_fail_msg) - 1] = '\0';
    longjmp(g_fail_jmp, 1);
}

static void expect_fail(const char *name, void (*fn)(void)) {
    g_fail_msg[0] = '\0';
    if (setjmp(g_fail_jmp) == 0) {
        fn();
        printf("[FAIL] %s: expected OS_Fail\n", name);
        exit(1);
    }
    printf("[OK] %s (\"%s\")\n", name, g_fail_msg);
}

static TSemaphore sem0 = 0;

/* ===== Test 1: EDF ===== */
TASK(TaskA, 30) { log_char('A'); TerminateTask(); }
TASK(TaskB, 10) { log_char('B'); TerminateTask(); }
TASK(TaskC, 20) { log_char('C'); TerminateTask(); }

TASK(StarterEDF, 100) {
    EnterISR();
    ISRActivateTask(TaskA);
    ISRActivateTask(TaskC);
    ISRActivateTask(TaskB);
    LeaveISR();
    log_char('S');
    TerminateTask();
}

/* ===== Test 2: Semaphores ===== */
TASK(SemWaiter, 10) {
    log_char('W');
    P(sem0);
    log_char('G');
    V(sem0);
    log_char('R');
    TerminateTask();
}

TASK(SemOwner, 20) {
    log_char('O');
    P(sem0);
    log_char('L');
    ActivateTask(SemWaiter);
    log_char('X');
    V(sem0);
    log_char('U');
    TerminateTask();
}

/* ===== Test 3: ISR ===== */
TASK(IsrTarget, 5) { log_char('T'); TerminateTask(); }

static void FakeISR(void) {
    EnterISR();
    { int local = 1; (void)local; }
    log_char('I');
    ISRActivateTask(IsrTarget);
    log_char('J');
    LeaveISR();
}

TASK(StarterISR, 50) {
    log_char('S');
    FakeISR();
    log_char('K');
    TerminateTask();
}

/* ===== Error/limit tests ===== */
static void fail_invalid_sem_init(void) { InitPVS((TSemaphore)MAX_RESOURCES); }

static void DummyTask(void) { }

static void fail_max_tasks_limit(void) {
    static TTaskStruct extra[MAX_TASKS + 1];
    unsigned char i = 0;

    while (g_task_count < MAX_TASKS) {
        extra[i].entry = DummyTask;
        extra[i].deadline = 1;
        (void)OS_TaskId(&extra[i]);
        i++;
    }

    extra[i].entry = DummyTask;
    extra[i].deadline = 1;
    (void)OS_TaskId(&extra[i]);
}

int main(void) {
    g_fail_handler = test_fail_handler;

    printf("=== RTOS compliance tests (variant 1) ===\n");

    reset_log();
    StartOS(StarterEDF);
    expect_str("EDF order", g_log, "BCAS");

    reset_log();
    InitPVS(sem0);
    StartOS(SemOwner);
    expect_str("Semaphore blocking", g_log, "OLWXGRU");

    reset_log();
    StartOS(StarterISR);
    expect_str("ISR activation", g_log, "SIJTK");

    expect_fail("Invalid semaphore id", fail_invalid_sem_init);
    expect_fail("Max tasks limit", fail_max_tasks_limit);

    printf("All tests passed.\n");
    return 0;
}
