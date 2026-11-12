# Video recording (om_record, F12)

Not in the original game. Its `avi` command (gl3_avi.c, SPEC-412 §13) records 10 frames per second through Video for
Windows; `om_record` records what the player sees and hears as an MP4 that can be posted as it is.

## Use

F12 (or `om_record`) starts a recording, F12 again stops it; `om_record stop` only stops. A blinking red square in the
top right corner of the window shows a running recording. The file: `<user>/video/openmerc-<date>_<time>.mp4`, by
default 1080 lines (the window scaled down, the aspect kept), 60 frames per second, H.264 (`libx264 -preset veryfast
-crf 18`) and AAC at 192 kbit/s. ffmpeg's messages go to `<user>/video/ffmpeg.log`.

ffmpeg is not shipped. It is looked for in `om_rec_ffmpeg`, then next to the executable, then in the PATH; without it
F12 prints how to install it.

| cvar | default | |
|---|---|---|
| `om_rec_height` | 1080 | height of the video; the window is only scaled down, 0 = the window's size |
| `om_rec_fps` | 60 | frames per second of the video (10..240) |
| `om_rec_audio` | 1 | record the sound |
| `om_rec_vcodec` | `libx264 -preset veryfast -crf 18 -pix_fmt yuv420p` | ffmpeg's `-c:v` and its options |
| `om_rec_ffmpeg` | empty | path of ffmpeg |
| `om_rec_indicator` | 1 | the red square |

F12 is in `configs/default_keys.cfg`. A saved `config.cfg` starts with `unbindall`, so an existing setup would never
get it: `Key_OpenMercDefaults` (cl_keyboard.c, after the configs at start) gives such later default binds once, to a
free key only, and remembers that in `om_bindver`.

## How it works (gl3_rec.c)

- **Frames.** At the end of the frame (before the screenshot and the swap) the window is copied on the GPU: a window
  with MSAA is resolved into a renderbuffer of its own, then scaled to the video size and turned upright into a second
  one, and read into one of eight pixel buffer objects with a fence. A later frame maps the buffer once the fence has
  passed, and a thread writes it from the mapping into ffmpeg's pipe (descriptor 3); the render thread only unmaps it
  afterwards. Where the driver refuses the copies (checked once with a test copy), the whole window is read and ffmpeg
  flips and scales it.
- **Constant rate.** The video has `om_rec_fps` frames per second of real time: a game frame is written as many times
  as video frames fell due since the one before. A slower game repeats frames, a faster one skips captures, a loading
  pause shows the last picture; the video stays in step with the sound. When every buffer is in use, the frames are
  given to the last capture (counted, reported at the stop).
- **Sound.** What the player hears: the monitor of the default output through libpulse-simple (`@DEFAULT_MONITOR@`,
  PulseAudio or PipeWire), loaded with dlopen when a recording starts, so nothing is linked. It goes to a raw file
  (`.openmerc-*.s16`), not to a second pipe: ffmpeg read such a pipe in bursts while the video went on, the sound server
  dropped what waited, and `-shortest` then cut the video to the 4.7 s of sound left from 14 s. The video goes to
  `.openmerc-*.mkv`; after the stop a second ffmpeg run copies it and encodes the sound into the MP4, then the two are
  deleted. Both start at the moment the sound recording opened.
- **ffmpeg is a process of its own.** If the game ends or crashes, ffmpeg reads the end of the pipe and closes its
  file (with sound, the `.mkv` and `.s16` are then left in the folder). On `vid_restart` and on quitting the recording
  stops first and the render thread waits for ffmpeg (20 s at most, then it is killed).
- The start line (`om_record: recording ...`) goes to the console only (cl_console.c), so it is not in the video; the
  "Video saved" line after the stop is shown as usual.
- Linux (checked); other Unix systems build the same code (macOS without libpulse: no sound). The Windows and
  Android builds say that it is not available.

## Checked (2026-11-12, Linux, RDNA 4 GPU (gfx1201) on Mesa, ffmpeg 7.0.2 static build)

- F12 through the real key path (`om_keyevent F12`) with a saved `config.cfg` that had no F12 bind: recording starts
  and stops, `bind F12 "om_record"` and `om_bindver 1` are written back.
- 2560x1440 window, 4x MSAA, High preset, vsync off, nyc1: frame time 6.07 ms with and without a recording (165 fps),
  capture work on the render thread 0.02 ms per frame. Reading the whole window instead (the first version) cost
  +1.2 ms per frame, the GPU's MSAA resolve and the 14.7 MB per frame on the bus.
- 14 s recording: 840 frames and 13.99 s of AAC; the corner of all 420 frames of a 7 s recording has no red square, a
  read-back of the window after the square gives (229, 13, 13).
- Sound against picture: `play` of a gunshot and an `echo` in the same frame, three times: the text changes at 2.000 /
  4.000 / 5.983 s, the sound starts at 2.010 / 4.016 / 6.010 s (the sound 10-27 ms later, the output latency).
- GLES 3 renderer at 2560x1440: the same GPU path, no GL errors with `gl3_debugcontext 1`.
- Quitting during a recording and `vid_restart` during one: the file is complete; a new recording after the restart
  works. Without ffmpeg (`om_rec_ffmpeg /nonexistent/ffmpeg`) the install hint is printed.
