/*
 * retrofire-wine libretro core.
 *
 * The content is a Windows program (.exe). The core does not emulate anything itself: it
 * starts a session (session/host.sh) that runs the program with Wine + Box86 inside the
 * bundled root file system, on a virtual X display (Xvfb) whose frame buffer is a file.
 * The core maps that file and hands it to the frontend as video, and sends mouse and
 * keyboard to the session through a pipe (session/rf-input.c turns them into X events).
 *
 * The bundle lives next to the cores folder of the app: <app data>/wb (run.sh, rootfs).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libretro.h"

#define SCREEN_W 640
#define SCREEN_H 480

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static char bundle_dir[1024];     /* <app data>/wb */
static char fb_path[1200];
static pid_t session_pid = -1;
static int input_fd = -1;         /* write end of the pipe to rf-input */
static uint8_t *fb_map;           /* the mapped XWD file */
static size_t fb_map_size;
static const uint8_t *fb_pixels;  /* inside fb_map */
static unsigned fb_w, fb_h, fb_pitch;
static uint32_t blank[SCREEN_W * SCREEN_H];
static int mouse_x = SCREEN_W / 2, mouse_y = SCREEN_H / 2;
static unsigned mouse_buttons, pad_state;

static void logf_(enum retro_log_level level, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (log_cb) log_cb(level, "[retrofire-wine] %s\n", buf);
	else fprintf(stderr, "[retrofire-wine] %s\n", buf);
}

/* ---- input pipe: lines "m X Y", "b BUTTON DOWN", "k KEYSYM DOWN" ---------------------- */

static void send_input(const char *fmt, ...)
{
	char buf[64];
	va_list ap;
	int n;
	if (input_fd < 0) return;
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0 && write(input_fd, buf, (size_t)n) < 0 && errno == EPIPE) { close(input_fd); input_fd = -1; }
}

/* libretro key -> X11 keysym (Latin-1 keys are the same number in both) */
static unsigned keysym_of(unsigned key)
{
	if (key >= RETROK_SPACE && key <= RETROK_z) return key;
	if (key >= RETROK_F1 && key <= RETROK_F12) return 0xFFBE + (key - RETROK_F1);
	if (key >= RETROK_KP0 && key <= RETROK_KP9) return 0xFFB0 + (key - RETROK_KP0);
	switch (key)
	{
		case RETROK_BACKSPACE: return 0xFF08;
		case RETROK_TAB: return 0xFF09;
		case RETROK_RETURN: return 0xFF0D;
		case RETROK_KP_ENTER: return 0xFF8D;
		case RETROK_ESCAPE: return 0xFF1B;
		case RETROK_DELETE: return 0xFFFF;
		case RETROK_HOME: return 0xFF50;
		case RETROK_LEFT: return 0xFF51;
		case RETROK_UP: return 0xFF52;
		case RETROK_RIGHT: return 0xFF53;
		case RETROK_DOWN: return 0xFF54;
		case RETROK_PAGEUP: return 0xFF55;
		case RETROK_PAGEDOWN: return 0xFF56;
		case RETROK_END: return 0xFF57;
		case RETROK_INSERT: return 0xFF63;
		case RETROK_LSHIFT: return 0xFFE1;
		case RETROK_RSHIFT: return 0xFFE2;
		case RETROK_LCTRL: return 0xFFE3;
		case RETROK_RCTRL: return 0xFFE4;
		case RETROK_LALT: return 0xFFE9;
		case RETROK_RALT: return 0xFFEA;
		case RETROK_CAPSLOCK: return 0xFFE5;
	}
	return 0;
}

static void RETRO_CALLCONV keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t mods)
{
	unsigned sym = keysym_of(keycode);
	(void)character; (void)mods;
	if (sym) send_input("k %u %d\n", sym, down ? 1 : 0);
}

/* ---- the session ---------------------------------------------------------------------- */

static void stop_session(void)
{
	if (input_fd >= 0) { close(input_fd); input_fd = -1; }
	if (session_pid > 0)
	{
		int i, status;
		kill(-session_pid, SIGTERM);
		for (i = 0; i < 30 && waitpid(session_pid, &status, WNOHANG) == 0; i++) usleep(100000);
		kill(-session_pid, SIGKILL);
		waitpid(session_pid, &status, 0);
		session_pid = -1;
	}
	if (fb_map) { munmap(fb_map, fb_map_size); fb_map = NULL; fb_pixels = NULL; }
}

static bool start_session(const char *exe_path)
{
	char script[1200], dir[1024], width[16], height[16];
	const char *name;
	int fds[2];

	snprintf(script, sizeof(script), "%s/host.sh", bundle_dir);
	if (access(script, R_OK) != 0) { logf_(RETRO_LOG_ERROR, "bundle not found: %s", script); return false; }
	snprintf(dir, sizeof(dir), "%s", exe_path);
	name = strrchr(dir, '/');
	if (!name) return false;
	dir[name - dir] = '\0';
	name = strrchr(exe_path, '/') + 1;
	snprintf(width, sizeof(width), "%d", SCREEN_W);
	snprintf(height, sizeof(height), "%d", SCREEN_H);

	unlink(fb_path);
	if (pipe(fds) != 0) return false;
	signal(SIGPIPE, SIG_IGN);
	session_pid = fork();
	if (session_pid < 0) { close(fds[0]); close(fds[1]); return false; }
	if (session_pid == 0)
	{
		int fd, log;
		char logpath[1200];
		setsid();
		dup2(fds[0], 3);                       /* rf-input reads its events from fd 3 */
		snprintf(logpath, sizeof(logpath), "%s/session.log", bundle_dir);
		log = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		fd = open("/dev/null", O_RDONLY);
		dup2(fd, 0);
		if (log >= 0) { dup2(log, 1); dup2(log, 2); }
		for (fd = 4; fd < 1024; fd++) close(fd);
		execl("/system/bin/sh", "sh", script, dir, name, width, height, (char *)NULL);
		_exit(127);
	}
	close(fds[0]);
	input_fd = fds[1];
	fcntl(input_fd, F_SETFL, fcntl(input_fd, F_GETFL, 0) | O_NONBLOCK);
	logf_(RETRO_LOG_INFO, "session %d started for %s", (int)session_pid, exe_path);
	return true;
}

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* Xvfb -fbdir writes an XWD file: header, colour map, then the pixels (32 bits, BGRX) */
static void map_framebuffer(void)
{
	struct stat st;
	int fd = open(fb_path, O_RDONLY);
	if (fd < 0) return;
	if (fstat(fd, &st) == 0 && st.st_size > 100)
	{
		uint8_t *m = (uint8_t *)mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (m != MAP_FAILED)
		{
			uint32_t header = be32(m), w = be32(m + 16), h = be32(m + 20), bpp = be32(m + 44), pitch = be32(m + 48), ncolors = be32(m + 76);
			size_t off = (size_t)header + (size_t)ncolors * 12;
			if (bpp == 32 && w && h && w <= 4096 && h <= 4096 && off + (size_t)pitch * h <= (size_t)st.st_size)
			{
				fb_map = m; fb_map_size = (size_t)st.st_size;
				fb_pixels = m + off; fb_w = w; fb_h = h; fb_pitch = pitch;
				logf_(RETRO_LOG_INFO, "frame buffer %ux%u mapped", w, h);
			}
			else munmap(m, (size_t)st.st_size);
		}
	}
	close(fd);
}

/* ---- libretro ------------------------------------------------------------------------- */

void retro_set_environment(retro_environment_t cb)
{
	struct retro_log_callback log;
	environ_cb = cb;
	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log)) log_cb = log.log;
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = "RetroFire Wine";
	info->library_version = "0.1";
	info->valid_extensions = "exe";
	info->need_fullpath = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = SCREEN_W;
	info->geometry.base_height = SCREEN_H;
	info->geometry.max_width = SCREEN_W;
	info->geometry.max_height = SCREEN_H;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps = 60.0;
	info->timing.sample_rate = 48000.0;
}

void retro_init(void)
{
	const char *core = NULL;
	char *slash;
	/* <app data>/cores/<core>.so -> <app data>/wb */
	if (environ_cb(RETRO_ENVIRONMENT_GET_LIBRETRO_PATH, &core) && core)
	{
		snprintf(bundle_dir, sizeof(bundle_dir), "%s", core);
		if ((slash = strrchr(bundle_dir, '/')) != NULL) *slash = '\0';
		if ((slash = strrchr(bundle_dir, '/')) != NULL) *slash = '\0';
		strncat(bundle_dir, "/wb", sizeof(bundle_dir) - strlen(bundle_dir) - 1);
	}
	snprintf(fb_path, sizeof(fb_path), "%s/rootfs/root/fb/Xvfb_screen0", bundle_dir);
}

void retro_deinit(void) { stop_session(); }

bool retro_load_game(const struct retro_game_info *game)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	struct retro_keyboard_callback kb = { keyboard_event };
	if (!game || !game->path) return false;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) return false;
	environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
	return start_session(game->path);
}

void retro_unload_game(void) { stop_session(); }

static void pad_key(unsigned id, unsigned sym)
{
	unsigned bit = 1u << id, down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id) ? bit : 0;
	if ((pad_state & bit) != down) { send_input("k %u %d\n", sym, down ? 1 : 0); pad_state = (pad_state & ~bit) | down; }
}

static void mouse_button(unsigned bit, unsigned button, bool down)
{
	unsigned now = down ? bit : 0;
	if ((mouse_buttons & bit) != now) { send_input("b %u %d\n", button, down ? 1 : 0); mouse_buttons = (mouse_buttons & ~bit) | now; }
}

void retro_run(void)
{
	static int16_t silence[800 * 2];
	int dx, dy, ax, ay, x = mouse_x, y = mouse_y;

	input_poll_cb();
	/* pointer: a real mouse, or the left stick */
	dx = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	dy = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
	ax = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
	ay = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
	if (ax > 4000 || ax < -4000) dx += ax / 4096;
	if (ay > 4000 || ay < -4000) dy += ay / 4096;
	x += dx; y += dy;
	if (x < 0) x = 0; else if (x >= SCREEN_W) x = SCREEN_W - 1;
	if (y < 0) y = 0; else if (y >= SCREEN_H) y = SCREEN_H - 1;
	if (x != mouse_x || y != mouse_y) { mouse_x = x; mouse_y = y; send_input("m %d %d\n", x, y); }
	mouse_button(1, 1, input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT)
		|| input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L));
	mouse_button(2, 3, input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT)
		|| input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R));
	/* pad: the keys most Windows games use */
	pad_key(RETRO_DEVICE_ID_JOYPAD_UP, 0xFF52);
	pad_key(RETRO_DEVICE_ID_JOYPAD_DOWN, 0xFF54);
	pad_key(RETRO_DEVICE_ID_JOYPAD_LEFT, 0xFF51);
	pad_key(RETRO_DEVICE_ID_JOYPAD_RIGHT, 0xFF53);
	pad_key(RETRO_DEVICE_ID_JOYPAD_B, 0x20);       /* space */
	pad_key(RETRO_DEVICE_ID_JOYPAD_A, 0xFF0D);     /* enter */
	pad_key(RETRO_DEVICE_ID_JOYPAD_Y, 0x7A);       /* z */
	pad_key(RETRO_DEVICE_ID_JOYPAD_X, 0x2F);       /* slash */
	pad_key(RETRO_DEVICE_ID_JOYPAD_START, 0xFFBF); /* F2 */
	pad_key(RETRO_DEVICE_ID_JOYPAD_SELECT, 0xFF1B);/* escape */

	if (!fb_pixels) map_framebuffer();
	if (fb_pixels) video_cb(fb_pixels, fb_w, fb_h, fb_pitch);
	else video_cb(blank, SCREEN_W, SCREEN_H, SCREEN_W * 4);
	audio_batch_cb(silence, 800);
}

void retro_reset(void) { }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { (void)type; (void)info; (void)num; return false; }
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
