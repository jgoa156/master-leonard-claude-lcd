/* lcd.h - the TURZX / Turing USB panels (VID 1CBE, WinUSB, vendor bulk protocol).
 *
 * Found by hardware id on any USB port, opened through WinUSB, re-found automatically when it is unplugged and plugged
 * back (or the PC wakes). Each frame is rotated to the panel's native portrait orientation, JPEG-encoded with GDI+ and
 * sent as command 101 on a worker thread, so the animation never waits on the USB.
 *
 * Protocol (community-documented: mathoudebine/turing-smart-screen-python lcd_comm_turing_usb.py, jijiechen/turing-monitor):
 *   every command = a 500-byte packet [cmd, 0, 0x1A, 0x6D, ms-since-midnight LE32, args...], zero-padded to 504,
 *   DES-CBC encrypted (key = IV = "slv3tuzx"), in a 512-byte block ending A1 1A; an upload appends its payload
 *   (length BE32 at [8]). Bulk OUT 0x01, IN 0x81; every command answers one 512-byte block.
 *   10 = sync/handshake, 14 = brightness ([8] = 0..102), 101 = JPEG still (native portrait size).
 */
#include <setupapi.h>
#include <winusb.h>
#include <objidl.h>
#include <ctype.h>

/* ------------------------------------------------------------ DES (single, CBC) */
static const unsigned char DES_IP[64] = {58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,
    57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7};
static const unsigned char DES_FP[64] = {40,8,48,16,56,24,64,32,39,7,47,15,55,23,63,31,38,6,46,14,54,22,62,30,37,5,45,13,53,21,61,29,
    36,4,44,12,52,20,60,28,35,3,43,11,51,19,59,27,34,2,42,10,50,18,58,26,33,1,41,9,49,17,57,25};
static const unsigned char DES_E[48] = {32,1,2,3,4,5,4,5,6,7,8,9,8,9,10,11,12,13,12,13,14,15,16,17,16,17,18,19,20,21,20,21,22,23,24,25,
    24,25,26,27,28,29,28,29,30,31,32,1};
static const unsigned char DES_P[32] = {16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25};
static const unsigned char DES_PC1[56] = {57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35,27,19,11,3,60,52,44,36,
    63,55,47,39,31,23,15,7,62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,28,20,12,4};
static const unsigned char DES_PC2[48] = {14,17,11,24,1,5,3,28,15,6,21,10,23,19,12,4,26,8,16,7,27,20,13,2,41,52,31,37,47,55,30,40,
    51,45,33,48,44,49,39,56,34,53,46,42,50,36,29,32};
static const unsigned char DES_SH[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
static const unsigned char DES_S[8][64] = {
    {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
    {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
    {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
    {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
    {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
    {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
    {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
    {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}};

typedef unsigned long long u64;
static u64 des_perm(u64 in, const unsigned char *tab, int n, int inbits) {
    u64 o = 0;
    for (int i = 0; i < n; i++) o = (o << 1) | ((in >> (inbits - tab[i])) & 1);
    return o;
}
static u64 DES_K[16];
static void des_key(const unsigned char k[8]) {
    u64 key = 0; for (int i = 0; i < 8; i++) key = (key << 8) | k[i];
    u64 cd = des_perm(key, DES_PC1, 56, 64);
    unsigned c = (unsigned)(cd >> 28) & 0xFFFFFFF, d = (unsigned)cd & 0xFFFFFFF;
    for (int r = 0; r < 16; r++) {
        for (int s = 0; s < DES_SH[r]; s++) { c = ((c << 1) | (c >> 27)) & 0xFFFFFFF; d = ((d << 1) | (d >> 27)) & 0xFFFFFFF; }
        DES_K[r] = des_perm(((u64)c << 28) | d, DES_PC2, 48, 56);
    }
}
static u64 des_block(u64 b) {
    b = des_perm(b, DES_IP, 64, 64);
    unsigned l = (unsigned)(b >> 32), r = (unsigned)b;
    for (int i = 0; i < 16; i++) {
        u64 e = des_perm(r, DES_E, 48, 32) ^ DES_K[i];
        unsigned f = 0;
        for (int s = 0; s < 8; s++) {
            int six = (int)(e >> (42 - 6 * s)) & 0x3F, row = ((six & 0x20) >> 4) | (six & 1), col = (six >> 1) & 0xF;
            f = (f << 4) | DES_S[s][row * 16 + col];
        }
        f = (unsigned)des_perm(f, DES_P, 32, 32);
        unsigned t = r; r = l ^ f; l = t;
    }
    return des_perm(((u64)r << 32) | l, DES_FP, 64, 64);
}

/* ------------------------------------------------------------ packets */
static long LCD_TS = -1;                                              /* tests: a fixed timestamp */
static void lcd_seal(unsigned char cmd, const unsigned char *args, int nargs, unsigned char out[512]) {
    static int keyed; if (!keyed) { des_key((const unsigned char *)"slv3tuzx"); keyed = 1; }
    unsigned char p[504] = {0};
    SYSTEMTIME st; GetLocalTime(&st);
    unsigned ms = LCD_TS >= 0 ? (unsigned)LCD_TS : ((st.wHour * 60u + st.wMinute) * 60u + st.wSecond) * 1000u + st.wMilliseconds;
    p[0] = cmd; p[2] = 0x1A; p[3] = 0x6D;
    p[4] = ms & 0xFF; p[5] = (ms >> 8) & 0xFF; p[6] = (ms >> 16) & 0xFF; p[7] = (ms >> 24) & 0xFF;
    for (int i = 0; i < nargs && 8 + i < 500; i++) p[8 + i] = args[i];
    memset(out, 0, 512);
    u64 iv = 0; for (int i = 0; i < 8; i++) iv = (iv << 8) | (unsigned char)"slv3tuzx"[i];
    for (int b = 0; b < 504; b += 8) {
        u64 v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p[b + i];
        iv = des_block(v ^ iv);
        for (int i = 0; i < 8; i++) out[b + i] = (unsigned char)(iv >> (56 - 8 * i));
    }
    out[510] = 0xA1; out[511] = 0x1A;
}

/* ------------------------------------------------------------ the device */
typedef struct { unsigned short pid; int w, h; } LcdModel;            /* native (portrait) sizes */
static const LcdModel LCD_MODELS[] = {{0x0028, 480, 480}, {0x0046, 320, 960}, {0x0050, 720, 1280}, {0x0080, 800, 1280},
                                      {0x0088, 480, 1920}, {0x0092, 462, 1920}, {0x0123, 720, 1920}};
static HANDLE LCD_FILE = INVALID_HANDLE_VALUE; static WINUSB_INTERFACE_HANDLE LCD_USB;
static int LCD_W, LCD_H;                                              /* native panel size, 0 = no panel */
static char LCD_NAME[64];

static int lcd_xfer(unsigned char *buf, ULONG n, unsigned char reply[512]) {
    ULONG done = 0;
    if (!WinUsb_WritePipe(LCD_USB, 0x01, buf, n, &done, NULL) || done != n) return 0;
    ULONG got = 0;                                                   /* the reply: optional (some commands stay silent) */
    if (!WinUsb_ReadPipe(LCD_USB, 0x81, reply, 512, &got, NULL)) return 1;
    ULONG t0 = 15, t1 = 2000; unsigned char junk[512];                /* the panel may answer more than once: drain it, or it stalls */
    WinUsb_SetPipePolicy(LCD_USB, 0x81, PIPE_TRANSFER_TIMEOUT, sizeof t0, &t0);
    for (int k = 0; k < 4 && WinUsb_ReadPipe(LCD_USB, 0x81, junk, 512, &got, NULL); k++) ;
    WinUsb_SetPipePolicy(LCD_USB, 0x81, PIPE_TRANSFER_TIMEOUT, sizeof t1, &t1);
    return 2;
}
static int lcd_cmd(unsigned char cmd, const unsigned char *args, int nargs) {
    unsigned char b[512], r[512]; lcd_seal(cmd, args, nargs, b); return lcd_xfer(b, 512, r);
}
static void lcd_close(void) {
    if (LCD_USB) { WinUsb_Free(LCD_USB); LCD_USB = NULL; }
    if (LCD_FILE != INVALID_HANDLE_VALUE) { CloseHandle(LCD_FILE); LCD_FILE = INVALID_HANDLE_VALUE; }
    LCD_W = LCD_H = 0;
}

/* scan the USB device interfaces for VID 1CBE (whatever port it is on) and open it through WinUSB */
static char LCD_ERR[128] = "searching"; static void (*LCD_PROBE)(void);   /* diagnostics hook */
static int lcd_try(const char *path) {
    char low[512]; size_t i = 0;
    for (; path[i] && i < sizeof low - 1; i++) low[i] = (char)tolower((unsigned char)path[i]);
    low[i] = 0;
    const char *v = strstr(low, "vid_1cbe&pid_"); if (!v) return 0;
    unsigned pid = (unsigned)strtoul(v + 13, NULL, 16);
    const LcdModel *m = NULL;
    for (size_t k = 0; k < sizeof LCD_MODELS / sizeof *LCD_MODELS; k++) if (LCD_MODELS[k].pid == pid) m = &LCD_MODELS[k];
    if (!m) { snprintf(LCD_ERR, sizeof LCD_ERR, "unknown model 1CBE:%04X", pid); return 0; }
    HANDLE f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (f == INVALID_HANDLE_VALUE) { snprintf(LCD_ERR, sizeof LCD_ERR, "open failed (%lu) - in use by another app?", GetLastError()); return 0; }
    WINUSB_INTERFACE_HANDLE u = NULL;
    if (!WinUsb_Initialize(f, &u)) { snprintf(LCD_ERR, sizeof LCD_ERR, "WinUsb_Initialize failed (%lu)", GetLastError()); CloseHandle(f); return 0; }
    ULONG to = 5000, ti = 2000; WinUsb_SetPipePolicy(u, 0x01, PIPE_TRANSFER_TIMEOUT, sizeof to, &to); WinUsb_SetPipePolicy(u, 0x81, PIPE_TRANSFER_TIMEOUT, sizeof ti, &ti);
    LCD_FILE = f; LCD_USB = u; LCD_W = m->w; LCD_H = m->h;
    snprintf(LCD_NAME, sizeof LCD_NAME, "1CBE:%04X-%dx%d", pid, m->w, m->h);
    if (LCD_PROBE) LCD_PROBE();
    WinUsb_ResetPipe(u, 0x01); WinUsb_ResetPipe(u, 0x81);           /* clear a stall left by an earlier session */
    { unsigned char junk[512]; ULONG g, t0 = 100; WinUsb_SetPipePolicy(u, 0x81, PIPE_TRANSFER_TIMEOUT, sizeof t0, &t0);
      for (int k = 0; k < 8 && WinUsb_ReadPipe(u, 0x81, junk, 512, &g, NULL); k++) ;                     /* drain stale replies */
      t0 = 2000; WinUsb_SetPipePolicy(u, 0x81, PIPE_TRANSFER_TIMEOUT, sizeof t0, &t0); }
    int r = lcd_cmd(10, NULL, 0);                                    /* handshake */
    if (r) { snprintf(LCD_ERR, sizeof LCD_ERR, r == 2 ? "ok" : "ok (silent)"); return 1; }
    snprintf(LCD_ERR, sizeof LCD_ERR, "handshake write failed (%lu)", GetLastError()); lcd_close(); return 0;
}
static int lcd_open(void) {
    static const GUID USB_DEV = {0xa5dcbf10, 0x6530, 0x11d2, {0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed}};   /* every USB device */
    HDEVINFO ifs = SetupDiGetClassDevsA(&USB_DEV, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (ifs == INVALID_HANDLE_VALUE) return 0;
    SP_DEVICE_INTERFACE_DATA id = {sizeof id}; int ok = 0, seen = 0;
    for (DWORD j = 0; !ok && SetupDiEnumDeviceInterfaces(ifs, NULL, &USB_DEV, j, &id); j++) {
        DWORD need = 0; SetupDiGetDeviceInterfaceDetailA(ifs, &id, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *det = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)calloc(1, need + 8);
        if (!det) continue;
        det->cbSize = sizeof *det;
        if (SetupDiGetDeviceInterfaceDetailA(ifs, &id, det, need, NULL, NULL)) {
            char low[16]; snprintf(low, sizeof low, "%s", ""); (void)low;
            if (strstr(det->DevicePath, "1cbe") || strstr(det->DevicePath, "1CBE")) { seen = 1; ok = lcd_try(det->DevicePath); }
        }
        free(det);
    }
    SetupDiDestroyDeviceInfoList(ifs);
    if (!seen) snprintf(LCD_ERR, sizeof LCD_ERR, "no panel plugged in / powered");
    return ok;
}

/* ------------------------------------------------------------ JPEG through GDI+ (flat API, declared by hand) */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread, SuppressExternalCodecs; } GpStartupInput;
typedef struct { GUID Guid; ULONG NumberOfValues; ULONG Type; void *Value; } GpEncParam;
typedef struct { UINT Count; GpEncParam Parameter[1]; } GpEncParams;
__declspec(dllimport) int __stdcall GdiplusStartup(ULONG_PTR *, const GpStartupInput *, void *);
__declspec(dllimport) int __stdcall GdipCreateBitmapFromScan0(INT, INT, INT, INT, BYTE *, void **);
__declspec(dllimport) int __stdcall GdipSaveImageToStream(void *, IStream *, const CLSID *, const GpEncParams *);
__declspec(dllimport) int __stdcall GdipDisposeImage(void *);

/* frame (PANEL_W x PANEL_H, RGB) -> rotated to the panel's portrait, letterboxed -> JPEG in a malloc'd buffer */
static unsigned char *lcd_jpeg(const unsigned char *rgb, int fw, int fh, int flip, ULONG *len) {
    static ULONG_PTR tok; if (!tok) { GpStartupInput si = {1, NULL, FALSE, FALSE}; if (GdiplusStartup(&tok, &si, NULL)) return NULL; }
    int W = LCD_W, H = LCD_H;                                      /* native portrait; our frame is landscape */
    unsigned *px = (unsigned *)calloc((size_t)W * H, 4); if (!px) return NULL;
    float s = fminf((float)H / fw, (float)W / fh);                 /* landscape frame onto the H x W landscape view */
    int dw = (int)(fw * s), dh = (int)(fh * s), ox = (H - dw) / 2, oy = (W - dh) / 2;
    for (int y = 0; y < W; y++) for (int x = 0; x < H; x++) {      /* (x, y) in the landscape view */
        int sx = (int)((x - ox) / s), sy = (int)((y - oy) / s); unsigned c = 0;
        if (x >= ox && y >= oy && sx >= 0 && sy >= 0 && sx < fw && sy < fh) { const unsigned char *q = rgb + ((size_t)sy * fw + sx) * 3; c = (unsigned)q[0] << 16 | q[1] << 8 | q[2]; }
        int nx = flip ? W - 1 - y : y, ny = flip ? x : H - 1 - x;   /* rotate a quarter turn into portrait */
        px[(size_t)ny * W + nx] = c;
    }
    void *bmp = NULL; unsigned char *out = NULL; *len = 0;
    if (GdipCreateBitmapFromScan0(W, H, W * 4, 0x00022009 /* PixelFormat32bppRGB */, (BYTE *)px, &bmp) == 0) {
        IStream *st = NULL;
        if (CreateStreamOnHGlobal(NULL, TRUE, &st) == S_OK) {
            static const CLSID JPG = {0x557cf401, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
            static const GUID QUAL = {0x1d5be4b5, 0xfa4a, 0x452d, {0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb}};
            ULONG q = 70; GpEncParams ep = {1, {{QUAL, 1, 4 /* long */, &q}}};
            if (GdipSaveImageToStream(bmp, st, &JPG, &ep) == 0) {
                HGLOBAL hg; STATSTG ss;
                if (GetHGlobalFromStream(st, &hg) == S_OK && st->lpVtbl->Stat(st, &ss, STATFLAG_NONAME) == S_OK) {
                    ULONG n = (ULONG)ss.cbSize.QuadPart; void *src = GlobalLock(hg);
                    if (src && (out = (unsigned char *)malloc(512 + n))) { memcpy(out + 512, src, n); *len = n; }
                    if (src) GlobalUnlock(hg);
                }
            }
            st->lpVtbl->Release(st);
        }
        GdipDisposeImage(bmp);
    }
    free(px);
    return out;
}

/* ------------------------------------------------------------ worker: finds the panel, then sends the latest frame */
static CRITICAL_SECTION LCD_CS; static unsigned char *LCD_FRAME; static int LCD_NEW, LCD_FLIP = 1, LCD_BRIGHT = 80;
static volatile int LCD_ON, LCD_STOP;                                   /* STOP: blank the panel, release it, stop looking */
static volatile LONG LCD_FPS10; static volatile int LCD_ENC_MS, LCD_USB_MS, LCD_KB;                                           /* frames per second x10, for status */
static DWORD WINAPI lcd_thread(void *arg) {
    (void)arg;
    unsigned char *frame = (unsigned char *)malloc((size_t)PANEL_W * PANEL_H * 3);
    long fps_t = (long)GetTickCount64(); int fps_n = 0;
    for (;;) {
        if (LCD_STOP) {                                                    /* stopped from the tray (or exiting): go dark */
            if (LCD_ON) {
                unsigned char *black = (unsigned char *)calloc((size_t)PANEL_W * PANEL_H, 3); ULONG n = 0, r2;
                unsigned char *buf = black ? lcd_jpeg(black, PANEL_W, PANEL_H, LCD_FLIP, &n) : NULL, rep[512];
                if (buf) { unsigned char len[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
                           lcd_seal(101, len, 4, buf); lcd_xfer(buf, 512 + n, rep); free(buf); }
                free(black); (void)r2;
                unsigned char z = 0; lcd_cmd(14, &z, 1);
                lcd_close(); LCD_ON = 0; snprintf(LCD_ERR, sizeof LCD_ERR, "stopped");
            }
            Sleep(200); continue;
        }
        if (!LCD_ON) {
            if (lcd_open()) {
                unsigned char b = (unsigned char)(LCD_BRIGHT * 102 / 100); lcd_cmd(14, &b, 1);
                LCD_ON = 1;
            } else { Sleep(2000); continue; }                             /* not plugged in / off: look again in 2 s */
        }
        int have = 0;
        EnterCriticalSection(&LCD_CS);
        if (LCD_NEW) { memcpy(frame, LCD_FRAME, (size_t)PANEL_W * PANEL_H * 3); LCD_NEW = 0; have = 1; }
        LeaveCriticalSection(&LCD_CS);
        if (!have) { Sleep(5); continue; }
        LARGE_INTEGER q0, q1, q2, qf; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
        ULONG n = 0; unsigned char *buf = lcd_jpeg(frame, PANEL_W, PANEL_H, LCD_FLIP, &n), reply[512];
        QueryPerformanceCounter(&q1);
        if (!buf) { snprintf(LCD_ERR, sizeof LCD_ERR, "jpeg encode failed"); Sleep(50); continue; }
        unsigned char len[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
        lcd_seal(101, len, 4, buf);
        int ok = lcd_xfer(buf, 512 + n, reply);
        QueryPerformanceCounter(&q2);
        LCD_ENC_MS = (int)((q1.QuadPart - q0.QuadPart) * 1000 / qf.QuadPart); LCD_USB_MS = (int)((q2.QuadPart - q1.QuadPart) * 1000 / qf.QuadPart); LCD_KB = (int)(n / 1024);
        free(buf);
        if (!ok) { snprintf(LCD_ERR, sizeof LCD_ERR, "frame write failed (%lu) jpeg=%lu", GetLastError(), n); lcd_close(); LCD_ON = 0; Sleep(500); continue; }       /* unplugged / turned off: back to searching */
        fps_n++;
        long t = (long)GetTickCount64(); if (t - fps_t >= 2000) { LCD_FPS10 = fps_n * 10000 / (t - fps_t); fps_n = 0; fps_t = t; }
    }
    return 0;
}
static void lcd_start(void) {
    InitializeCriticalSection(&LCD_CS);
    LCD_FRAME = (unsigned char *)malloc((size_t)PANEL_W * PANEL_H * 3);
    CreateThread(NULL, 0, lcd_thread, NULL, 0, NULL);
}
static void lcd_push(const unsigned char *rgb) {                          /* hand the newest frame to the worker */
    if (!LCD_ON) return;
    EnterCriticalSection(&LCD_CS); memcpy(LCD_FRAME, rgb, (size_t)PANEL_W * PANEL_H * 3); LCD_NEW = 1; LeaveCriticalSection(&LCD_CS);
}
