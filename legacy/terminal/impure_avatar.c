/* impure_avatar.c - animated ASCII ram skull avatar with eye LEDs
 *
 *   build:  python gen_sprites.py; gcc -O2 -o impure_avatar.exe impure_avatar.c -lm
 *   run:    impure_avatar [state]     states: idle thinking speaking happy alert sleepy
 *   keys:   1-6 change state, q / Esc quit
 *
 * Three hand-made poses (looking left / centre / right, from sprites.h) snap
 * Doom-face style; the eye sockets glow in the state colour and carry a small
 * eye glyph, with the same moods and behaviour as goat-avatar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#endif
#include "sprites.h"
#include "avatar.h"

#define MAXW 400
#define MAXH 120

/* ------------------------------------------------------------ platform */
static long now_ms(void) {
#ifdef _WIN32
    return (long)GetTickCount64();
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
#endif
}
static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}
static void term_size(int *cols, int *rows) {
    *cols = 150; *rows = 46;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO i;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i)) {
        *cols = i.srWindow.Right - i.srWindow.Left + 1;
        *rows = i.srWindow.Bottom - i.srWindow.Top + 1;
    }
#else
    struct winsize w;
    if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_col) { *cols = w.ws_col; *rows = w.ws_row; }
#endif
}
#ifndef _WIN32
static struct termios old_tio;
#endif
static void term_init(void) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD m = 0;
    GetConsoleMode(h, &m); SetConsoleMode(h, m | 0x0004 /* VT processing */);
    SetConsoleOutputCP(65001);
#else
    struct termios t; tcgetattr(0, &old_tio); t = old_tio;
    t.c_lflag &= ~(ICANON | ECHO); t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &t);
#endif
    printf("\x1b[?1049h\x1b[?25l\x1b[2J"); fflush(stdout);
}
static void term_done(void) {
    printf("\x1b[0m\x1b[?25h\x1b[?1049l"); fflush(stdout);
#ifndef _WIN32
    tcsetattr(0, TCSANOW, &old_tio);
#endif
}
static int key_poll(void) {
#ifdef _WIN32
    return _kbhit() ? _getch() : -1;
#else
    fd_set s; struct timeval tv = {0, 0}; FD_ZERO(&s); FD_SET(0, &s);
    if (select(1, &s, NULL, NULL, &tv) > 0) { unsigned char c; if (read(0, &c, 1) == 1) return c; }
    return -1;
#endif
}
static float sq(float v) { return v * v; }

/* ------------------------------------------------------------ poses */
typedef struct { const char **rows; const float (*eye)[4]; int w, h; } Pose;
static Pose poses[3];                          /* 0 = looking left, 1 = centre, 2 = right */

static void init_pose(Pose *p, const char **rows, const float (*eye)[4]) {
    p->rows = rows; p->eye = eye; p->w = p->h = 0;
    for (; rows[p->h]; p->h++) { int l = (int)strlen(rows[p->h]); if (l > p->w) p->w = l; }
}
static char cell(const Pose *p, int c, int r) {
    if (r < 0 || r >= p->h || c < 0) return ' ';
    const char *s = p->rows[r]; return c < (int)strlen(s) ? s[c] : ' ';
}

/* ------------------------------------------------------------ eye glyphs */
static void eye_text(int state, int blink, int n, int sx, char *out) {
    if (n < 2) n = 2;
    if (n > 12) n = 12;
    int i = 0;
    if (blink || state == SLEEPY) { for (; i < n; i++) out[i] = '-'; }
    else if (state == THINKING)   { out[i++] = '.'; while (i < n - 1) out[i++] = '='; out[i++] = '.'; }
    else if (state == HAPPY)      { out[i++] = '/'; while (i < n - 1) out[i++] = '"'; out[i++] = '\\'; }
    else if (state == ALERT)      { if (sx < 0) { out[i++] = '\\'; while (i < n) out[i++] = '='; }
                                    else { while (i < n - 1) out[i++] = '='; out[i++] = '/'; } }
    else                          { out[i++] = '<'; while (i < n - 1) out[i++] = '='; out[i++] = '>'; }
    out[i] = 0;
}

/* ------------------------------------------------------------ frame output */
static char *obuf; static size_t olen;
static void oput(const char *s, size_t n) { memcpy(obuf + olen, s, n); olen += n; }
static int ov_set[MAXH][MAXW], ov_c[MAXH][MAXW][3]; static char ov_g[MAXH][MAXW];

static void compose(const Av *a, long t, int cols, int rows) {
    const Pose *P = &poses[a->dir + 1];
    int GH = rows - 1 < MAXH ? rows - 1 : MAXH, GW = cols < MAXW ? cols : MAXW;
    int ox = (P->w - GW) / 2, oy = (P->h - GH) / 2;      /* sprite cell = screen cell + offset */
    float m = av_pulse(a, t);
    const int *ec = ECOL[a->state];
    memset(ov_set, 0, sizeof ov_set);
    for (int s = 0; s < 2; s++) {
        float ec_x = P->eye[s][0], ec_y = P->eye[s][1], wx = P->eye[s][2], wy = P->eye[s][3];
        int sx = ec_x < P->w / 2.0f ? -1 : 1;
        for (int r = (int)(ec_y - wy - 1); r <= (int)(ec_y + wy + 1); r++)
            for (int c = (int)(ec_x - wx - 1); c <= (int)(ec_x + wx + 1); c++) {
                int R = r - oy, C = c - ox;
                if (R < 0 || R >= GH || C < 0 || C >= GW || cell(P, c, r) != ' ') continue;
                float d = sqrtf(sq((c - ec_x) / wx) + sq((r - ec_y) / wy));
                if (d >= 1.05f) continue;
                float k = powf(fmaxf(0, 1 - d), 1.1f) * m;
                static const char G[] = ".:+*";
                int gi = (int)(k * 4); if (gi > 3) gi = 3;
                ov_set[R][C] = 1; ov_g[R][C] = G[gi];
                for (int q = 0; q < 3; q++) ov_c[R][C][q] = (int)(ec[q] * (0.3f + 0.7f * k));
            }
        char txt[16]; eye_text(a->state, a->blink, (int)lroundf(wx * 1.3f), sx, txt);
        int len = (int)strlen(txt), c0 = (int)lroundf(ec_x) - len / 2, R = (int)lroundf(ec_y) - oy;
        for (int i = 0; i < len; i++) {
            int C = c0 + i - ox; if (C < 0 || C >= GW || R < 0 || R >= GH) continue;
            ov_set[R][C] = 2; ov_g[R][C] = txt[i];
            for (int q = 0; q < 3; q++) { int v = ec[q] + (int)((255 - ec[q]) * 0.4f); ov_c[R][C][q] = (int)(v * (0.55f + 0.45f * m)); }
        }
    }
    olen = 0; oput("\x1b[H", 3);
    for (int R = 0; R < GH; R++) {
        int lr = -1, lg = -1, lb = -1;
        for (int C = 0; C < GW; C++) {
            char ch = cell(P, C + ox, R + oy); int r, g, b;
            if (ov_set[R][C]) { ch = ov_g[R][C]; r = ov_c[R][C][0]; g = ov_c[R][C][1]; b = ov_c[R][C][2]; }
            else if (ch != ' ') { int v = strchr(".,'`", ch) ? 150 : 225; r = v; g = v; b = v - 8; }
            else { oput(" ", 1); continue; }
            if (r != lr || g != lg || b != lb) { olen += sprintf(obuf + olen, "\x1b[38;2;%d;%d;%dm", r, g, b); lr = r; lg = g; lb = b; }
            if (ov_set[R][C] == 2) { oput("\x1b[1m", 4); oput(&ch, 1); oput("\x1b[22m", 5); }
            else oput(&ch, 1);
        }
        oput("\x1b[0m\x1b[K\n", 8);
    }
    char cap[64]; int n = snprintf(cap, sizeof cap, "%s%.*s", SNAME[a->state], a->state == THINKING ? a->dots : 0, "...");
    int pad = (GW - n) / 2; if (pad < 0) pad = 0;
    olen += sprintf(obuf + olen, "\x1b[90m%*s%s\x1b[0m\x1b[K", pad, "", cap);
}

/* ------------------------------------------------------------ main */
int main(int argc, char **argv) {
    srand((unsigned)time(NULL));
    init_pose(&poses[0], SPR_LEFT, EYE_LEFT);
    init_pose(&poses[1], SPR_FRONT, EYE_FRONT);
    init_pose(&poses[2], SPR_RIGHT, EYE_RIGHT);
    int st = IDLE, dump = argc > 1 && !strcmp(argv[1], "--dump"), ai = dump ? 2 : 1;
    if (argc > ai) for (int i = 0; i < NSTATE; i++) if (!strcmp(argv[ai], SNAME[i])) st = i;

    obuf = malloc((size_t)MAXW * MAXH * 48 + 4096);
    Av a = {0}; a.t0 = now_ms();
    if (dump) {                                   /* one plain frame: --dump [state] [dir -1|0|1] */
        a.state = st; a.dir = argc > ai + 1 ? atoi(argv[ai + 1]) : 0; a.dots = 3;
        compose(&a, a.t0, 150, 46);
        for (size_t i = 0; i < olen; i++) {
            if (obuf[i] == 0x1b) { while (i < olen && !strchr("HmKJlh", obuf[i])) i++; continue; }
            putchar(obuf[i]);
        }
        putchar('\n'); return 0;
    }
    int cols, rows; term_size(&cols, &rows);
    term_init(); atexit(term_done);
    av_set(&a, st, now_ms()); a.next_blink = now_ms() + rnd(2500, 6000);
    for (int frame = 0, run = 1; run; frame++) {
        long t = now_ms();
        int k; while ((k = key_poll()) != -1) {
            if (k == 'q' || k == 27) run = 0;
            if (k >= '1' && k <= '6') av_set(&a, k - '1', t);
        }
        av_tick(&a, t);
        if (frame % 30 == 0) {
            int c2, r2; term_size(&c2, &r2);
            if (c2 != cols || r2 != rows) { cols = c2; rows = r2; printf("\x1b[2J"); }
        }
        compose(&a, t, cols, rows);
        fwrite(obuf, 1, olen, stdout); fflush(stdout);
        sleep_ms(33);
    }
    return 0;
}
