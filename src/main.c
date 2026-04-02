// The main file of the test emulator
// This is not a serious emulator intended for practical use,
// it is simply just to test stuff and showcase the API.
// Error handling has been omitted in most places.

#include "neonucleus.h"
#include "ncomplib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <raylib.h>
#include <errno.h>

#ifdef NN_WINDOWS
#include <setjmp.h>
#include <signal.h>
#endif

nn_Architecture getLuaArch();

static const char minBIOS[] = {
#embed "minBIOS.lua"
,'\0'
};

static nn_Exit sandbox_handler(nn_ComponentRequest *req) {
	nn_Computer *c = req->computer;
	switch(req->action) {
	case NN_COMP_INVOKE:
		if(nn_getstacksize(c) != 1) {
			nn_setError(c, "bad argument count");
			return NN_EBADCALL;
		}
		const char *s = nn_tostring(c, 0);
		puts(s);
		return NN_OK;
	case NN_COMP_CHECKMETHOD:
		req->methodEnabled = true; // all methods always enabled
		return NN_OK;
	case NN_COMP_DROP:
		return NN_OK;
	case NN_COMP_SIGNAL:
		return NN_OK;
	}
	return NN_OK;
}

Color ne_processColor(unsigned int color) {
    color <<= 8;
    color |= 0xFF;
    return GetColor(color);
}

double ne_timeProc(void *_) {
	(void)_;
	double t = GetTime();
	return (int)(t*100) / 100.0;
}

int keycode_to_oc(int keycode) {
    switch (keycode) {
        case KEY_NULL:
            return 0;
        case KEY_APOSTROPHE:
            return NN_KEY_APOSTROPHE;
        case KEY_COMMA:
            return NN_KEY_COMMA;
        case KEY_MINUS:
            return NN_KEY_MINUS;
        case KEY_PERIOD:
            return NN_KEY_PERIOD;
        case KEY_SLASH:
            return NN_KEY_SLASH;
        case KEY_ZERO:
            return NN_KEY_0;
        case KEY_ONE:
            return NN_KEY_1;
        case KEY_TWO:
            return NN_KEY_2;
        case KEY_THREE:
            return NN_KEY_3;
        case KEY_FOUR:
            return NN_KEY_4;
        case KEY_FIVE:
            return NN_KEY_5;
        case KEY_SIX:
            return NN_KEY_6;
        case KEY_SEVEN:
            return NN_KEY_7;
        case KEY_EIGHT:
            return NN_KEY_8;
        case KEY_NINE:
            return NN_KEY_9;
        case KEY_SEMICOLON:
            return NN_KEY_SEMICOLON;
        case KEY_EQUAL:
            return NN_KEY_EQUALS;
        case KEY_A:
            return NN_KEY_A;
        case KEY_B:
            return NN_KEY_B;
        case KEY_C:
            return NN_KEY_C;
        case KEY_D:
            return NN_KEY_D;
        case KEY_E:
            return NN_KEY_E;
        case KEY_F:
            return NN_KEY_F;
        case KEY_G:
            return NN_KEY_G;
        case KEY_H:
            return NN_KEY_H;
        case KEY_I:
            return NN_KEY_I;
        case KEY_J:
            return NN_KEY_J;
        case KEY_K:
            return NN_KEY_K;
        case KEY_L:
            return NN_KEY_L;
        case KEY_M:
            return NN_KEY_M;
        case KEY_N:
            return NN_KEY_N;
        case KEY_O:
            return NN_KEY_O;
        case KEY_P:
            return NN_KEY_P;
        case KEY_Q:
            return NN_KEY_Q;
        case KEY_R:
            return NN_KEY_R;
        case KEY_S:
            return NN_KEY_S;
        case KEY_T:
            return NN_KEY_T;
        case KEY_U:
            return NN_KEY_U;
        case KEY_V:
            return NN_KEY_V;
        case KEY_W:
            return NN_KEY_W;
        case KEY_X:
            return NN_KEY_X;
        case KEY_Y:
            return NN_KEY_Y;
        case KEY_Z:
            return NN_KEY_Z;
        case KEY_LEFT_BRACKET:
            return NN_KEY_LBRACKET;
        case KEY_BACKSLASH:
            return NN_KEY_BACKSLASH;
        case KEY_RIGHT_BRACKET:
            return NN_KEY_RBRACKET;
        case KEY_GRAVE:
            return NN_KEY_GRAVE;
        case KEY_SPACE:
            return NN_KEY_SPACE;
        case KEY_ESCAPE:
            return 0;
        case KEY_ENTER:
            return NN_KEY_ENTER;
        case KEY_TAB:
            return NN_KEY_TAB;
        case KEY_BACKSPACE:
            return NN_KEY_BACK;
        case KEY_INSERT:
            return NN_KEY_INSERT;
        case KEY_DELETE:
            return NN_KEY_DELETE;
        case KEY_RIGHT:
            return NN_KEY_RIGHT;
        case KEY_LEFT:
            return NN_KEY_LEFT;
        case KEY_DOWN:
            return NN_KEY_DOWN;
        case KEY_UP:
            return NN_KEY_UP;
        case KEY_PAGE_UP:
            return NN_KEY_PAGEUP;
        case KEY_PAGE_DOWN:
            return NN_KEY_PAGEDOWN;
        case KEY_HOME:
            return NN_KEY_HOME;
        case KEY_END:
            return NN_KEY_END;
        case KEY_CAPS_LOCK:
            return NN_KEY_CAPITAL;
        case KEY_SCROLL_LOCK:
            return NN_KEY_SCROLL;
        case KEY_NUM_LOCK:
            return NN_KEY_NUMLOCK;
        case KEY_PRINT_SCREEN:
            return 0;
        case KEY_PAUSE:
            return NN_KEY_PAUSE;
        case KEY_F1:
            return NN_KEY_F1;
        case KEY_F2:
            return NN_KEY_F2;
        case KEY_F3:
            return NN_KEY_F3;
        case KEY_F4:
            return NN_KEY_F4;
        case KEY_F5:
            return NN_KEY_F5;
        case KEY_F6:
            return NN_KEY_F6;
        case KEY_F7:
            return NN_KEY_F7;
        case KEY_F8:
            return NN_KEY_F8;
        case KEY_F9:
            return NN_KEY_F9;
        case KEY_F10:
            return NN_KEY_F10;
        case KEY_F11:
            return NN_KEY_F11;
        case KEY_F12:
            return NN_KEY_F12;
        case KEY_LEFT_SHIFT:
            return NN_KEY_LSHIFT;
        case KEY_LEFT_CONTROL:
            return NN_KEY_LCONTROL;
        case KEY_LEFT_ALT:
            return NN_KEY_LMENU;
        case KEY_LEFT_SUPER:
            return 0;
        case KEY_RIGHT_SHIFT:
            return NN_KEY_RSHIFT;
        case KEY_RIGHT_CONTROL:
            return NN_KEY_RCONTROL;
        case KEY_RIGHT_ALT:
            return NN_KEY_RMENU;
        case KEY_RIGHT_SUPER:
            return 0;
        case KEY_KB_MENU:
            return 0;
        case KEY_KP_0:
            return NN_KEY_NUMPAD0;
        case KEY_KP_1:
            return NN_KEY_NUMPAD1;
        case KEY_KP_2:
            return NN_KEY_NUMPAD2;
        case KEY_KP_3:
            return NN_KEY_NUMPAD3;
        case KEY_KP_4:
            return NN_KEY_NUMPAD4;
        case KEY_KP_5:
            return NN_KEY_NUMPAD5;
        case KEY_KP_6:
            return NN_KEY_NUMPAD6;
        case KEY_KP_7:
            return NN_KEY_NUMPAD7;
        case KEY_KP_8:
            return NN_KEY_NUMPAD8;
        case KEY_KP_9:
            return NN_KEY_NUMPAD9;
        case KEY_KP_DECIMAL:
            return NN_KEY_NUMPADDECIMAL;
        case KEY_KP_DIVIDE:
            return NN_KEY_NUMPADDIV;
        case KEY_KP_MULTIPLY:
            return NN_KEY_NUMPADMUL;
        case KEY_KP_SUBTRACT:
            return NN_KEY_NUMPADSUB;
        case KEY_KP_ADD:
            return NN_KEY_NUMPADADD;
        case KEY_KP_ENTER:
            return NN_KEY_NUMPADENTER;
        case KEY_KP_EQUAL:
            return NN_KEY_NUMPADEQUALS;
        case KEY_BACK:
            return 0;
        case KEY_MENU:
            return 0;
        case KEY_VOLUME_DOWN:
            return 0;
        case KEY_VOLUME_UP:
            return 0;
    }
    return 0;
}

size_t ne_alignAlloc(size_t num, size_t align) {
	if(num % align == 0) return num;
	return num + align - (num % align);
}

typedef struct ne_memSand {
	char *buf;
	size_t used;
	size_t cap;
} ne_memSand;

void *ne_sandbox_alloc(void *state, void *memory, size_t oldSize, size_t newSize) {
	ne_memSand *sand = (ne_memSand *)state;

	oldSize = ne_alignAlloc(oldSize, NN_ALLOC_ALIGN);
	newSize = ne_alignAlloc(newSize, NN_ALLOC_ALIGN);

	// never free
	if(newSize == 0) return NULL;
	if(memory == NULL) {
		if(sand->cap - sand->used < newSize) return NULL;
		// alloc new
		void *mem = sand->buf + sand->used;
		sand->used += newSize;
		return mem;
	}
	// realloc
	if(newSize <= oldSize) return memory;
	if(sand->cap - sand->used < newSize) return NULL;
	void *mem = sand->buf + sand->used;
	sand->used += newSize;
	memcpy(mem, memory, oldSize);
	return mem;
}

double accumulatedEnergyCost = 0;
double totalEnergyLoss = 0;

double ne_energy_accumulator(void *state, nn_Computer *c, double n) {
	accumulatedEnergyCost += n;
	totalEnergyLoss += n;
	return nn_getTotalEnergy(c);
}



#ifdef NN_WINDOWS
// Quick self-tests for Windows-specific fixes
// These run before anything else so failures are caught early

static jmp_buf nn_test_jmpbuf;
static volatile int nn_test_caught;

static void nn_test_crash_handler(int sig) {
    nn_test_caught = 1;
    longjmp(nn_test_jmpbuf, 1);
}

static int nn_test_try(void (*func)(void *), void *arg) {
    nn_test_caught = 0;
    signal(SIGSEGV, nn_test_crash_handler);
    signal(SIGABRT, nn_test_crash_handler);
    if(setjmp(nn_test_jmpbuf) == 0) {
        func(arg);
    }
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    return nn_test_caught;
}

static int nn_test_failed = 0;
static int nn_test_passed = 0;

static void nn_test_report(const char *name, int crashed, int expected_crash) {
    if(crashed && !expected_crash) {
        printf("[CRASH] %s\n", name);
        nn_test_failed++;
    } else if(!crashed && expected_crash) {
        printf("[FAIL]  %s (expected crash)\n", name);
        nn_test_failed++;
    } else {
        printf("[OK]    %s\n", name);
        nn_test_passed++;
    }
    fflush(stdout);
}

// --- realloc tests ---

static void nn_test_realloc_null(void *arg) {
    nn_Context *ctx = arg;
    void *p = nn_realloc(ctx, NULL, 0, 64);
    if(p == NULL) { nn_test_failed++; return; }
    nn_free(ctx, p, 64);
}

static void nn_test_realloc_grow(void *arg) {
    nn_Context *ctx = arg;
    void *a = nn_alloc(ctx, 64);
    if(a == NULL) return;
    void *b = nn_realloc(ctx, a, 64, 128);
    if(b != NULL) nn_free(ctx, b, 128);
    else nn_free(ctx, a, 64);
}

static void nn_test_realloc_free(void *arg) {
    nn_Context *ctx = arg;
    void *c = nn_alloc(ctx, 64);
    if(c == NULL) return;
    nn_realloc(ctx, c, 64, 0);
}

// --- lock tests ---

static void nn_test_lock_create_destroy(void *arg) {
    nn_Context *ctx = arg;
    nn_Lock *lock = nn_createLock(ctx);
    if(lock == NULL) { nn_test_failed++; return; }
    nn_destroyLock(ctx, lock);
}

static void nn_test_lock_cycle(void *arg) {
    nn_Context *ctx = arg;
    nn_Lock *lock = nn_createLock(ctx);
    if(lock == NULL) { nn_test_failed++; return; }
    // lock and unlock 100 times to stress it
    for(int i = 0; i < 100; i++) {
        nn_lock(ctx, lock);
        nn_unlock(ctx, lock);
    }
    nn_destroyLock(ctx, lock);
}

static void nn_test_lock_two(void *arg) {
    nn_Context *ctx = arg;
    // two locks at the same time, make sure they dont interfere
    nn_Lock *a = nn_createLock(ctx);
    nn_Lock *b = nn_createLock(ctx);
    if(a == NULL || b == NULL) { nn_test_failed++; return; }
    nn_lock(ctx, a);
    nn_lock(ctx, b);
    nn_unlock(ctx, b);
    nn_unlock(ctx, a);
    nn_destroyLock(ctx, a);
    nn_destroyLock(ctx, b);
}

// --- VFS tests ---

static void nn_test_vfs_stat(void *arg) {
    (void)arg;
    // stat current directory, should always work
    ncl_Stat s;
    bool ok = ncl_stat(ncl_defaultFS, ".", &s);
    if(!ok) nn_test_failed++;
    if(!s.isDirectory) nn_test_failed++;
}

static void nn_test_vfs_dir(void *arg) {
    (void)arg;
    void *dir = ncl_opendir(ncl_defaultFS, ".");
    if(dir == NULL) { nn_test_failed++; return; }
    char name[NN_MAX_PATH];
    // just read one entry, dont care what it is
    ncl_readdir(ncl_defaultFS, dir, name);
    ncl_closedir(ncl_defaultFS, dir);
}

static void nn_test_vfs_mkdir_remove(void *arg) {
    (void)arg;
    const char *testdir = "nn_test_tmpdir";
    ncl_mkdir(ncl_defaultFS, testdir);
    ncl_Stat s;
    bool ok = ncl_stat(ncl_defaultFS, testdir, &s);
    if(!ok || !s.isDirectory) nn_test_failed++;
    ncl_remove(ncl_defaultFS, testdir);
    // should be gone now
    ok = ncl_stat(ncl_defaultFS, testdir, &s);
    if(ok) nn_test_failed++;
}

static void nn_test_vfs_seek(void *arg) {
    (void)arg;
    // write a small file, seek around, read back
    const char *path = "nn_test_seekfile";
    const char *data = "abcdefghij";
    void *f = ncl_openfile(ncl_defaultFS, path, "w");
    if(f == NULL) { nn_test_failed++; return; }
    ncl_writefile(ncl_defaultFS, f, data, 10);
    ncl_closefile(ncl_defaultFS, f);

    f = ncl_openfile(ncl_defaultFS, path, "r");
    if(f == NULL) { nn_test_failed++; ncl_remove(ncl_defaultFS, path); return; }
    // seek to offset 5 from start
    int off = 5;
    bool ok = ncl_seekfile(ncl_defaultFS, f, NN_SEEK_SET, &off);
    if(!ok || off != 5) nn_test_failed++;
    // read from there
    char buf[5];
    size_t len = 5;
    ok = ncl_readfile(ncl_defaultFS, f, buf, &len);
    if(!ok || len != 5) nn_test_failed++;
    // should be "fghij"
    if(buf[0] != 'f' || buf[4] != 'j') nn_test_failed++;
    ncl_closefile(ncl_defaultFS, f);
    ncl_remove(ncl_defaultFS, path);
}

static void nn_run_selftests(nn_Context *ctx) {
    printf("--- nn self tests ---\n");
    fflush(stdout);

    nn_test_report("realloc(NULL)",
        nn_test_try(nn_test_realloc_null, ctx), 0);
    nn_test_report("realloc(ptr, grow)",
        nn_test_try(nn_test_realloc_grow, ctx), 0);
    nn_test_report("realloc(ptr, free)",
        nn_test_try(nn_test_realloc_free, ctx), 0);

    nn_test_report("lock create/destroy",
        nn_test_try(nn_test_lock_create_destroy, ctx), 0);
    nn_test_report("lock 100 cycles",
        nn_test_try(nn_test_lock_cycle, ctx), 0);
    nn_test_report("two locks interleaved",
        nn_test_try(nn_test_lock_two, ctx), 0);

    nn_test_report("vfs stat cwd",
        nn_test_try(nn_test_vfs_stat, NULL), 0);
    nn_test_report("vfs readdir cwd",
        nn_test_try(nn_test_vfs_dir, NULL), 0);
    nn_test_report("vfs mkdir/remove",
        nn_test_try(nn_test_vfs_mkdir_remove, NULL), 0);
    nn_test_report("vfs seek",
        nn_test_try(nn_test_vfs_seek, NULL), 0);

    printf("--- %d passed, %d failed ---\n\n", nn_test_passed, nn_test_failed);
    fflush(stdout);

    if(nn_test_failed > 0) {
        printf("self tests failed, aborting\n");
        fflush(stdout);
        exit(1);
    }
}
#endif

int main(int argc, char **argv) {
	const char *player = getenv("USER");
#ifdef NN_WINDOWS
	if(player == NULL) player = getenv("USERNAME");
#endif
	if(player == NULL) player = "me";

	bool sandboxMem = getenv("NN_MEMSAND") != NULL;
	bool showStats = getenv("NN_STAT") != NULL;

	const char *mainDir = "openos";
	if(argc > 1) mainDir = argv[1];

	nn_Context ctx;
	nn_initContext(&ctx);
	nn_initPalettes();
#ifdef NN_WINDOWS
	nn_run_selftests(&ctx);
#endif
	ne_memSand sand;
	sand.buf = NULL;
	
	if(sandboxMem) {
		// 1 MiB pre-allocated to prevent erasing the free-list
		sand.used = NN_MiB;
		sand.cap = 1 * NN_GiB;
		sand.buf = malloc(sand.cap);
		ctx.state = &sand;
		ctx.alloc = ne_sandbox_alloc;
	}

	ctx.time = ne_timeProc;

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(800, 600, "NeoNucleus Test Emulator");

	// create the universe
	nn_Universe *u = nn_createUniverse(&ctx);

	nn_Architecture arch = getLuaArch();

	nn_Method sandboxMethods[] = {
		{"log", "log(msg: string) - Log to stdout", NN_DIRECT},
		{NULL},
	};
	nn_Component *ocelotCard = nn_createComponent(u, "ocelot", "ocelot");
	nn_setComponentMethods(ocelotCard, sandboxMethods);
	nn_setComponentHandler(ocelotCard, sandbox_handler);

	const nn_VEEPROM veeprom = {
		.code = minBIOS,
		.codelen = strlen(minBIOS),
		.data = NULL,
		.datalen = 0,
		.label = NULL,
		.labellen = 0,
		.arch = NULL,
		.isReadonly = false,
	};

	nn_Component *eepromCard = nn_createVEEPROM(u, "eeprom", &veeprom, &nn_defaultEEPROMs[3]);

	nn_Component *managedfs = ncl_createFilesystem(u, "mainFS", "data/openos", &nn_defaultFilesystems[3], true);

	size_t ramTotal = 0;
	ramTotal += nn_ramSizes[5];
	
	SetExitKey(KEY_NULL);

	Font font = LoadFont("unscii-16-full.ttf");
	double tickDelay = 0.05;

	if(getenv("NN_TICKDELAY") != NULL) {
		tickDelay = atof(getenv("NN_TICKDELAY"));
	}
	
	struct {int key; nn_codepoint unicode;} keybuf[512];
	memset(keybuf, 0, sizeof(keybuf));
	size_t keycap = sizeof(keybuf) / sizeof(keybuf[0]);

	double nextTick = 0;
	double nextSecond = 0;
	double wattage = 0;

	nn_Component *screen = ncl_createScreen(u, NULL, &nn_defaultScreens[3]);
	nn_Component *gpuCard = ncl_createGPU(u, NULL, &nn_defaultGPUs[3]);

	{
		// draw test
		const char *s = "hello there";
		for(size_t i = 0; s[i]; i++) {
			unsigned char c = s[i];
			ncl_ScreenState *scrstate = nn_getComponentState(screen);
			ncl_setScreenPixel(scrstate, i+1, 1, c, 0xFFFFFF, 0x000000, false, false);
		}
	}


restart:;
	nn_Computer *c = nn_createComputer(u, NULL, "computer0", ramTotal, 256, 256);
	if(showStats) {
		// collects stats
		nn_setEnergyHandler(c, NULL, ne_energy_accumulator);
	}

	// default for 64-bit
	if(sizeof(void *) > 4) nn_setMemoryScale(c, 1.8);
	
	nn_setArchitecture(c, &arch);
	nn_addSupportedArchitecture(c, &arch);

	nn_mountComponent(c, screen, -1);
	nn_mountComponent(c, ocelotCard, -1);
	nn_mountComponent(c, eepromCard, 0);
	nn_mountComponent(c, managedfs, 1);
	nn_mountComponent(c, gpuCard, 2);

	while(true) {
		if(WindowShouldClose()) break;

		BeginDrawing();
		ClearBackground(BLACK);

		// drawing the screen
		{
			int offX = 0;
			int offY = 0;
			int cheight = 20;
			int cwidth = MeasureText("A", cheight);

			ncl_ScreenState *scrbuf = nn_getComponentState(screen);
			ncl_lockScreen(scrbuf);
			size_t scrw, scrh;
			ncl_getScreenResolution(scrbuf, &scrw, &scrh);
			for(int y = 1; y <= scrh; y++) {
				for(int x = 1; x <= scrw; x++) {
					ncl_Pixel p = ncl_getScreenPixel(scrbuf, x, y);
					Vector2 pos = {
						offX + (x - 1) * cwidth,
						offY + (y - 1) * cheight,
					};
					DrawRectangle(pos.x, pos.y, cwidth, cheight, ne_processColor(p.bgColor));
					DrawTextCodepoint(font, p.codepoint, pos, cheight, ne_processColor(p.fgColor));
				}
			}
			ncl_unlockScreen(scrbuf);
		}

		int statY = 10;
		if(sand.buf != NULL) {
			DrawText(TextFormat("mem used: %.2f%%", (double)sand.used / sand.cap * 100), 10, statY, 20, YELLOW);
			statY += 20;
		}
		if(showStats) {
			double memUsagePercent = (double)nn_getUsedMemory(c) * 100 / nn_getTotalMemory(c);
			DrawText(TextFormat("power usage: %.2f W", wattage), 10, statY, 20, GREEN);
			statY += 20;
			DrawText(TextFormat("energy loss: %.2f J", totalEnergyLoss), 10, statY, 20, GREEN);
			statY += 20;
			DrawText(TextFormat("VM mem usage: %.2f%%", memUsagePercent), 10, statY, 20, GREEN);
			statY += 20;
		}

		EndDrawing();

		// keyboard input

		// 1: clipboard
		if(IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
			nn_pushClipboard(c, "mainKB", GetClipboardText(), player);
		}

		while(1) {
			int keycode = GetKeyPressed();
			nn_codepoint unicode = GetCharPressed();

			if(keycode == 0 && unicode == 0) break;
			
			keybuf[keycode].key = keycode;
			keybuf[keycode].unicode = unicode;

			if(keycode != 0) {
				if(keycode == KEY_ENTER) unicode = '\r';
				if(keycode == KEY_BACKSPACE) unicode = '\b';
				if(keycode == KEY_TAB) unicode = '\t';
			}

			nn_pushKeyDown(c, "mainKB", unicode, keycode_to_oc(keycode), player);
		}

		for(size_t i = 0; i < keycap; i++) {
			if(keybuf[i].key != 0) {
				if(IsKeyReleased(keybuf[i].key)) {
					int key = keycode_to_oc(keybuf[i].key);
					keybuf[i].key = 0;
					nn_pushKeyUp(c, "mainKB", keybuf[i].unicode, key, player);
				}
			}
		}

		double tickNow = GetTime();

		if(tickNow >= nextSecond) {
			nextSecond = tickNow + 1;
			wattage = accumulatedEnergyCost;
			accumulatedEnergyCost = 0;
		}

		if(tickNow >= nextTick) {
			accumulatedEnergyCost = 0;
			nextTick = tickNow + tickDelay;
			nn_clearstack(c);

			if(getenv("NN_NOIDLE") != NULL) nn_resetIdleTime(c);
			nn_Exit e = nn_tick(c);
			if(e != NN_OK) {
				nn_setErrorFromExit(c, e);
				printf("error: %s\n", nn_getError(c));
				goto cleanup;
			}

			nn_ComputerState state = nn_getComputerState(c);
			if(state == NN_POWEROFF) break;
			if(state == NN_CRASHED) {
				printf("error: %s\n", nn_getError(c));
				goto cleanup;
			}

			if(state == NN_CHARCH) {
				printf("new arch: %s\n", nn_getDesiredArchitecture(c).name);
				goto cleanup;
			}
			if(state == NN_BLACKOUT) {
				printf("out of energy\n");
				goto cleanup;
			}
			if(state == NN_RESTART) {
				printf("restart requested\n");
				nn_destroyComputer(c);
				goto restart;
			}
		}
	}

cleanup:;
	nn_destroyComputer(c);
	nn_dropComponent(ocelotCard);
	nn_dropComponent(eepromCard);
	nn_dropComponent(managedfs);
	nn_dropComponent(screen);
	nn_dropComponent(gpuCard);
	// rip the universe
	nn_destroyUniverse(u);
	UnloadFont(font);
	CloseWindow();
	free(sand.buf);
	return 0;
}
