/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: a blocking parallel-for for pure CPU work at load
 * time (the material maps of the world textures, the seam repair of upscaled
 * textures). GL3_ParallelFor(n, fn, ctx) runs fn(i, ctx) for i = 0..n-1 on the
 * calling thread and up to seven worker threads and returns when all are done.
 * fn must not call OpenGL (the context belongs to the main thread) nor the
 * engine's imports.
 *
 * The workers are created once and sleep between calls (a call costs a wake-up,
 * not a thread creation: the seam repair makes several calls per texture, a
 * map hundreds); GL3_Jobs_Shutdown stops them before the renderer library is
 * unloaded. A call made while another one runs (from a job) runs on the calling
 * thread alone.
 */

#include "header/local.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#define JOBS_MAXTHREADS 8

typedef struct
{
	int n;
	void (*fn)(int i, void *ctx);
	void *ctx;
	volatile long next;
} jobset_t;

static struct
{
	qboolean started;
	int nthreads;          /* workers (the calling thread works as well) */
	jobset_t *set;         /* the call being worked on */
	long gen;              /* incremented by every call: wakes the workers */
	int pending;           /* workers still on the current call */
	qboolean quit;
	volatile long busy;    /* a call is running (a nested one runs alone) */
#ifdef _WIN32
	CRITICAL_SECTION lock;
	CONDITION_VARIABLE wake, done;
	HANDLE threads[JOBS_MAXTHREADS];
#else
	pthread_mutex_t lock;
	pthread_cond_t wake, done;
	pthread_t threads[JOBS_MAXTHREADS];
#endif
} J;

static int
JobsTake(jobset_t *s)
{
#ifdef _WIN32
	return (int)InterlockedIncrement(&s->next) - 1;
#else
	return (int)__sync_fetch_and_add(&s->next, 1);
#endif
}

static void
JobsWork(jobset_t *s)
{
	int i;

	while ((i = JobsTake(s)) < s->n)
	{
		s->fn(i, s->ctx);
	}
}

#ifdef _WIN32
#define JLOCK() EnterCriticalSection(&J.lock)
#define JUNLOCK() LeaveCriticalSection(&J.lock)
#define JWAIT(cv) SleepConditionVariableCS(&(cv), &J.lock, INFINITE)
#define JBROADCAST(cv) WakeAllConditionVariable(&(cv))
#define JSIGNAL(cv) WakeConditionVariable(&(cv))
#else
#define JLOCK() pthread_mutex_lock(&J.lock)
#define JUNLOCK() pthread_mutex_unlock(&J.lock)
#define JWAIT(cv) pthread_cond_wait(&(cv), &J.lock)
#define JBROADCAST(cv) pthread_cond_broadcast(&(cv))
#define JSIGNAL(cv) pthread_cond_signal(&(cv))
#endif

static void
JobsWorker(void)
{
	long seen = 0;

	JLOCK();

	for (;;)
	{
		jobset_t *s;

		while ((J.gen == seen) && !J.quit)
		{
			JWAIT(J.wake);
		}

		if (J.quit)
		{
			break;
		}

		seen = J.gen;
		s = J.set;
		JUNLOCK();

		JobsWork(s);

		JLOCK();

		if (--J.pending == 0)
		{
			JSIGNAL(J.done);
		}
	}

	JUNLOCK();
}

#ifdef _WIN32
static DWORD WINAPI
JobsThread(LPVOID arg)
{
	(void)arg;
	JobsWorker();
	return 0;
}
#else
static void *
JobsThread(void *arg)
{
	(void)arg;
	JobsWorker();
	return NULL;
}
#endif

static int
JobsCores(void)
{
#ifdef _WIN32
	SYSTEM_INFO si;

	GetSystemInfo(&si);
	return (int)si.dwNumberOfProcessors;
#else
	long c = sysconf(_SC_NPROCESSORS_ONLN);

	return (c > 0) ? (int)c : 1;
#endif
}

static void
JobsStart(void)
{
	int want, t;

	J.started = true;
	J.quit = false;
	J.gen = 0;
	J.nthreads = 0;
#ifdef _WIN32
	InitializeCriticalSection(&J.lock);
	InitializeConditionVariable(&J.wake);
	InitializeConditionVariable(&J.done);
#else
	pthread_mutex_init(&J.lock, NULL);
	pthread_cond_init(&J.wake, NULL);
	pthread_cond_init(&J.done, NULL);
#endif

	/* one thread less than the cores (the driver and the OS keep one) */
	want = JobsCores() - 1;
	want = (want > JOBS_MAXTHREADS - 1) ? JOBS_MAXTHREADS - 1 : want;

	for (t = 0; t < want; t++)
	{
#ifdef _WIN32
		J.threads[J.nthreads] = CreateThread(NULL, 0, JobsThread, NULL, 0, NULL);

		if (J.threads[J.nthreads])
		{
			J.nthreads++;
		}
#else
		if (!pthread_create(&J.threads[J.nthreads], NULL, JobsThread, NULL))
		{
			J.nthreads++;
		}
#endif
	}
}

void
GL3_ParallelFor(int n, void (*fn)(int i, void *ctx), void *ctx)
{
	jobset_t s;

	if (n <= 0)
	{
		return;
	}

	s.n = n;
	s.fn = fn;
	s.ctx = ctx;
	s.next = 0;

#ifdef _WIN32
	if ((n == 1) || (InterlockedCompareExchange(&J.busy, 1, 0) != 0))
#else
	if ((n == 1) || !__sync_bool_compare_and_swap(&J.busy, 0, 1))
#endif
	{
		JobsWork(&s); /* one item, or nested in a running call: this thread alone */
		return;
	}

	if (!J.started)
	{
		JobsStart();
	}

	if (J.nthreads)
	{
		JLOCK();
		J.set = &s;
		J.pending = J.nthreads;
		J.gen++;
		JBROADCAST(J.wake);
		JUNLOCK();
	}

	JobsWork(&s); /* the calling thread works too (and alone without workers) */

	if (J.nthreads)
	{
		JLOCK();

		while (J.pending > 0)
		{
			JWAIT(J.done);
		}

		JUNLOCK();
	}

	J.busy = 0;
}

/* before the renderer library goes: the workers end (they run its code) */
void
GL3_Jobs_Shutdown(void)
{
	int t;

	if (!J.started)
	{
		return;
	}

	JLOCK();
	J.quit = true;
	JBROADCAST(J.wake);
	JUNLOCK();

#ifdef _WIN32
	if (J.nthreads)
	{
		WaitForMultipleObjects((DWORD)J.nthreads, J.threads, TRUE, INFINITE);
	}

	for (t = 0; t < J.nthreads; t++)
	{
		CloseHandle(J.threads[t]);
	}

	DeleteCriticalSection(&J.lock);
#else
	for (t = 0; t < J.nthreads; t++)
	{
		pthread_join(J.threads[t], NULL);
	}

	pthread_mutex_destroy(&J.lock);
	pthread_cond_destroy(&J.wake);
	pthread_cond_destroy(&J.done);
#endif

	memset(&J, 0, sizeof(J));
}
