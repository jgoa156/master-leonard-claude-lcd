/* avatar.h - mood/state logic shared by leonard.c (the panel app) and the terminal version */
#ifndef AVATAR_H
#define AVATAR_H
#include <stdlib.h>
#include <math.h>

enum { IDLE, THINKING, SPEAKING, HAPPY, ALERT, SLEEPY, DEAD, NSTATE };
static const char *SNAME[NSTATE] = {"idle", "thinking", "speaking", "happy", "alert", "sleepy", "dead"};
static const int ECOL[NSTATE][3] = {{255,150,0},{51,214,255},{255,236,150},{93,255,122},{255,30,30},{150,100,20},{210,20,20}};

/* the red of the alert blink and of the dead eyes' crosses (sampled from the approved mock) */
static const int BLINK_RED[3] = {230, 10, 16};

/* Which mood is SHOWN when several Claude sessions disagree: the highest number wins. Reorder it here.
 *   dead     out of credits / usage limit (StopFailure)  - on top: nothing can work. Eyes are red crosses,
 *            head always centred. Clears when that session sends its next event (credits are back).
 *   alert    a session needs you (permission / input)  - the goat blinks white <-> red
 *   happy    a run that used tools just finished
 *   speaking a plain answer just arrived
 *   thinking a session is working                      - below the "something finished" moods
 *   idle     sessions open but quiet
 *   sleepy   no sessions                               - bottom
 * happy / speaking are momentary (about 6 s), then that session counts as idle. */
static const int PRIORITY[NSTATE] = {
    [IDLE] = 1, [THINKING] = 2, [SPEAKING] = 3, [HAPPY] = 4, [ALERT] = 5, [DEAD] = 6, [SLEEPY] = 0 };

/* Head follows the mouse: the primary monitor is cut into 3 vertical slices (left / centre / right) and the
 * head looks at the slice the cursor is in. A cursor on another monitor counts as left / right of the primary
 * one. A small dead band around each cut stops the head flickering when the cursor rests on the line.
 * cur / return: -1 left, 0 centre, 1 right. */
static inline int look_dir(int cur, int x, int w) {
    float f = w > 0 ? (float)x / (float)w : 0.5f, A = 1.0f / 3, B = 2.0f / 3, m = 0.012f;
    if (cur < 0)      { if (f > A + m) cur = f > B + m ? 1 : 0; }
    else if (cur > 0) { if (f < B - m) cur = f < A - m ? -1 : 0; }
    else              { if (f < A - m) cur = -1; else if (f > B + m) cur = 1; }
    return cur;
}

typedef struct { int state, dir, blink, dots, sessions; long next_look, next_blink, blink_end, t0; } Av;

static int rnd(int a, int b) { return a + rand() % (b - a + 1); }

/* pick where to look next (dir: -1 left, 0 centre, 1 right) */
static void av_look(Av *a, long t) {
    int wait = 1000;
    switch (a->state) {
    case IDLE:     { static const int d[4] = {0, 0, -1, 1}; a->dir = d[rand() % 4]; wait = rnd(700, 2200); } break;
    case THINKING: a->dir = a->dir == 1 ? -1 : 1; wait = 900; a->dots = (a->dots + 1) % 4; break;
    case SPEAKING: a->dir = rand() % 10 < 7 ? 0 : (rand() % 2 ? -1 : 1); wait = rnd(900, 2500); break;
    case HAPPY:    a->dir = 0; wait = 450; break;
    case ALERT:    a->dir = 0; wait = 1000; break;
    case SLEEPY:   a->dir = 0; wait = 1500; break;
    case DEAD:     a->dir = 0; wait = 1500; break;
    }
    a->next_look = t + wait;
}
static void av_set(Av *a, int s, long t) { a->state = s; a->dots = 0; av_look(a, t); }

/* advance looks and blinks */
static void av_tick(Av *a, long t) {
    if (t >= a->next_look) av_look(a, t);
    if (!a->blink && t >= a->next_blink) { a->blink = 1; a->blink_end = t + 140; }
    if (a->blink && t >= a->blink_end) { a->blink = 0; a->next_blink = t + rnd(2500, 6000); }
}

/* eye brightness multiplier for the state's pulse */
static float av_pulse(const Av *a, long t) {
    float ts = (t - a->t0) / 1000.0f;
    switch (a->state) {
    case THINKING: return 0.65f + 0.35f * sinf(ts * 5.7f);
    case SPEAKING: return sinf(ts * 35) > 0 ? 1.0f : 0.72f;
    case ALERT:    return 0.6f + 0.4f * sinf(ts * 18);
    case SLEEPY:   return 0.5f;
    case DEAD:     return 0.9f;
    }
    return 1;
}
#endif
