/*
 * uTask
 *
 * Description:
 * uTask is a small 'C' based single header and source file message based tasker.
 * It is aimed at small embedded controllers, where a formal OS or RTOS is not
 * required but the developer would like to have more structure in the code
 * opposed to a simple for-ground / back-ground type infinite loop construct.
 *
 * Goals:
 * Small - in both memory and code space utilization.
 * Easy - there are few api's and most are very simple to use
 * Growth - can be grown and used into a larger solution
 * Portable - written is portable 'C'
 *
 * Example Usage: (blinky)
 *
 * #include <stdio.h>
 * #include "utask.h"
 *
 * void AppHandler(uTask_T *pTask, int Id, void *pMsg);
 *
 * static uTask_T gAppTask = {AppHandler};
 *
 * // Platform specific - timer isr to update uTask internal timer
 * void PlatformTickISR(void)
 * {
 *     uTaskTick();
 * }
 *
 * // Platform specific - disable interrupts and return current interrupt state
 * int uTaskInterruptDisable(void)
 * {
 *     return INTDisableInterrupts();
 * }
 *
 * // Platform specific - restore previous interrupt state
 * void uTaskInterruptRestore(int PrevIntState)
 * {
 *     INTRestoreInterrupts(PrevIntState);
 * }
 *
 * void AppHandler(uTask_T *pTask, int Id, void *pMsg)
 * {
 *     switch(Id)
 *     {
 *     case 0:
 *         // Send message in 1 second
 *         uTaskMessageSend(pTask, 1, NULL, UTASK_SEC(1));
 *         break;
 *
 *     case 1:
 *         // Send message in 2 seconds
 *         uTaskMessageSend(pTask, 0, NULL, UTASK_SEC(2));
 *         break;
 *     }
 * }
 *
 * int main(int ac, char **av)
 * {
 *     // Initialize platform timer to call uTaskTick every 1 ms
 *     PlatformCtor();
 *
 *     // Start the utask
 *     uTaskCtor();
 *
 *     // Send initial message gets thing started
 *     uTaskMessageSend(&gAppTask, 0, NULL, UTASK_IMMEDIATE);
 *
 *     // This call processes messages and does not return
 *     uTaskMessageLoop();
 *
 * }
 *
 * Todo(s):
 * - Write document on uTask usage
 * - Explain why the ISR queue exists and how to use it correctly
 *
 */
#ifndef UTASK_H
#define UTASK_H

/* Set this to a 1 to enable debug support */
#define UTASK_DEBUG             0

/*
 * A TCB is a task control block.  They are used by uTask to track and queue
 * task requests.  The number of UTASK_TCB_SLOTS is the number of entries in 
 * the task queue.  If you plan on having a large number of outstanding task 
 * messages you should increase this number. 
 */
#define UTASK_TCB_SLOTS         32

/*
 * UTASK_ISR_QUEUE_SIZE is the number of outstanding ISR entries that can be
 * queued up.  ISR queue entries are added using the function
 * uTaskMessageSendISR from isr context.
 */
#define UTASK_ISR_QUEUE_SIZE    8

/* Time macros */
#define UTASK_TICKS_PER_SEC     1000
#define UTASK_IMMEDIATE         0
#define UTASK_SEC(s)            ((s)*UTASK_TICKS_PER_SEC)
#define UTASK_MIN(m)            ((m)*60*UTASK_TICKS_PER_SEC)
#define UTASK_HOUR(h)           ((h)*60*60*UTASK_TICKS_PER_SEC)

/* Error values */
#define UTASK_S_OK              0
#define UTASK_E_FAIL            -1

/*
 * Memory pool is a fix block allocator.  Currently 4 memory
 * slots are supported, use the below #define to set the sizes of
 * memory pool slots and the count of each block that size.
 *
 */

/*
 * Set to 1 to use the memory pool, set to 0 to exclude memory
 * pool code and use.
 */
#define UTASK_POOL_USE          1

/*
 * Set to 1 to make the memory pool safe to use in isr context,
 * set to 0 to only use memory pool code in task context
 */
#define UTASK_POLL_ISR_SAFE     1

/*
 * Set to 1 to enabled pool block head and tail checking.  It will
 * display debug message if memory block experienced an under or
 * overwrite.
 */
#define UTASK_POOL_DEBUG        1

/*
 * Set memory pool count and size of each fixed pool.  Note the size order
 * is not a requirement, the code will sort memory pools into size order
 * to aid in lookup efficiency.
 */
#define UTASK_POOL_COUNT1       16
#define UTASK_POOL_SIZE1        8
#define UTASK_POOL_COUNT2       8
#define UTASK_POOL_SIZE2        16
#define UTASK_POOL_COUNT3       4
#define UTASK_POOL_SIZE3        32
#define UTASK_POOL_COUNT4       2
#define UTASK_POOL_SIZE4        64

/* Types used by uTask */
struct uTask_T;

/* This is the task call back function prototype */
typedef void (*pfuTask)(
    struct uTask_T  *pTask,
    int             Id,
    void            *pMsg
    );

/* uTask is a structure with only a handler */
typedef struct uTask_T
{
    pfuTask Handler;
} uTask_T;

/*
 * PORT function, must be !!implemented!!
 *
 * This function should hold a system wide lock, it usually just
 * disables interrupts if running on a uni-processor system / bare
 * bone type of embedded system.
 */
int
uTaskInterruptDisable(
    void
    );

/*
 * PORT function, must be !!implemented!!
 *
 * This function should restore the interrupt state saved in the
 * PrevInState parameter.  By saving and restoring the interrupt
 * state this will allow proper interrupt disable / enable nesting.
 */
void
uTaskInterruptRestore(
    int PrevIntState
    );

/************************* uTask primary api's ********************************/

/*
 * This function initializes the internal data structures for uTask.  It
 * must be called before all other uTask functions, including the uTaskTick
 * routine.
 */
int
uTaskCtor(
    void
    );

/*
 * Call to terminate the uTasker, this function will cause the uTaskMessageLoop
 * to exit and return.  It is here for completeness, not usually called in most
 * systems.
 */
void
uTaskDtor(
    void
    );

/*
 * Must be called by external code on a regular basis.  It is used by
 * uTask for time keeping purposes.
 */
void
uTaskTick(
    void
    );

/*
 * Returns the current system tick as maintained by uTask.  This call is safe
 * to call from both Task and ISR context.
 */
unsigned long
uTaskGetTick(
    void
    );

/*
 * Called after uTaskCtor has returned.  This routine is the main message
 * pump for uTask.  It does not return, unless uTaskDtor is called.
 */
void
uTaskMessageLoop(
    void
    );

/*
 * Send a task message, with delay, this function should only be
 * called from task context, opposed to ISR context.
 */
int
uTaskMessageSend(
    uTask_T         *pTask,
    int             Id,
    void            *pMsg,
    unsigned long   Time
    );

/*
 * Use this function if you are in ISR content and want to send a message to a
 * task.  Drivers typically use this function.  It enforces locking on critical
 * data structures in the core.
 */
int
uTaskMessageSendIsr(
    uTask_T         *pTask,
    int             Id,
    void            *pData
    );

/*
 * Cancels all messages in the queue that match this task and id.  This function
 * cannot be called from ISR context and it will not cancel messages sent
 * using the function uTaskMessageSendIsr
 */
int
uTaskMessageCancel(
    uTask_T         *pTask,
    int             Id
    );

/*
 * Allocate a memory block from the fix block pool.  Memory allocated from the
 * fixed block pool are freed automatically when passed as an argument to
 * uTaskMessageSend.  Fails if uSize is zero or there are no available memory
 * blocks that meet the requested size.
 */
void *
uTaskAlloc(
    int              uSize
    );

/*
 * Free a memory block that was allocated using the function uTaskAlloc.  This
 * call should not be called on message blocks that were passed as an argument
 * to uTaskMessageSend.  pMem can be NULL
 */
void
uTaskFree(
    void             *pMem
    );

#endif


