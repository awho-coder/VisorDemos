/*
Copyright (C) 2026 VisorDemos contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// keycam.c -- camera keyframes for demo playback (Keygrip style)
//
// The user places camera points at moments of the demo (position, view
// angles, field of view). While the demo runs, the camera travels between
// them on a smooth curve, timed by the demo clock, so clip_export renders
// moving shots without having to fly them live. A point can also aim at a
// player: the camera then keeps turning to look at them. Points are saved
// per demo in campaths/<demo>.keys and loaded again automatically.

#include "client.h"

#define MAX_KEYS        256
#define MIN_SPEED       0.05f
#define MAX_SPEED       4.0f
#define SAME_KEY_MS     100     // a point closer than this is "the point here"
                                // (one demo frame: a seek lands on its start)

typedef enum {
    FOLLOW_NONE,
    FOLLOW_PLAYER,  // behind `target`, like the chase camera
    FOLLOW_PROJ     // behind a projectile (proj.c)
} follow_t;

typedef enum {
    SEG_CURVE,      // smooth curve through the point
    SEG_LINE        // straight line, constant speed, to the next point
} segmode_t;

typedef struct {
    int         time;       // demo_time(), ms
    int         frame;      // CL_DemoSeekFrame(), to seek back to it
    float       pos;        // demo file position, for the timeline
    segmode_t   mode;       // how to go from this point to the next
    int         target;     // player entity to look at, 0 = free view
    vec3_t      org, ang;
    float       fov;
    float       speed;      // demo speed from this point to the next (slow motion)
    follow_t    follow;     // from this point to the next the camera follows
    int         proj_ent;   // FOLLOW_PROJ: entity number and server frame
    int         proj_first; // of the throw
} keyframe_t;

static keyframe_t   keys[MAX_KEYS];
static int          num_keys;
static bool         playing = true;
static char         loaded_for[MAX_QPATH];
static int          pending_view = -1;  // jumped to this point: show its view

static cvar_t       *keycam_aim_height;
static cvar_t       *keycam_show;

static int key_here(void);

// same clock as campath.c: stays put across seeks
static int demo_time(void)
{
    return CL_DemoTimeMs();
}

// a DM2 or a MVD demo file
static bool watching_dm2(void)
{
    return CL_DemoView();
}

static void demo_basename(char *buf, size_t size)
{
    char *p;

    Q_strlcpy(buf, COM_SkipPath(CL_DemoName()), size);
    p = strrchr(buf, '.');
    if (p)
        *p = 0;
    for (p = buf; *p; p++) {
        if (!Q_isalnum(*p) && *p != '-' && *p != '_')
            *p = '_';
    }
    if (!cls.demo.playback)
        Q_strlcat(buf, "_mvd", size);   // not mixed up with a .dm2 of the same name
}

static const char *player_name(int num)
{
    const char *name;

    if (num < 1 || num > MAX_CLIENTS)
        return "?";
    name = cl.clientinfo[num - 1].name;
    return *name ? name : va("jugador %d", num);
}

/*
===============================================================================

SAVE / LOAD

===============================================================================
*/

static void keys_save(void)
{
    char name[MAX_QPATH], file[MAX_OSPATH];
    qhandle_t f;

    demo_basename(name, sizeof(name));
    Q_snprintf(file, sizeof(file), "campaths/%s.keys", name);

    if (FS_OpenFile(file, &f, FS_MODE_WRITE | FS_FLAG_TEXT) < 0) {
        Com_EPrintf("No se pudieron guardar los puntos de camara en %s\n", file);
        return;
    }

    FS_FPrintf(f, "VISORKEYS 1\n");
    for (int i = 0; i < num_keys; i++) {
        const keyframe_t *k = &keys[i];
        FS_FPrintf(f, "%d %d %.5f %d %d %.2f %.2f %.2f %.3f %.3f %.3f %.2f %g %d %d %d\n",
                   k->time, k->frame, k->pos, k->mode, k->target,
                   k->org[0], k->org[1], k->org[2],
                   k->ang[0], k->ang[1], k->ang[2], k->fov, k->speed,
                   k->follow, k->proj_ent, k->proj_first);
    }
    FS_CloseFile(f);
}

static void keys_load(void)
{
    char name[MAX_QPATH], file[MAX_OSPATH];
    const char *line;
    char *buf;
    keyframe_t k;
    int mode, follow;

    num_keys = 0;

    demo_basename(name, sizeof(name));
    Q_snprintf(file, sizeof(file), "campaths/%s.keys", name);

    if (FS_LoadFile(file, (void **)&buf) < 0)
        return;

    if (strncmp(buf, "VISORKEYS 1", 11)) {
        FS_FreeFile(buf);
        return;
    }

    for (line = strchr(buf, '\n'); line && num_keys < MAX_KEYS; line = strchr(line, '\n')) {
        char one[256];
        const char *end;

        line++;
        // one line at a time: files from before 0.6 have no speed at the end
        end = strchr(line, '\n');
        Q_strlcpy(one, line, min(end ? end - line + 1 : (int)sizeof(one), (int)sizeof(one)));
        memset(&k, 0, sizeof(k));
        k.speed = 1;
        follow = 0;
        if (sscanf(one, "%d %d %f %d %d %f %f %f %f %f %f %f %f %d %d %d",
                   &k.time, &k.frame, &k.pos, &mode, &k.target,
                   &k.org[0], &k.org[1], &k.org[2],
                   &k.ang[0], &k.ang[1], &k.ang[2], &k.fov, &k.speed,
                   &follow, &k.proj_ent, &k.proj_first) < 12)
            continue;
        k.follow = follow == FOLLOW_PLAYER && k.target ? FOLLOW_PLAYER :
                   follow == FOLLOW_PROJ ? FOLLOW_PROJ : FOLLOW_NONE;
        k.speed = Q_clipf(k.speed, MIN_SPEED, MAX_SPEED);
        if (num_keys && k.time <= keys[num_keys - 1].time)
            continue;   // keep them sorted
        k.mode = mode == SEG_LINE ? SEG_LINE : SEG_CURVE;
        k.fov = Q_clipf(k.fov, 10, 160);
        keys[num_keys++] = k;
    }

    FS_FreeFile(buf);

    if (num_keys)
        Com_Printf("Puntos de camara cargados (%d). V los activa o desactiva.\n", num_keys);
}

// switching demos: drop the old points and pick up this demo's saved ones
static void check_demo(void)
{
    if (!strcmp(loaded_for, CL_DemoName()))
        return;

    Q_strlcpy(loaded_for, CL_DemoName(), sizeof(loaded_for));
    keys_load();
}

/*
===============================================================================

INTERPOLATION

Position, angles and field of view follow a cubic Hermite curve between
points. The slope at each point is the time-weighted average of the slopes
of its two segments (a non-uniform Catmull-Rom), so the camera keeps its
speed through a point even when points are unevenly spaced. Endpoints and
"straight" segments use the straight-line slope.

===============================================================================
*/

#define NUM_CH  7       // org[3], ang[3], fov

// shortest signed difference between two angles, in -180..180
static float angle_delta(float to, float from)
{
    float d = fmodf(to - from, 360.0f);
    if (d > 180)
        d -= 360;
    else if (d < -180)
        d += 360;
    return d;
}

// values of point `i`, with angles unwrapped to be continuous with `ref`
static void key_values(int i, const float *ref, float *out)
{
    const keyframe_t *k = &keys[i];

    VectorCopy(k->org, out);
    for (int j = 0; j < 3; j++)
        out[3 + j] = ref ? ref[3 + j] + angle_delta(k->ang[j], ref[3 + j]) : k->ang[j];
    out[6] = k->fov;
}

static float hermite(float p0, float p1, float m0, float m1, float h, float s)
{
    float s2 = s * s, s3 = s2 * s;

    return (2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * h * m0 +
           (-2 * s3 + 3 * s2) * p1 + (s3 - s2) * h * m1;
}

static float smoothstep(float s)
{
    return s * s * (3 - 2 * s);
}

// where a point's player is now; keeps the last known spot when they are out
// of the demo for a moment (a .dm2 only has players near the recorder)
static bool target_position(int num, vec3_t out)
{
    static vec3_t   last[MAX_CLIENTS + 1];
    static bool     have[MAX_CLIENTS + 1];

    if (num < 1 || num > MAX_CLIENTS)
        return false;

    if (CL_PlayerPosition(num, out)) {
        out[2] += keycam_aim_height->value;
        VectorCopy(out, last[num]);
        have[num] = true;
        return true;
    }
    if (!have[num])
        return false;
    VectorCopy(last[num], out);
    return true;
}

static void look_angles(const vec3_t from, const vec3_t to, vec3_t ang)
{
    vec3_t dir;

    VectorSubtract(to, from, dir);
    vectoangles2(dir, ang);     // pitch comes as -(0..360), down positive
    if (ang[PITCH] < -180)
        ang[PITCH] += 360;
    ang[ROLL] = 0;
}

// camera between points a and a + 1 at time t
// where the camera is when the point follows something now
static bool follow_pose(const keyframe_t *k, vec3_t org, vec3_t ang)
{
    if (k->follow == FOLLOW_PLAYER)
        return CL_ChasePose(k->target, org, ang);
    if (k->follow == FOLLOW_PROJ) {
        int id = CL_ProjFind(k->proj_ent, k->proj_first);
        return id >= 0 && CL_ProjFollowPose(id, NULL, -1, org, ang);
    }
    return false;
}

// now: t is the moment shown (the followed things are where they are now)
static void sample_segment(int a, int t, vec3_t org, vec3_t ang, float *fov, bool now)
{
    const keyframe_t *ka = &keys[a], *kb = &keys[a + 1];
    float v[4][NUM_CH], m0[NUM_CH], m1[NUM_CH];
    float h = kb->time - ka->time;
    float s = Q_clipf((t - ka->time) / h, 0, 1);
    bool has_prev = a > 0, has_next = a + 2 < num_keys;
    bool line = ka->mode == SEG_LINE;
    float h0 = has_prev ? ka->time - keys[a - 1].time : 0;
    float h2 = has_next ? keys[a + 2].time - kb->time : 0;
    float out[NUM_CH];

    // v[0] = point before, v[1] = a, v[2] = a + 1, v[3] = point after
    key_values(a, NULL, v[1]);
    key_values(a + 1, v[1], v[2]);
    if (has_prev)
        key_values(a - 1, v[1], v[0]);
    if (has_next)
        key_values(a + 2, v[2], v[3]);

    for (int c = 0; c < NUM_CH; c++) {
        float chord = (v[2][c] - v[1][c]) / h;

        if (line || !has_prev || h0 <= 0) {
            m0[c] = chord;
        } else {
            float before = (v[1][c] - v[0][c]) / h0;
            m0[c] = (before * h + chord * h0) / (h0 + h);
        }
        if (line || !has_next || h2 <= 0) {
            m1[c] = chord;
        } else {
            float after = (v[3][c] - v[2][c]) / h2;
            m1[c] = (chord * h2 + after * h) / (h + h2);
        }
        out[c] = line ? v[1][c] + (v[2][c] - v[1][c]) * s
                      : hermite(v[1][c], v[2][c], m0[c], m1[c], h, s);
    }

    VectorCopy(out, org);
    VectorCopy(out + 3, ang);
    *fov = Q_clipf(out[6], 10, 160);

    // aiming at players: blend from the free view to looking at them
    if (ka->target || kb->target) {
        vec3_t pa, pb, aim, look;
        bool ha = ka->target && target_position(ka->target, pa);
        bool hb = kb->target && target_position(kb->target, pb);
        float w, f = smoothstep(s);

        if (ha && hb) {
            LerpVector(pa, pb, f, aim);     // panning from one player to another
            w = 1;
        } else if (ha) {
            VectorCopy(pa, aim);
            w = 1 - f;
        } else if (hb) {
            VectorCopy(pb, aim);
            w = f;
        } else {
            return;
        }

        look_angles(org, aim, look);
        for (int j = 0; j < 2; j++)
            ang[j] += w * angle_delta(look[j], ang[j]);
        ang[ROLL] *= 1 - w;
    }

    // following a player or a projectile from this point to the next: the
    // camera eases from the point's view to behind it and back at the end
    if (now && ka->follow) {
        vec3_t fo, fa;
        float ramp = min(400.0f, h / 3), w;

        if (follow_pose(ka, fo, fa)) {
            w = smoothstep(Q_clipf((t - ka->time) / ramp, 0, 1)) *
                smoothstep(Q_clipf((kb->time - t) / ramp, 0, 1));
            LerpVector(org, fo, w, org);
            for (int j = 0; j < 3; j++)
                ang[j] += w * angle_delta(fa[j], ang[j]);
        }
    }
}

/*
===============
CL_KeyCamView

Camera from the points for the current demo time. False when the points
are off or don't cover this moment (before the first or after the last).
===============
*/
bool CL_KeyCamView(vec3_t org, vec3_t ang, float *fov)
{
    int t, a;

    if (!watching_dm2())
        return false;

    check_demo();

    if (!playing || num_keys < 2)
        return false;

    t = demo_time();
    if (t < keys[0].time || t > keys[num_keys - 1].time)
        return false;

    for (a = 0; a < num_keys - 2 && keys[a + 1].time <= t; a++)
        ;
    sample_segment(a, t, org, ang, fov, true);
    return true;
}

// the view saved in point i, looking at its player if it has one
static void point_view(int i, vec3_t org, vec3_t ang, float *fov)
{
    const keyframe_t *k = &keys[i];
    vec3_t aim;

    VectorCopy(k->org, org);
    VectorCopy(k->ang, ang);
    *fov = k->fov;
    if (k->target && target_position(k->target, aim))
        look_angles(org, aim, ang);
}

/*
===============
CL_KeyCamPendingView

After jumping to a point (1 / 2, a click on the timeline): the view saved
in it, once. The camera is put there so the point can be changed from its
own view.
===============
*/
bool CL_KeyCamPendingView(vec3_t org, vec3_t ang, float *fov)
{
    int i = pending_view;

    pending_view = -1;
    if (i < 0 || i >= num_keys || !watching_dm2())
        return false;
    point_view(i, org, ang, fov);
    return true;
}

/*
===============
CL_KeyCamAddPath

"Ver recorrido": the path through the points as a line in the map, and at
each point a short line where it looks. Not in the videos.
===============
*/
#define PATH_COLOR      MakeColor(  0, 220, 230, 255)
#define POINT_COLOR     MakeColor(255, 255, 255, 255)
#define POINT_HERE      MakeColor(255, 200,  60, 255)

void CL_KeyCamAddPath(void)
{
    vec3_t org, ang, prev, fwd, tip;
    float fov;
    int here;

    if (!keycam_show->integer || CL_CaptureActive() || !watching_dm2())
        return;
    check_demo();
    here = key_here();

    for (int a = 0; a + 1 < num_keys; a++) {
        int t0 = keys[a].time, t1 = keys[a + 1].time;
        int steps = Q_clip((t1 - t0) / 50, 1, 60);

        VectorCopy(keys[a].org, prev);
        for (int s = 1; s <= steps; s++) {
            sample_segment(a, t0 + (t1 - t0) * s / steps, org, ang, &fov, false);
            CL_AddBeam(prev, org, PATH_COLOR, 0.8f, 2);
            VectorCopy(org, prev);
        }
    }

    for (int i = 0; i < num_keys; i++) {
        AngleVectors(keys[i].ang, fwd, NULL, NULL);
        VectorMA(keys[i].org, 40, fwd, tip);
        CL_AddBeam(keys[i].org, tip, i == here ? POINT_HERE : POINT_COLOR, 1, 4);
    }
}

/*
===============================================================================

TIMELINE

===============================================================================
*/

// point at the current moment, -1 if none
static int key_here(void)
{
    int t = demo_time(), best = -1, best_d = SAME_KEY_MS + 1;

    for (int i = 0; i < num_keys; i++) {
        int d = abs(keys[i].time - t);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

bool CL_KeyCamPlaying(void)
{
    return watching_dm2() && playing && num_keys >= 2;
}

// demo positions of the points; *current = index of the point here (-1)
int CL_KeyCamMarks(float *pos, int max, int *current)
{
    int n = 0;

    *current = -1;
    if (!watching_dm2())
        return 0;

    check_demo();
    for (int i = 0; i < num_keys && n < max; i++)
        pos[n++] = keys[i].pos;
    *current = key_here();
    return n;
}

// for the timeline text: "PUNTO 2 (TOTAL 5), mira a nick"
const char *CL_KeyCamStatus(void)
{
    int i;

    if (!watching_dm2() || (i = key_here()) < 0)
        return NULL;
    // short: the speed shows as TRAMO next to it while it plays
    return va("PUNTO %d (TOTAL %d)%s%s%s", i + 1, num_keys,
              keys[i].mode == SEG_LINE ? ", recta" : "",
              keys[i].speed != 1 && i < num_keys - 1 && CL_KeyCamSpeed() == 1 ? va(", x%g", keys[i].speed) : "",
              keys[i].follow == FOLLOW_PROJ ? ", sigue proyectil" :
              keys[i].follow ? va(", sigue a %s", player_name(keys[i].target)) :
              keys[i].target ? va(", mira a %s", player_name(keys[i].target)) : "");
}

/*
===============================================================================

COMMANDS

===============================================================================
*/

static bool can_edit(void)
{
    if (!watching_dm2()) {
        Com_Printf("Los puntos de camara funcionan al ver un demo.\n");
        return false;
    }
    if (CL_CaptureActive()) {
        Com_Printf("No se pueden cambiar los puntos mientras se graba un clip.\n");
        return false;
    }
    check_demo();
    return true;
}

static void announce(const char *msg)
{
    SCR_CenterPrint(msg, false);
}

// keycam_add: point at this moment with the current view (P)
static void KeyCam_Add_f(void)
{
    keyframe_t k = { 0 };
    int i;

    if (!can_edit())
        return;

    k.time = demo_time();
    k.frame = CL_DemoSeekFrame();
    k.pos = CL_DemoProgress();
    VectorCopy(cl.refdef.vieworg, k.org);
    VectorCopy(cl.refdef.viewangles, k.ang);
    if (k.ang[PITCH] > 180)
        k.ang[PITCH] -= 360;
    k.fov = Q_clipf(cl.fov_x, 10, 160);
    k.speed = 1;

    i = key_here();
    if (i >= 0) {
        // replace it, keeping how it moves on and who it looks at
        k.mode = keys[i].mode;
        k.target = keys[i].target;
        k.speed = keys[i].speed;
        k.follow = keys[i].follow;
        k.proj_ent = keys[i].proj_ent;
        k.proj_first = keys[i].proj_first;
        keys[i] = k;
        keys_save();
        announce(va("Punto %d actualizado (total %d)", i + 1, num_keys));
        return;
    }

    if (num_keys == MAX_KEYS) {
        announce(va("Maximo %d puntos de camara", MAX_KEYS));
        return;
    }

    // following a projectile with the camera: the new point follows it too
    if (CL_FreecamProj() >= 0) {
        k.follow = FOLLOW_PROJ;
        CL_ProjIdentity(CL_FreecamProj(), &k.proj_ent, &k.proj_first);
    }

    for (i = num_keys; i > 0 && keys[i - 1].time > k.time; i--)
        keys[i] = keys[i - 1];
    keys[i] = k;
    num_keys++;
    playing = true;
    keys_save();

    if (num_keys == 1)
        announce("Punto 1 puesto. Avanza el demo, mueve la camara y pon otro con P (sin limite)");
    else
        announce(va("Punto %d puesto (total %d, puedes poner mas)", i + 1, num_keys));
}

// keycam_delete: remove the point at this moment (U)
static void KeyCam_Delete_f(void)
{
    int i;

    if (!can_edit())
        return;

    i = key_here();
    if (i < 0) {
        announce(num_keys ? "No hay punto aqui (1 / 2 para ir a uno)" : "No hay puntos de camara");
        return;
    }

    memmove(&keys[i], &keys[i + 1], (num_keys - i - 1) * sizeof(keys[0]));
    num_keys--;
    keys_save();
    announce(va("Punto borrado, quedan %d", num_keys));
}

// keycam_mode: curve or straight line from the point here to the next (M)
static void KeyCam_Mode_f(void)
{
    int i;

    if (!can_edit())
        return;

    i = key_here();
    if (i < 0) {
        announce("No hay punto aqui (1 / 2 para ir a uno)");
        return;
    }

    keys[i].mode = keys[i].mode == SEG_LINE ? SEG_CURVE : SEG_LINE;
    keys_save();
    announce(keys[i].mode == SEG_LINE ? va("Punto %d: recta hasta el siguiente", i + 1)
                                      : va("Punto %d: curva suave", i + 1));
}

// player closest to the middle of the screen, 0 = none
static int player_at_crosshair(void)
{
    vec3_t forward, dir, pos;
    float best = cosf(DEG2RAD(30));
    int found = 0;

    AngleVectors(cl.refdef.viewangles, forward, NULL, NULL);

    for (int num = 1; num <= cl.maxclients && num <= MAX_CLIENTS; num++) {
        float d;

        if (!CL_PlayerPosition(num, pos))
            continue;
        VectorSubtract(pos, cl.refdef.vieworg, dir);
        if (VectorNormalize(dir) < 48)
            continue;   // the camera is on them (the recorder's own view)
        d = DotProduct(dir, forward);
        if (d > best) {
            best = d;
            found = num;
        }
    }
    return found;
}

// keycam_target [num]: the point here looks at the player in the middle of
// the screen, or back to its own view (L). With a number: that player
// (entity number, 0 = own view).
static void KeyCam_Target_f(void)
{
    int i, num;

    if (!can_edit())
        return;

    if (Cmd_Argc() > 1) {
        num = Q_atoi(Cmd_Argv(1));
        if (num < 0 || num > MAX_CLIENTS) {
            Com_Printf("Uso: keycam_target [numero de jugador, 0 = mirada libre]\n");
            return;
        }
        if ((i = key_here()) < 0) {
            KeyCam_Add_f();
            if ((i = key_here()) < 0)
                return;
        }
        keys[i].target = num;
        keys[i].follow = FOLLOW_NONE;
        keys_save();
        announce(num ? va("Punto %d: mira a %s", i + 1, player_name(num))
                     : va("Punto %d: mirada libre", i + 1));
        return;
    }

    // nothing -> looks at a player -> follows them -> nothing
    i = key_here();
    if (i < 0) {
        // no point yet: put one here, then aim it
        KeyCam_Add_f();
        if ((i = key_here()) < 0)
            return;
    } else if (keys[i].target && !keys[i].follow) {
        keys[i].follow = FOLLOW_PLAYER;
        keys_save();
        announce(va("Punto %d: sigue a %s hasta el punto siguiente", i + 1, player_name(keys[i].target)));
        return;
    } else if (keys[i].target || keys[i].follow) {
        keys[i].target = 0;
        keys[i].follow = FOLLOW_NONE;
        keys_save();
        announce(va("Punto %d: mirada libre", i + 1));
        return;
    }

    num = player_at_crosshair();
    if (!num) {
        announce("Apunta a un jugador (al centro de la pantalla) y presiona L");
        return;
    }

    keys[i].target = num;
    keys_save();
    announce(va("Punto %d: mira a %s", i + 1, player_name(num)));
}

// projectile a camera point follows right now, -1 none (its trail stands out)
int CL_KeyCamProjNow(void)
{
    int t, a;

    if (!CL_FreecamKeysDriving() || num_keys < 2)
        return -1;
    t = demo_time();
    for (a = 0; a < num_keys - 2 && keys[a + 1].time <= t; a++)
        ;
    if (keys[a].follow != FOLLOW_PROJ)
        return -1;
    return CL_ProjFind(keys[a].proj_ent, keys[a].proj_first);
}

// keycam_follow_proj: from the point here to the next the camera follows
// the projectile being followed now, or the one in the middle of the screen
static void KeyCam_FollowProj_f(void)
{
    vec3_t fwd;
    int i, id;

    if (!can_edit())
        return;

    i = key_here();
    if (i >= 0 && keys[i].follow == FOLLOW_PROJ) {
        keys[i].follow = FOLLOW_NONE;
        keys_save();
        announce(va("Punto %d: ya no sigue el proyectil", i + 1));
        return;
    }

    id = CL_FreecamProj();
    if (id < 0) {
        AngleVectors(cl.refdef.viewangles, fwd, NULL, NULL);
        id = CL_ProjAtRay(fwd);
    }
    if (id < 0) {
        announce("Apunta a una granada, cohete o cuchillo en vuelo (o siguelo con 5) y vuelve a intentar");
        return;
    }

    if (i < 0) {
        KeyCam_Add_f();
        if ((i = key_here()) < 0)
            return;
    }
    keys[i].target = 0;
    keys[i].follow = FOLLOW_PROJ;
    CL_ProjIdentity(id, &keys[i].proj_ent, &keys[i].proj_first);
    keys_save();
    announce(va("Punto %d: sigue la %s hasta el punto siguiente", i + 1, CL_ProjName(id)));
}

// seek to point i and stop on it, so it can be changed
static void go_to(int i)
{
    const keyframe_t *k = &keys[i];

    Cmd_ExecuteString(&cmd_buffer, va("seek %d.%d", k->frame / 10, k->frame % 10));
    if (!CL_DemoPaused())
        Cmd_ExecuteString(&cmd_buffer, "pause");
    pending_view = i;
    announce(va("Punto %d (total %d)", i + 1, num_keys));
}

static void jump_to(int dir)
{
    int t = demo_time(), i;
    const keyframe_t *k = NULL;

    if (!can_edit())
        return;
    if (!num_keys) {
        announce("No hay puntos de camara (P para poner uno)");
        return;
    }

    if (dir > 0) {
        for (i = 0; i < num_keys; i++)
            if (keys[i].time > t + SAME_KEY_MS) {
                k = &keys[i];
                break;
            }
    } else {
        for (i = num_keys - 1; i >= 0; i--)
            if (keys[i].time < t - SAME_KEY_MS) {
                k = &keys[i];
                break;
            }
    }

    if (!k) {
        announce(dir > 0 ? "No hay mas puntos" : "No hay puntos antes");
        return;
    }

    go_to(i);
}

// keycam_goto <n>: jump to point n (1 = first), e.g. clicked on the timeline
static void KeyCam_Goto_f(void)
{
    int i;

    if (!can_edit())
        return;
    i = Q_atoi(Cmd_Argv(1)) - 1;
    if (i < 0 || i >= num_keys) {
        Com_Printf("Uso: keycam_goto <1 a %d>\n", num_keys);
        return;
    }
    go_to(i);
}

static void KeyCam_Next_f(void)
{
    jump_to(1);
}

static void KeyCam_Prev_f(void)
{
    jump_to(-1);
}

static void KeyCam_Clear_f(void)
{
    if (!can_edit())
        return;
    num_keys = 0;
    keys_save();
    announce("Puntos de camara borrados");
}

// camera_clear: all the points and the recorded path of this demo
static void Camera_Clear_f(void)
{
    if (!can_edit())
        return;
    num_keys = 0;
    keys_save();
    CL_CamPathClear();
    announce("Camara borrada: puntos y recorrido");
}

int CL_KeyCamCount(void)
{
    if (!watching_dm2())
        return 0;
    check_demo();
    return num_keys;
}

// point at this moment: 0 none, 1 free view, 2 looks at a player; *line
// = goes on in a straight line (for the editor buttons)
int CL_KeyCamHere(bool *line)
{
    int i;

    *line = false;
    if (!watching_dm2() || (i = key_here()) < 0)
        return 0;
    *line = keys[i].mode == SEG_LINE;
    return keys[i].target ? 2 : 1;
}

// point i for the editor's list; false if there is no such point
bool CL_KeyCamGet(int i, keyinfo_t *out)
{
    const keyframe_t *k;

    if (!watching_dm2())
        return false;
    check_demo();
    if (i < 0 || i >= num_keys)
        return false;
    k = &keys[i];
    out->frame = k->frame;
    out->line = k->mode == SEG_LINE;
    out->target = k->target;
    out->fov = k->fov;
    out->target_name = k->target ? player_name(k->target) : NULL;
    out->speed = k->speed;
    out->follow = k->follow;
    if (k->follow == FOLLOW_PROJ) {
        int id = CL_ProjFind(k->proj_ent, k->proj_first);
        out->target_name = id >= 0 ? CL_ProjName(id) : "proyectil";
    }
    return true;
}

// index of the point at this moment, -1 if none
int CL_KeyCamIndex(void)
{
    if (!watching_dm2())
        return -1;
    check_demo();
    return key_here();
}

// keycam_fov <change>: zoom of the point here (- / + in the editor)
static void KeyCam_Fov_f(void)
{
    int i;

    if (!can_edit())
        return;
    i = key_here();
    if (i < 0) {
        announce("No hay punto aqui (1 / 2 para ir a uno)");
        return;
    }
    keys[i].fov = Q_clipf(keys[i].fov + Q_atof(Cmd_Argv(1)), 10, 160);
    keys_save();
    CL_FreecamSetFov(keys[i].fov);
    announce(va("Punto %d: zoom %g", i + 1, keys[i].fov));
}

/*
===============
CL_KeyCamSpeed

Slow motion of the camera points: each point has a speed for the stretch
to the next one. The camera crosses that stretch in video time, so at x0.2
it moves at its usual pace while the game runs five times slower (the
"Matrix" effect). Around each point the speed changes smoothly. 1 when
the points are off, don't cover this moment or the demo is paused.
===============
*/
#define SPEED_RAMP_MS   200

static float log_lerp(float a, float b, float f)
{
    return expf(logf(a) + (logf(b) - logf(a)) * f);
}

float CL_KeyCamSpeed(void)
{
    int t, a, len, r;
    bool first, last;
    float s;

    if (!CL_KeyCamPlaying() || CL_DemoPaused())
        return 1;

    t = demo_time();
    if (t < keys[0].time || t >= keys[num_keys - 1].time)
        return 1;

    for (a = 0; a < num_keys - 2 && keys[a + 1].time <= t; a++)
        ;
    s = keys[a].speed;
    len = keys[a + 1].time - keys[a].time;
    first = a == 0;
    last = a + 2 >= num_keys;

    // the change is spread over [point - r, point + r]; before the first
    // and after the last point it is normal speed, eased in [point, point + r]
    r = min(SPEED_RAMP_MS, len / 2);
    if (!first)
        r = min(r, (keys[a].time - keys[a - 1].time) / 2);
    if (r > 0 && t < keys[a].time + r) {
        float f = (float)(t - keys[a].time) / r;
        float before = first ? 1 : keys[a - 1].speed;
        s = log_lerp(before, keys[a].speed, smoothstep(first ? f : 0.5f + 0.5f * f));
    }

    r = min(SPEED_RAMP_MS, len / 2);
    if (!last)
        r = min(r, (keys[a + 2].time - keys[a + 1].time) / 2);
    if (r > 0 && t > keys[a + 1].time - r) {
        float f = (float)(t - (keys[a + 1].time - r)) / r;
        float after = last ? 1 : keys[a + 1].speed;
        s = log_lerp(keys[a].speed, after, smoothstep(last ? f : 0.5f * f));
    }
    return Q_clipf(s, MIN_SPEED, MAX_SPEED);
}

// keycam_speed up | down | reset | <value>: slow motion from the point
// here to the next (keys 3 and 4)
static const float key_speeds[] = { 0.05f, 0.1f, 0.2f, 0.25f, 0.33f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4 };

static void KeyCam_Speed_f(void)
{
    const char *arg = Cmd_Argv(1);
    int i, n = q_countof(key_speeds);
    float cur, v;

    if (!can_edit())
        return;
    i = key_here();
    if (i < 0) {
        announce("No hay punto aqui (1 / 2 para ir a uno, P para poner uno)");
        return;
    }
    if (i == num_keys - 1) {
        announce("La velocidad vale del punto al siguiente: este es el ultimo");
        return;
    }

    cur = keys[i].speed;
    if (!Q_stricmp(arg, "up")) {
        for (v = key_speeds[n - 1], i = 0; i < n; i++)
            if (key_speeds[i] > cur + 0.001f) {
                v = key_speeds[i];
                break;
            }
    } else if (!Q_stricmp(arg, "down")) {
        for (v = key_speeds[0], i = n - 1; i >= 0; i--)
            if (key_speeds[i] < cur - 0.001f) {
                v = key_speeds[i];
                break;
            }
    } else if (!Q_stricmp(arg, "reset")) {
        v = 1;
    } else if (*arg) {
        v = Q_atof(arg);
    } else {
        Com_Printf("Uso: %s up | down | reset | <0.05 a 4>\n", Cmd_Argv(0));
        return;
    }

    i = key_here();
    keys[i].speed = Q_clipf(v, MIN_SPEED, MAX_SPEED);
    keys_save();
    announce(keys[i].speed == 1 ? va("Punto %d: velocidad normal hasta el siguiente", i + 1)
                                : va("Punto %d: x%g hasta el siguiente", i + 1, keys[i].speed));
}

// on / off; campath_play (V) switches both the recorded path and the points
void CL_KeyCamSetPlaying(bool on)
{
    playing = on;
}

bool CL_KeyCamHaveKeys(void)
{
    if (!watching_dm2())
        return false;
    check_demo();
    return num_keys >= 2;
}

static const cmdreg_t c_keycam[] = {
    { "keycam_add", KeyCam_Add_f },
    { "keycam_delete", KeyCam_Delete_f },
    { "keycam_mode", KeyCam_Mode_f },
    { "keycam_target", KeyCam_Target_f },
    { "keycam_next", KeyCam_Next_f },
    { "keycam_prev", KeyCam_Prev_f },
    { "keycam_goto", KeyCam_Goto_f },
    { "keycam_fov", KeyCam_Fov_f },
    { "keycam_speed", KeyCam_Speed_f },
    { "keycam_follow_proj", KeyCam_FollowProj_f },
    { "keycam_clear", KeyCam_Clear_f },
    { "camera_clear", Camera_Clear_f },
    { NULL }
};

void CL_InitKeyCam(void)
{
    keycam_aim_height = Cvar_Get("keycam_aim_height", "12", CVAR_ARCHIVE);
    keycam_show = Cvar_Get("keycam_show", "0", CVAR_ARCHIVE);
    Cmd_Register(c_keycam);
}
