/*
 * Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include <DPC.h>

#include <util/AutoLock.h>


#define NORMAL_PRIORITY		B_NORMAL_PRIORITY
#define HIGH_PRIORITY		B_URGENT_DISPLAY_PRIORITY
#define REAL_TIME_PRIORITY	B_FIRST_REAL_TIME_PRIORITY

#define DEFAULT_QUEUE_SLOT_COUNT	64


static DPCQueue sNormalPriorityQueue;
static DPCQueue sHighPriorityQueue;
static DPCQueue sRealTimePriorityQueue;


// #pragma mark - FunctionDPCCallback


FunctionDPCCallback::FunctionDPCCallback(DPCQueue* owner)
	:
	fOwner(owner)
{
}


void
FunctionDPCCallback::SetTo(void (*function)(void*), void* argument)
{
	fFunction = function;
	fArgument = argument;
}


void
FunctionDPCCallback::DoDPC(DPCQueue* queue)
{
	fFunction(fArgument);

	if (fOwner != NULL)
		fOwner->Recycle(this);
}


// #pragma mark - DPCCallback


DPCCallback::DPCCallback()
	:
	fInQueue(NULL),
	fExpireAt(0)
{
}


DPCCallback::~DPCCallback()
{
}


// #pragma mark - DPCQueue


DPCQueue::DPCQueue()
	:
	fThreadID(-1),
	fCallbackInProgress(NULL),
	fCallbackDoneCondition(NULL)
{
	B_INITIALIZE_SPINLOCK(&fLock);

	fPendingCallbacksCondition.Init(this, "dpc queue");
}


DPCQueue::~DPCQueue()
{
	// close, if not closed yet
	{
		InterruptsSpinLocker locker(fLock);
		if (!_IsClosed()) {
			locker.Unlock();
			Close(false);
		}
	}

	// delete function callbacks
	while (DPCCallback* callback = fUnusedFunctionCallbacks.RemoveHead())
		delete callback;
}


/*static*/ DPCQueue*
DPCQueue::DefaultQueue(int priority)
{
	if (priority <= NORMAL_PRIORITY)
		return &sNormalPriorityQueue;

	if (priority <= HIGH_PRIORITY)
		return &sHighPriorityQueue;

	return &sRealTimePriorityQueue;
}


status_t
DPCQueue::Init(const char* name, int32 priority, uint32 reservedSlots)
{
	// create function callbacks
	for (uint32 i = 0; i < reservedSlots; i++) {
		FunctionDPCCallback* callback
			= new(std::nothrow) FunctionDPCCallback(this);
		if (callback == NULL)
			return B_NO_MEMORY;

		fUnusedFunctionCallbacks.Add(callback);
	}

	// spawn the thread
	fThreadID = spawn_kernel_thread(&_ThreadEntry, name, priority, this);
	if (fThreadID < 0)
		return fThreadID;

	resume_thread(fThreadID);

	return B_OK;
}


void
DPCQueue::Close(bool cancelPending)
{
	InterruptsSpinLocker locker(fLock);

	if (_IsClosed())
		return;

	// If requested, dequeue all pending callbacks
	if (cancelPending)
		fCallbacks.MakeEmpty();

	// mark the queue closed
	thread_id thread = fThreadID;
	fThreadID = -1;

	locker.Unlock();

	// wake up the thread and wait for it
	fPendingCallbacksCondition.NotifyAll();
	wait_for_thread(thread, NULL);
}


status_t
DPCQueue::Add(DPCCallback* callback)
{
	return AddAfter(callback, 0);
}


status_t
DPCQueue::Add(void (*function)(void*), void* argument)
{
	return AddAfter(function, argument, 0);
}

status_t
DPCQueue::AddAfter(void (*function)(void*), void* argument, bigtime_t after)
{
	if (function == NULL)
		return B_BAD_VALUE;

	// get a free callback
	InterruptsSpinLocker locker(fLock);

	DPCCallback* callback = fUnusedFunctionCallbacks.RemoveHead();
	if (callback == NULL)
		return B_NO_MEMORY;

	locker.Unlock();

	// init the callback
	FunctionDPCCallback* functionCallback
		= static_cast<FunctionDPCCallback*>(callback);
	functionCallback->SetTo(function, argument);

	// add it
	status_t error = AddAfter(functionCallback, after);

	if (error != B_OK)
		Recycle(functionCallback);

	return error;
}

void DPCQueue::_Enqueue(DPCCallback * callback)
{
	if(fCallbacks.IsEmpty()) {
		fCallbacks.Add(callback);
	} else if(callback->fExpireAt >= fCallbacks.Last()->fExpireAt) {
		fCallbacks.Add(callback);
	} else {
		DPCCallback * next = nullptr;
		for(next = fCallbacks.First() ; next ; next = fCallbacks.GetNext(next)) {
			if(next->fExpireAt > callback->fExpireAt) {
				break;
			}
		}

		if(next == nullptr) {
			fCallbacks.Add(callback, true);
		} else {
			fCallbacks.InsertBefore(next, callback);
		}
	}

	callback->fInQueue = this;

}
status_t
DPCQueue::AddAfter(DPCCallback * callback, bigtime_t after)
{
	// queue the callback, if the queue isn't closed already
	InterruptsSpinLocker locker(fLock);

	if (_IsClosed())
		return B_NOT_INITIALIZED;

	if(callback->fInQueue == this) {
		// We may need to cancel
		fCallbacks.Remove(callback);
		callback->fInQueue = nullptr;
	} else if(fCallbackInProgress == callback) {
		fReQueuedCallbackInProgress = true;
		callback->fExpireAt = after;
		return B_OK;
	}

	bool wasEmpty = fCallbacks.IsEmpty();

	callback->fExpireAt = system_time() + after;

	_Enqueue(callback);

	bool isFirst = fCallbacks.First() == callback;

	locker.Unlock();

	// notify the condition variable, if necessary
	if (wasEmpty || isFirst) {
		fPendingCallbacksCondition.NotifyAll();
	}

	return B_OK;
}

bool
DPCQueue::Cancel(DPCCallback* callback)
{
	InterruptsSpinLocker locker(fLock);

	// If the callback is queued, remove it.
	if (callback->fInQueue == this) {
		fCallbacks.Remove(callback);
		return true;
	}

	// The callback is not queued. If it isn't in progress, we're done, too.
	if (callback != fCallbackInProgress)
		return false;

	// The callback is currently being executed. We need to wait for it to be
	// done.

	// Set the respective condition, if not set yet. For the unlikely case that
	// there are multiple threads trying to cancel the callback at the same
	// time, the condition variable of the first thread will be used.
	ConditionVariable condition;
	if (fCallbackDoneCondition == NULL)
		fCallbackDoneCondition = &condition;

	// add our wait entry
	ConditionVariableEntry waitEntry;
	fCallbackDoneCondition->Add(&waitEntry);

	// wait
	locker.Unlock();
	waitEntry.Wait();

	return false;
}

bool DPCQueue::LazyCancel(DPCCallback* callback)
{
	InterruptsSpinLocker locker(fLock);

	// If the callback is queued, remove it.
	if (callback->fInQueue == this) {
		fCallbacks.Remove(callback);
		callback->fInQueue = nullptr;
		return true;
	}

	// The callback is not queued. If it isn't in progress, we're done, too.
	if (callback != fCallbackInProgress) {
		return false;
	}

	fReQueuedCallbackInProgress = false;
	return false;
}

void
DPCQueue::Recycle(FunctionDPCCallback* callback)
{
	InterruptsSpinLocker locker(fLock);
	fUnusedFunctionCallbacks.Insert(callback, false);
}


/*static*/ status_t
DPCQueue::_ThreadEntry(void* data)
{
	return ((DPCQueue*)data)->_Thread();
}


status_t
DPCQueue::_Thread()
{
	while (true) {
		InterruptsSpinLocker locker(fLock);

		bigtime_t now = system_time();

		// get the next pending callback
		DPCCallback* callback = fCallbacks.First();

		if (callback == NULL) {
			// nothing is pending -- wait unless the queue is already closed
			if (_IsClosed())
				break;

			ConditionVariableEntry waitEntry;
			fPendingCallbacksCondition.Add(&waitEntry);

			locker.Unlock();
			waitEntry.Wait();

			continue;
		}

		if(callback->fExpireAt > now) {
			bigtime_t delta = callback->fExpireAt - now;
			ConditionVariableEntry waitEntry;
			fPendingCallbacksCondition.Add(&waitEntry);

			locker.Unlock();
			waitEntry.Wait(B_RELATIVE_TIMEOUT, delta);
			continue;
		}

		fCallbacks.RemoveHead();

		callback->fInQueue = NULL;
		fCallbackInProgress = callback;

		// call the callback
		locker.Unlock();
		callback->DoDPC(this);
		locker.Lock();

		fCallbackInProgress = NULL;

		if(fReQueuedCallbackInProgress) {
			// We need to requeue the callback
			fReQueuedCallbackInProgress = false;
			_Enqueue(callback);
		} else {
			// wake up threads waiting for the callback to be done
			ConditionVariable* doneCondition = fCallbackDoneCondition;
			fCallbackDoneCondition = NULL;
			locker.Unlock();
			if (doneCondition != NULL)
				doneCondition->NotifyAll();
		}
	}

	return B_OK;
}


// #pragma mark - kernel private


void
dpc_init()
{
	// create the default queues
	new(&sNormalPriorityQueue) DPCQueue;
	new(&sHighPriorityQueue) DPCQueue;
	new(&sRealTimePriorityQueue) DPCQueue;

	if (sNormalPriorityQueue.Init("dpc: normal priority", NORMAL_PRIORITY,
			DEFAULT_QUEUE_SLOT_COUNT) != B_OK
		|| sHighPriorityQueue.Init("dpc: high priority", HIGH_PRIORITY,
			DEFAULT_QUEUE_SLOT_COUNT) != B_OK
		|| sRealTimePriorityQueue.Init("dpc: real-time priority",
			REAL_TIME_PRIORITY, DEFAULT_QUEUE_SLOT_COUNT) != B_OK) {
		panic("Failed to create default DPC queues!");
	}
}
