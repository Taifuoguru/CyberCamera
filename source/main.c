#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <malloc.h>
#include <dirent.h>
#include <sys/stat.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240
#define FRAME_PIXELS (TOP_W * TOP_H)
#define FRAME_BYTES (FRAME_PIXELS * 2)
#define CAM_BUF_BYTES (FRAME_BYTES * 2)
#define WAIT_TIMEOUT 1000000000ULL
#define CONFIG_3D_SLIDERSTATE (*(volatile float*)0x1FF81080)
#define APP_DIR "sdmc:/3ds/CyberCamera"
#define CAP_DIR "sdmc:/3ds/CyberCamera/captures"
#define SETTINGS_PATH "sdmc:/3ds/CyberCamera/settings.ini"
#define BUILD_LABEL "CYBERCAMERA 20260703"

typedef enum {
	MODE_CAMERA,
	MODE_GALLERY
} Mode;

typedef enum {
	F_NORMAL,
	F_MATRIX,
	F_RED,
	F_AMBER,
	F_ICE,
	F_THERMAL,
	F_CRT,
	F_NIGHT,
	F_GAMEBOY,
	F_MAC1,
	F_EGA,
	F_PS1,
	F_SOLAR,
	F_VECTOR,
	F_OVER,
	F_COUNT
} Filter;

typedef struct {
	Mode mode;
	Filter filter;
	bool front;
	bool hud;
	bool dateStamp;
	bool capture;
	bool cameraDirty;
	bool deletePhoto;
	bool interrupted;
	bool dirtyBottom;
	bool stereoSave;
	int brightness;
	int contrast;
	int saved;
	int saveScale;
	int timerSeconds;
	int countdown;
	u64 timerStart;
	char status[48];
	char photos[128][128];
	int photoCount;
	int photoIndex;
	int loadedIndex;
	bool loadedOk;
} App;

typedef struct {
	int y, mo, d, h, mi, s;
} Clock;

static u16 frame[FRAME_PIXELS];
static u16 rightFrame[FRAME_PIXELS];
static u16 galleryFrame[FRAME_PIXELS];
static u16 shotFrame[FRAME_PIXELS];
static u16 anaglyphFrame[FRAME_PIXELS];

static const char* filterNames[F_COUNT] = {
	"NORMAL", "MATRIX", "RED", "AMBER", "ICE", "THERMAL", "CRT",
	"NIGHT", "GAMEBOY", "MAC1", "EGA", "PS1", "SOLAR", "VECTOR", "OVER"
};

static const u8 font[37][7] = {
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
	{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
	{0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
	{0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
	{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
	{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
	{0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
	{0,0,0,0,0,0,0}
};

static inline u8 camR(u16 c) { return (c & 0x1F) << 3; }
static inline u8 camG(u16 c) { return ((c >> 5) & 0x3F) << 2; }
static inline u8 camB(u16 c) { return ((c >> 11) & 0x1F) << 3; }
static inline u16 makePx(int r, int g, int b) {
	if (r < 0) r = 0;
	if (r > 255) r = 255;
	if (g < 0) g = 0;
	if (g > 255) g = 255;
	if (b < 0) b = 0;
	if (b > 255) b = 255;
	return ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
}

static int glyphIndex(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'Z') return 10 + c - 'A';
	if (c >= 'a' && c <= 'z') return 10 + c - 'a';
	return 36;
}

static void fbPx(void* fb, int w, int h, int x, int y, u8 r, u8 g, u8 b) {
	if (x < 0 || y < 0 || x >= w || y >= h) return;
	u8* p = (u8*)fb;
	int v = ((h - 1 - y) + x * h) * 3;
	p[v] = b;
	p[v + 1] = g;
	p[v + 2] = r;
}

static void fbFill(void* fb, int w, int h, int x, int y, int rw, int rh, u8 r, u8 g, u8 b) {
	for (int yy = 0; yy < rh; yy++)
		for (int xx = 0; xx < rw; xx++)
			fbPx(fb, w, h, x + xx, y + yy, r, g, b);
}

static void fbRect(void* fb, int w, int h, int x, int y, int rw, int rh, u8 r, u8 g, u8 b) {
	for (int i = 0; i < rw; i++) {
		fbPx(fb, w, h, x + i, y, r, g, b);
		fbPx(fb, w, h, x + i, y + rh - 1, r, g, b);
	}
	for (int i = 0; i < rh; i++) {
		fbPx(fb, w, h, x, y + i, r, g, b);
		fbPx(fb, w, h, x + rw - 1, y + i, r, g, b);
	}
}

static void fbText(void* fb, int w, int h, int x, int y, const char* s, u8 r, u8 g, u8 b) {
	int cx = x;
	while (*s) {
		char c = *s++;
		if (c == '\n') { cx = x; y += 9; continue; }
		if (c == ':' || c == '.' || c == '/' || c == '-' || c == '_') {
			for (int yy = 0; yy < 7; yy++) {
				bool on = (c == ':' && (yy == 2 || yy == 5)) || (c == '.' && yy == 6) ||
					(c == '-' && yy == 3) || (c == '_' && yy == 6) || (c == '/' && yy >= 1 && yy <= 5);
				if (on) fbPx(fb, w, h, cx + (c == '/' ? 5 - yy : 2), y + yy, r, g, b);
			}
			cx += 6;
			continue;
		}
		int gi = glyphIndex(c);
		for (int yy = 0; yy < 7; yy++)
			for (int xx = 0; xx < 5; xx++)
				if (font[gi][yy] & (1 << (4 - xx))) fbPx(fb, w, h, cx + xx, y + yy, r, g, b);
		cx += 6;
	}
}

static Clock nowClock(void) {
	time_t raw = time(NULL);
	struct tm* t = localtime(&raw);
	Clock c;
	c.y = t ? t->tm_year + 1900 : 2026;
	c.mo = t ? t->tm_mon + 1 : 7;
	c.d = t ? t->tm_mday : 3;
	c.h = t ? t->tm_hour : 0;
	c.mi = t ? t->tm_min : 0;
	c.s = t ? t->tm_sec : 0;
	return c;
}

static void ensureDirs(void) {
	mkdir("sdmc:/3ds", 0777);
	mkdir(APP_DIR, 0777);
	mkdir(CAP_DIR, 0777);
}

static void saveSettings(App* app) {
	FILE* fp = fopen(SETTINGS_PATH, "w");
	if (!fp) return;
	fprintf(fp, "filter=%d\n", (int)app->filter);
	fprintf(fp, "front=%d\n", app->front ? 1 : 0);
	fprintf(fp, "hud=%d\n", app->hud ? 1 : 0);
	fprintf(fp, "datestamp=%d\n", app->dateStamp ? 1 : 0);
	fprintf(fp, "scale=%d\n", app->saveScale);
	fprintf(fp, "timer=%d\n", app->timerSeconds);
	fprintf(fp, "stereo=%d\n", app->stereoSave ? 1 : 0);
	fclose(fp);
}

static void loadSettings(App* app) {
	FILE* fp = fopen(SETTINGS_PATH, "r");
	if (!fp) return;
	char key[32];
	int val = 0;
	while (fscanf(fp, "%31[^=]=%d\n", key, &val) == 2) {
		if (strcmp(key, "filter") == 0 && val >= 0 && val < F_COUNT) app->filter = (Filter)val;
		else if (strcmp(key, "front") == 0) app->front = val != 0;
		else if (strcmp(key, "hud") == 0) app->hud = val != 0;
		else if (strcmp(key, "datestamp") == 0) app->dateStamp = val != 0;
		else if (strcmp(key, "scale") == 0) app->saveScale = val == 2 ? 2 : 1;
		else if (strcmp(key, "timer") == 0) app->timerSeconds = (val == 3 || val == 5 || val == 10) ? val : 0;
		else if (strcmp(key, "stereo") == 0) app->stereoSave = val != 0;
	}
	fclose(fp);
}

static int lum(u16 c) {
	return (camR(c) * 30 + camG(c) * 59 + camB(c) * 11) / 100;
}

static void filterFrame(App* app, u16* f) {
	for (int i = 0; i < FRAME_PIXELS; i++) {
		int r = camR(f[i]), g = camG(f[i]), b = camB(f[i]);
		int l = (r * 30 + g * 59 + b * 11) / 100;
		if (app->filter == F_MATRIX) { r = 0; g = l > 70 ? 255 : l * 2; b = 0; }
		else if (app->filter == F_RED) { r = l > 70 ? 255 : l * 2; g = 0; b = 0; }
		else if (app->filter == F_AMBER) { r = l; g = l * 3 / 4; b = l / 8; }
		else if (app->filter == F_ICE) { r = 0; g = l; b = l > 60 ? 255 : l * 2; }
		else if (app->filter == F_THERMAL) {
			if (l < 85) { r = 0; g = l * 2; b = 255 - l; }
			else if (l < 170) { r = (l - 85) * 3; g = 255; b = 0; }
			else { r = 255; g = 255 - (l - 170) * 3; b = 0; }
		}
		else if (app->filter == F_NIGHT) { r = l / 8; g = l + 60; b = l / 8; }
		else if (app->filter == F_GAMEBOY) {
			static const int p[4][3] = {{15,56,15},{48,98,48},{139,172,15},{155,188,15}};
			int q = l / 64; if (q > 3) q = 3; r = p[q][0]; g = p[q][1]; b = p[q][2];
		}
		else if (app->filter == F_MAC1) { r = g = b = l > 128 ? 255 : 0; }
		else if (app->filter == F_SOLAR && l > 128) { r = 255 - r; g = 255 - g; b = 255 - b; }
		else {
			r = ((r - 128) * app->contrast) / 100 + 128 + app->brightness;
			g = ((g - 128) * app->contrast) / 100 + 128 + app->brightness;
			b = ((b - 128) * app->contrast) / 100 + 128 + app->brightness;
			if (app->filter == F_PS1) { r &= 0xF8; g &= 0xFC; b &= 0xF8; }
			if (app->filter == F_OVER) { r = (r - 128) * 2 + 128; g = (g - 128) * 2 + 128; b = (b - 128) * 2 + 128; }
		}
		f[i] = makePx(r, g, b);
	}
	if (app->filter == F_CRT) {
		for (int y = 1; y < TOP_H; y += 2)
			for (int x = 0; x < TOP_W; x++) {
				u16 c = f[y * TOP_W + x];
				f[y * TOP_W + x] = makePx(camR(c) / 3, camG(c) / 3, camB(c) / 3);
			}
	}
	if (app->filter == F_VECTOR) {
		static u8 lumaBuf[FRAME_PIXELS];
		for (int i = 0; i < FRAME_PIXELS; i++) lumaBuf[i] = lum(f[i]);
		for (int y = 1; y < TOP_H - 1; y++)
			for (int x = 1; x < TOP_W - 1; x++) {
				int i = y * TOP_W + x;
				int e = abs(lumaBuf[i - 1] - lumaBuf[i + 1]) + abs(lumaBuf[i - TOP_W] - lumaBuf[i + TOP_W]);
				f[i] = e > 45 ? makePx(0, 255, 170) : makePx(0, 0, 0);
			}
	}
}

static void drawFrame(void* fb, const u16* f) {
	u8* out = (u8*)fb;
	for (int y = 0; y < TOP_H; y++) {
		for (int x = 0; x < TOP_W; x++) {
			u16 data = f[y * TOP_W + x];
			int drawY = TOP_H - 1 - y;
			int v = (drawY + x * TOP_H) * 3;
			out[v] = camB(data);
			out[v + 1] = camG(data);
			out[v + 2] = camR(data);
		}
	}
}

static void copyCamToFrame(const u8* camBuf, u16* out) {
	memcpy(out, camBuf, FRAME_BYTES);
}

static void clearTopBoth(void) {
	void* left = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	memset(left, 0, TOP_W * TOP_H * 3);
}

static void presentTop(void) {
	gfxFlushBuffers();
	gfxScreenSwapBuffers(GFX_TOP, true);
}

static void stampDate(u16* f) {
	Clock c = nowClock();
	char line[40];
	snprintf(line, sizeof(line), "%04d.%02d.%02d//%02d:%02d", c.y, c.mo, c.d, c.h, c.mi);
	u16 col = makePx(255, 140, 20);
	int x0 = 244, y0 = 222;
	for (const char* s = line; *s; s++, x0 += 6) {
		int gi = glyphIndex(*s);
		for (int yy = 0; yy < 7; yy++)
			for (int xx = 0; xx < 5; xx++)
				if (font[gi][yy] & (1 << (4 - xx))) {
					int x = x0 + xx, y = y0 + yy;
					if (x >= 0 && x < TOP_W && y >= 0 && y < TOP_H) f[y * TOP_W + x] = col;
				}
	}
}

static bool saveCcp(const char* path, const u16* f) {
	FILE* fp = fopen(path, "wb");
	if (!fp) return false;
	fwrite("CCP2", 1, 4, fp);
	u16 w = TOP_W, h = TOP_H;
	fwrite(&w, 1, 2, fp);
	fwrite(&h, 1, 2, fp);
	bool ok = fwrite(f, 2, FRAME_PIXELS, fp) == FRAME_PIXELS;
	fclose(fp);
	return ok;
}

static bool loadCcp(const char* path, u16* f) {
	FILE* fp = fopen(path, "rb");
	if (!fp) return false;
	char magic[4];
	u16 w = 0, h = 0;
	if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, "CCP2", 4) != 0) { fclose(fp); return false; }
	if (fread(&w, 1, 2, fp) != 2 || fread(&h, 1, 2, fp) != 2 || w != TOP_W || h != TOP_H) { fclose(fp); return false; }
	bool ok = fread(f, 2, FRAME_PIXELS, fp) == FRAME_PIXELS;
	fclose(fp);
	return ok;
}

static bool saveBmpScaled(const char* path, const u16* f, int scale) {
	if (scale < 1) scale = 1;
	if (scale > 2) scale = 2;
	FILE* fp = fopen(path, "wb");
	if (!fp) return false;
	int outW = TOP_W * scale;
	int outH = TOP_H * scale;
	int rowBytes = ((outW * 3 + 3) / 4) * 4;
	int fileSize = 54 + rowBytes * outH;
	u8 header[54] = {'B','M',0,0,0,0,0,0,0,0,54,0,0,0,40,0,0,0};
	header[2] = fileSize & 255; header[3] = (fileSize >> 8) & 255; header[4] = (fileSize >> 16) & 255; header[5] = (fileSize >> 24) & 255;
	header[18] = outW & 255; header[19] = (outW >> 8) & 255;
	header[22] = outH & 255; header[23] = (outH >> 8) & 255;
	header[26] = 1; header[28] = 24;
	fwrite(header, 1, 54, fp);
	u8 pad[3] = {0,0,0};
	for (int y = outH - 1; y >= 0; y--) {
		int sy = y / scale;
		for (int x = 0; x < outW; x++) {
			int sx = x / scale;
			u16 c = f[sy * TOP_W + sx];
			u8 bgr[3] = {camB(c), camG(c), camR(c)};
			fwrite(bgr, 1, 3, fp);
		}
		fwrite(pad, 1, rowBytes - outW * 3, fp);
	}
	fclose(fp);
	return true;
}

static void refreshGallery(App* app) {
	app->photoCount = 0;
	DIR* d = opendir(CAP_DIR);
	if (!d) { strcpy(app->status, "NO DIR"); return; }
	struct dirent* e;
	while ((e = readdir(d)) && app->photoCount < 128) {
		int len = strlen(e->d_name);
		if (len > 4 && strcasecmp(e->d_name + len - 4, ".ccp") == 0) {
			snprintf(app->photos[app->photoCount], sizeof(app->photos[0]), "%s/%s", CAP_DIR, e->d_name);
			app->photoCount++;
		}
	}
	closedir(d);
	if (app->photoIndex >= app->photoCount) app->photoIndex = app->photoCount - 1;
	if (app->photoIndex < 0) app->photoIndex = 0;
	app->loadedIndex = -1;
	snprintf(app->status, sizeof(app->status), "PICS:%02d", app->photoCount);
}

static void matchingBmp(const char* ccp, char* bmp, size_t n) {
	snprintf(bmp, n, "%s", ccp);
	char* dot = strrchr(bmp, '.');
	if (dot) strcpy(dot, ".bmp");
}

static void deleteCurrent(App* app) {
	if (app->photoCount <= 0) return;
	char bmp[160];
	matchingBmp(app->photos[app->photoIndex], bmp, sizeof(bmp));
	remove(bmp);
	remove(app->photos[app->photoIndex]);
	if (app->photoIndex > 0) app->photoIndex--;
	refreshGallery(app);
	strcpy(app->status, "DELETED");
}

static void closeEvents(Handle* ev) {
	for (int i = 0; i < 4; i++) {
		if (ev[i]) { svcCloseHandle(ev[i]); ev[i] = 0; }
	}
}

static u32 camSelect(App* app) { return app->front ? SELECT_IN1_OUT2 : SELECT_OUT1_OUT2; }
static u32 camPort(App* app) { (void)app; return PORT_BOTH; }

static bool setupCamera(App* app, Handle* ev, u32* transfer) {
	CAMU_StopCapture(PORT_BOTH);
	closeEvents(ev);
	CAMU_Activate(SELECT_NONE);
	u32 select = camSelect(app);
	u32 port = camPort(app);
	CAMU_SetSize(select, SIZE_CTR_TOP_LCD, CONTEXT_A);
	CAMU_SetOutputFormat(select, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetFrameRate(select, FRAME_RATE_30);
	CAMU_SetNoiseFilter(select, true);
	CAMU_SetAutoExposure(select, true);
	CAMU_SetAutoWhiteBalance(select, true);
	CAMU_SetTrimming(PORT_CAM1, false);
	CAMU_SetTrimming(PORT_CAM2, false);
	CAMU_GetMaxBytes(transfer, TOP_W, TOP_H);
	CAMU_SetTransferBytes(port, *transfer, TOP_W, TOP_H);
	CAMU_Activate(select);
	CAMU_GetBufferErrorInterruptEvent(&ev[0], PORT_CAM1);
	CAMU_GetBufferErrorInterruptEvent(&ev[1], PORT_CAM2);
	CAMU_ClearBuffer(port);
	if (!app->front) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
	bool ok = R_SUCCEEDED(CAMU_StartCapture(port));
	snprintf(app->status, sizeof(app->status), ok ? (app->front ? "FRONT" : "OUTER") : "CAM ERR");
	app->interrupted = false;
	return ok;
}

static bool receiveFrame(App* app, Handle* ev, u8* camBuf, u32 transfer) {
	s32 index = 0;
	if (ev[2] == 0) CAMU_SetReceiving(&ev[2], camBuf, PORT_CAM1, FRAME_BYTES, (s16)transfer);
	if (ev[3] == 0) CAMU_SetReceiving(&ev[3], camBuf + FRAME_BYTES, PORT_CAM2, FRAME_BYTES, (s16)transfer);
	if (app->interrupted) {
		CAMU_StartCapture(camPort(app));
		app->interrupted = false;
	}
	if (R_FAILED(svcWaitSynchronizationN(&index, ev, 4, false, WAIT_TIMEOUT))) return false;
	if (index == 0) {
		if (ev[2]) { svcCloseHandle(ev[2]); ev[2] = 0; }
		app->interrupted = true;
		return false;
	}
	if (index == 1) {
		if (ev[3]) { svcCloseHandle(ev[3]); ev[3] = 0; }
		app->interrupted = true;
		return false;
	}
	if (index == 2) {
		if (ev[2]) { svcCloseHandle(ev[2]); ev[2] = 0; }
		return true;
	}
	if (index == 3) {
		if (ev[3]) { svcCloseHandle(ev[3]); ev[3] = 0; }
		copyCamToFrame(camBuf + FRAME_BYTES, rightFrame);
		return false;
	}
	return false;
}

static void stopCamera(Handle* ev) {
	CAMU_StopCapture(PORT_BOTH);
	closeEvents(ev);
	CAMU_Activate(SELECT_NONE);
}

static void drawHud(App* app, void* top) {
	if (!app->hud) return;
	fbRect(top, TOP_W, TOP_H, 6, 6, TOP_W - 12, TOP_H - 12, 0, 255, 80);
	fbText(top, TOP_W, TOP_H, 14, 14, "CYBERCAMERA", 0, 255, 80);
	char line[64];
	snprintf(line, sizeof(line), "MODE:%s", filterNames[app->filter]);
	fbText(top, TOP_W, TOP_H, 236, 14, line, 0, 255, 80);
	snprintf(line, sizeof(line), "CAM:%s", app->front ? "FRONT" : "OUTER");
	fbText(top, TOP_W, TOP_H, 236, 26, line, 0, 220, 220);
	Clock c = nowClock();
	snprintf(line, sizeof(line), "%04d.%02d.%02d//%02d:%02d", c.y, c.mo, c.d, c.h, c.mi);
	fbText(top, TOP_W, TOP_H, 244, 222, line, 255, 140, 20);
}

static void drawBottom(App* app) {
	void* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	memset(fb, 0, BOT_W * BOT_H * 3);
	fbRect(fb, BOT_W, BOT_H, 4, 4, BOT_W - 8, BOT_H - 8, 0, 220, 220);
	if (app->mode == MODE_GALLERY) {
		fbText(fb, BOT_W, BOT_H, 12, 14, "CYBERCAMERA GALLERY", 0, 255, 220);
		fbText(fb, BOT_W, BOT_H, 212, 14, app->status, 255, 150, 20);
		fbText(fb, BOT_W, BOT_H, 64, 86, "L/R CHANGE PHOTO", 0, 255, 220);
		fbText(fb, BOT_W, BOT_H, 64, 112, "Y DELETE", 255, 90, 90);
		fbText(fb, BOT_W, BOT_H, 64, 138, "B BACK CAMERA", 255, 160, 20);
		fbText(fb, BOT_W, BOT_H, 18, 214, BUILD_LABEL, 255, 90, 40);
		return;
	}
	fbText(fb, BOT_W, BOT_H, 12, 14, "CYBERCAMERA", 0, 255, 220);
	fbText(fb, BOT_W, BOT_H, 212, 14, app->status, 255, 150, 20);
	fbText(fb, BOT_W, BOT_H, 68, 72, "A CAPTURE", 255, 160, 20);
	fbText(fb, BOT_W, BOT_H, 68, 96, "B GALLERY", 0, 255, 220);
	fbText(fb, BOT_W, BOT_H, 68, 120, "SEL FRONT OUTER", 255, 80, 220);
	fbText(fb, BOT_W, BOT_H, 68, 144, "L/R FILTER  X TIMER", 0, 255, 80);
	char line[64];
	snprintf(line, sizeof(line), "SCALE:%dx  3D:%s", app->saveScale, app->stereoSave ? "ANA" : "OFF");
	fbText(fb, BOT_W, BOT_H, 68, 168, line, 255, 180, 60);
	snprintf(line, sizeof(line), "TIMER:%02d  UP 3D  DOWN SIZE", app->timerSeconds);
	fbText(fb, BOT_W, BOT_H, 38, 192, line, 0, 220, 220);
	fbText(fb, BOT_W, BOT_H, 18, 214, BUILD_LABEL, 255, 90, 40);
}

static void drawGallery(App* app) {
	clearTopBoth();
	void* top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	if (app->deletePhoto) {
		deleteCurrent(app);
		app->deletePhoto = false;
	}
	if (app->photoCount <= 0) {
		fbText(top, TOP_W, TOP_H, 108, 104, "NO PHOTOS FOUND", 255, 120, 20);
		fbText(top, TOP_W, TOP_H, 82, 122, "TAKE NEW PHOTO", 0, 255, 80);
	} else {
		if (app->loadedIndex != app->photoIndex) {
			app->loadedOk = loadCcp(app->photos[app->photoIndex], galleryFrame);
			app->loadedIndex = app->photoIndex;
		}
		if (app->loadedOk) drawFrame(top, galleryFrame);
		else fbText(top, TOP_W, TOP_H, 130, 104, "LOAD ERROR", 255, 80, 40);
		char line[64];
		snprintf(line, sizeof(line), "PHOTO %02d/%02d", app->photoIndex + 1, app->photoCount);
		fbFill(top, TOP_W, TOP_H, 10, 8, 150, 14, 0, 0, 0);
		fbText(top, TOP_W, TOP_H, 14, 12, line, 255, 160, 20);
	}
	fbRect(top, TOP_W, TOP_H, 6, 6, TOP_W - 12, TOP_H - 12, 0, 220, 90);
	fbText(top, TOP_W, TOP_H, 222, 12, BUILD_LABEL, 255, 90, 40);
}

static void saveShot(App* app) {
	ensureDirs();
	Clock c = nowClock();
	char ccp[160], bmp[160];
	memcpy(shotFrame, frame, FRAME_BYTES);
	if (app->dateStamp) stampDate(shotFrame);
	if (app->stereoSave) {
		for (int i = 0; i < FRAME_PIXELS; i++) {
			int r = camR(frame[i]);
			int g = camG(rightFrame[i]);
			int b = camB(rightFrame[i]);
			anaglyphFrame[i] = makePx(r, g, b);
		}
		if (app->dateStamp) stampDate(anaglyphFrame);
		snprintf(ccp, sizeof(ccp), "%s/ANA_%04d%02d%02d_%02d%02d%02d.ccp", CAP_DIR, c.y, c.mo, c.d, c.h, c.mi, c.s);
		snprintf(bmp, sizeof(bmp), "%s/ANA_%04d%02d%02d_%02d%02d%02d.bmp", CAP_DIR, c.y, c.mo, c.d, c.h, c.mi, c.s);
		bool ok = saveCcp(ccp, anaglyphFrame);
		ok = saveBmpScaled(bmp, anaglyphFrame, app->saveScale) && ok;
		CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
		if (ok) app->saved++;
		snprintf(app->status, sizeof(app->status), ok ? "ANA:%03d" : "SAVE ERR", app->saved);
		app->capture = false;
		app->countdown = 0;
		app->dirtyBottom = true;
		return;
	}
	snprintf(ccp, sizeof(ccp), "%s/CYBER_%04d%02d%02d_%02d%02d%02d.ccp", CAP_DIR, c.y, c.mo, c.d, c.h, c.mi, c.s);
	snprintf(bmp, sizeof(bmp), "%s/CYBER_%04d%02d%02d_%02d%02d%02d.bmp", CAP_DIR, c.y, c.mo, c.d, c.h, c.mi, c.s);
	bool ok = saveCcp(ccp, shotFrame);
	ok = saveBmpScaled(bmp, shotFrame, app->saveScale) && ok;
	CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
	if (ok) app->saved++;
	snprintf(app->status, sizeof(app->status), ok ? "SAVED:%03d" : "SAVE ERR", app->saved);
	app->capture = false;
	app->countdown = 0;
	app->dirtyBottom = true;
}

static void enterGallery(App* app, Handle* ev) {
	stopCamera(ev);
	clearTopBoth();
	app->mode = MODE_GALLERY;
	app->loadedIndex = -1;
	refreshGallery(app);
}

static void leaveGallery(App* app) {
	app->mode = MODE_CAMERA;
	app->cameraDirty = true;
	app->dirtyBottom = true;
	strcpy(app->status, "CAMERA");
}

static void readInput(App* app, Handle* ev) {
	hidScanInput();
	u32 down = hidKeysDown();
	if (down & KEY_B) {
		if (app->mode == MODE_CAMERA) enterGallery(app, ev);
		else leaveGallery(app);
	}
	if (app->mode == MODE_GALLERY) {
		if ((down & KEY_L) && app->photoIndex > 0) { app->photoIndex--; app->loadedIndex = -1; }
		if ((down & KEY_R) && app->photoIndex < app->photoCount - 1) { app->photoIndex++; app->loadedIndex = -1; }
		if (down & KEY_Y) app->deletePhoto = true;
		return;
	}
	if (down & KEY_A) app->capture = true;
	if (down & KEY_SELECT) { app->front = !app->front; app->cameraDirty = true; }
	if (down & KEY_L) { app->filter = (app->filter + F_COUNT - 1) % F_COUNT; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_R) { app->filter = (app->filter + 1) % F_COUNT; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_Y) { app->hud = !app->hud; saveSettings(app); }
	if (down & KEY_X) {
		app->timerSeconds = app->timerSeconds == 0 ? 3 : app->timerSeconds == 3 ? 5 : app->timerSeconds == 5 ? 10 : 0;
		app->dirtyBottom = true;
		saveSettings(app);
	}
	if (down & KEY_DUP) { app->stereoSave = !app->stereoSave; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_DDOWN) { app->saveScale = app->saveScale == 1 ? 2 : 1; app->dirtyBottom = true; saveSettings(app); }
}

int main(void) {
	gfxInitDefault();
	gfxSet3D(false);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);
	ensureDirs();

	App app;
	memset(&app, 0, sizeof(app));
	app.mode = MODE_CAMERA;
	app.filter = F_NORMAL;
	app.contrast = 100;
	app.hud = false;
	app.dateStamp = true;
	app.saveScale = 1;
	app.loadedIndex = -1;
	app.dirtyBottom = true;
	strcpy(app.status, "BOOT");
	loadSettings(&app);

	u8* camBuf = (u8*)memalign(0x1000, CAM_BUF_BYTES);
	Handle ev[4] = {0};
	u32 transfer = FRAME_BYTES;
	bool failed = false;
	bool haveFrame = false;
	if (!camBuf || R_FAILED(camInit())) failed = true;
	if (!failed && !setupCamera(&app, ev, &transfer)) failed = true;

	while (aptMainLoop()) {
		readInput(&app, ev);
		if (hidKeysDown() & KEY_START) break;

		if (app.mode == MODE_GALLERY) {
			gfxSet3D(false);
			drawGallery(&app);
			drawBottom(&app);
			presentTop();
			svcSleepThread(16000000LL);
			continue;
		}

		if (app.cameraDirty) {
			failed = !setupCamera(&app, ev, &transfer);
			app.cameraDirty = false;
			haveFrame = false;
		}

		if (!failed && receiveFrame(&app, ev, camBuf, transfer)) {
			copyCamToFrame(camBuf, frame);
			copyCamToFrame(camBuf + FRAME_BYTES, rightFrame);
			filterFrame(&app, frame);
			filterFrame(&app, rightFrame);
			if (app.capture && app.timerSeconds > 0 && app.countdown == 0) {
				app.countdown = app.timerSeconds;
				app.timerStart = svcGetSystemTick();
				snprintf(app.status, sizeof(app.status), "TIMER:%02d", app.countdown);
				app.dirtyBottom = true;
			}
			if (app.countdown > 0) {
				u64 elapsed = (svcGetSystemTick() - app.timerStart) / SYSCLOCK_ARM11;
				int left = app.timerSeconds - (int)elapsed;
				if (left < 0) left = 0;
				if (left != app.countdown) {
					app.countdown = left;
					snprintf(app.status, sizeof(app.status), "TIMER:%02d", app.countdown);
					app.dirtyBottom = true;
				}
				if (left == 0) saveShot(&app);
			} else if (app.capture) {
				saveShot(&app);
			}
			haveFrame = true;
		}

		if (haveFrame) {
			bool use3d = CONFIG_3D_SLIDERSTATE > 0.0f && !app.front;
			gfxSet3D(use3d);
			void* top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
			drawFrame(top, frame);
			if (use3d) {
				void* topRight = gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
				drawFrame(topRight, rightFrame);
			}
			drawHud(&app, top);
		} else {
			gfxSet3D(false);
			clearTopBoth();
		}
		if (app.dirtyBottom) {
			drawBottom(&app);
			app.dirtyBottom = false;
		}
		presentTop();
	}

	stopCamera(ev);
	if (camBuf) free(camBuf);
	camExit();
	gfxExit();
	return 0;
}
