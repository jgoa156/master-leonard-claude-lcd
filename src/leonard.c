/* leonard.c - Master Leonard: the ram-skull Claude status avatar. ONE exe does everything.
 *
 *   build:  build.ps1  (python tools\gen_panel.py, then
 *           gcc -O2 -mwindows -I gen -I src -o bin\leonard.exe src\leonard.c -lgdi32 -lws2_32 -lm)
 *
 *   leonard.exe                        run in the background: no window, no console. Listens for Claude
 *                                      Code sessions and renders the 480x320 frame (the USB panel output
 *                                      plugs in at the marked spot in main)
 *   leonard.exe --hook                 the Claude Code hook: reads the hook JSON on stdin, sends
 *                                      "<session_id> <mood>" to 127.0.0.1:47474, exits in a few ms
 *   leonard.exe --preview              show a 2x preview window; replaces a running copy. The X hides the window\r\n *                                      (the app keeps running); keys 1-6 force a mood for 15 s,
 *                                      a = automatic again, q / Esc quit)
 *   leonard.exe --stop                 stop the running instance
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
#include <winsock2.h>
#include <windows.h>
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
/* Never prints, never waits, always exits 0: it cannot slow down or block Claude, even when the app is not running. */
static int hook_main(void) {
    static char js[1 << 16];                    /* head of the payload: the fields we need come first */
    size_t n = fread(js, 1, sizeof js - 1, stdin); js[n] = 0;
    char ev[64] = "", sid[128] = "";
    field(js, "hook_event_name", ev, sizeof ev);
    if (!*ev) return 0;                          /* not a hook payload (garbage, empty stdin): ignore it, do not invent a session */
    if (!field(js, "session_id", sid, sizeof sid)) strcpy(sid, "unknown");

    const char *mood =
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

/* blend colour c at strength k (0..1) over the frame, keeping the brighter */
static void lit(int x, int y, const float c[3], float k) {
    if (x < 0 || y < 0 || x >= PANEL_W || y >= PANEL_H || k <= 0) return;
    for (int q = 0; q < 3; q++) { int v = (int)(c[q] * clampf(k, 0, 1)); if (v > FB[y][x][q]) FB[y][x][q] = (unsigned char)v; }
}

/* mood-shaped eye core, centred on (ex,ey), sized to the socket */
static int core(int state, int blink, float dx, float dy, float rx, float ry, int sx) {
    float ax = fabsf(dx);
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
    const PPose *P = &POSES[a->dir + 1];
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        unsigned char g = P->img[y * PANEL_W + x];
        FB[y][x][0] = g; FB[y][x][1] = g; FB[y][x][2] = (unsigned char)(g * 0.96f);
    }
    float m = av_pulse(a, t), glow[3], hot[3];
    for (int q = 0; q < 3; q++) { glow[q] = (float)ECOL[a->state][q]; hot[q] = ECOL[a->state][q] + (255 - ECOL[a->state][q]) * 0.45f; }
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
    /* alert = "I need you": every 0.5 s the whole screen flips to a black & white negative */
    if (a->state == ALERT && ((t - a->t0) / 500) % 2 == 1)
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
            unsigned char *p = FB[y][x];
            unsigned char v = (unsigned char)(255 - (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100);
            p[0] = p[1] = p[2] = v;
        }
}

/* ------------------------------------------------------------ Claude sessions (leonard_hook.exe -> UDP) */
#define MAXS 64
typedef struct { char id[256]; int mood, worked; long t; } Sess;
static Sess SS[MAXS]; static int NS, QUIT; static SOCKET SK = INVALID_SOCKET;
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
        if (!strcmp(buf, "ctl")) { if (!strcmp(sp, "quit")) QUIT = 1; continue; }
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
        if (t - SS[i].t > 30 * 60000L) { SS[i--] = SS[--NS]; continue; }         /* gone silent */
        if ((SS[i].mood == SPEAKING || SS[i].mood == HAPPY) && t - SS[i].t > 6000) SS[i].mood = IDLE;   /* shown, back to idle */
        if (SS[i].mood == THINKING && t - SS[i].t > STALE_MS) { SS[i].mood = IDLE; SS[i].worked = 0; }   /* interrupted: no Stop hook exists for Esc */
        if (best < 0 || PRIORITY[SS[i].mood] > PRIORITY[best]) best = SS[i].mood;
    }
    return best < 0 ? SLEEPY : best;
}

/* ------------------------------------------------------------ preview window */
static BITMAPINFO BI; static unsigned int PIX[PANEL_H][PANEL_W]; static int KEY = -1;
static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CHAR: KEY = (int)w; return 0;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;           /* the X hides the window; the app keeps running (q / Esc quit it) */
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, r.right, r.bottom, 0, 0, PANEL_W, PANEL_H, PIX, &BI, DIB_RGB_COLORS, SRCCOPY);
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
        compose(&a, a.t0 + (argc > ai + 3 ? atol(argv[ai + 3]) : 0));
        FILE *f = fopen(argc > ai + 2 ? argv[ai + 2] : "frame.ppm", "wb"); if (!f) return 1;
        fprintf(f, "P6 %d %d 255\n", PANEL_W, PANEL_H); fwrite(FB, 1, sizeof FB, f); fclose(f);
        return 0;
    }

    int preview = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--preview")) preview = 1;
        if (!strcmp(argv[i], "--stale") && i + 1 < argc) STALE_MS = atol(argv[++i]) * 1000L;
        if (!strcmp(argv[i], "--stop")) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); net_send("ctl quit"); return 0; }
    }
    if (!net_init()) {                                        /* a copy is already running: only one instance at a time */
        if (!preview) return 0;
        net_send("ctl quit"); Sleep(900);                     /* --preview takes over, so a window really appears */
        if (!net_init()) return 0;
    }
    rt_init();
    { FILE *f = rt_fopen("events.log", "w"); if (f) fclose(f); }   /* fresh event log per run */

    HWND hw = NULL;
    if (preview) {                                            /* the only visible window; background mode has none */
        WNDCLASS wc = {0}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "impure_panel"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
        RECT r = {0, 0, PANEL_W * 2, PANEL_H * 2}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hw = CreateWindow("impure_panel", "Master Leonard", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
        BI.bmiHeader.biSize = sizeof BI.bmiHeader; BI.bmiHeader.biWidth = PANEL_W; BI.bmiHeader.biHeight = -PANEL_H;
        BI.bmiHeader.biPlanes = 1; BI.bmiHeader.biBitCount = 32; BI.bmiHeader.biCompression = BI_RGB;
    }

    long manual_until = 0;                                   /* preview keys 1-6 override the sessions for 15 s */
    char last_sig[2048] = "";
    av_set(&a, st, (long)GetTickCount64()); a.next_blink = (long)GetTickCount64() + rnd(2500, 6000);
    for (int run = 1; run && !QUIT;) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = 0; TranslateMessage(&msg); DispatchMessage(&msg); }
        long t = (long)GetTickCount64();
        if (KEY == 'q' || KEY == 27) run = 0;
        if (KEY >= '1' && KEY <= '6') { av_set(&a, KEY - '1', t); manual_until = t + 15000; }
        if (KEY == 'a') manual_until = 0;                    /* back to automatic */
        KEY = -1;
        net_poll(t);
        if (t >= manual_until) { int m = sessions_mood(t); if (m != a.state) av_set(&a, m, t); }
        av_tick(&a, t);
        compose(&a, t);                                      /* FB = the 480x320 frame: THE USB PANEL OUTPUT GOES HERE */
        {   /* status file: shown mood + every session's own mood, rewritten whenever any of it changes */
            char sig[2048]; int n = snprintf(sig, sizeof sig, "%s %d\n", SNAME[a.state], NS);
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