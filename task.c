#include <stdlib.h>
#include "rtos_api.h"

#ifdef _WIN32
#include <windows.h>
#endif

static TTask g_task_defs[MAX_TASKS];

unsigned char OS_TaskId(TTask task) {
    unsigned char i;

    if (!task) {
        OS_Fail("Invalid task pointer");
    }

    for (i = 0; i < g_task_count; ++i) {
        if (g_task_defs[i] == task) {
            return i;
        }
    }

    if (g_task_count >= MAX_TASKS) {
        OS_Fail("Too many tasks");
    }

    i = g_task_count;
    g_task_count++;

    g_task_defs[i] = task;
    g_tasks[i].entry = task->entry;
    g_tasks[i].deadline = task->deadline;
    g_tasks[i].state = TASK_SUSPENDED;
    g_tasks[i].active = 0;
    g_tasks[i].waiting_sem = INVALID_SEM;
    g_tasks[i].waiting_active = 0;
    g_tasks[i].critical_count = 0;
#ifdef _WIN32
    g_tasks[i].fiber = 0;
#endif

    return i;
}

static unsigned char OS_SelectNextTask(void) {
    unsigned char best = INVALID_TASK;
    unsigned char i;

    for (i = 0; i < g_task_count; ++i) {
        if (g_tasks[i].state == TASK_READY && g_tasks[i].active) {
            if (best == INVALID_TASK || g_tasks[i].deadline < g_tasks[best].deadline) {
                best = i;
            }
        }
    }

    return best;
}

static unsigned char OS_HasPreemptor(TDeadline current_deadline) {
    unsigned char i;
    for (i = 0; i < g_task_count; ++i) {
        if (g_tasks[i].state == TASK_READY && g_tasks[i].active) {
            if (g_tasks[i].deadline < current_deadline) {
                return 1;
            }
        }
    }
    return 0;
}

#ifdef _WIN32
static void *g_kernel_fiber = 0;
static unsigned char g_task_to_delete = INVALID_TASK;
static unsigned char g_task_id_param[MAX_TASKS];

static VOID WINAPI OS_TaskFiberProc(LPVOID param) {
    unsigned char id = *(unsigned char *)param;

    g_current_task = id;
    g_tasks[id].state = TASK_RUNNING;
    g_tasks[id].entry();

    TerminateTask();
    OS_Fail("Task fiber returned unexpectedly");
}

static void OS_EnsureFiber(unsigned char task_id) {
    if (!g_tasks[task_id].fiber) {
        g_task_id_param[task_id] = task_id;
        g_tasks[task_id].fiber = CreateFiber(0, OS_TaskFiberProc, &g_task_id_param[task_id]);
        if (!g_tasks[task_id].fiber) {
            OS_Fail("CreateFiber failed");
        }
    }
}

void OS_Yield(void) {
    if (!g_os_running) {
        return;
    }

    if (g_in_isr) {
        OS_Fail("OS_Yield forbidden in ISR");
    }

    if (g_current_task == INVALID_TASK) {
        OS_Fail("OS_Yield: no running task");
    }

    if (!g_kernel_fiber) {
        OS_Fail("OS_Yield: OS not initialized as fiber");
    }

    SwitchToFiber(g_kernel_fiber);
}

#else
void OS_Yield(void) {
    OS_Fail("OS_Yield not supported on this platform");
}
#endif

void OS_Dispatch(void) {
#ifdef _WIN32
    unsigned char next;
    unsigned char t;

    if (!g_kernel_fiber) {
        g_kernel_fiber = ConvertThreadToFiber(0);
        if (!g_kernel_fiber) {
            OS_Fail("ConvertThreadToFiber failed");
        }
    }

    while (g_os_running) {
        next = OS_SelectNextTask();
        if (next == INVALID_TASK) {
            ShutdownOS();
            return;
        }

        OS_EnsureFiber(next);

        g_current_task = next;
        g_tasks[next].state = TASK_RUNNING;

        SwitchToFiber(g_tasks[next].fiber);

        g_current_task = INVALID_TASK;

        if (g_task_to_delete != INVALID_TASK) {
            t = g_task_to_delete;
            g_task_to_delete = INVALID_TASK;
            if (g_tasks[t].fiber) {
                DeleteFiber(g_tasks[t].fiber);
                g_tasks[t].fiber = 0;
            }
        }
    }
#else
    {
        unsigned char next;
        while (g_os_running) {
            next = OS_SelectNextTask();
            if (next == INVALID_TASK) {
                ShutdownOS();
                return;
            }
            g_current_task = next;
            g_tasks[next].state = TASK_RUNNING;
            g_tasks[next].entry();
            OS_Fail("Task returned without TerminateTask");
        }
    }
#endif
}

void OS_ScheduleFromTask(void) {
    if (!g_os_running || g_in_isr) {
        return;
    }

    if (g_current_task == INVALID_TASK) {
        return;
    }

    if (OS_HasPreemptor(g_tasks[g_current_task].deadline)) {
        g_tasks[g_current_task].state = TASK_READY;
        OS_Yield();
        g_tasks[g_current_task].state = TASK_RUNNING;
    }
}

void OS_ScheduleFromISR(void) {
    if (!g_os_running) {
        return;
    }

    if (g_in_isr) {
        return;
    }

    if (g_current_task == INVALID_TASK) {
        return;
    }

    if (OS_HasPreemptor(g_tasks[g_current_task].deadline)) {
        g_tasks[g_current_task].state = TASK_READY;
        OS_Yield();
        g_tasks[g_current_task].state = TASK_RUNNING;
    }
}

void ActivateTask(TTask task) {
    unsigned char id;
    if (g_in_isr) {
        OS_Fail("ActivateTask forbidden in ISR");
    }

    if (!g_os_running) {
        OS_Fail("ActivateTask: OS is not running");
    }

    id = OS_TaskId(task);

    if (g_tasks[id].active) {
        OS_Fail("ActivateTask: duplicate activation");
    }

    g_tasks[id].active = 1;
    g_tasks[id].state = TASK_READY;
    g_tasks[id].waiting_sem = INVALID_SEM;
    g_tasks[id].waiting_active = 0;

    OS_ScheduleFromTask();
}

void ISRActivateTask(TTask task) {
    unsigned char id;
    if (!g_in_isr) {
        OS_Fail("ISRActivateTask only in ISR");
    }

    if (!g_os_running) {
        OS_Fail("ISRActivateTask: OS is not running");
    }

    id = OS_TaskId(task);

    if (g_tasks[id].active) {
        OS_Fail("ISRActivateTask: duplicate activation");
    }

    g_tasks[id].active = 1;
    g_tasks[id].state = TASK_READY;
    g_tasks[id].waiting_sem = INVALID_SEM;
    g_tasks[id].waiting_active = 0;
}

void TerminateTask(void) {
    unsigned char id;

    if (g_in_isr) {
        OS_Fail("TerminateTask forbidden in ISR");
    }

    if (g_current_task == INVALID_TASK) {
        OS_Fail("TerminateTask: no running task");
    }

    id = g_current_task;

    if (g_tasks[id].critical_count) {
        OS_Fail("TerminateTask in critical section forbidden");
    }

    g_tasks[id].state = TASK_SUSPENDED;
    g_tasks[id].active = 0;
    g_tasks[id].waiting_sem = INVALID_SEM;
    g_tasks[id].waiting_active = 0;

#ifdef _WIN32
    g_task_to_delete = id;
    OS_Yield();
#else
    g_current_task = INVALID_TASK;
    OS_ScheduleFromTask();
#endif

    OS_Fail("TerminateTask returned unexpectedly");
}

#ifdef _WIN32
void OS_ResetFibers(void) {
    unsigned char i;
    for (i = 0; i < g_task_count; ++i) {
        if (g_tasks[i].fiber) {
            DeleteFiber(g_tasks[i].fiber);
            g_tasks[i].fiber = 0;
        }
    }
    g_task_to_delete = INVALID_TASK;
}
#endif
