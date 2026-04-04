#include "rtos_api.h"

void InitPVS(TSemaphore s) {
    if (s >= MAX_RESOURCES) {
        OS_Fail("InitPVS: invalid semaphore id");
    }

    g_sems[s].locked = 0;
    g_sems[s].owner = INVALID_TASK;
}

void P(TSemaphore s) {
    if (s >= MAX_RESOURCES) {
        OS_Fail("P: invalid semaphore id");
    }

    if (g_in_isr) {
        OS_Fail("P forbidden in ISR");
    }

    if (g_current_task == INVALID_TASK) {
        OS_Fail("P: no running task");
    }

    while (1) {
        if (g_sems[s].locked && g_sems[s].owner == g_current_task) {
            OS_Fail("P: nested lock of same semaphore forbidden");
        }

        if (!g_sems[s].locked) {
            g_sems[s].locked = 1;
            g_sems[s].owner = g_current_task;
            g_tasks[g_current_task].critical_count++;
            g_tasks[g_current_task].waiting_sem = INVALID_SEM;
            g_tasks[g_current_task].waiting_active = 0;
            return;
        }

        g_tasks[g_current_task].state = TASK_WAITING_SEM;
        g_tasks[g_current_task].waiting_sem = s;
        g_tasks[g_current_task].waiting_active = 1;

        OS_Yield();

        g_tasks[g_current_task].state = TASK_RUNNING;
    }
}

void V(TSemaphore s) {
    unsigned char i;
    unsigned char awakened = INVALID_TASK;

    if (s >= MAX_RESOURCES) {
        OS_Fail("V: invalid semaphore id");
    }

    if (g_in_isr) {
        OS_Fail("V forbidden in ISR");
    }

    if (g_current_task == INVALID_TASK) {
        OS_Fail("V: no running task");
    }

    if (!g_sems[s].locked) {
        OS_Fail("V: semaphore not locked");
    }

    if (g_sems[s].owner != g_current_task) {
        OS_Fail("V: only owner can unlock semaphore");
    }

    if (!g_tasks[g_current_task].critical_count) {
        OS_Fail("V: internal critical counter underflow");
    }

    g_tasks[g_current_task].critical_count--;
    g_sems[s].locked = 0;
    g_sems[s].owner = INVALID_TASK;

    for (i = 0; i < g_task_count; ++i) {
        if (g_tasks[i].active &&
            g_tasks[i].state == TASK_WAITING_SEM &&
            g_tasks[i].waiting_active &&
            g_tasks[i].waiting_sem == s) {
            if (awakened == INVALID_TASK ||
                g_tasks[i].deadline < g_tasks[awakened].deadline) {
                awakened = i;
            }
        }
    }

    if (awakened == INVALID_TASK) {
        /* nothing to wake */
    } else {
        g_tasks[awakened].state = TASK_READY;
    }

    OS_ScheduleFromTask();
}
