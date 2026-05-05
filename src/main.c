// The main file of the test emulator
// This is not a serious emulator intended for practical use,
// it is simply just to test stuff and showcase the API.
// Error handling has been omitted in most places.

#include "neonucleus.h"
#include "ncomplib.h"
#include "glyphcache.h"
#include <ctype.h>
#include <math.h>
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

static nn_Exit ne_dataBullshit(nn_DataCardRequest *req) {
	nn_Computer *C = req->computer;
	nn_DataCardAction action = req->action;
	nn_Exit e;

	if(action == NN_DATA_DROP) {
		return NN_OK;
	}
	if(action == NN_DATA_ENCODE64) {
		int outSize = 0;
		char *out = EncodeDataBase64((const unsigned char *)req->data, req->datalen, &outSize);
		if(out == NULL) return NN_ENOMEM;
		// -1 because raylib includes the NUL terminator??
		e = nn_pushlstring(C, out, outSize-1);
		MemFree(out);
		return e;
	}
	if(action == NN_DATA_DECODE64) {
		int outSize = 0;
		char *out = (char *)DecodeDataBase64(req->data, &outSize);
		if(out == NULL) return NN_ENOMEM;
		e = nn_pushlstring(C, out, outSize);
		MemFree(out);
		return e;
	}
	if(action == NN_DATA_DEFLATE) {
		int outSize = 0;
		char *out = (char *)CompressData((const unsigned char *)req->data, req->datalen, &outSize);
		if(out == NULL) return NN_ENOMEM;
		e = nn_pushlstring(C, out, outSize);
		MemFree(out);
		return e;
	}
	if(action == NN_DATA_INFLATE) {
		int outSize = 0;
		char *out = (char *)DecompressData((unsigned char *)req->data, req->datalen, &outSize);
		if(out == NULL) return NN_ENOMEM;
		e = nn_pushlstring(C, out, outSize);
		MemFree(out);
		return e;
	}
	if(action == NN_DATA_CRC32) {
		unsigned int check = nn_computeCRC32(req->crc32.data, req->crc32.datalen);
		req->crc32.checksum[0] = (check >> 0) & 0xFF;
		req->crc32.checksum[1] = (check >> 8) & 0xFF;
		req->crc32.checksum[2] = (check >> 16) & 0xFF;
		req->crc32.checksum[3] = (check >> 24) & 0xFF;
		return NN_OK;
	}
	if(action == NN_DATA_MD5) {
		unsigned int *out = ComputeMD5((unsigned char *)req->md5.data, req->md5.datalen);
		if(out == NULL) return NN_ENOMEM;
		memcpy(req->md5.checksum, out, 16);
		return NN_OK;
	}
	if(action == NN_DATA_SHA256) {
		// does not match OC, dunno why
		unsigned int *out = ComputeSHA256((unsigned char *)req->sha256.data, req->sha256.datalen);
		if(out == NULL) return NN_ENOMEM;
		memcpy(req->sha256.checksum, out, 32);
		return NN_OK;
	}
	if(action == NN_DATA_RANDOM) {
		for(size_t i = 0; i < req->randbuf.buflen; i++) {
			req->randbuf.buf[i] = rand();
		}
		return NN_OK;
	}
	if(action == NN_DATA_ENCRYPT) {
		return nn_pushlstring(C, req->data, req->datalen);
	}
	if(action == NN_DATA_DECRYPT) {
		return nn_pushlstring(C, req->data, req->datalen);
	}

	if(C) nn_setError(C, "ne: data method not implemented");
	return NN_EBADCALL;
}

static nn_Exit ne_modemBullshit(nn_ModemRequest *req) {
	nn_Computer *C = req->computer;

	if(req->action == NN_MODEM_DROP) {
		return NN_OK;
	}
	if(req->action == NN_MODEM_SEND) {
		printf("Transmission from %s to %s (port %zu) of %zu bytes (%zu values)\n", req->localAddress, req->send.address == NULL ? "*" : req->send.address, req->send.port, req->send.contents->buflen, req->send.contents->valueCount);
		return nn_pushModemMessage(C, req->localAddress, nn_getComputerAddress(C), req->send.port, 0, req->send.contents);
	}

	if(C) nn_setError(C, "ne: modem method not implemented");
	return NN_EBADCALL;
}

static unsigned char ne_processColorPart(unsigned char channel, double brightness) {
	double n = (double)channel / 255;
	n *= brightness;
	if(n < 0) n = 0;
	if(n > 1) n = 1;
	return n * 255;
}

Color ne_processColor(unsigned int color, double brightness) {
    color <<= 8;
    color |= 0xFF;
    Color c = GetColor(color);
	c.r = ne_processColorPart(c.r, brightness);
	c.g = ne_processColorPart(c.g, brightness);
	c.b = ne_processColorPart(c.b, brightness);
	return c;
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
// default capacity of a tablet
double allEnergy = 10000;

void ne_env(nn_EnvironmentRequest *req) {
	if(req->action == NN_ENV_BEEP) {
		printf("beep: %f Hz %fs %.02f%%\n", req->beep.frequency, req->beep.duration, req->beep.volume*100);
		return;
	}
	if(req->action == NN_ENV_DRAWENERGY) {
		accumulatedEnergyCost += req->energy;
		totalEnergyLoss += req->energy;
		allEnergy -= req->energy;
		req->energy = nn_getTotalEnergy(req->computer);
		req->energy = allEnergy;
		return;
	}
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
	nn_Universe *u = nn_createUniverse(&ctx, NULL);

	nn_Architecture arch = getLuaArch();

	nn_Method sandboxMethods[] = {
		{"log", "log(msg: string) - Log to stdout", NN_DIRECT},
		{NULL},
	};
	nn_Component *ocelotCard = nn_createComponent(u, NULL, "ocelot");
	nn_setComponentMethods(ocelotCard, sandboxMethods);
	nn_setComponentHandler(ocelotCard, sandbox_handler);

	nn_Component *dataCard = nn_createDataCard(u, NULL, &nn_defaultDataCards[2], NULL, ne_dataBullshit);
	nn_Component *modem = nn_createModem(u, NULL, &nn_defaultWiredModem, NULL, ne_modemBullshit);

	char *eepromCode = (char *)minBIOS;
	size_t eepromSize = strlen(minBIOS);
	const char *eepromPath = getenv("NN_EEPROM");
	if(eepromPath != NULL) {
		FILE *eeprom = fopen(eepromPath, "rb");
		if(eeprom == NULL) {
			fprintf(stderr, "no such eeprom: %s\n", eepromPath);
			return 1;
		}
		
		fseek(eeprom, 0, SEEK_END);
		eepromSize = ftell(eeprom);
		fseek(eeprom, 0, SEEK_SET);

		eepromCode = malloc(eepromSize);
		size_t amount = 0;
		while(amount < eepromSize) {
			amount += fread(eepromCode + amount, sizeof(char), eepromSize - amount, eeprom);
		}
	}

	nn_Component *eepromCard = ncl_createEEPROM(u, NULL, &nn_defaultEEPROMs[3], eepromCode, eepromSize, false);

	nn_Filesystem mainfsconf;
	nn_Filesystem fsparts[] = {
		nn_defaultFilesystems[3],
		nn_defaultFilesystems[3],
		nn_defaultFilesystems[3],
	};
	nn_mergeFilesystems(&mainfsconf, fsparts, sizeof(fsparts) / sizeof(fsparts[0]));

	char mainfspath[NN_MAX_PATH];
	snprintf(mainfspath, NN_MAX_PATH, "data/%s", mainDir);
	nn_Component *managedfs = ncl_createFilesystem(u, NULL, mainfspath, &mainfsconf, true);
	//nn_Component *tmpfs = ncl_createTmpFS(u, NULL, &nn_defaultTmpFS, NCL_FILECOST_DEFAULT, false);
	nn_Component *tmpfs = ncl_createFilesystem(u, NULL, "/tmp", &mainfsconf, false);
	nn_Component *testingfs = ncl_createFilesystem(u, NULL, "aux", &nn_defaultFilesystems[3], false);

	const char * const testDriveData = 
		"local g, s = component.list('gpu')(), component.list('screen')()\n"
		"local d = computer.getBootAddress()\n"
		"component.invoke(g, 'bind', s, true)\n"
		"component.invoke(g, 'set', 1, 1, 'starting sequential bench...')\n"
		"local start = computer.uptime()\n"
		"local ss = component.invoke(d, 'getSectorSize')\n"
		"local cap = component.invoke(d, 'getCapacity')\n"
		"local bc = cap / ss\n"
		"local tc = 256\n"
		"for i=1,tc do component.invoke(d, 'readSector', i) end\n"
		"local now = computer.uptime()\n"
		"component.invoke(g, 'set', 1, 2, 'took ' .. (now - start) .. 's')\n"
		"component.invoke(g, 'set', 1, 3, 'sequential read speed: ' .. (tc * ss / (now - start)) .. 'B/s')\n"
		"while computer.uptime() < now + 3 do computer.pullSignal(0.05) end\n"
		"component.invoke(g, 'bind', s, true)\n"
		"component.invoke(g, 'set', 1, 1, 'starting random bench...')\n"
		"start = computer.uptime()\n"
		"for i=1,tc do local i = math.random(1, bc) component.invoke(d, 'readSector', i) end\n"
		"now = computer.uptime()\n"
		"component.invoke(g, 'set', 1, 2, 'took ' .. (now - start) .. 's')\n"
		"component.invoke(g, 'set', 1, 3, 'random read speed: ' .. (tc * ss / (now - start)) .. 'B/s')\n"
		"while computer.uptime() < now + 3 do computer.pullSignal(0.05) end\n"
		"computer.shutdown(true)\n"
	;
	nn_Component *testDrive = ncl_createDrive(u, NULL, &nn_defaultDrives[3], testDriveData, strlen(testDriveData), false);
	nn_Component *testFlash = ncl_createFlash(u, NULL, &nn_defaultSSDs[3], testDriveData, strlen(testDriveData), false);

	ncl_setCLabel(eepromCard, "EEPROM");
	ncl_setCLabel(managedfs, "Main Filesystem");
	ncl_setCLabel(testingfs, "Secondary Filesystem");
	ncl_setCLabel(testDrive, "Unmanaged Storage");
	ncl_setCLabel(testFlash, "Flash Storage");

	size_t ramTotal = 0;
	ramTotal += 4 * nn_ramSizes[5];
	//ramTotal += nn_ramSizes[0];
	
	SetExitKey(KEY_NULL);

	const char *fontPath = getenv("NN_FONT");
	if(fontPath == NULL) fontPath = "unscii-16-full.ttf";
	ncl_GlyphCache *gc = ncl_createGlyphCache(fontPath, 20);
	double tickDelay = 0.05;
	bool noIdle = getenv("NN_NOIDLE") != NULL;

	if(getenv("NN_TICKDELAY") != NULL) {
		tickDelay = atof(getenv("NN_TICKDELAY"));
	}
	if(getenv("NN_FAST") != NULL) {
		tickDelay = 0;
		noIdle = true;
	}
	
	struct {int key; nn_codepoint unicode;} keybuf[512];
	memset(keybuf, 0, sizeof(keybuf));
	size_t keycap = sizeof(keybuf) / sizeof(keybuf[0]);

	double nextTick = 0;
	double nextSecond = 0;
	double wattage = 0;

	nn_Component *screen = ncl_createScreen(u, NULL, &nn_defaultScreens[2]);
	nn_Component *gpuCard = ncl_createGPU(u, NULL, &nn_defaultGPUs[2]);
    nn_Component *keyboard = nn_createComponent(
    u, "mainKB", "keyboard");

    ncl_ScreenState *scrstate = nn_getComponentState(screen);
    ncl_mountKeyboard(scrstate, "mainKB");

	nn_Computer *c = nn_createComputer(u, NULL, NULL, ramTotal, 256, 256);
	nn_Environment cEnv = {
		.userdata = NULL,
		.handler = ne_env,
	};
	nn_setComputerEnvironment(c, cEnv);
	nn_setCallBudget(c, 0);
	nn_setTotalEnergy(c, allEnergy);
	
	nn_setArchitecture(c, &arch);
	nn_addSupportedArchitecture(c, &arch);

	nn_setTmpAddress(c, nn_getComponentAddress(tmpfs));

	nn_CommonDeviceInfo cinfo;
	nn_clearCommonDeviceInfo(&cinfo);
	cinfo.CLASS = NN_DEVICECLASS_SYSTEM;
	cinfo.DESC = "The main computer";
	cinfo.VENDOR = "NeoNucleus Inc.";
	cinfo.PRODUCT = "NeoComputer";
	cinfo.VERSION = "0.-1.0";
	cinfo.CLOCK = "1 MHz";
	cinfo.CAPACITY = "10 kJ";

	nn_addCommonDeviceInfo(c, nn_getComputerAddress(c), cinfo);

	nn_mountComponent(c, screen, -1, false);
	nn_mountComponent(c, ocelotCard, -1, false);
	nn_mountComponent(c, tmpfs, -1, false);
	nn_mountComponent(c, keyboard, -1, false);
	nn_mountComponent(c, eepromCard, 0, false);
	nn_mountComponent(c, managedfs, 1, false);
	nn_mountComponent(c, gpuCard, 2, false);
	nn_mountComponent(c, testingfs, 3, false);
	nn_mountComponent(c, testDrive, 4, false);
	nn_mountComponent(c, testFlash, 5, false);
	nn_mountComponent(c, dataCard, 6, false);
	nn_mountComponent(c, modem, 7, false);
	int ltx = 0, lty = 0;
	double scrollBuf = 0;
	double tickTime = 0;
	SetTargetFPS(60);
	while(true) {
		if(WindowShouldClose()) break;

		BeginDrawing();
		ClearBackground(BLACK);

		// drawing the screen + screen events
		{
			const char *scraddr = nn_getComponentAddress(screen);
			ncl_ScreenState *scrbuf = nn_getComponentState(screen);
			ncl_lockScreen(scrbuf);
			size_t scrw, scrh;
			ncl_getScreenViewport(scrbuf, &scrw, &scrh);

			ncl_ScreenFlags scrflags = ncl_getScreenFlags(scrbuf);

			int cheight = GetScreenHeight() / scrh;
			if(cheight != ncl_cellHeight(gc)) {
				ncl_destroyGlyphCache(gc);
				gc = ncl_createGlyphCache(fontPath, cheight);
			}
			int cwidth = ncl_cellWidth(gc);
			int offX = (GetScreenWidth() - cwidth * scrw) / 2;
			int offY = (GetScreenHeight() - cheight * scrh) / 2;

			double scrbright = ncl_getScreenBrightness(scrbuf);
			if(scrflags & NCL_SCREEN_ON) {
				for(int y = 1; y <= scrh; y++) {
					for(int x = 1; x <= scrw; x++) {
						ncl_Pixel p = ncl_getScreenPixel(scrbuf, x, y);
						Vector2 pos = {
							offX + (x - 1) * cwidth,
							offY + (y - 1) * cheight,
						};
						DrawRectangle(pos.x, pos.y, cwidth, cheight, ne_processColor(p.bgColor, scrbright));
					}
				}
				for(int y = 1; y <= scrh; y++) {
					for(int x = 1; x <= scrw; x++) {
						ncl_Pixel p = ncl_getScreenPixel(scrbuf, x, y);
						Vector2 pos = {
							offX + (x - 1) * cwidth,
							offY + (y - 1) * cheight,
						};
						ncl_needGlyph(gc, p.codepoint);
						if(p.codepoint != 0) {
							ncl_drawGlyph(gc, p.codepoint, pos, cheight, ne_processColor(p.fgColor, scrbright));
						}
					}
				}
			}
			DrawRectangleLines(offX, offY, cwidth * scrw, cheight * scrh, WHITE);
			ncl_unlockScreen(scrbuf);
			ncl_flushGlyphs(gc);

			int tx = (double)(GetMouseX() - offX) / cwidth + 1;
			int ty = (double)(GetMouseY() - offY) / cheight + 1;

			if(tx >= 1 && ty >= 1 && tx <= scrw && ty <= scrh) {
				struct {int btn; int ocbtn;} btns[] = {
					{MOUSE_BUTTON_LEFT, 0},
					{MOUSE_BUTTON_RIGHT, 1},
					{MOUSE_BUTTON_MIDDLE, 2},
				};
				size_t btnc = sizeof(btns) / sizeof(btns[0]);
				for(size_t i = 0; i < btnc; i++) {
					// we only care about left click here
					int mbtn = btns[i].btn;
					int ocbtn = btns[i].ocbtn;
					if(IsMouseButtonPressed(mbtn)) {
						nn_pushTouch(c, scraddr, tx, ty, ocbtn, player);
					}
					if(IsMouseButtonReleased(mbtn)) {
						nn_pushDrop(c, scraddr, tx, ty, ocbtn, player);
					}
					if(IsMouseButtonDown(mbtn)) {
						if(ltx != tx || lty != ty) {
							nn_pushDrag(c, scraddr, tx, ty, ocbtn, player);
						}
					}
				}
				if(fabs(scrollBuf) >= 1) {
					nn_pushScroll(c, scraddr, tx, ty, scrollBuf, player);
					scrollBuf = 0;
				}
				ltx = tx;
				lty = ty;
			}
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
			DrawText(TextFormat("Tick time: %.5fs", tickTime), 10, statY, 20, (tickTime < tickDelay || tickDelay == 0) ? GREEN : RED);
			statY += 20;
		}

		EndDrawing();

		scrollBuf += GetMouseWheelMove();

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

				bool isCtrlPressed = keybuf[KEY_LEFT_CONTROL].key != 0;
				if(isCtrlPressed && isalpha(keycode)) {
					unicode = keycode - 'A' + 1;
				}
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
				// unicode keys handled by raylib
				if(IsKeyPressedRepeat(keybuf[i].key) && keybuf[i].unicode == 0) {
					int key = keycode_to_oc(keybuf[i].key);
					nn_pushKeyDown(c, "mainKB", keybuf[i].unicode, key, player);
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

			nn_removeEnergy(c, ncl_getScreenEnergyUsage(nn_getComponentState(screen)));

			if(noIdle) nn_resetIdleTime(c);
			// OC computers consume 0.5W when running, 0.05W when running but idle
			double normalPowerUsage = 0.5, idlePowerUsage = 0.05;
			bool isIdle = false;
			nn_Exit e = nn_tick(c);
			tickTime = GetTime() - tickNow;
			if(tickTime < tickDelay) {
				double working = tickTime / tickDelay;
				nn_removeEnergy(c, normalPowerUsage * working + idlePowerUsage * (1 - working));
			} else if(tickDelay == 0) {
				nn_removeEnergy(c, normalPowerUsage);
			} else {
				nn_removeEnergy(c, normalPowerUsage * tickTime / tickDelay);
			}
			if(e != NN_OK) {
				printf("error: %s\n", nn_getError(c));
				goto cleanup;
			}
			e = nn_tickSynchronized(c);
			if(e != NN_OK) {
				printf("sync method error: %s\n", nn_getError(c));
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
				nn_stopComputer(c);
				ncl_resetScreen(nn_getComponentState(screen));
				nn_addIdleTime(c, 1);
				continue;
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
	nn_dropComponent(testFlash);
	nn_dropComponent(screen);
	nn_dropComponent(gpuCard);
	nn_dropComponent(keyboard);
	nn_dropComponent(dataCard);
	nn_dropComponent(modem);
	// rip the universe
	nn_destroyUniverse(u);
	ncl_destroyGlyphCache(gc);
	if(eepromPath != NULL) free(eepromCode);
	CloseWindow();
	free(sand.buf);
	return 0;
}
