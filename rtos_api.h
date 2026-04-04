#ifndef RTOS_API_H
#define RTOS_API_H

#define MAX_TASKS 32
#define MAX_RESOURCES 16
#define INVALID_TASK 255
#define INVALID_SEM 255

typedef unsigned char TSemaphore;
typedef unsigned int  TDeadline;

/* ===== Task definitions (API spec style) ===== */
typedef void (*TOSTaskEntry)(void);

typedef struct {
    TOSTaskEntry entry;
    TDeadline deadline;
} TTaskStruct;

typedef const TTaskStruct *TTask;

/* Пользовательские макросы API */
#define DeclareTask(TaskID) extern const TTaskStruct TaskID[1]
#define TASK(TaskID, Deadline)            \
    void TaskID##body(void);              \
    const TTaskStruct TaskID[1] =         \
        { { TaskID##body, (TDeadline)(Deadline) } }; \
    void TaskID##body(void)
#define ISR(IsrID) void IsrID(void)

/* Внутренние типы ОС */
typedef enum {
    TASK_SUSPENDED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING_SEM
} TTaskState;

typedef struct {
    void (*entry)(void);
    TDeadline deadline;
    TTaskState state;
    unsigned char active;
    unsigned char waiting_sem;
    unsigned char waiting_active;
    unsigned char critical_count;
#ifdef _WIN32
    void *fiber;
#endif
} TTaskControlBlock;

typedef struct {
    unsigned char locked;
    unsigned char owner;
} TSemaphoreControlBlock;

/* Глобальные данные */
extern TTaskControlBlock g_tasks[MAX_TASKS];
extern TSemaphoreControlBlock g_sems[MAX_RESOURCES];

extern unsigned char g_task_count;
extern unsigned char g_current_task;
extern unsigned char g_os_running;
extern unsigned char g_in_isr;
extern void (*g_fail_handler)(const char *msg);

/* Внутренние функции */
unsigned char OS_TaskId(TTask task);
void OS_Dispatch(void);
void OS_ScheduleFromTask(void);
void OS_ScheduleFromISR(void);
void OS_Yield(void);
void OS_Fail(const char *msg);
#ifdef _WIN32
void OS_ResetFibers(void);
#endif

/* API задачи */
void ActivateTask(TTask task);
void ISRActivateTask(TTask task);
void TerminateTask(void);

/* API ISR */
void EnterISR(void);
void LeaveISR(void);

/* API семафоров */
void InitPVS(TSemaphore s);
void P(TSemaphore s);
void V(TSemaphore s);

/* API ОС */
void StartOS(TTask task);
void ShutdownOS(void);

#endif
