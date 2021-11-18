#include "HAL9000.h"
#include "thread_internal.h"
#include "mutex.h"

#define MUTEX_MAX_RECURSIVITY_DEPTH         MAX_BYTE

_No_competing_thread_
void
MutexInit(
    OUT         PMUTEX      Mutex,
    IN          BOOLEAN     Recursive
    )
{
    ASSERT( NULL != Mutex );

    memzero(Mutex, sizeof(MUTEX));

    LockInit(&Mutex->MutexLock);

    InitializeListHead(&Mutex->WaitingList);

    Mutex->MaxRecursivityDepth = Recursive ? MUTEX_MAX_RECURSIVITY_DEPTH : 1;
}

ACQUIRES_EXCL_AND_REENTRANT_LOCK(*Mutex)
REQUIRES_NOT_HELD_LOCK(*Mutex)
void
MutexAcquire(
    INOUT       PMUTEX      Mutex
    )

    //////// CHANGE STUFF HERE
    /////// CALL ThreadDonatePriority
{
    INTR_STATE dummyState;
    INTR_STATE oldState;
    PTHREAD pCurrentThread = GetCurrentThread();

    THREAD_PRIORITY crtThPrio; ////////////
    THREAD_PRIORITY holderPrio; ///////////

    ASSERT( NULL != Mutex);
    ASSERT( NULL != pCurrentThread );

    if (pCurrentThread == Mutex->Holder)
    {
        ASSERT( Mutex->CurrentRecursivityDepth < Mutex->MaxRecursivityDepth );

        Mutex->CurrentRecursivityDepth++;
        return;
    }

    oldState = CpuIntrDisable();

    LockAcquire(&Mutex->MutexLock, &dummyState );
    if (NULL == Mutex->Holder)
    {
        Mutex->Holder = pCurrentThread;
        Mutex->CurrentRecursivityDepth = 1;
    }

    while (Mutex->Holder != pCurrentThread)
    {
        InsertTailList(&Mutex->WaitingList, &pCurrentThread->ReadyList);
        ThreadTakeBlockLock();
        LockRelease(&Mutex->MutexLock, dummyState);
        pCurrentThread->WaitedMutex = Mutex;//////////
        ThreadBlock();
        LockAcquire(&Mutex->MutexLock, &dummyState );

        //crtThPrio = ThreadGetPriority(pCurrentThread); ////////////
        //holderPrio = ThreadGetPriority(Mutex->Holder); ///////////

        ThreadDonatePriority(pCurrentThread, Mutex->Holder);

    }

    InsertHeadList(&pCurrentThread->AcquiredMutexesList, &Mutex->AcquiredMutexListElem); /////

    _Analysis_assume_lock_acquired_(*Mutex);

    LockRelease(&Mutex->MutexLock, dummyState);

    CpuIntrSetState(oldState);
}

RELEASES_EXCL_AND_REENTRANT_LOCK(*Mutex)
REQUIRES_EXCL_LOCK(*Mutex)
void
MutexRelease(
    INOUT       PMUTEX      Mutex
    )

    ////////// CHANGE STUFF HERE
    ////////// CALL ThreadRecomputePriority
{
    INTR_STATE oldState;
    PLIST_ENTRY pEntry;

    ASSERT(NULL != Mutex);
    ASSERT(GetCurrentThread() == Mutex->Holder);

    if (Mutex->CurrentRecursivityDepth > 1)
    {
        Mutex->CurrentRecursivityDepth--;
        return;
    }

    pEntry = NULL;

    if (Mutex->Holder->Priority != Mutex->Holder->RealPriority)
    {
        Mutex->Holder->Priority = Mutex->Holder->RealPriority;
    } ///////////////

   // ThreadRecomputePriority(Mutex->Holder);

    LockAcquire(&Mutex->MutexLock, &oldState);

    pEntry = RemoveHeadList(&Mutex->WaitingList);
    if (pEntry != &Mutex->WaitingList)
    {
        PTHREAD pThread = CONTAINING_RECORD(pEntry, THREAD, ReadyList);

        // wakeup first thread
        Mutex->Holder = pThread;
        Mutex->CurrentRecursivityDepth = 1;
        ThreadUnblock(pThread);
    }
    else
    {
        Mutex->Holder = NULL;
    }

    RemoveEntryList(&Mutex->Holder->AcquiredMutexesList, &Mutex->AcquiredMutexListElem); ///////////////

    _Analysis_assume_lock_released_(*Mutex);

    LockRelease(&Mutex->MutexLock, oldState);
}