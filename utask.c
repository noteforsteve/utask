/*
 * uTask
 *
 */
#include <stdio.h>
#include <string.h>
#include "utask.h"

/* Documentation macros */
#define IN
#define OUT
#define OPTIONAL

/* Array size */
#define COUNTOF(a)          (sizeof(a)/sizeof(*(a)))

/* Keep compiler happy */
#define UNUSED_PARAM(p)     ((void)(p))

/* Quick and dirty debug macros */
#if UTASK_DEBUG

#define DBG_TRACE   1
#define DBG_WARN    2
#define DBG_ERROR   3

#define DBG_MSG(level, ...)\
    if (gDebug & level)\
        fprintf(stderr, __VA_ARGS__)

#else

#define DBG_MSG(level, ...) /* Empty */

#endif

/*
 * Time comparison macros, tick is the system tick count the
 * timeout parameter is the expiration timeout i.e. the tick
 * plus some time to wait.
 */
#define TIME_AFTER(tick, timeout)\
        ((int32)(timeout) - (int32)(tick) < 0)

#define TIME_BEFORE(tick, timeout)\
        ((int32)(tick) - (int32)(timeout) < 0)

#define TIME_AFTER_EQ(tick, timeout)\
        ((int32)(tick) - (int32)(timeout) >= 0)

#define TIME_BEFORE_EQ(tick, timeout)\
        ((int32)(timeout) - (int32)(tick) >= 0)

#define CORE_FLAGS_INIT     (1 << 0)
#define CORE_FLAGS_SHUTDOWN (1 << 1)

#define TCB_FLAGS_APP       (1 << 0)
#define TCB_FLAGS_ISR       (1 << 1)

/*****************************************************************************/
/*
 * Example usage:
 *
 * #define Q_SIZE 10
 *
 * struct
 * {
 *     queue_hdr_t hdr;                     // must be named "hdr"
 *     my_type_t     items[Q_SIZE+1];       // must be named "items", 1 space wasted
 * } my_q;
 *
 * my_type_t an_item;
 *
 * QUEUE_INIT(my_q);
 *
 * if (!QUEUE_FULL(q))
 * {
 *    QUEUE_PUT(my_q,an_item);
 * }
 *
 * if (!QUEUE_EMPTY(q))
 * {
 *    QUEUE_GET(my_q,an_item);
 * }
 *
 */
#define QUEUE_INIT(q)\
    q.hdr.front = q.hdr.rear = 0;\
    q.hdr.size =  sizeof(q.items)/sizeof(q.items[0])

#define QUEUE_PUT(q, item)\
    q.items[q.hdr.rear] = item;\
    q.hdr.rear = (q.hdr.rear+1) % q.hdr.size

#define QUEUE_GET(q, item)\
    item = q.items[q.hdr.front];\
    q.hdr.front = (q.hdr.front + 1) % q.hdr.size

#define QUEUE_FRONT(q, item)\
    item = q.items[q.hdr.front]

#define QUEUE_EMPTY(q)\
    (q.hdr.front == q.hdr.rear)

#define QUEUE_FULL(q)\
    ((q.hdr.rear + 1) % q.hdr.size == q.hdr.front)

/* Private - do not access directly - use above macros */
typedef struct
{
    int front;
    int rear;
    int size;
} queue_hdr_t;

/*****************************************************************************/

typedef char            int8;
typedef unsigned char   uint8;
typedef short           int16;
typedef unsigned short  uint16;
typedef long            int32;
typedef unsigned long   uint32;
typedef unsigned int    uint;

typedef struct Tcb_T
{
    struct Tcb_T        *pNext;
    struct Tcb_T        *pPrev;
    uint16              Flags;
    uTask_T             *pTask;
    int                 Id;
    void                *pMsg;
    uint32              Expire;
} Tcb_T;

typedef struct
{
    queue_hdr_t         hdr;
    Tcb_T               items[UTASK_ISR_QUEUE_SIZE+1];
} IsrQ_T;

typedef struct
{
    uint16              Flags;
    uint32              Tick;
    Tcb_T               *pFree;
    Tcb_T               *pHead;
    Tcb_T               *pTail;
    IsrQ_T              IsrQ;
    Tcb_T               Tcb[UTASK_TCB_SLOTS];
} uTaskCore_T;

/******************************************************************************/

void
TcbInit(
    void
    );

Tcb_T *
TcbAlloc(
    void
    );

void
TcbFree(
    IN Tcb_T *pTcb
    );

Tcb_T *
TcbFront(
    void
    );

void
TcbEnqueue(
    IN Tcb_T *pTcb
    );

Tcb_T *
TcbDequeue(
    void
    );

/******************************************************************************/

void
PoolInit(
    void
    );

void *
PoolAlloc(
    uint uSize
    );

void
PoolFree(
    void *pMem
    );

/******************************************************************************/

#if UTASK_DEBUG
static uint16 gDebug = {DBG_TRACE|DBG_WARN|DBG_ERROR};
#endif

static uTaskCore_T gCore;

/******************************************************************************/

int
uTaskCtor(
    void
    )
{
    DBG_MSG(DBG_TRACE, "%s\n", __FUNCTION__);

    memset(&gCore, 0, sizeof(gCore));

    QUEUE_INIT(gCore.IsrQ);

    TcbInit();

    PoolInit();

    gCore.Flags = CORE_FLAGS_INIT;

    return UTASK_S_OK;
}

void
uTaskDtor(
    void
    )
{
    DBG_MSG(DBG_TRACE, "%s\n", __FUNCTION__);

    gCore.Flags = gCore.Flags | CORE_FLAGS_SHUTDOWN;
}

void
uTaskTick(
    void
    )
{
    int PrevState = uTaskInterruptDisable();
    gCore.Tick++;
    uTaskInterruptRestore(PrevState);
}

unsigned long
uTaskGetTick(
    void
    )
{
    return gCore.Tick;
}

int
uTaskMessageSend(
    IN uTask_T          *pTask,
    IN int              Id,
    IN void             *pMsg,
    IN unsigned long    Time
    )
{
    Tcb_T *pTcb;

    /* Valid task and handler must be provided */
    if (pTask && pTask->Handler)
    {
        pTcb = TcbAlloc();

        if (pTcb)
        {
            pTcb->Flags     = TCB_FLAGS_APP;
            pTcb->pTask     = pTask;
            pTcb->Id        = Id;
            pTcb->pMsg      = pMsg;
            pTcb->Expire    = Time + uTaskGetTick();

            TcbEnqueue(pTcb);

            return UTASK_S_OK;
        }
        else
        {
            DBG_MSG(DBG_ERROR, "Tcb exhaustion\n");
        }
    }

    return UTASK_E_FAIL;
}

int
uTaskMessageSendIsr(
    IN uTask_T          *pTask,
    IN int              Id,
    IN void             *pData
    )
{
    /* The task and handler must be valid */
    if (pTask && pTask->Handler)
    {
        if (!QUEUE_FULL(gCore.IsrQ))
        {
            Tcb_T Tcb;

            Tcb.Flags   = TCB_FLAGS_ISR;
            Tcb.pTask   = pTask;
            Tcb.Id      = Id;
            Tcb.pMsg    = pData;
            Tcb.Expire  = uTaskGetTick();

            QUEUE_PUT(gCore.IsrQ, Tcb);

            return UTASK_S_OK;
        }
    }

    return UTASK_E_FAIL;
}

void
uTaskMessageLoop(
    void
    )
{
    Tcb_T *pTcb;

    if (!gCore.Flags & CORE_FLAGS_INIT)
    {
        return;
    }

    for ( ; ; )
    {
        /* Has a shutdown request occurred */
        if (gCore.Flags & CORE_FLAGS_SHUTDOWN)
        {
            DBG_MSG(DBG_WARN, "Shutdown request\n");
            break;
        }

        /* If the are any isr queue items, move them into tcb queue */
        if (!QUEUE_EMPTY(gCore.IsrQ))
        {
            pTcb = TcbAlloc();

            if (pTcb)
            {
                QUEUE_GET(gCore.IsrQ, *pTcb);

                TcbEnqueue(pTcb);
            }
        }

        pTcb = TcbFront();

        if (pTcb)
        {
            /* Has pTcb expired */
            if (TIME_AFTER_EQ(uTaskGetTick(), pTcb->Expire))
            {
                pTcb = TcbDequeue();

                DBG_MSG(DBG_TRACE, "Delay(%ld) Task %p Id %d pMsg %p\n",
                                   uTaskGetTick()-pTcb->Expire,
                                   pTcb->pTask,
                                   pTcb->Id,
                                   pTcb->pMsg);

                /* Send the message to the task */
                pTcb->pTask->Handler(pTcb->pTask, pTcb->Id, pTcb->pMsg);

                /* Free the message structure */
                uTaskFree(pTcb->pMsg);

                TcbFree(pTcb);
            }
        }
    }
}

int
uTaskMessageCancel(
    IN uTask_T          *pTask,
    IN int              Id
    )
{
    int i = 0;
    Tcb_T *pEntry;
    Tcb_T *pTemp;

    /* Traverse the queue */
    for (pEntry = gCore.pHead; pEntry; )
    {
        pTemp = pEntry;
        pEntry = pEntry->pNext;

        if (pTemp->pTask == pTask && pTemp->Id == Id)
        {
            /* Count the number of cancelled items */
            i = i + 1;

            /* Entry found only one Tcb in queue */
            if (pTemp == gCore.pHead && pTemp == gCore.pTail)
            {
                gCore.pHead = NULL;
                gCore.pTail = NULL;
            }
            /* Entry found at head of the queue */
            else if (pTemp == gCore.pHead)
            {
                gCore.pHead = gCore.pHead->pNext;
                gCore.pHead->pPrev = NULL;
            }
            /* Entry found at tail of the queue */
            else if (pTemp == gCore.pTail)
            {
                gCore.pTail = gCore.pTail->pPrev;
                gCore.pTail->pNext = NULL;
            }
            /* Entry found in the middle of the queue */
            else
            {
                pTemp->pNext->pPrev = pTemp->pPrev;
                pTemp->pPrev->pNext = pTemp->pNext;
            }

            TcbFree(pTemp);
        }
    }

    return i;
}

void *
uTaskAlloc(
    IN int              uSize
    )
{
    void *p;

    /* Disable interrupts, allowing pool allocs during isr execution */
    int PrevState = uTaskInterruptDisable();

    /* Allocate the pool block, note this is not your normal alloc */
    p = PoolAlloc(uSize);

    /* Restore the interrupt state */
    uTaskInterruptRestore(PrevState);

    return p;
}

void
uTaskFree(
    IN void             *pMem
    )
{
    /* Disable interrupts, allowing pool frees during isr execution */
    int PrevState = uTaskInterruptDisable();
   /* 
    * Release the pool block, caution if in an isr pool over write 
    * detection cannot print if executing in isr context.
    */
    PoolFree(pMem);

    /* Restore the interrupt state */
    uTaskInterruptRestore(PrevState);
}

/******************************************************************************/

void
TcbInit(
    void
    )
{
    int i;
    Tcb_T *p;

    p = gCore.Tcb;

    /* Create Tcb free list */
    for (i = 0; i < COUNTOF(gCore.Tcb); i = i + 1)
    {
        TcbFree(p);
        p = p + 1;
    }
}

Tcb_T *
TcbAlloc(
    void
    )
{
    Tcb_T *pTcb;

    /* Remove item from head of free list */
    pTcb = gCore.pFree;
    gCore.pFree = gCore.pFree->pNext;
    pTcb->pNext = NULL;
    return pTcb;
}

void
TcbFree(
    IN Tcb_T *pTcb
    )
{
    /* Add item to head of free list */
    pTcb->pNext = gCore.pFree;
    gCore.pFree = pTcb;
    pTcb->pPrev = NULL;
}

/* Items enter at the tail head and leave at the head */
Tcb_T *
TcbFront(
    void
    )
{
    /* Return item at head */
    return gCore.pHead;
}

/* Add at head */
void
TcbEnqueue(
    IN Tcb_T *pTcb
    )
{
    Tcb_T *pEntry;

    /* The queue is empty update head and tail */
    if (gCore.pHead == NULL && gCore.pTail == NULL)
    {
        gCore.pHead = pTcb;
        gCore.pTail = pTcb;
        pTcb->pNext = NULL;
        pTcb->pPrev = NULL;
        return;
    }

    /* Traverse the queue */
    for (pEntry = gCore.pHead; pEntry; pEntry = pEntry->pNext)
    {
        /* Is the current pEntry after the new pTcb entry */
        if (TIME_AFTER(pEntry->Expire, pTcb->Expire))
        {
            /* Insert new pTcb entry before current pEntry */
            if (pEntry->pPrev)
            {
                pEntry->pPrev->pNext = pTcb;
                pTcb->pPrev = pEntry->pPrev;
                pEntry->pPrev = pTcb;
                pTcb->pNext = pEntry;
            }
            else
            {
                pEntry->pPrev = pTcb;
                pTcb->pNext = pEntry;
                pTcb->pPrev = NULL;
                gCore.pHead = pTcb;
            }
            return;
        }
    }

    /* The queue end was found, insert at tail */
    gCore.pTail->pNext = pTcb;
    pTcb->pPrev = gCore.pTail;
    gCore.pTail = pTcb;
    pTcb->pNext = NULL;
    return;
}

/* Remove at tail */
Tcb_T *
TcbDequeue(
    void
    )
{
    Tcb_T *pTcb;

    /* Case 1, Head and Tail point to nothing */
    if (gCore.pHead == NULL && gCore.pTail == NULL)
    {
        pTcb = NULL;
    }

    /* Case 2, Head and Tail point to same element */
    else if (gCore.pHead == gCore.pTail)
    {
        pTcb = gCore.pHead;
        gCore.pHead = NULL;
        gCore.pTail = NULL;
    }

    /* Case 3, Head and Tail point to different elements */
    else
    {
        pTcb = gCore.pHead;
        gCore.pHead = gCore.pHead->pNext;
        gCore.pHead->pPrev = NULL;
    }

    return pTcb;
}

/******************************************************************************/

/* If the sum of all block counts are zero we disable the pool */
#if ((UTASK_POOL_COUNT1+UTASK_POOL_COUNT2+\
      UTASK_POOL_COUNT3+UTASK_POOL_COUNT4) == 0)
#undef UTASK_POOL_USE
#endif

#if UTASK_POOL_USE

#if UTASK_DEBUG && UTASK_POOL_DEBUG

/*
 * For overwrite detection we increase the block size to include the
 * beginning signature, the actual size of the block and the end
 * signature.  The size of the block is needed because the caller could
 * request a block of 5 bytes, but the only block avaiable is a 64 byte
 * block.  If this happens we need the requested size to ensure the caller
 * is not writing passed the requested size, not just writing passed the
 * data avilable in the block.
 */
#define UTASK_POOL_SIG_SIZE     (sizeof(uint16)+sizeof(uint16)+sizeof(uint))
#define UTASK_POOL_SIG_BEG      0xDEAD
#define UTASK_POOL_SIG_END      0xFFED
#define UTASK_POOL_SIG_EMPTY    0xEE

#else

#define UTASK_POOL_SIG_SIZE     0

#endif

#define UTASK_POOL_UP(n)\
    ((((n)+(sizeof(PoolBlock_T) - 1)) & ~(sizeof(PoolBlock_T)-1))+UTASK_POOL_SIG_SIZE)

typedef struct PoolBlock_T
{
	struct PoolBlock_T	*pNext;
} PoolBlock_T;

typedef struct
{
    uint                uCount;
    uint				uSize;
    void                *pBeg;
    PoolBlock_T         *pHead;
} PoolHead_T;

static uint8 gPoolMem
[
#if UTASK_POOL_COUNT1
    (UTASK_POOL_COUNT1*UTASK_POOL_UP(UTASK_POOL_SIZE1)) +
#endif
#if UTASK_POOL_COUNT2
    (UTASK_POOL_COUNT2*UTASK_POOL_UP(UTASK_POOL_SIZE2)) +
#endif
#if UTASK_POOL_COUNT4
    (UTASK_POOL_COUNT3*UTASK_POOL_UP(UTASK_POOL_SIZE3)) +
#endif
#if UTASK_POOL_COUNT4
    (UTASK_POOL_COUNT4*UTASK_POOL_UP(UTASK_POOL_SIZE4)) +
#endif
    0
];

static PoolHead_T gPool[] =
{
#if UTASK_POOL_COUNT1
    {UTASK_POOL_COUNT1, UTASK_POOL_SIZE1, NULL, NULL},
#endif
#if UTASK_POOL_COUNT2
    {UTASK_POOL_COUNT2, UTASK_POOL_SIZE2, NULL, NULL},
#endif
#if UTASK_POOL_COUNT3
    {UTASK_POOL_COUNT3, UTASK_POOL_SIZE3, NULL, NULL},
#endif
#if UTASK_POOL_COUNT4
    {UTASK_POOL_COUNT4, UTASK_POOL_SIZE4, NULL, NULL},
#endif
};

void
PoolInit(
    void
    )
{
    int i;
    int j;
    uint8 *p;
    PoolHead_T Temp;

    DBG_MSG(DBG_TRACE, "%s\n", __FUNCTION__);

    /* Sort the pool blocks into accending sizes */
    for (i = COUNTOF(gPool) - 2; i >= 0; i = i - 1)
    {
        for (j = 0; j <= i; j = j + 1)
        {
            if (gPool[j].uSize > gPool[j+1].uSize)
            {
                Temp = gPool[j];
                gPool[j] = gPool[j+1];
                gPool[j+1] = Temp;
            }
        }
    }

    /* Build the pool free blocks lists */
    p = gPoolMem;

    for (i = 0; i < COUNTOF(gPool); i = i + 1)
    {
        if (gPool[i].uCount)
        {
            gPool[i].pBeg = p;

            for (j = 0; j < (int)gPool[i].uCount; j = j + 1)
            {
                ((PoolBlock_T *)p)->pNext = gPool[i].pHead;
                gPool[i].pHead = (PoolBlock_T *)p;
                p = p + UTASK_POOL_UP(gPool[i].uSize);
            }
        }
    }
}

void *
PoolAlloc(
    uint uSize
    )
{
    uint i;
    void *p = NULL;

    for (i = 0; i < COUNTOF(gPool); i = i + 1)
    {
        if (uSize <= gPool[i].uSize)
        {
            if (gPool[i].pHead)
            {
                p = gPool[i].pHead;
                gPool[i].pHead = gPool[i].pHead->pNext;

#if UTASK_DEBUG && UTASK_POOL_DEBUG
		        /* Set the alloc size and beg and end signatures */
		        *(uint *)p = uSize;
                *(uint16 *)((uint8 *)p+sizeof(uint)) = UTASK_POOL_SIG_BEG;
                p = (uint8 *)p + sizeof(uint16) + sizeof(uint);
                memset(p, UTASK_POOL_SIG_EMPTY, uSize);
                *(uint16 *)((uint8 *)p + uSize) = UTASK_POOL_SIG_END;
#endif
            }
            break;
        }
    }

    return p;
}

void
PoolFree(
    void *pMem
    )
{
    uint8 *p = pMem;
    uint i;

    /* Is this memory block in the pool */
    if (p >= (uint8 *)gPoolMem &&
        p <= ((uint8 *)gPoolMem + sizeof(gPoolMem)))
    {
        for (i = 0; i < COUNTOF(gPool); i = i + 1)
        {
            if (p >= (uint8 *)gPool[i].pBeg &&
                p <= ((uint8 *)gPool[i].pBeg + 
                     (gPool[i].uCount * UTASK_POOL_UP(gPool[i].uSize))))
            {
#if UTASK_DEBUG && UTASK_POOL_DEBUG
				uint uSize = *(uint *)((uint8 *)p - 
                             (sizeof(uint) + sizeof(uint16)));

				/* Validate the memory size */
				if (uSize > gPool[i].uSize)
				{
                    /* Size overwrite */
                    DBG_MSG(DBG_WARN, "Pool block %p size out of range\n", p);
				}

                if (*(uint16 *)((uint8 *)p - sizeof(uint16)) != UTASK_POOL_SIG_BEG)
                {
                    /* Beginning signature overwrite */
                    DBG_MSG(DBG_WARN, "Pool block %p beg signature overwrite\n", p);
                }

                if (*(uint16 *)((uint8 *)p + uSize) != UTASK_POOL_SIG_END)
                {
                    /* Ending signature overwrite */
                    DBG_MSG(DBG_WARN, "Pool block %p end signature overwrite\n", p);
                }

                p = (uint8 *)p - (sizeof(uint16) + sizeof(uint));
#endif
                ((PoolBlock_T *)p)->pNext = gPool[i].pHead;
                gPool[i].pHead = (PoolBlock_T *)p;

                break;
            }
        }
    }
}

#else

void
PoolInit(
	void
	)
{
}

void *
PoolAlloc(
    uint uSize
    )
{
    (void)uSize;
    return NULL;
}

void
PoolFree(
    void *pMem
    )
{
    (void)pMem;
}

#endif
