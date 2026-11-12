/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: video recording - "om_record" starts a recording and stops it again (bound to F12). Not in
 * the original game; its "avi" command (gl3_avi.c) records 10 frames per second through Video for Windows.
 *
 * The frames go to an ffmpeg process (installed by the player, not shipped): at the end of every frame the window
 * is read into a pixel buffer object (asynchronously, with a fence), a few frames later the finished buffer is copied
 * out, and a thread writes it into ffmpeg's video pipe. The video has a constant rate (om_rec_fps): a frame the game
 * draws is written as many times as video frames fell due since the last one (repeated when the game is slower,
 * skipped when it is faster), so a loading pause stays in step with the sound. The GPU scales the frame down to
 * om_rec_height lines first (a window with MSAA is resolved into a buffer of its own for that), so less is read and
 * piped; where the driver refuses those copies the whole window is read and ffmpeg scales. ffmpeg encodes H.264. The sound is what the player hears, recorded from the monitor of the default output (PulseAudio
 * or PipeWire through libpulse-simple, loaded when a recording starts) into a file of its own: given to ffmpeg as a
 * second pipe, it was read in bursts while the video went on, and the sound server dropped what waited too long.
 * After the stop a second, short ffmpeg run copies the video and adds the sound as AAC:
 * <user>/video/openmerc-<date>-<time>.mp4 (both start at the moment the sound recording opened). ffmpeg's messages
 * go to <user>/video/ffmpeg.log.
 *
 * ffmpeg is a process of its own: when the game ends or crashes, it reads the end of its pipe and still closes the
 * file (then the video without sound, .openmerc-*.mkv, and the raw sound, .openmerc-*.s16, are left in the folder).
 * A blinking red square in the top right corner shows a running recording; it is drawn after the frame is read, so
 * it is not in the video.
 *
 * Unix systems (Linux; macOS without sound); the Windows and Android builds say it is not available.
 */

#include "header/local.h"

#if !defined(_WIN32) && !defined(__ANDROID__)
#define REC_SUPPORTED 1
#endif

static cvar_t *om_rec_fps;
static cvar_t *om_rec_height;
static cvar_t *om_rec_ffmpeg;
static cvar_t *om_rec_vcodec;
static cvar_t *om_rec_audio;
static cvar_t *om_rec_indicator;

static void
Rec_Cvars(void)
{
	if (om_rec_fps)
	{
		return;
	}

	om_rec_fps = ri.Cvar_Get("om_rec_fps", "60", CVAR_ARCHIVE);
	/* the video's height: the window is scaled down to it (0: the window's size) */
	om_rec_height = ri.Cvar_Get("om_rec_height", "1080", CVAR_ARCHIVE);
	/* the ffmpeg program: empty - next to the game's executable, then the PATH */
	om_rec_ffmpeg = ri.Cvar_Get("om_rec_ffmpeg", "", CVAR_ARCHIVE);
	/* the video encoder and its options (ffmpeg's -c:v ...) */
	om_rec_vcodec = ri.Cvar_Get("om_rec_vcodec", "libx264 -preset veryfast -crf 18 -pix_fmt yuv420p", CVAR_ARCHIVE);
	om_rec_audio = ri.Cvar_Get("om_rec_audio", "1", CVAR_ARCHIVE);
	om_rec_indicator = ri.Cvar_Get("om_rec_indicator", "1", CVAR_ARCHIVE);
}

#ifdef REC_SUPPORTED

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define REC_SLOTS 8 /* pixel buffers: filled by the GPU, then mapped while the writer sends them */
#define REC_RATE 48000
#define REC_MAXARGS 64

/* libpulse-simple, loaded when a recording starts (pulse/simple.h, pulse/sample.h, pulse/def.h) */
typedef struct pa_simple pa_simple;

typedef struct
{
	int format;
	uint32_t rate;
	uint8_t channels;
} pa_sample_spec;

typedef struct
{
	uint32_t maxlength, tlength, prebuf, minreq, fragsize;
} pa_buffer_attr;

#define PA_SAMPLE_S16LE 3
#define PA_STREAM_RECORD 2

static pa_simple *(*p_pa_simple_new)(const char *, const char *, int, const char *, const char *,
		const pa_sample_spec *, const void *, const pa_buffer_attr *, int *);
static int (*p_pa_simple_read)(pa_simple *, void *, size_t, int *);
static void (*p_pa_simple_free)(pa_simple *);
static void *pulselib;

enum
{
	SLOT_FREE,
	SLOT_READING, /* glReadPixels issued, the fence not passed yet */
	SLOT_QUEUED,  /* mapped; the writer sends it */
	SLOT_WRITTEN  /* sent; to be unmapped on the render thread */
};

typedef struct
{
	GLuint pbo;
	GLsync fence;
	void *map;
	int frames; /* video frames this capture stands for */
	volatile int state;
	unsigned seq;
} recslot_t;

static struct
{
	qboolean on;        /* frames are being captured */
	qboolean finishing; /* stopped; the threads and ffmpeg are still at work */
	int w, h, fps;
	int ow, oh;         /* the size read: the window's, or scaled down by the GPU (then also turned upright) */
	qboolean gpuscale, msaa;
	GLuint rbresolve, fboresolve, rbscaled, fboscaled;
	size_t size;
	double t0;
	long long due;      /* video frames handed to captures so far */
	unsigned seq, harvest;
	int carry;          /* frames no capture could take, given to the next one */
	long long dropped;
	long long drawn;    /* game frames during the recording, and the time the capture took on this thread */
	double capturetime;
	recslot_t slot[REC_SLOTS];
	char path[MAX_OSPATH], log[MAX_OSPATH], ffmpeg[MAX_OSPATH];
	char vtmp[MAX_OSPATH], atmp[MAX_OSPATH]; /* the video without sound and the raw sound, until joined */
	volatile pid_t pid;

	/* writer thread: a queue of mapped slots, sent straight from the mapping (no copy on the render thread) */
	pthread_t writer;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int qslot[REC_SLOTS], qhead, qcount;
	qboolean wdone;
	volatile int werror, wexited;
	int vfd;
	long long written;

	/* sound thread */
	qboolean haveaudio;
	pthread_t audio;
	pa_simple *pa;
	volatile int astop;
	int afd;

	/* waits for the threads and ffmpeg after a stop */
	pthread_t reaper;
	volatile int done;
	int status;
} rec;

static double
Rec_Now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static qboolean
Rec_WriteAll(int fd, const void *data, size_t size)
{
	const byte *p = data;

	while (size)
	{
		ssize_t n = write(fd, p, size);

		if (n < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}

			return false;
		}

		p += n;
		size -= (size_t)n;
	}

	return true;
}

static void *
Rec_Writer(void *arg)
{
	(void)arg;

	for (;;)
	{
		recslot_t *s;
		int n, k;

		pthread_mutex_lock(&rec.lock);

		while (!rec.qcount && !rec.wdone)
		{
			pthread_cond_wait(&rec.cond, &rec.lock);
		}

		if (!rec.qcount)
		{
			pthread_mutex_unlock(&rec.lock);
			break;
		}

		s = &rec.slot[rec.qslot[rec.qhead]];
		n = s->frames; /* read under the lock: a drop may still add to the last queued slot */
		rec.qhead = (rec.qhead + 1) % REC_SLOTS;
		rec.qcount--;
		pthread_mutex_unlock(&rec.lock);

		for (k = 0; (k < n) && !rec.werror; k++)
		{
			if (!s->map || !Rec_WriteAll(rec.vfd, s->map, rec.size))
			{
				rec.werror = 1;
			}
		}

		pthread_mutex_lock(&rec.lock);
		s->state = SLOT_WRITTEN;
		rec.written += n;
		pthread_mutex_unlock(&rec.lock);
	}

	close(rec.vfd);
	rec.vfd = -1;
	__atomic_store_n(&rec.wexited, 1, __ATOMIC_RELEASE);
	return NULL;
}

static void *
Rec_Audio(void *arg)
{
	short buf[REC_RATE / 50 * 2]; /* 20 ms of stereo */
	int err;

	(void)arg;

	while (!rec.astop)
	{
		if (p_pa_simple_read(rec.pa, buf, sizeof(buf), &err) < 0)
		{
			break;
		}

		if (!Rec_WriteAll(rec.afd, buf, sizeof(buf)))
		{
			break;
		}
	}

	close(rec.afd);
	rec.afd = -1;
	p_pa_simple_free(rec.pa);
	rec.pa = NULL;
	return NULL;
}

static int
Rec_Wait(pid_t pid)
{
	int status = 1 << 8;

	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
	{
	}

	return status;
}

/* runs args[0] with stdin /dev/null, stdout and stderr into rec.log (emptied first when newlog), and the read end
   of the video pipe as descriptor 3 (vfd >= 0); 0 or an errno */
static int
Rec_Spawn(char **args, int vfd, qboolean newlog, pid_t *pid)
{
	posix_spawn_file_actions_t fa;
	int logfd, err;

	logfd = open(rec.log, O_WRONLY | O_CREAT | O_CLOEXEC | (newlog ? O_TRUNC : O_APPEND), 0644);

	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);

	if (logfd >= 0)
	{
		posix_spawn_file_actions_adddup2(&fa, logfd, 1);
		posix_spawn_file_actions_adddup2(&fa, logfd, 2);
	}

	if (vfd >= 0)
	{
		posix_spawn_file_actions_adddup2(&fa, vfd, 3);
	}

	err = posix_spawnp(pid, args[0], &fa, NULL, args, environ);
	posix_spawn_file_actions_destroy(&fa);

	if (logfd >= 0)
	{
		close(logfd);
	}

	return err;
}

static void *
Rec_Reaper(void *arg)
{
	(void)arg;

	/* the sound ends after the last frame was written (the join cuts it to the video, -shortest) */
	rec.astop = 1;

	if (rec.haveaudio)
	{
		pthread_join(rec.audio, NULL);
	}

	rec.status = Rec_Wait(rec.pid);

	if (rec.haveaudio && WIFEXITED(rec.status) && WEXITSTATUS(rec.status) == 0)
	{
		char *args[] = {rec.ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "warning", "-y", "-i", rec.vtmp,
			"-f", "s16le", "-ar", "48000", "-ac", "2", "-i", rec.atmp, "-map", "0:v", "-map", "1:a", "-c:v", "copy",
			"-c:a", "aac", "-b:a", "192k", "-shortest", "-movflags", "+faststart", rec.path, NULL};
		pid_t pid;

		if (Rec_Spawn(args, -1, false, (pid_t *)&pid) == 0)
		{
			rec.pid = pid;
			rec.status = Rec_Wait(pid);
		}
		else
		{
			rec.status = 1 << 8;
		}

		if (WIFEXITED(rec.status) && WEXITSTATUS(rec.status) == 0)
		{
			unlink(rec.vtmp);
			unlink(rec.atmp);
		}
	}

	__atomic_store_n(&rec.done, 1, __ATOMIC_RELEASE);
	return NULL;
}

/* the sound the player hears: the monitor of the default output */
static pa_simple *
Rec_OpenPulse(void)
{
	pa_sample_spec ss = {PA_SAMPLE_S16LE, REC_RATE, 2};
	pa_buffer_attr ba = {(uint32_t)-1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, REC_RATE / 50 * 4};
	pa_simple *s;
	int err = 0;

	if (!pulselib)
	{
		pulselib = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);

		if (!pulselib)
		{
			Com_Printf("om_record: no libpulse-simple (PulseAudio / PipeWire) - the video has no sound\n");
			return NULL;
		}

		p_pa_simple_new = dlsym(pulselib, "pa_simple_new");
		p_pa_simple_read = dlsym(pulselib, "pa_simple_read");
		p_pa_simple_free = dlsym(pulselib, "pa_simple_free");
	}

	if (!p_pa_simple_new || !p_pa_simple_read || !p_pa_simple_free)
	{
		return NULL;
	}

	s = p_pa_simple_new(NULL, "openMERC", PA_STREAM_RECORD, "@DEFAULT_MONITOR@", "video recording", &ss, NULL,
			&ba, &err);

	if (!s)
	{
		Com_Printf("om_record: the sound output cannot be recorded (error %d) - the video has no sound\n", err);
	}

	return s;
}

/* a pipe whose ends are not 0..4 (the child's descriptors are set from them) */
static qboolean
Rec_Pipe(int fds[2])
{
	int i;

	if (pipe(fds) < 0)
	{
		return false;
	}

	for (i = 0; i < 2; i++)
	{
		int fd = fcntl(fds[i], F_DUPFD_CLOEXEC, 10);

		close(fds[i]);
		fds[i] = fd;
	}

	if (fds[0] < 0 || fds[1] < 0)
	{
		return false;
	}

#ifdef F_SETPIPE_SZ
	fcntl(fds[1], F_SETPIPE_SZ, 1024 * 1024);
#endif
	return true;
}

/* splits s into args (spaces); the copies live in store */
static int
Rec_Split(const char *s, char *store, size_t storesize, char **args, int max)
{
	int n = 0;
	size_t used = 0;

	while (*s && n < max)
	{
		size_t len = 0;

		while (*s == ' ')
		{
			s++;
		}

		if (!*s)
		{
			break;
		}

		while (s[len] && s[len] != ' ')
		{
			len++;
		}

		if (used + len + 1 > storesize)
		{
			break;
		}

		memcpy(store + used, s, len);
		store[used + len] = 0;
		args[n++] = store + used;
		used += len + 1;
		s += len;
	}

	return n;
}

/* the ffmpeg to run: om_rec_ffmpeg, else the one next to the executable, else "ffmpeg" from the PATH */
static void
Rec_FindFfmpeg(char *out, size_t size)
{
	if (om_rec_ffmpeg->string[0])
	{
		Q_strlcpy(out, om_rec_ffmpeg->string, size);
		return;
	}

#ifdef __linux__
	{
		char exe[MAX_OSPATH];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

		if (n > 0)
		{
			char *slash;

			exe[n] = 0;
			slash = strrchr(exe, '/');

			if (slash)
			{
				*slash = 0;
				Com_sprintf(out, size, "%s/ffmpeg", exe);

				if (access(out, X_OK) == 0)
				{
					return;
				}
			}
		}
	}
#endif

	Q_strlcpy(out, "ffmpeg", size);
}

/* the window into fboscaled: resolved first when it has MSAA, then scaled and turned upright; leaves fboscaled
   bound for reading */
static void
Rec_GpuBlit(void)
{
	GLuint src = 0;

	glDisable(GL_SCISSOR_TEST);

	if (rec.msaa)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rec.fboresolve);
		glBlitFramebuffer(0, 0, rec.w, rec.h, 0, 0, rec.w, rec.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		src = rec.fboresolve;
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rec.fboscaled);
	glBlitFramebuffer(0, 0, rec.w, rec.h, 0, rec.oh, rec.ow, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, rec.fboscaled);
}

static void
Rec_GpuFree(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (rec.fboresolve)
	{
		glDeleteFramebuffers(1, &rec.fboresolve);
		glDeleteRenderbuffers(1, &rec.rbresolve);
	}

	if (rec.fboscaled)
	{
		glDeleteFramebuffers(1, &rec.fboscaled);
		glDeleteRenderbuffers(1, &rec.rbscaled);
	}

	rec.fboresolve = rec.rbresolve = rec.fboscaled = rec.rbscaled = 0;
	rec.gpuscale = false;
}

static GLuint
Rec_Target(GLuint *rb, int w, int h)
{
	GLuint fbo;

	glGenRenderbuffers(1, rb);
	glBindRenderbuffer(GL_RENDERBUFFER, *rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, *rb);
	return fbo;
}

/* the GPU scaling to ow x oh; a test copy tells whether the driver takes it */
static qboolean
Rec_GpuSetup(void)
{
	GLint sb = 0;
	qboolean ok = true;

	while (glGetError() != GL_NO_ERROR)
	{
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glGetIntegerv(GL_SAMPLE_BUFFERS, &sb);
	rec.msaa = sb > 0;

	if (rec.msaa)
	{
		rec.fboresolve = Rec_Target(&rec.rbresolve, rec.w, rec.h);
		ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	}

	rec.fboscaled = Rec_Target(&rec.rbscaled, rec.ow, rec.oh);
	ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

	if (ok)
	{
		Rec_GpuBlit();
		ok = glGetError() == GL_NO_ERROR;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!ok)
	{
		Rec_GpuFree();
		return false;
	}

	rec.gpuscale = true;
	return true;
}

static void
Rec_Start(void)
{
	char dir[MAX_OSPATH], stamp[64], size[32], rate[16], vf[64], gop[16], store[1024];
	char *args[REC_MAXARGS];
	int vpipe[2] = {-1, -1}, n = 0, i, err, outh;
	time_t now = time(NULL);
	struct tm *lt = localtime(&now);

	if (rec.finishing)
	{
		Com_Printf("om_record: the last video is still being written\n");
		return;
	}

	rec.w = vid.width & ~1;
	rec.h = vid.height & ~1;
	rec.fps = (int)Q_clamp(om_rec_fps->value, 10, 240);

	if (rec.w < 16 || rec.h < 16)
	{
		return;
	}

	/* scaled down only, to an even size with the window's aspect */
	outh = (int)om_rec_height->value & ~1;
	rec.ow = rec.w;
	rec.oh = rec.h;
	rec.gpuscale = false;

	if (outh >= 16 && outh < rec.h)
	{
		rec.oh = outh;
		rec.ow = (int)((double)rec.w * outh / rec.h + 0.5) & ~1;

		if (!Rec_GpuSetup())
		{
			rec.ow = rec.w;
			rec.oh = rec.h;
		}
	}

	rec.size = (size_t)rec.ow * rec.oh * 4;

	Com_sprintf(dir, sizeof(dir), "%s/video", GL3_UserDir());
	mkdir(dir, 0755);
	Com_sprintf(stamp, sizeof(stamp), "openmerc-%04d-%02d-%02d_%02d-%02d-%02d", lt->tm_year + 1900, lt->tm_mon + 1,
			lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
	Com_sprintf(rec.path, sizeof(rec.path), "%s/%s.mp4", dir, stamp);
	Com_sprintf(rec.vtmp, sizeof(rec.vtmp), "%s/.%s.mkv", dir, stamp);
	Com_sprintf(rec.atmp, sizeof(rec.atmp), "%s/.%s.s16", dir, stamp);
	Com_sprintf(rec.log, sizeof(rec.log), "%s/ffmpeg.log", dir);

	if (!Rec_Pipe(vpipe))
	{
		Com_Printf("om_record: no pipe\n");
		Rec_GpuFree();
		return;
	}

	rec.afd = -1;
	rec.pa = om_rec_audio->value ? Rec_OpenPulse() : NULL;

	if (rec.pa)
	{
		rec.afd = open(rec.atmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

		if (rec.afd < 0)
		{
			p_pa_simple_free(rec.pa);
			rec.pa = NULL;
		}
	}

	rec.haveaudio = rec.pa != NULL;
	/* the sound is recorded from here on: the video's first frame is this moment as well */
	rec.t0 = Rec_Now();

	/* the command line */
	Rec_FindFfmpeg(rec.ffmpeg, sizeof(rec.ffmpeg));
	Com_sprintf(size, sizeof(size), "%dx%d", rec.ow, rec.oh);
	Com_sprintf(rate, sizeof(rate), "%d", rec.fps);
	Com_sprintf(gop, sizeof(gop), "%d", rec.fps * 2);

	/* the window's rows come bottom first (the GPU copy turns them upright); ffmpeg scales when the GPU could not */
	if (rec.gpuscale)
	{
		vf[0] = 0;
	}
	else if (outh >= 16 && outh < rec.h)
	{
		Com_sprintf(vf, sizeof(vf), "vflip,scale=-2:%d:flags=bicubic", outh);
	}
	else
	{
		Q_strlcpy(vf, "vflip", sizeof(vf));
	}

	args[n++] = rec.ffmpeg;
	args[n++] = "-hide_banner";
	args[n++] = "-nostdin";
	args[n++] = "-loglevel";
	args[n++] = "warning";
	args[n++] = "-y";
	args[n++] = "-f";
	args[n++] = "rawvideo";
	args[n++] = "-pix_fmt";
	args[n++] = "rgba";
	args[n++] = "-video_size";
	args[n++] = size;
	args[n++] = "-framerate";
	args[n++] = rate;
	args[n++] = "-thread_queue_size";
	args[n++] = "8";
	args[n++] = "-i";
	args[n++] = "pipe:3";

	if (vf[0])
	{
		args[n++] = "-vf";
		args[n++] = vf;
	}

	args[n++] = "-c:v";
	n += Rec_Split(om_rec_vcodec->string, store, sizeof(store), args + n, REC_MAXARGS - 16 - n);
	args[n++] = "-g";
	args[n++] = gop;

	/* with sound: a Matroska file first (it stays readable if the game ends before the join) */
	if (!rec.haveaudio)
	{
		args[n++] = "-movflags";
		args[n++] = "+faststart";
	}

	args[n++] = rec.haveaudio ? rec.vtmp : rec.path;
	args[n] = NULL;

	/* a write into the pipe of an ffmpeg that has ended must fail, not end the game */
	signal(SIGPIPE, SIG_IGN);

	{
		pid_t pid;

		err = Rec_Spawn(args, vpipe[0], true, &pid);
		rec.pid = pid;
	}

	close(vpipe[0]);

	if (err)
	{
		close(vpipe[1]);
		Rec_GpuFree();

		if (rec.haveaudio)
		{
			close(rec.afd);
			unlink(rec.atmp);
			p_pa_simple_free(rec.pa);
			rec.pa = NULL;
		}

		if (err == ENOENT)
		{
			Com_Printf("Video recording needs ffmpeg: install it (Debian, Ubuntu: sudo apt install ffmpeg), put the "
					"ffmpeg program next to the game or set om_rec_ffmpeg to its path\n");
		}
		else
		{
			Com_Printf("om_record: %s cannot be started: %s\n", rec.ffmpeg, strerror(err));
		}

		return;
	}

	/* the pixel buffers */
	pthread_mutex_init(&rec.lock, NULL);
	pthread_cond_init(&rec.cond, NULL);

	for (i = 0; i < REC_SLOTS; i++)
	{
		recslot_t *s = &rec.slot[i];

		glGenBuffers(1, &s->pbo);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
		glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)rec.size, NULL, GL_STREAM_READ);
		s->state = SLOT_FREE;
		s->fence = NULL;
		s->map = NULL;
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	rec.qhead = rec.qcount = 0;
	rec.wdone = false;
	rec.werror = 0;
	rec.wexited = 0;
	rec.written = 0;
	rec.dropped = 0;
	rec.drawn = 0;
	rec.capturetime = 0;
	rec.carry = 0;
	rec.seq = rec.harvest = 0;
	rec.due = 0;
	rec.vfd = vpipe[1];
	rec.astop = 0;
	rec.done = 0;

	pthread_create(&rec.writer, NULL, Rec_Writer, NULL);

	if (rec.haveaudio)
	{
		pthread_create(&rec.audio, NULL, Rec_Audio, NULL);
	}

	rec.on = true;

	/* "om_record: recording" lines stay in the console (cl_console.c): not on the screen, so not in the video */
	Com_Printf("om_record: recording %dx%d (%s), %d fps%s into %s\n", rec.ow, rec.oh,
			rec.gpuscale ? "scaled by the GPU" : (rec.ow == rec.w ? "the window" : "scaled by ffmpeg"), rec.fps,
			rec.haveaudio ? "" : ", no sound", rec.path);
}

/* the oldest captures whose pixels have arrived are mapped and go to the writer (all of them, waiting, when wait is
   set) */
static void
Rec_Harvest(qboolean wait)
{
	for (;;)
	{
		recslot_t *s = NULL;
		GLenum r;
		int i;

		for (i = 0; i < REC_SLOTS; i++)
		{
			if (rec.slot[i].state == SLOT_READING && rec.slot[i].seq == rec.harvest)
			{
				s = &rec.slot[i];
				break;
			}
		}

		if (!s)
		{
			return;
		}

		r = glClientWaitSync(s->fence, wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, wait ? 2000000000ull : 0);

		if (r == GL_TIMEOUT_EXPIRED && !wait)
		{
			return;
		}

		glDeleteSync(s->fence);
		s->fence = NULL;
		rec.harvest++;

		glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
		s->map = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)rec.size, GL_MAP_READ_BIT);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

		pthread_mutex_lock(&rec.lock);
		s->frames += rec.carry;
		rec.carry = 0;
		s->state = SLOT_QUEUED;
		rec.qslot[(rec.qhead + rec.qcount) % REC_SLOTS] = (int)(s - rec.slot);
		rec.qcount++;
		pthread_cond_signal(&rec.cond);
		pthread_mutex_unlock(&rec.lock);
	}
}

/* slots the writer has sent are unmapped and free again */
static void
Rec_Reclaim(void)
{
	int i;

	for (i = 0; i < REC_SLOTS; i++)
	{
		recslot_t *s = &rec.slot[i];
		int state;

		pthread_mutex_lock(&rec.lock);
		state = s->state;
		pthread_mutex_unlock(&rec.lock);

		if (state == SLOT_WRITTEN)
		{
			if (s->map)
			{
				glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
				glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
				s->map = NULL;
			}

			s->state = SLOT_FREE;
		}
	}
}

static void
Rec_Stop(void)
{
	double until;
	struct timespec ts = {0, 2000000};
	int i;

	if (!rec.on)
	{
		return;
	}

	rec.on = false;
	Rec_Harvest(true);

	/* the writer sends what is queued (from the mappings, so before they go); ffmpeg gets 10 seconds to read it */
	pthread_mutex_lock(&rec.lock);
	rec.wdone = true;
	pthread_cond_signal(&rec.cond);
	pthread_mutex_unlock(&rec.lock);

	until = Rec_Now() + 10.0;

	while (!__atomic_load_n(&rec.wexited, __ATOMIC_ACQUIRE))
	{
		if (Rec_Now() > until)
		{
			kill(rec.pid, SIGKILL);
			until = Rec_Now() + 1e9;
		}

		nanosleep(&ts, NULL);
	}

	pthread_join(rec.writer, NULL);

	for (i = 0; i < REC_SLOTS; i++)
	{
		recslot_t *s = &rec.slot[i];

		if (s->fence)
		{
			glDeleteSync(s->fence);
			s->fence = NULL;
		}

		if (s->map)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			s->map = NULL;
		}

		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glDeleteBuffers(1, &s->pbo);
		s->pbo = 0;
		s->state = SLOT_FREE;
	}

	Rec_GpuFree();
	rec.finishing = true;
	pthread_create(&rec.reaper, NULL, Rec_Reaper, NULL);
}

/* after a stop: the result once ffmpeg has ended */
static void
Rec_Finish(qboolean wait)
{
	long long secs;

	if (!rec.finishing || (!wait && !__atomic_load_n(&rec.done, __ATOMIC_ACQUIRE)))
	{
		return;
	}

	if (wait)
	{
		/* the game is closing: ffmpeg gets 20 seconds to finish the file */
		double until = Rec_Now() + 20.0;
		struct timespec ts = {0, 10000000};

		while (!__atomic_load_n(&rec.done, __ATOMIC_ACQUIRE) && Rec_Now() < until)
		{
			nanosleep(&ts, NULL);
		}

		if (!__atomic_load_n(&rec.done, __ATOMIC_ACQUIRE))
		{
			kill(rec.pid, SIGKILL);
		}
	}

	pthread_join(rec.reaper, NULL);
	pthread_mutex_destroy(&rec.lock);
	pthread_cond_destroy(&rec.cond);
	rec.finishing = false;
	secs = rec.written / (rec.fps ? rec.fps : 1);

	if (WIFEXITED(rec.status) && WEXITSTATUS(rec.status) == 0 && !rec.werror)
	{
		Com_Printf("Video saved: %s (%lld:%02lld)\n", rec.path, secs / 60, secs % 60);
	}
	else
	{
		Com_Printf("om_record: ffmpeg did not finish the video, see %s\n", rec.log);
	}

	if (rec.dropped)
	{
		Com_Printf("om_record: %lld game frames were left out (the encoder or the GPU was behind)\n", rec.dropped);
	}

	R_Printf(PRINT_DEVELOPER, "om_record: %lld game frames (%.0f per second), capture %.2f ms per game frame\n",
			rec.drawn, rec.written ? (double)rec.drawn * rec.fps / (double)rec.written : 0.0,
			rec.drawn ? rec.capturetime * 1000.0 / (double)rec.drawn : 0.0);
}

void
GL3_Rec_Frame(void)
{
	long long target;
	double start;
	int n, i;
	recslot_t *s = NULL;

	if (!rec.on)
	{
		Rec_Finish(false);
		return;
	}

	if ((vid.width & ~1) != rec.w || (vid.height & ~1) != rec.h)
	{
		Com_Printf("om_record: the window size changed, the recording stops\n");
		Rec_Stop();
		return;
	}

	if (rec.werror)
	{
		Com_Printf("om_record: ffmpeg stopped reading, the recording stops (see %s)\n", rec.log);
		Rec_Stop();
		return;
	}

	start = Rec_Now();
	rec.drawn++;
	Rec_Reclaim();
	Rec_Harvest(false);

	/* the video frames that fell due up to this one */
	target = (long long)((start - rec.t0) * rec.fps) + 1;
	n = (int)(target - rec.due);

	if (n <= 0)
	{
		rec.capturetime += Rec_Now() - start;
		return;
	}

	rec.due = target;

	for (i = 0; i < REC_SLOTS; i++)
	{
		if (rec.slot[i].state == SLOT_FREE)
		{
			s = &rec.slot[i];
			break;
		}
	}

	if (!s)
	{
		/* every buffer is in use (the GPU or ffmpeg is behind): the last capture stands for these frames as well,
		   if the writer has not taken it yet, else the next one */
		recslot_t *last = NULL;

		for (i = 0; i < REC_SLOTS; i++)
		{
			if (rec.slot[i].seq == rec.seq - 1)
			{
				last = &rec.slot[i];
			}
		}

		pthread_mutex_lock(&rec.lock);

		if (last && (last->state == SLOT_READING ||
				(last->state == SLOT_QUEUED && rec.qcount &&
				rec.qslot[(rec.qhead + rec.qcount - 1) % REC_SLOTS] == (int)(last - rec.slot))))
		{
			last->frames += n;
		}
		else
		{
			rec.carry += n;
		}

		pthread_mutex_unlock(&rec.lock);
		rec.dropped++;
		rec.capturetime += Rec_Now() - start;
		return;
	}

	if (rec.gpuscale)
	{
		Rec_GpuBlit();
	}
	else
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, s->pbo);
	glReadPixels(0, 0, rec.ow, rec.oh, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	s->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	s->frames = n;
	s->seq = rec.seq++;
	s->state = SLOT_READING;
	rec.capturetime += Rec_Now() - start;
}

/* the blinking square, after the frame was read */
void
GL3_Rec_Indicator(void)
{
	static const GLfloat red[4] = {0.9f, 0.05f, 0.05f, 1.0f};
	int s;

	if (!rec.on || !om_rec_indicator->value || ((int)((Rec_Now() - rec.t0) * 2.0) & 1))
	{
		return;
	}

	s = vid.height / 90;

	if (s < 6)
	{
		s = 6;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glEnable(GL_SCISSOR_TEST);
	glScissor(vid.width - 2 * s, vid.height - 2 * s, s, s);
	glClearBufferfv(GL_COLOR, 0, red);
	glDisable(GL_SCISSOR_TEST);
}

void
GL3_Rec_Init(void)
{
	Rec_Cvars();
}

void
GL3_Rec_f(void)
{
	const char *a = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "";

	if (!Q_stricmp(a, "stop") || (!a[0] && rec.on))
	{
		Rec_Stop();
	}
	else if (!rec.on)
	{
		Rec_Start();
	}
}

void
GL3_Rec_Shutdown(void)
{
	Rec_Stop();
	Rec_Finish(true);
}

#else /* !REC_SUPPORTED */

void
GL3_Rec_Frame(void)
{
}

void
GL3_Rec_Indicator(void)
{
}

void
GL3_Rec_Init(void)
{
	Rec_Cvars();
}

void
GL3_Rec_f(void)
{
	Com_Printf("om_record: video recording is not available in this build yet\n");
}

void
GL3_Rec_Shutdown(void)
{
}

#endif
