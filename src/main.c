// The main file of the test emulator
// This is not a serious emulator intended for practical use,
// it is simply just to test stuff and showcase the API.
// Error handling has been omitted in most places.

#include "neonucleus.h"
#include "ncomplib.h"
#include "glyphcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <raylib.h>

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

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(800, 600, "NeoNucleus Test Emulator");

	// create the universe
	nn_Universe *u = nn_createUniverse(&ctx);

	nn_Architecture arch = getLuaArch();

	nn_Method sandboxMethods[] = {
		{"log", "log(msg: string) - Log to stdout", NN_DIRECT},
		{NULL},
	};
	nn_Component *ocelotCard = nn_createComponent(u, NULL, "ocelot");
	nn_setComponentMethods(ocelotCard, sandboxMethods);
	nn_setComponentHandler(ocelotCard, sandbox_handler);

	nn_Component *eepromCard = ncl_createEEPROM(u, NULL, &nn_defaultEEPROMs[3], minBIOS, strlen(minBIOS), false);

	char mainfspath[NN_MAX_PATH];
	snprintf(mainfspath, NN_MAX_PATH, "data/%s", mainDir);
	nn_Component *managedfs = ncl_createFilesystem(u, NULL, mainfspath, &nn_defaultFilesystems[3], true);
	nn_Component *tmpfs = ncl_createTmpFS(u, NULL, &nn_defaultTmpFS, NCL_FILECOST_DEFAULT, false);
	nn_Component *testingfs = ncl_createTmpFS(u, NULL, &nn_defaultFilesystems[3], NCL_FILECOST_DEFAULT, false);

	const char * const testDriveData = 
		"local g, s, d = component.list('gpu')(), component.list('screen')(), component.list('drive')()\n"
		"component.invoke(g, 'bind', s, true)\n"
		"component.invoke(g, 'set', 1, 1, 'starting...')\n"
		"local start = computer.uptime()\n"
		"local bc = component.invoke(d, 'getCapacity') / component.invoke(d, 'getSectorSize')\n"
		"for i=1,bc do component.invoke(d, 'readSector', i) end\n"
		"local now = computer.uptime()\n"
		"component.invoke(g, 'set', 1, 2, 'took ' .. (now - start) .. 's')\n"
		"while computer.uptime() < now + 3 do computer.pullSignal(0.05) end\n"
		"computer.shutdown(true)\n"
	;
	nn_Component *testDrive = ncl_createDrive(u, NULL, &nn_floppyDrive, testDriveData, strlen(testDriveData), false);

	ncl_setCLabel(managedfs, "Main Filesystem");
	ncl_setCLabel(testingfs, "Secondary Filesystem");
	ncl_setCLabel(testDrive, "Unmanaged Storage");

	size_t ramTotal = 0;
	ramTotal += nn_ramSizes[5];
	
	SetExitKey(KEY_NULL);

	const char *fontPath = getenv("NN_FONT");
	if(fontPath == NULL) fontPath = "unscii-16-full.ttf";
	ncl_GlyphCache *gc = ncl_createGlyphCache(fontPath, 20);
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
    nn_Component *keyboard = nn_createComponent(
    u, "mainKB", "keyboard");

    ncl_ScreenState *scrstate = nn_getComponentState(screen);
    ncl_mountKeyboard(scrstate, "mainKB");
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

	//nn_setTmpAddress(c, nn_getComponentAddress(tmpfs));

	nn_mountComponent(c, screen, -1);
	nn_mountComponent(c, ocelotCard, -1);
	//nn_mountComponent(c, tmpfs, -1);
    nn_mountComponent(c, keyboard, -1);
	nn_mountComponent(c, eepromCard, 0);
	nn_mountComponent(c, managedfs, 1);
	nn_mountComponent(c, gpuCard, 2);
	nn_mountComponent(c, testingfs, 3);
	nn_mountComponent(c, testDrive, 4);
	while(true) {
		if(WindowShouldClose()) break;

		BeginDrawing();
		ClearBackground(BLACK);

		// drawing the screen
		{
			ncl_ScreenState *scrbuf = nn_getComponentState(screen);
			ncl_lockScreen(scrbuf);
			size_t scrw, scrh;
			ncl_getScreenViewport(scrbuf, &scrw, &scrh);

			int cheight = GetScreenHeight() / scrh;
			if(cheight != ncl_cellHeight(gc)) {
				ncl_destroyGlyphCache(gc);
				gc = ncl_createGlyphCache(fontPath, cheight);
			}
			int cwidth = ncl_cellWidth(gc);
			int offX = (GetScreenWidth() - cwidth * scrw) / 2;
			int offY = (GetScreenHeight() - cheight * scrh) / 2;

			for(int y = 1; y <= scrh; y++) {
				for(int x = 1; x <= scrw; x++) {
					ncl_Pixel p = ncl_getScreenPixel(scrbuf, x, y);
					Vector2 pos = {
						offX + (x - 1) * cwidth,
						offY + (y - 1) * cheight,
					};
					ncl_needGlyph(gc, p.codepoint);
					DrawRectangle(pos.x, pos.y, cwidth, cheight, ne_processColor(p.bgColor));
					if(p.codepoint != 0) {
						ncl_drawGlyph(gc, p.codepoint, pos, cheight, ne_processColor(p.fgColor));
					}
				}
			}
			DrawRectangleLines(offX, offY, cwidth * scrw, cheight * scrh, WHITE);
			ncl_unlockScreen(scrbuf);
			ncl_flushGlyphs(gc);
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
			const char *t = GetClipboardText();
			if(t != NULL) nn_pushClipboard(c, "mainKB", t, player);
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
	nn_dropComponent(tmpfs);
	nn_dropComponent(testingfs);
    nn_dropComponent(testDrive);
	nn_dropComponent(screen);
	nn_dropComponent(gpuCard);
    nn_dropComponent(keyboard);
	// rip the universe
	nn_destroyUniverse(u);
	ncl_destroyGlyphCache(gc);
	CloseWindow();
	free(sand.buf);
	return 0;
}
