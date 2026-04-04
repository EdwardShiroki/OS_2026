#include "rtos_api.h"

TTaskControlBlock g_tasks[MAX_TASKS];
TSemaphoreControlBlock g_sems[MAX_RESOURCES];

unsigned char g_task_count = 0;
unsigned char g_current_task = INVALID_TASK;
unsigned char g_os_running = 0;
unsigned char g_in_isr = 0;
void (*g_fail_handler)(const char *msg) = 0;
