/* leonard.c - Master Leonard: the ram-skull Claude status avatar. ONE exe does everything.
 *
 *   build:  build.ps1  (python tools\gen_panel.py, then
 *           gcc -O2 -mwindows -I gen -I src -o bin\leonard.exe src\leonard.c -lgdi32 -lws2_32 -lm)
 *
 *   leonard.exe                        (a double-click) show the window; a copy already running just shows its own. Listens for Claude
 *                                      Code sessions and renders the 480x320 frame (the USB panel output
 *                                      plugs in at the marked spot in main)
 *   (the window has no title bar: drag it by its picture; the close button is an invisible hot corner at the top
 *    right that shows an ASCII x in #dc4583 only while the mouse is over it, and hides the window - the app keeps running)
 *   leonard.exe --hook                 the Claude Code hook: reads the hook JSON on stdin, sends
 *                                      "<session_id> <mood>" to 127.0.0.1:47474, exits in a few ms
 *   leonard.exe --preview              show a 2x preview window; replaces a running copy. The X hides the window\r\n *                                      (the app keeps running); keys 1-7 force a mood for 15 s,
 *                                      a = automatic again, q / Esc quit)
 *   leonard.exe --stop                 stop the running instance
 *   leonard.exe --meter N              diagnostic: record the raw left / right output level for N s -> meter.txt
 *   leonard.exe --no-audio             no audio ghosts (the cyan / #dc4583 copies that glow with the left / right channel)
 *   leonard.exe --no-follow            the head does not follow the mouse (it glances around by mood instead)
 *   leonard.exe --stale N              seconds of silence before "thinking" counts as interrupted (120)
 *   leonard.exe --dump mood dir out.ppm [ms]    one frame to a PPM file (tests / mockups)
 *
 * Runtime files live in %LOCALAPPDATA%\MasterLeonard (status.txt, events.log), not next to the exe.
 * The poses in gen\panel.h are the ASCII sprites pre-rendered and scaled to the panel (tools\gen_panel.py).
 * Each frame copies the pose and paints the eye LEDs in pixels. Moods and behaviour: src\avatar.h.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#define COBJMACROS
#include <winsock2.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include "panel.h"
#include "avatar.h"

/* UDP port the hook sends to and the app listens on; LEONARD_PORT overrides it (tests run an isolated copy) */
static int port(void) { char b[16]; return GetEnvironmentVariableA("LEONARD_PORT", b, sizeof b) ? atoi(b) : 47474; }

/* ------------------------------------------------------------ runtime files */
static char RT[MAX_PATH + 32];                                  /* %LOCALAPPDATA%\MasterLeonard */
static void rt_init(void) {
    char base[MAX_PATH]; DWORD n;
    if (GetEnvironmentVariableA("LEONARD_DIR", RT, MAX_PATH)) { CreateDirectoryA(RT, NULL); return; }   /* tests */
    n = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base);
    snprintf(RT, sizeof RT, "%s\\MasterLeonard", n ? base : ".");
    CreateDirectoryA(RT, NULL);
}
static FILE *rt_fopen(const char *name, const char *mode) {
    char p[MAX_PATH + 64]; snprintf(p, sizeof p, "%s\\%s", RT, name); return fopen(p, mode);
}

/* ------------------------------------------------------------ --hook: Claude Code hook -> UDP */
/* value of a top-level string field "key": "value" (enough for hook payloads) */
static int field(const char *js, const char *key, char *out, int n) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(js, pat); if (!p) return 0;
    p = strchr(p + strlen(pat), ':'); if (!p) return 0;
    while (*++p == ' ' || *p == '\t') ;
    if (*p != '"') return 0;
    int i = 0; for (p++; *p && *p != '"' && i < n - 1; p++) out[i++] = *p;
    out[i] = 0; return 1;
}
/* StopFailure = the turn ended on an API error. Out of credits / usage limit / account on hold: the goat "dies".
 * Any other API error (overloaded, server error ...) still needs you to look, so it is an alert. */
static const char *failure_mood(const char *js) {
    char er[48] = "";
    field(js, "error", er, sizeof er);
    return (!strcmp(er, "billing_error") || !strcmp(er, "rate_limit") || !strcmp(er, "account_on_hold")) ? "dead" : "alert";
}
/* Never prints, never waits, always exits 0: it cannot slow down or block Claude, even when the app is not running. */
static int hook_main(void) {
    static char js[1 << 16];                    /* head of the payload: the fields we need come first */
    size_t n = fread(js, 1, sizeof js - 1, stdin); js[n] = 0;
    char ev[64] = "", sid[128] = "";
    field(js, "hook_event_name", ev, sizeof ev);
    if (!*ev) return 0;                          /* not a hook payload (garbage, empty stdin): ignore it, do not invent a session */
    if (!field(js, "session_id", sid, sizeof sid)) strcpy(sid, "unknown");

    const char *mood =
        !strcmp(ev, "StopFailure")      ? failure_mood(js) :   /* API error: out of credits -> dead, other -> alert */
        !strcmp(ev, "Notification")     ? "alert"    :   /* permission prompt / waiting for you */
        !strcmp(ev, "Stop")             ? "done"     :   /* reply finished: the app picks happy (task) or speaking (chat) */
        !strcmp(ev, "PreToolUse")       ? "work"     :   /* tools running: marks this run as a task */
        !strcmp(ev, "PostToolUse")      ? "work"     :
        !strcmp(ev, "UserPromptSubmit") ? "prompt"   :   /* a new run begins */
        !strcmp(ev, "SessionStart")     ? "idle"     :
        !strcmp(ev, "SessionEnd")       ? "end"      :
                                          "thinking";    /* subagents, compaction */

    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return 0;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port());
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        char msg[256]; int len = snprintf(msg, sizeof msg, "%s %s", sid, mood);
        sendto(s, msg, len, 0, (struct sockaddr *)&a, sizeof a);
        closesocket(s);
    }
    WSACleanup();
    return 0;
}

typedef struct { const unsigned char *img; const float (*eye)[4]; } PPose;
static const PPose POSES[3] = {{PAN_LEFT, PEYE_LEFT}, {PAN_FRONT, PEYE_FRONT}, {PAN_RIGHT, PEYE_RIGHT}};
static unsigned char FB[PANEL_H][PANEL_W][3];               /* RGB frame */

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* ---- session tally: ASCII, in the free strip at the bottom. One '|' per open Claude session; at five, a '/' is
 *      laid across the four (prison-style groups). Always centred; nothing when no session is open. */
static void tblit(const unsigned char *g, int gw, int gh, int x0, int y0, const float ink[3]) {
    for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
        int X = x0 + x, Y = y0 + y; unsigned char v = g[y * gw + x];
        if (!v || X < 0 || Y < 0 || X >= PANEL_W || Y >= PANEL_H) continue;
        for (int q = 0; q < 3; q++) { int c = (int)(v * ink[q]); if (c > FB[Y][X][q]) FB[Y][X][q] = (unsigned char)c; }
    }
}
static void tally(int n, const float ink[3]) {
    if (n <= 0) return;
    if (n > 40) n = 40;
    int groups = (n + 4) / 5, cells = 0;
    for (int g = 0; g < groups; g++) cells += n - g * 5 >= 5 ? 4 : n - g * 5;
    int x = PANEL_W / 2 - (cells + groups - 1) * TALLY_CW / 2;          /* groups are separated by one blank cell */
    for (int g = 0; g < groups; g++) {
        int k = n - g * 5 >= 5 ? 4 : n - g * 5;
        for (int i = 0; i < k; i++) tblit(TALLY_BAR, TALLY_CW, TALLY_H, x + i * TALLY_CW, TALLY_Y, ink);
        if (n - g * 5 >= 5) tblit(TALLY_STRIKE, TALLY_SW, TALLY_SH, x + 2 * TALLY_CW - TALLY_SW / 2, TALLY_Y + TALLY_H / 2 - TALLY_SH / 2, ink);
        x += (k + 1) * TALLY_CW;
    }
}

/* ---- thicker strokes for the main (white / red) skull. The ghosts keep the thin original.
 * THICK 0 = as baked, 1 = about 2 px strokes (4 neighbours), 2 = about 3 px strokes (all 8 neighbours). */
#ifndef THICK
#define THICK 1
#endif
static unsigned char TK[3][PANEL_H][PANEL_W];
static void thick_init(void) {
    for (int p = 0; p < 3; p++) {
        const unsigned char *img = POSES[p].img;
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
            int v = img[y * PANEL_W + x];
            if (THICK >= 1) {
                int n4 = 0, n8 = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int yy = y + dy, xx = x + dx; if (yy < 0 || xx < 0 || yy >= PANEL_H || xx >= PANEL_W) continue;
                    int q = img[yy * PANEL_W + xx];
                    if (!dx || !dy) { if (q > n4) n4 = q; } else if (q > n8) n8 = q;
                }
                if (n4 * 0.92f > v) v = (int)(n4 * 0.92f);
                if (THICK >= 2 && n8 * 0.85f > v) v = (int)(n8 * 0.85f);
            }
            TK[p][y][x] = (unsigned char)(v > 255 ? 255 : v);
        }
    }
}

/* ---- audio ghosts: a cyan copy of the skull (left channel) and a #dc4583 copy (right channel) glow behind the
 *      white one, each as bright and as far out as its channel is loud. LEV = smoothed levels 0..1. */
static float LEV[2], RAW[2];                               /* smoothed 0..1 levels, and the last raw peaks */
static const float GHOST_MAG = 1.5f;                             /* the #dc4583 copy: raised opacity */
static const float GHOST_CYAN = 0.70f;                           /* the cyan copy shines a bit less than the #dc4583 one */
/* glitch: on each beat the ghosts jump to a random offset and a few horizontal slices of them shear sideways,
 * for ~0.1-0.2 s, then they snap back. Only the ghosts move; the white skull stays put. */
static float GLX[2], GLY[2]; static int GBN, GB[4][3]; static long GLITCH_UNTIL, GLITCH_COOL;
static void glitch_kick(long t) {
    float sc = PANEL_W / 800.0f;
    for (int i = 0; i < 2; i++) { GLX[i] = (float)(rand() % 33 - 16) * sc; GLY[i] = (float)(rand() % 9 - 4) * sc; }
    GBN = 1 + rand() % 3;
    for (int b = 0; b < GBN; b++) {
        int h = (int)((6 + rand() % 26) * sc); if (h < 2) h = 2;
        GB[b][0] = rand() % (PANEL_H - h); GB[b][1] = GB[b][0] + h; GB[b][2] = (int)((rand() % 2 ? 1 : -1) * (8 + rand() % 22) * sc);
    }
    GLITCH_UNTIL = t + 90 + rand() % 90;
}
static const float GHOST_R[3] = {220 / 255.0f, 69 / 255.0f, 131 / 255.0f};   /* right-channel ghost: #dc4583 */
static unsigned char GH[3][PANEL_H][PANEL_W];                 /* per pose: the skull's glow (sharp lines + soft halo) */
/* system audio level, per channel: the default output device's peak meter. Re-opened every few seconds so a
 * switch between speakers / headphones is followed; if there is no device the ghosts simply stay off. */
static const GUID LEO_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID LEO_IID_IMMDeviceEnumerator  = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID LEO_IID_IAudioMeterInfo      = {0xC02216F6, 0x8C67, 0x4B5B, {0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64}};
/* IAudioMeterInformation, declared here because MinGW's endpointvolume.h only has the C++ version.
 * Method order is the COM interface's: IUnknown, then GetPeakValue, GetMeteringChannelCount, GetChannelsPeakValues, ... */
typedef struct LeoMeter LeoMeter;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(LeoMeter *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(LeoMeter *);
    ULONG   (STDMETHODCALLTYPE *Release)(LeoMeter *);
    HRESULT (STDMETHODCALLTYPE *GetPeakValue)(LeoMeter *, float *);
    HRESULT (STDMETHODCALLTYPE *GetMeteringChannelCount)(LeoMeter *, UINT *);
    HRESULT (STDMETHODCALLTYPE *GetChannelsPeakValues)(LeoMeter *, UINT32, float *);
    HRESULT (STDMETHODCALLTYPE *QueryHardwareSupport)(LeoMeter *, DWORD *);
} LeoMeterVtbl;
struct LeoMeter { const LeoMeterVtbl *lpVtbl; };
static LeoMeter *METER; static long meter_reopen;
static void audio_close(void) { if (METER) METER->lpVtbl->Release(METER); METER = NULL; }
static void audio_open(void) {
    IMMDeviceEnumerator *en = NULL; IMMDevice *dev = NULL;
    audio_close();
    if (FAILED(CoCreateInstance(&LEO_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &LEO_IID_IMMDeviceEnumerator, (void **)&en))) return;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev))) {
        IMMDevice_Activate(dev, &LEO_IID_IAudioMeterInfo, CLSCTX_ALL, NULL, (void **)&METER);
        IMMDevice_Release(dev);
    }
    IMMDeviceEnumerator_Release(en);
}
static void audio_poll(long t) {
    float pk[8] = {0}; UINT ch = 0;
    if (t >= meter_reopen) { audio_open(); meter_reopen = t + 5000; }
    if (METER && (FAILED(METER->lpVtbl->GetMeteringChannelCount(METER, &ch)) || !ch ||
                  FAILED(METER->lpVtbl->GetChannelsPeakValues(METER, ch > 8 ? 8 : ch, pk)))) { audio_close(); ch = 0; }
    float in[2] = {ch ? pk[0] : 0, ch > 1 ? pk[1] : (ch ? pk[0] : 0)};
    RAW[0] = in[0]; RAW[1] = in[1];
    {   static float slow; float e = fmaxf(in[0], in[1]);                /* a beat: the level jumps above its recent average */
        if (e > 0.08f && e > slow * 1.35f + 0.06f && t >= GLITCH_COOL) { glitch_kick(t); GLITCH_COOL = t + 180; }
        slow += (e - slow) * 0.04f;
    }
    for (int i = 0; i < 2; i++) {                           /* jump up fast, fade out slowly */
        float v = clampf(in[i] * 1.25f, 0, 1);
        LEV[i] += (v - LEV[i]) * (v > LEV[i] ? 0.6f : 0.10f);
        if (LEV[i] < 0.005f) LEV[i] = 0;
    }
}
static void ghost_init(void) {
    static float a[PANEL_H][PANEL_W], b[PANEL_H][PANEL_W];
    const int R = PANEL_W >= 800 ? 4 : 3;
    for (int p = 0; p < 3; p++) {
        const unsigned char *img = &TK[p][0][0];                  /* ghosts come from the thickened skull so they survive a small window */
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) a[y][x] = img[y * PANEL_W + x] / 255.0f;
        for (int pass = 0; pass < 2; pass++) {                /* two box blurs ~ a soft glow */
            for (int y = 0; y < PANEL_H; y++) { float acc = 0; for (int x = -R; x < PANEL_W + R; x++) {
                if (x + R < PANEL_W) acc += a[y][x + R];
                if (x - R - 1 >= 0) acc -= a[y][x - R - 1];
                if (x >= 0 && x < PANEL_W) b[y][x] = acc / (2 * R + 1); } }
            for (int x = 0; x < PANEL_W; x++) { float acc = 0; for (int y = -R; y < PANEL_H + R; y++) {
                if (y + R < PANEL_H) acc += b[y + R][x];
                if (y - R - 1 >= 0) acc -= b[y - R - 1][x];
                if (y >= 0 && y < PANEL_H) a[y][x] = acc / (2 * R + 1); } }
        }
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++)
            GH[p][y][x] = (unsigned char)(255 * clampf(img[y * PANEL_W + x] / 255.0f * 0.55f + a[y][x] * 2.4f, 0, 1));
    }
}

/* blend colour c at strength k (0..1) over the frame, keeping the brighter */
static void lit(int x, int y, const float c[3], float k) {
    if (x < 0 || y < 0 || x >= PANEL_W || y >= PANEL_H || k <= 0) return;
    for (int q = 0; q < 3; q++) { int v = (int)(c[q] * clampf(k, 0, 1)); if (v > FB[y][x][q]) FB[y][x][q] = (unsigned char)v; }
}

/* mood-shaped eye core, centred on (ex,ey), sized to the socket */
static int core(int state, int blink, float dx, float dy, float rx, float ry, int sx) {
    float ax = fabsf(dx);
    if (state == DEAD) {                                                                       /* X X : crosses */
        float hx = rx * 1.05f, hy = ry * 0.62f, len = sqrtf(hx * hx + hy * hy), w = fmaxf(1.1f, rx * 0.16f);
        if (ax > hx || fabsf(dy) > hy) return 0;
        return fabsf(dx * hy - dy * hx) / len < w || fabsf(dx * hy + dy * hx) / len < w;
    }
    if (blink || state == SLEEPY) return fabsf(dy) < 1.0f && ax < rx * 0.8f;                   /* ---- */
    switch (state) {
    case THINKING: return sqrtf((ax - rx * 0.4f) * (ax - rx * 0.4f) + dy * dy) < fmaxf(1.2f, rx * 0.2f);   /* . . */
    case HAPPY:  { float r = sqrtf(dx * dx / (rx * rx * 0.5f) + (dy + ry * 0.15f) * (dy + ry * 0.15f) / (ry * ry * 0.2f));
                   return dy < ry * 0.05f && fabsf(r - 1) < 0.35f; }                                  /* ^  */
    case ALERT:    return ax < rx * 0.8f && fabsf(dy - sx * dx * 0.5f * ry / rx) < fmaxf(1.0f, ry * 0.12f);  /* \= */
    }
    return dx * dx / (rx * rx * 0.45f) + dy * dy / (ry * ry * 0.12f) < 1;                            /* <=> */
}

static void compose(const Av *a, long t) {
    const PPose *P = &POSES[(a->state == DEAD ? 0 : a->dir) + 1];          /* dead: always facing front */
    /* alert = "I need you": the goat itself blinks white <-> red every 0.5 s; the background stays black */
    int red = a->state == ALERT && ((t - a->t0) / 500) % 2 == 1;
    float ink[3] = {1.0f, 1.0f, 0.96f};
    if (red) for (int q = 0; q < 3; q++) ink[q] = BLINK_RED[q] / 255.0f;
    int pi = (a->state == DEAD ? 0 : a->dir) + 1;
    float sc = PANEL_W / 800.0f, L = LEV[0], Rt = LEV[1];
    int oxL = (int)lroundf((3 + 9 * L) * sc), oxR = (int)lroundf((3 + 9 * Rt) * sc);   /* louder = further out */
    int gl = t < GLITCH_UNTIL;                                /* a beat just hit: the ghosts are displaced */
    float bl = powf(L, 0.75f), br = powf(Rt, 0.75f), boost = gl ? 1.3f : 1.0f;     /* brighter at low levels, flash on a beat */
    for (int y = 0; y < PANEL_H; y++) {
        int bdx = 0;
        if (gl) for (int b = 0; b < GBN; b++) if (y >= GB[b][0] && y < GB[b][1]) bdx = GB[b][2];
        int ycl = gl ? y - (int)GLY[0] : y, ycr = gl ? y - (int)GLY[1] : y;
        int dxl = (gl ? (int)GLX[0] : 0) + bdx, dxr = (gl ? (int)GLX[1] : 0) + bdx;
        for (int x = 0; x < PANEL_W; x++) {
        int sxl = x + oxL - dxl, sxr = x - oxR - dxr;
        float c = (L  > 0.01f && sxl >= 0 && sxl < PANEL_W && ycl >= 0 && ycl < PANEL_H) ? GH[pi][ycl][sxl] / 255.0f * bl * GHOST_CYAN * boost : 0;  /* cyan, left  */
        float m = (Rt > 0.01f && sxr >= 0 && sxr < PANEL_W && ycr >= 0 && ycr < PANEL_H) ? GH[pi][ycr][sxr] / 255.0f * br * GHOST_MAG * boost : 0;               /* #dc4583, right */
        if (m > 1.0f) m = 1.0f;
        float bg[3] = {m * GHOST_R[0], c + m * GHOST_R[1], c + m * GHOST_R[2]};   /* cyan (0,1,1) + #dc4583 */
        unsigned char g = TK[pi][y][x];                       /* the skull: the thick version */
        for (int q = 0; q < 3; q++) {
            float v = fmaxf(clampf(bg[q], 0, 1) * 255, g * ink[q]);    /* the skull is drawn over its ghosts */
            FB[y][x][q] = (unsigned char)clampf(v, 0, 255);
        }
        }
    }
    float m = av_pulse(a, t), glow[3], hot[3];
    for (int q = 0; q < 3; q++) {
        glow[q] = (float)ECOL[a->state][q];
        hot[q] = a->state == DEAD ? (float)BLINK_RED[q] : ECOL[a->state][q] + (255 - ECOL[a->state][q]) * 0.45f;
    }
    for (int s = 0; s < 2; s++) {
        float ex = P->eye[s][0], ey = P->eye[s][1], rx = P->eye[s][2], ry = P->eye[s][3];
        int sx = ex < PANEL_W / 2.0f ? -1 : 1;
        for (int y = (int)(ey - ry * 1.4f) - 1; y <= (int)(ey + ry * 1.4f) + 1; y++)
            for (int x = (int)(ex - rx * 1.4f) - 1; x <= (int)(ex + rx * 1.4f) + 1; x++) {
                float dx = x + 0.5f - ex, dy = y + 0.5f - ey, d = sqrtf(dx * dx / (rx * rx) + dy * dy / (ry * ry));
                if (d < 1.4f) lit(x, y, glow, powf(1 - d / 1.4f, 1.3f) * m * 0.85f);
                if (core(a->state, a->blink, dx, dy, rx, ry, sx)) lit(x, y, hot, 0.55f + 0.45f * m);
            }
    }
    tally(a->sessions, ink);                                  /* one slash per open session, centred on top */
}

/* ------------------------------------------------------------ Claude sessions (leonard_hook.exe -> UDP) */
#define MAXS 64
typedef struct { char id[256]; int mood, worked; long t; } Sess;
static Sess SS[MAXS]; static int NS, QUIT, SHOW_REQ; static SOCKET SK = INVALID_SOCKET;
static long STALE_MS = 120000;                              /* "thinking" with no event for this long -> assume interrupted/closed -> idle */

static int net_init(void) {                                   /* 0 = another instance already owns the port */
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return 0;
    SK = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port()); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    u_long nb = 1;
    if (SK == INVALID_SOCKET || bind(SK, (struct sockaddr *)&a, sizeof a) || ioctlsocket(SK, FIONBIO, &nb)) {
        SK = INVALID_SOCKET; return 0;
    }
    return 1;
}
static void net_send(const char *msg) {                      /* used by --stop */
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port()); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(s, msg, (int)strlen(msg), 0, (struct sockaddr *)&a, sizeof a); closesocket(s);
}
/* drain pending "<session_id> <mood>" datagrams into the session table */
static void net_poll(long t) {
    char buf[256]; int n;
    while (SK != INVALID_SOCKET && (n = recv(SK, buf, sizeof buf - 1, 0)) > 0) {
        buf[n] = 0;
        { FILE *lf = rt_fopen("events.log", "a"); if (lf) { fprintf(lf, "%ld %s\n", t, buf); fclose(lf); } }   /* every event, for debugging */
        char *sp = strrchr(buf, ' '); if (!sp) continue; *sp++ = 0;
        if (!strcmp(buf, "ctl")) { if (!strcmp(sp, "quit")) QUIT = 1; if (!strcmp(sp, "show")) SHOW_REQ = 1; continue; }
        int i = 0; while (i < NS && strcmp(SS[i].id, buf)) i++;
        if (!strcmp(sp, "end")) { if (i < NS) SS[i] = SS[--NS]; continue; }
        int m = -1; for (int k = 0; k < NSTATE; k++) if (!strcmp(sp, SNAME[k])) m = k;
        int work = !strcmp(sp, "work"), done = !strcmp(sp, "done"), prompt = !strcmp(sp, "prompt");
        if (work || prompt) m = THINKING;
        if (m < 0 && !done) continue;
        if (i == NS) { if (NS == MAXS) continue; NS++; snprintf(SS[i].id, sizeof SS[i].id, "%s", buf); SS[i].worked = 0; }
        if (prompt) SS[i].worked = 0;                          /* new run */
        if (work) SS[i].worked = 1;                            /* tools ran: this run is a task */
        if (done) m = SS[i].worked ? HAPPY : SPEAKING;         /* finished a task vs answered a chat */
        SS[i].mood = m; SS[i].t = t;
    }
}
/* one mood for all sessions: the highest PRIORITY (avatar.h) wins; no sessions -> sleepy */
static int sessions_mood(long t) {
    int best = -1;
    for (int i = 0; i < NS; i++) {
        if (t - SS[i].t > (SS[i].mood == DEAD ? 6 * 3600000L : 30 * 60000L)) { SS[i--] = SS[--NS]; continue; }   /* gone silent (dead: 6 h) */
        if ((SS[i].mood == SPEAKING || SS[i].mood == HAPPY) && t - SS[i].t > 6000) SS[i].mood = IDLE;   /* shown, back to idle */
        if (SS[i].mood == THINKING && t - SS[i].t > STALE_MS) { SS[i].mood = IDLE; SS[i].worked = 0; }   /* interrupted: no Stop hook exists for Esc */
        if (best < 0 || PRIORITY[SS[i].mood] > PRIORITY[best]) best = SS[i].mood;
    }
    return best < 0 ? SLEEPY : best;
}

/* ------------------------------------------------------------ preview window */
/* the preview window is the real screen size: 12 x 7 cm, landscape, at this display's DPI. The frame is scaled to fit
 * inside it with its proportions kept (letterboxed), so it shows how the panel will look at its physical size. */
#define PREVIEW_W_CM 12.0
#define PREVIEW_H_CM 7.0
static int preview_px(double cm) {
    HDC dc = GetDC(NULL); int dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc);
    return (int)lround(cm / 2.54 * (dpi > 0 ? dpi : 96));
}
static BITMAPINFO BI; static unsigned int PIX[PANEL_H][PANEL_W]; static int KEY = -1;
static int CLOSE_HOVER;
#define CLOSE_SZ 64                                          /* the hot corner, top right, in window pixels */
static int in_close(HWND h, int sx, int sy) {                /* screen point inside the close corner? */
    RECT r; GetWindowRect(h, &r);
    return sx >= r.right - CLOSE_SZ && sx < r.right && sy >= r.top && sy < r.top + CLOSE_SZ;
}
static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CHAR: KEY = (int)w; return 0;
    case WM_NCHITTEST: {                                     /* no title bar: drag the window by its picture, except the close corner */
        int sx = (short)LOWORD(l), sy = (short)HIWORD(l);
        return in_close(h, sx, sy) ? HTCLIENT : HTCAPTION; }
    case WM_LBUTTONUP: {                                     /* a click on the close corner hides the window; the app keeps running */
        POINT p; GetCursorPos(&p);
        if (in_close(h, p.x, p.y)) ShowWindow(h, SW_HIDE);
        return 0; }
    case WM_NCLBUTTONDBLCLK: case WM_NCRBUTTONDOWN: case WM_NCRBUTTONUP: return 0;     /* no maximize, no system menu */
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;         /* Alt+F4 / taskbar close: hide, keep running (q / Esc quit) */
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r);
        SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL);      /* averaging when shrinking: thin lines survive */
        int cw = r.right, ch = r.bottom, dw = cw, dh = ch;
        if (cw * PANEL_H > ch * PANEL_W) dw = ch * PANEL_W / PANEL_H; else dh = cw * PANEL_H / PANEL_W;   /* fit, keep proportions */
        int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
        PatBlt(dc, 0, 0, cw, ch, BLACKNESS);
        StretchDIBits(dc, dx, dy, dw, dh, 0, 0, PANEL_W, PANEL_H, PIX, &BI, DIB_RGB_COLORS, SRCCOPY);
        if (CLOSE_HOVER) {                                   /* the close button: an ASCII X, only there (and red) while hovered */
            HFONT f = CreateFontA(-34, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            HFONT old = (HFONT)SelectObject(dc, f);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(220, 69, 131));          /* a plain ASCII x in #dc4583 */
            TextOutA(dc, cw - 28, 2, "x", 1);
            SelectObject(dc, old); DeleteObject(f);
        }
        EndPaint(h, &ps); return 0; }
    }
    return DefWindowProc(h, msg, w, l);
}

int main(int argc, char **argv) {
    {   /* --hook = the Claude Code hook, but only when hook data is piped or redirected in (as Claude Code does). Typed by hand it is ignored,
           so "leonard.exe --hook --preview" just opens the preview. First thing in main: the hook must stay fast. */
        int want_hook = 0;
        for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--hook")) want_hook = 1;
        HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD ft = (hin && hin != INVALID_HANDLE_VALUE) ? GetFileType(hin) : FILE_TYPE_UNKNOWN;
        if (want_hook && (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_DISK)) return hook_main();     /* data piped / redirected in */
    }
    srand((unsigned)time(NULL));
    int st = IDLE, dump = argc > 1 && !strcmp(argv[1], "--dump"), ai = dump ? 2 : 1;
    if (argc > ai) for (int i = 0; i < NSTATE; i++) if (!strcmp(argv[ai], SNAME[i])) st = i;
    Av a = {0}; a.t0 = (long)GetTickCount64();

    if (dump) {                                           /* --dump state dir out.ppm [ms] */
        a.state = st; a.dir = argc > ai + 1 ? atoi(argv[ai + 1]) : 0;
        thick_init(); ghost_init();
        if (argc > ai + 5) { LEV[0] = (float)atof(argv[ai + 4]); LEV[1] = (float)atof(argv[ai + 5]); }   /* audio levels to show */
        if (argc > ai + 6) a.sessions = atoi(argv[ai + 6]);                    /* ... [L R [sessions [glitch]]] */
        if (argc > ai + 7 && atoi(argv[ai + 7])) { srand(7); glitch_kick(a.t0); GLITCH_UNTIL = a.t0 + 3600000L; }
        compose(&a, a.t0 + (argc > ai + 3 ? atol(argv[ai + 3]) : 0));
        FILE *f = fopen(argc > ai + 2 ? argv[ai + 2] : "frame.ppm", "wb"); if (!f) return 1;
        fprintf(f, "P6 %d %d 255\n", PANEL_W, PANEL_H); fwrite(FB, 1, sizeof FB, f); fclose(f);
        return 0;
    }

    int preview = 1, follow = 1, cur_dir = 0, audio = 1;          /* plain "leonard.exe" (a double-click) shows the window */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--meter") && i + 1 < argc) {      /* diagnostic: record the raw L/R output meter for N s */
            long until = (long)GetTickCount64() + atol(argv[i + 1]) * 1000L; double sl = 0, sr = 0; float ml = 0, mr = 0; int k = 0;
            rt_init(); CoInitializeEx(NULL, COINIT_MULTITHREADED);
            while ((long)GetTickCount64() < until) {
                audio_poll((long)GetTickCount64()); sl += RAW[0]; sr += RAW[1]; k++;
                if (RAW[0] > ml) ml = RAW[0];
                if (RAW[1] > mr) mr = RAW[1];
                Sleep(20);
            }
            FILE *f = rt_fopen("meter.txt", "w");
            if (f) { fprintf(f, "device=%s samples=%d meanL=%.3f meanR=%.3f maxL=%.3f maxR=%.3f\n", METER ? "ok" : "none", k, k ? sl / k : 0, k ? sr / k : 0, ml, mr); fclose(f); }
            return 0;
        }
        if (!strcmp(argv[i], "--no-audio")) audio = 0;
        if (!strcmp(argv[i], "--no-follow")) follow = 0;
        if (!strcmp(argv[i], "--preview")) preview = 1;
        if (!strcmp(argv[i], "--background")) preview = 0;           /* hidden: no window, no console (the startup shortcut) */
        if (!strcmp(argv[i], "--stale") && i + 1 < argc) STALE_MS = atol(argv[++i]) * 1000L;
        if (!strcmp(argv[i], "--set") && i + 1 < argc) {       /* scripted mood: leonard.exe --set <mood> [id]  (id defaults to "cli") */
            int ok = 0; for (int k = 0; k < NSTATE; k++) if (!strcmp(argv[i + 1], SNAME[k])) ok = 1;
            if (!ok) return 2;                                 /* unknown mood: nothing sent (exit code 2) */
            char msg[160]; snprintf(msg, sizeof msg, "%s %s", i + 2 < argc ? argv[i + 2] : "cli", argv[i + 1]);
            WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); net_send(msg); return 0;
        }
        if (!strcmp(argv[i], "--stop")) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); net_send("ctl quit"); return 0; }
    }
    if (!net_init()) {                                        /* a copy is already running: only one instance at a time */
        if (preview) net_send("ctl show");                    /* a double-click: bring the running copy's window up (it keeps its sessions) */
        return 0;
    }
    rt_init();
    { FILE *f = rt_fopen("events.log", "w"); if (f) fclose(f); }   /* fresh event log per run */
    thick_init(); ghost_init();
    if (audio) CoInitializeEx(NULL, COINIT_MULTITHREADED);

    HWND hw = NULL;
    #define MAKE_WINDOW() do { \
        WNDCLASS wc = {0}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandle(NULL); \
        wc.lpszClassName = "impure_panel"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); \
        RegisterClass(&wc); \
        int ww = preview_px(PREVIEW_W_CM), wh = preview_px(PREVIEW_H_CM); \
        hw = CreateWindow("impure_panel", "Master Leonard", WS_POPUP | WS_VISIBLE, \
                          (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, NULL, NULL, wc.hInstance, NULL); \
        BI.bmiHeader.biSize = sizeof BI.bmiHeader; BI.bmiHeader.biWidth = PANEL_W; BI.bmiHeader.biHeight = -PANEL_H; \
        BI.bmiHeader.biPlanes = 1; BI.bmiHeader.biBitCount = 32; BI.bmiHeader.biCompression = BI_RGB; \
    } while (0)
    if (preview) MAKE_WINDOW();                               /* the only visible window; --background has none */

    long manual_until = 0;                                   /* preview keys 1-6 override the sessions for 15 s */
    char last_sig[2048] = "";
    av_set(&a, st, (long)GetTickCount64()); a.next_blink = (long)GetTickCount64() + rnd(2500, 6000);
    for (int run = 1; run && !QUIT;) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = 0; TranslateMessage(&msg); DispatchMessage(&msg); }
        long t = (long)GetTickCount64();
        if (KEY == 'q' || KEY == 27) run = 0;
        if (KEY >= '1' && KEY <= '7') { av_set(&a, KEY - '1', t); manual_until = t + 15000; }
        if (KEY == 'a') manual_until = 0;                    /* back to automatic */
        KEY = -1;
        net_poll(t);
        if (SHOW_REQ) {                                      /* "ctl show": a second launch asked for the window */
            SHOW_REQ = 0;
            if (!hw) MAKE_WINDOW(); else { ShowWindow(hw, SW_SHOW); ShowWindow(hw, SW_RESTORE); SetForegroundWindow(hw); }
        }
        if (audio) audio_poll(t);
        if (t >= manual_until) { int m = sessions_mood(t); if (m != a.state) av_set(&a, m, t); }
        av_tick(&a, t);
        if (follow) {                                        /* the head looks at the screen third the cursor is in */
            POINT cp; if (GetCursorPos(&cp)) cur_dir = look_dir(cur_dir, cp.x, GetSystemMetrics(SM_CXSCREEN));
            a.dir = cur_dir;
        }
        if (a.state == DEAD) a.dir = 0;                      /* dead: centred, never follows */
        a.sessions = NS;
        if (hw && IsWindowVisible(hw)) { POINT cp; GetCursorPos(&cp); CLOSE_HOVER = in_close(hw, cp.x, cp.y); } else CLOSE_HOVER = 0;
        compose(&a, t);                                      /* FB = the 480x320 frame: THE USB PANEL OUTPUT GOES HERE */
        {   /* status file: shown mood + every session's own mood, rewritten whenever any of it changes */
            char sig[2048]; static const char *LOOK[3] = {"left", "center", "right"};
            int n = snprintf(sig, sizeof sig, "%s %d audio=%c%c look=%s\n", SNAME[a.state], NS,
                             LEV[0] > 0.15f ? 'L' : '-', LEV[1] > 0.15f ? 'R' : '-', LOOK[a.dir + 1]);   /* audio: coarse, so it is not rewritten every frame */
            for (int i = 0; i < NS && n < 1900; i++) n += snprintf(sig + n, sizeof sig - n, "  ...%.8s %s%s\n", SS[i].id + (strlen(SS[i].id) > 8 ? strlen(SS[i].id) - 8 : 0), SNAME[SS[i].mood], SS[i].worked ? " (worked)" : "");
            if (strcmp(sig, last_sig)) { strcpy(last_sig, sig); FILE *f = rt_fopen("status.txt", "w"); if (f) { fputs(sig, f); fclose(f); } }
        }        if (hw) {
            for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++)
                PIX[y][x] = (unsigned)FB[y][x][0] << 16 | (unsigned)FB[y][x][1] << 8 | FB[y][x][2];
            char title[96]; snprintf(title, sizeof title, "Master Leonard - %s - %d session(s)%s", SNAME[a.state], NS, t < manual_until ? " - manual" : "");
            SetWindowText(hw, title);
            InvalidateRect(hw, NULL, FALSE);
        }
        Sleep(33);
    }
    return 0;
}