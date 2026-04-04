#include <stdio.h>
#include <stdlib.h>
#include "rtos_api.h"
#ifdef _WIN32
#include <windows.h>
#endif

void OS_Fail(const char *msg) {
    if (g_fail_handler) {
        g_fail_handler(msg);
        return;
    }

    fprintf(stderr, "RTOS error: %s\n", msg);
    exit(1);
}

void EnterISR(void) {
    if (g_in_isr) {
        OS_Fail("Nested ISR is not supported");
    }
    g_in_isr = 1;
}

void LeaveISR(void) {
    if (!g_in_isr) {
        OS_Fail("LeaveISR without EnterISR");
    }

    g_in_isr = 0;
    OS_ScheduleFromISR();
}

void StartOS(TTask task) {
    unsigned char i;
    unsigned char start_id;

    if (g_os_running) {
        OS_Fail("StartOS called while OS already running");
    }

    start_id = OS_TaskId(task);

    for (i = 0; i < MAX_RESOURCES; ++i) {
        g_sems[i].locked = 0;
        g_sems[i].owner = INVALID_TASK;
    }

#ifdef _WIN32
    OS_ResetFibers();
#endif

    g_os_running = 1;
    g_current_task = INVALID_TASK;
    g_in_isr = 0;

    for (i = 0; i < g_task_count; ++i) {
        g_tasks[i].state = TASK_SUSPENDED;
        g_tasks[i].active = 0;
        g_tasks[i].waiting_sem = INVALID_SEM;
        g_tasks[i].waiting_active = 0;
        g_tasks[i].critical_count = 0;
    }

    g_tasks[start_id].active = 1;
    g_tasks[start_id].state = TASK_READY;

    OS_Dispatch();
}

void ShutdownOS(void) {
    g_os_running = 0;
    g_current_task = INVALID_TASK;
    g_in_isr = 0;
}
