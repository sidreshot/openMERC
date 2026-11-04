/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: a blocking parallel-for for pure CPU work at load
 * time (the material maps of the world textures). GL3_ParallelFor(n, fn, ctx)
 * runs fn(i, ctx) for i = 0..n-1 on the calling thread and up to seven worker
 * threads and returns when all are done. fn must not call OpenGL (the context
 * belongs to the main thread) nor the engine's imports.
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
static DWORD WINAPI
JobsThread(LPVOID arg)
{
	JobsWork((jobset_t *)arg);
	return 0;
}
#else
static void *
JobsThread(void *arg)
{
	JobsWork((jobset_t *)arg);
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

void
GL3_ParallelFor(int n, void (*fn)(int i, void *ctx), void *ctx)
{
	jobset_t s;
	int threads, t, started = 0;
#ifdef _WIN32
	HANDLE h[JOBS_MAXTHREADS];
#else
	pthread_t h[JOBS_MAXTHREADS];
#endif

	if (n <= 0)
	{
		return;
	}

	s.n = n;
	s.fn = fn;
	s.ctx = ctx;
	s.next = 0;

	/* one thread less than the cores (the driver and the OS keep one), at most one per item */
	threads = JobsCores() - 1;
	threads = (threads > JOBS_MAXTHREADS - 1) ? JOBS_MAXTHREADS - 1 : threads;
	threads = (threads > n - 1) ? n - 1 : threads;

	for (t = 0; t < threads; t++)
	{
#ifdef _WIN32
		h[started] = CreateThread(NULL, 0, JobsThread, &s, 0, NULL);

		if (h[started])
		{
			started++;
		}
#else
		if (!pthread_create(&h[started], NULL, JobsThread, &s))
		{
			started++;
		}
#endif
	}

	JobsWork(&s); /* the calling thread works too (and alone if no thread started) */

#ifdef _WIN32
	if (started)
	{
		WaitForMultipleObjects((DWORD)started, h, TRUE, INFINITE);
	}

	for (t = 0; t < started; t++)
	{
		CloseHandle(h[t]);
	}
#else
	for (t = 0; t < started; t++)
	{
		pthread_join(h[t], NULL);
	}
#endif
}
