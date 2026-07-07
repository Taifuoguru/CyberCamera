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
#define BUILD_LABEL "CYBERCAMERA 20260707"

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
	bool galleryStereo;
	bool dsiMode;
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
	float zoom;
} App;

typedef struct {
	int y, mo, d, h, mi, s;
} Clock;

static u16 frame[FRAME_PIXELS];
static u16 rightFrame[FRAME_PIXELS];
static u16 galleryFrame[FRAME_PIXELS];
static u16 galleryRightFrame[FRAME_PIXELS];
static u16 shotFrame[FRAME_PIXELS];
static u16 rightShotFrame[FRAME_PIXELS];
static u16 anaglyphFrame[FRAME_PIXELS];
static u16 scratchFrame[FRAME_PIXELS];

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
		if (c == ':' || c == '.' || c == '/' || c == '-' || c == '_' || c == '+') {
			for (int yy = 0; yy < 7; yy++) {
				bool on = (c == ':' && (yy == 2 || yy == 5)) || (c == '.' && yy == 6) ||
					(c == '-' && yy == 3) || (c == '_' && yy == 6) || (c == '+' && (yy == 1 || yy == 2 || yy == 3 || yy == 4 || yy == 5)) ||
					(c == '/' && yy >= 1 && yy <= 5);
				if (on) fbPx(fb, w, h, cx + (c == '/' ? 5 - yy : 2), y + yy, r, g, b);
				if (c == '+' && yy == 3) for (int xx = 0; xx < 5; xx++) fbPx(fb, w, h, cx + xx, y + yy, r, g, b);
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
	fprintf(fp, "dsi=%d\n", app->dsiMode ? 1 : 0);
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
		else if (strcmp(key, "dsi") == 0) app->dsiMode = val != 0;
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

static void applyDsiMode(u16* f) {
	enum { DSI_W = 256, DSI_H = 192 };
	static u16 dsiBuf[DSI_W * DSI_H];
	for (int y = 0; y < DSI_H; y++) {
		int sy = (y * TOP_H) / DSI_H;
		for (int x = 0; x < DSI_W; x++) {
			int sx = (x * TOP_W) / DSI_W;
			u16 c = f[sy * TOP_W + sx];
			int r = camR(c) & 0xF8;
			int g = camG(c) & 0xF8;
			int b = camB(c) & 0xF8;
			dsiBuf[y * DSI_W + x] = makePx(r, g, b);
		}
	}
	for (int y = 0; y < TOP_H; y++) {
		int sy = (y * DSI_H) / TOP_H;
		for (int x = 0; x < TOP_W; x++) {
			int sx = (x * DSI_W) / TOP_W;
			f[y * TOP_W + x] = dsiBuf[sy * DSI_W + sx];
		}
	}
}

static bool quality2x(const App* app) {
	return app->saveScale >= 2;
}

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
	return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void fbPanel(void* fb, int w, int h, int x, int y, int rw, int rh, bool active, const char* title, const char* value) {
	u8 br = active ? 255 : 100;
	u8 bg = active ? 200 : 140;
	u8 bb = active ? 70 : 180;
	fbFill(fb, w, h, x, y, rw, rh, 0, 0, 0);
	fbRect(fb, w, h, x, y, rw, rh, br, bg, bb);
	if (title) fbText(fb, w, h, x + 6, y + 5, title, 0, 220, 220);
	if (value) fbText(fb, w, h, x + 6, y + 16, value, br, bg, bb);
}

static void applyZoom(const u16* src, u16* dst, float zoom) {
	if (zoom <= 1.01f) {
		memcpy(dst, src, FRAME_BYTES);
		return;
	}
	float inv = 1.0f / zoom;
	float cx = (TOP_W - 1) * 0.5f;
	float cy = (TOP_H - 1) * 0.5f;
	for (int y = 0; y < TOP_H; y++) {
		for (int x = 0; x < TOP_W; x++) {
			float sx = cx + (x - cx) * inv;
			float sy = cy + (y - cy) * inv;
			int ix = (int)(sx + 0.5f);
			int iy = (int)(sy + 0.5f);
			if (ix < 0) ix = 0;
			if (ix >= TOP_W) ix = TOP_W - 1;
			if (iy < 0) iy = 0;
			if (iy >= TOP_H) iy = TOP_H - 1;
			dst[y * TOP_W + x] = src[iy * TOP_W + ix];
		}
	}
}

static void enhanceQuality(u16* f) {
	memcpy(scratchFrame, f, FRAME_BYTES);
	for (int y = 1; y < TOP_H - 1; y++) {
		for (int x = 1; x < TOP_W - 1; x++) {
			int i = y * TOP_W + x;
			int r = camR(scratchFrame[i]) * 5 - camR(scratchFrame[i - 1]) - camR(scratchFrame[i + 1]) - camR(scratchFrame[i - TOP_W]) - camR(scratchFrame[i + TOP_W]);
			int g = camG(scratchFrame[i]) * 5 - camG(scratchFrame[i - 1]) - camG(scratchFrame[i + 1]) - camG(scratchFrame[i - TOP_W]) - camG(scratchFrame[i + TOP_W]);
			int b = camB(scratchFrame[i]) * 5 - camB(scratchFrame[i - 1]) - camB(scratchFrame[i + 1]) - camB(scratchFrame[i - TOP_W]) - camB(scratchFrame[i + TOP_W]);
			f[i] = makePx(r, g, b);
		}
	}
}

static void filterFrame(App* app, u16* f) {
	if (app->dsiMode) applyDsiMode(f);
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
	void* right = gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
	memset(left, 0, TOP_W * TOP_H * 3);
	if (right && right != left) memset(right, 0, TOP_W * TOP_H * 3);
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

static bool saveCcpStereo(const char* path, const u16* left, const u16* right) {
	FILE* fp = fopen(path, "wb");
	if (!fp) return false;
	fwrite("CCP3", 1, 4, fp);
	u16 w = TOP_W, h = TOP_H;
	fwrite(&w, 1, 2, fp);
	fwrite(&h, 1, 2, fp);
	bool ok = fwrite(left, 2, FRAME_PIXELS, fp) == FRAME_PIXELS;
	ok = fwrite(right, 2, FRAME_PIXELS, fp) == FRAME_PIXELS && ok;
	fclose(fp);
	return ok;
}

static bool loadCcp(const char* path, u16* f, u16* right, bool* stereo) {
	FILE* fp = fopen(path, "rb");
	if (!fp) return false;
	char magic[4];
	u16 w = 0, h = 0;
	if (stereo) *stereo = false;
	if (fread(magic, 1, 4, fp) != 4) { fclose(fp); return false; }
	if (fread(&w, 1, 2, fp) != 2 || fread(&h, 1, 2, fp) != 2 || w != TOP_W || h != TOP_H) { fclose(fp); return false; }
	bool ok = fread(f, 2, FRAME_PIXELS, fp) == FRAME_PIXELS;
	if (ok && memcmp(magic, "CCP3", 4) == 0 && right) {
		ok = fread(right, 2, FRAME_PIXELS, fp) == FRAME_PIXELS;
		if (stereo) *stereo = ok;
	}
	fclose(fp);
	return ok;
}

static u16 sampleBilinear(const u16* f, float sx, float sy) {
	int x0 = (int)sx;
	int y0 = (int)sy;
	int x1 = x0 + 1;
	int y1 = y0 + 1;
	float fx = sx - x0;
	float fy = sy - y0;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= TOP_W) x1 = TOP_W - 1;
	if (y1 >= TOP_H) y1 = TOP_H - 1;
	u16 c00 = f[y0 * TOP_W + x0];
	u16 c10 = f[y0 * TOP_W + x1];
	u16 c01 = f[y1 * TOP_W + x0];
	u16 c11 = f[y1 * TOP_W + x1];
	float r = camR(c00) * (1.0f - fx) * (1.0f - fy) + camR(c10) * fx * (1.0f - fy) + camR(c01) * (1.0f - fx) * fy + camR(c11) * fx * fy;
	float g = camG(c00) * (1.0f - fx) * (1.0f - fy) + camG(c10) * fx * (1.0f - fy) + camG(c01) * (1.0f - fx) * fy + camG(c11) * fx * fy;
	float b = camB(c00) * (1.0f - fx) * (1.0f - fy) + camB(c10) * fx * (1.0f - fy) + camB(c01) * (1.0f - fx) * fy + camB(c11) * fx * fy;
	return makePx((int)r, (int)g, (int)b);
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
		float sy = scale == 2 ? (float)y / 2.0f : (float)y;
		for (int x = 0; x < outW; x++) {
			float sx = scale == 2 ? (float)x / 2.0f : (float)x;
			u16 c = scale == 2 ? sampleBilinear(f, sx, sy) : f[(int)sy * TOP_W + (int)sx];
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
	app->galleryStereo = false;
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
	CAMU_SetNoiseFilter(select, !app->dsiMode);
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
	snprintf(line, sizeof(line), "FILTER:%s", filterNames[app->filter]);
	fbText(top, TOP_W, TOP_H, 236, 14, line, 0, 255, 80);
	snprintf(line, sizeof(line), "CAM:%s ZOOM:%.1fx", app->front ? "FRONT" : "OUTER", app->zoom);
	fbText(top, TOP_W, TOP_H, 14, 28, line, 0, 220, 220);
	snprintf(line, sizeof(line), "3D:%s DSI:%s", !app->front ? "READY" : "OFF", app->dsiMode ? "ON" : "OFF");
	fbText(top, TOP_W, TOP_H, 236, 28, line, 255, 180, 60);
	Clock c = nowClock();
	snprintf(line, sizeof(line), "%04d.%02d.%02d//%02d:%02d", c.y, c.mo, c.d, c.h, c.mi);
	fbText(top, TOP_W, TOP_H, 244, 222, line, 255, 140, 20);
}

static void drawCameraBottom(App* app) {
	void* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	memset(fb, 0, BOT_W * BOT_H * 3);
	fbRect(fb, BOT_W, BOT_H, 4, 4, BOT_W - 8, BOT_H - 8, 0, 220, 220);
	fbText(fb, BOT_W, BOT_H, 12, 12, "CAMERA CONTROLS", 0, 255, 220);
	fbText(fb, BOT_W, BOT_H, 206, 12, app->status, 255, 150, 20);
	fbPanel(fb, BOT_W, BOT_H, 10, 36, 94, 34, app->capture, "CAPTURE", "A");
	fbPanel(fb, BOT_W, BOT_H, 113, 36, 94, 34, false, "GALLERY", "B");
	fbPanel(fb, BOT_W, BOT_H, 216, 36, 94, 34, app->front, "CAMERA", app->front ? "FRONT" : "OUTER");
	fbPanel(fb, BOT_W, BOT_H, 10, 78, 45, 34, false, "FLT", "-");
	fbPanel(fb, BOT_W, BOT_H, 61, 78, 198, 34, true, "FILTER", filterNames[app->filter]);
	fbPanel(fb, BOT_W, BOT_H, 265, 78, 45, 34, false, "FLT", "+");
	fbPanel(fb, BOT_W, BOT_H, 10, 120, 94, 34, app->stereoSave, "3D SAVE", app->stereoSave ? "ON" : "OFF");
	fbPanel(fb, BOT_W, BOT_H, 113, 120, 94, 34, quality2x(app), "QUALITY", quality2x(app) ? "2X" : "1X");
	fbPanel(fb, BOT_W, BOT_H, 216, 120, 94, 34, app->dsiMode, "DSI CAM", app->dsiMode ? "ON" : "OFF");
	char zoom[32];
	snprintf(zoom, sizeof(zoom), "%.1fx", app->zoom);
	fbPanel(fb, BOT_W, BOT_H, 10, 162, 94, 34, app->timerSeconds > 0, "TIMER", app->timerSeconds ? (app->timerSeconds == 10 ? "10S" : app->timerSeconds == 5 ? "05S" : "03S") : "OFF");
	fbPanel(fb, BOT_W, BOT_H, 113, 162, 94, 34, app->zoom > 1.05f, "ZOOM", zoom);
	fbPanel(fb, BOT_W, BOT_H, 216, 162, 94, 34, app->hud, "HUD", app->hud ? "ON" : "OFF");
	fbText(fb, BOT_W, BOT_H, 12, 206, "L/R FILTER  X TIMER  Y DSI  SELECT CAMERA", 0, 220, 180);
	fbText(fb, BOT_W, BOT_H, 12, 218, "CPAD UP/DOWN ZOOM  DUP 3D SAVE  DDOWN 2X", 0, 220, 180);
	fbText(fb, BOT_W, BOT_H, 12, 230, BUILD_LABEL, 255, 90, 40);
}

static void drawGalleryBottom(App* app) {
	void* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	memset(fb, 0, BOT_W * BOT_H * 3);
	fbRect(fb, BOT_W, BOT_H, 4, 4, BOT_W - 8, BOT_H - 8, 0, 220, 220);
	fbText(fb, BOT_W, BOT_H, 12, 12, "CYBERCAMERA GALLERY", 0, 255, 220);
	fbText(fb, BOT_W, BOT_H, 206, 12, app->status, 255, 150, 20);
	fbPanel(fb, BOT_W, BOT_H, 10, 40, 70, 42, false, "PREV", "PHOTO");
	fbPanel(fb, BOT_W, BOT_H, 88, 40, 70, 42, false, "NEXT", "PHOTO");
	fbPanel(fb, BOT_W, BOT_H, 166, 40, 70, 42, false, "BACK", "CAM");
	fbPanel(fb, BOT_W, BOT_H, 244, 40, 66, 42, false, "DEL", "PHOTO");
	fbPanel(fb, BOT_W, BOT_H, 10, 96, 300, 56, app->galleryStereo && CONFIG_3D_SLIDERSTATE > 0.0f, "3D PREVIEW", app->galleryStereo ? (CONFIG_3D_SLIDERSTATE > 0.0f ? "SLIDER ACTIVE" : "MOVE 3D SLIDER") : "PHOTO IS 2D");
	fbText(fb, BOT_W, BOT_H, 12, 166, "L/R CHANGE PHOTO  Y DELETE  B BACK CAMERA", 0, 220, 180);
	fbText(fb, BOT_W, BOT_H, 12, 178, "STEREO SHOTS OPEN IN REAL 3D ON THE TOP SCREEN", 0, 220, 180);
	fbText(fb, BOT_W, BOT_H, 18, 214, BUILD_LABEL, 255, 90, 40);
}

static void drawBottom(App* app) {
	if (app->mode == MODE_GALLERY) drawGalleryBottom(app);
	else drawCameraBottom(app);
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
			app->loadedOk = loadCcp(app->photos[app->photoIndex], galleryFrame, galleryRightFrame, &app->galleryStereo);
			app->loadedIndex = app->photoIndex;
		}
		if (app->loadedOk) {
			drawFrame(top, galleryFrame);
			if (app->galleryStereo && CONFIG_3D_SLIDERSTATE > 0.0f) {
				void* topRight = gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
				drawFrame(topRight, galleryRightFrame);
			}
		}
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
	memcpy(rightShotFrame, rightFrame, FRAME_BYTES);
	if (app->dateStamp) stampDate(shotFrame);
	if (app->stereoSave && !app->front) {
		if (app->dateStamp) stampDate(rightShotFrame);
		for (int i = 0; i < FRAME_PIXELS; i++) {
			int r = camR(shotFrame[i]);
			int g = camG(rightShotFrame[i]);
			int b = camB(rightShotFrame[i]);
			anaglyphFrame[i] = makePx(r, g, b);
		}
		if (app->dateStamp) stampDate(anaglyphFrame);
		snprintf(ccp, sizeof(ccp), "%s/ANA_%04d%02d%02d_%02d%02d%02d.ccp", CAP_DIR, c.y, c.mo, c.d, c.h, c.mi, c.s);
		snprintf(bmp, sizeof(bmp), "%s/ANA_%04d%02d%02d_%02d%02d%02d.bmp", CAP_DIR, c.y, c.mo, c.d, c.h, c.mi, c.s);
		bool ok = saveCcpStereo(ccp, shotFrame, rightShotFrame);
		ok = saveBmpScaled(bmp, anaglyphFrame, app->saveScale) && ok;
		CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
		if (ok) app->saved++;
		snprintf(app->status, sizeof(app->status), ok ? "3D SAVED:%03d" : "SAVE ERR", app->saved);
		app->capture = false;
		app->countdown = 0;
		app->dirtyBottom = true;
		return;
	}
	if (app->stereoSave && app->front) {
		snprintf(app->status, sizeof(app->status), "3D NEEDS OUTER");
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

static void cycleTimer(App* app) {
	app->timerSeconds = app->timerSeconds == 0 ? 3 : app->timerSeconds == 3 ? 5 : app->timerSeconds == 5 ? 10 : 0;
	app->dirtyBottom = true;
	saveSettings(app);
}

static void handleTouchCamera(App* app, int x, int y) {
	if (inRect(x, y, 10, 36, 94, 34)) {
		app->capture = true;
		return;
	}
	if (inRect(x, y, 113, 36, 94, 34)) {
		app->mode = MODE_GALLERY;
		app->loadedIndex = -1;
		refreshGallery(app);
		app->dirtyBottom = true;
		return;
	}
	if (inRect(x, y, 216, 36, 94, 34)) {
		app->front = !app->front;
		app->cameraDirty = true;
		app->dirtyBottom = true;
		saveSettings(app);
		return;
	}
	if (inRect(x, y, 10, 78, 45, 34)) {
		app->filter = (app->filter + F_COUNT - 1) % F_COUNT;
		app->dirtyBottom = true;
		saveSettings(app);
		return;
	}
	if (inRect(x, y, 265, 78, 45, 34)) {
		app->filter = (app->filter + 1) % F_COUNT;
		app->dirtyBottom = true;
		saveSettings(app);
		return;
	}
	if (inRect(x, y, 10, 120, 94, 34)) {
		app->stereoSave = !app->stereoSave;
		app->dirtyBottom = true;
		saveSettings(app);
		return;
	}
	if (inRect(x, y, 113, 120, 94, 34)) {
		app->saveScale = app->saveScale == 1 ? 2 : 1;
		app->dirtyBottom = true;
		saveSettings(app);
		return;
	}
	if (inRect(x, y, 216, 120, 94, 34)) {
		app->dsiMode = !app->dsiMode;
		app->cameraDirty = true;
		app->dirtyBottom = true;
		saveSettings(app);
		return;
	}
	if (inRect(x, y, 10, 162, 94, 34)) {
		cycleTimer(app);
		return;
	}
	if (inRect(x, y, 113, 162, 94, 34)) {
		app->zoom += 0.5f;
		if (app->zoom > 3.0f) app->zoom = 1.0f;
		app->dirtyBottom = true;
		return;
	}
	if (inRect(x, y, 216, 162, 94, 34)) {
		app->hud = !app->hud;
		app->dirtyBottom = true;
		saveSettings(app);
		return;
	}
}

static void handleTouchGallery(App* app, int x, int y) {
	if (inRect(x, y, 10, 40, 70, 42) && app->photoIndex > 0) {
		app->photoIndex--;
		app->loadedIndex = -1;
		return;
	}
	if (inRect(x, y, 88, 40, 70, 42) && app->photoIndex < app->photoCount - 1) {
		app->photoIndex++;
		app->loadedIndex = -1;
		return;
	}
	if (inRect(x, y, 166, 40, 70, 42)) {
		leaveGallery(app);
		return;
	}
	if (inRect(x, y, 244, 40, 66, 42)) {
		app->deletePhoto = true;
		return;
	}
}

static void readInput(App* app, Handle* ev) {
	hidScanInput();
	u32 down = hidKeysDown();
	if (down & KEY_TOUCH) {
		touchPosition touch;
		hidTouchRead(&touch);
		if (app->mode == MODE_GALLERY) handleTouchGallery(app, touch.px, touch.py);
		else handleTouchCamera(app, touch.px, touch.py);
	}
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
	if (down & KEY_SELECT) { app->front = !app->front; app->cameraDirty = true; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_L) { app->filter = (app->filter + F_COUNT - 1) % F_COUNT; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_R) { app->filter = (app->filter + 1) % F_COUNT; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_Y) { app->dsiMode = !app->dsiMode; app->cameraDirty = true; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_X) cycleTimer(app);
	if (down & KEY_DUP) { app->stereoSave = !app->stereoSave; app->dirtyBottom = true; saveSettings(app); }
	if (down & KEY_DDOWN) { app->saveScale = app->saveScale == 1 ? 2 : 1; app->dirtyBottom = true; saveSettings(app); }
	circlePosition cpad;
	hidCircleRead(&cpad);
	if (cpad.dy > 100 && app->zoom < 3.0f) {
		app->zoom += 0.05f;
		if (app->zoom > 3.0f) app->zoom = 3.0f;
		app->dirtyBottom = true;
	}
	if (cpad.dy < -100 && app->zoom > 1.0f) {
		app->zoom -= 0.05f;
		if (app->zoom < 1.0f) app->zoom = 1.0f;
		app->dirtyBottom = true;
	}
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
	app.hud = true;
	app.dateStamp = true;
	app.saveScale = 1;
	app.zoom = 1.0f;
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
			gfxSet3D(app.galleryStereo && CONFIG_3D_SLIDERSTATE > 0.0f);
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
			copyCamToFrame(camBuf, shotFrame);
			copyCamToFrame(camBuf + FRAME_BYTES, rightShotFrame);
			applyZoom(shotFrame, frame, app.zoom);
			applyZoom(rightShotFrame, rightFrame, app.zoom);
			filterFrame(&app, frame);
			filterFrame(&app, rightFrame);
			if (quality2x(&app)) {
				enhanceQuality(frame);
				enhanceQuality(rightFrame);
			}
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
