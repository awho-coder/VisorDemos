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

// proj.c -- projectiles in DM2 demos: rockets, grenades, thrown knives
//
// When a demo is opened it is read ahead once (demo_index). Every frame of
// that pass goes through here, and the flight of each projectile is kept:
// one position per server frame, from the throw to the explosion. With the
// whole flight known the viewer can:
//   - follow a projectile with the camera, behind it (freecam.c),
//   - mark the throws on the timeline and jump to them (proj_next / prev),
//   - draw laser trails behind them, also after seeking or while paused.
//
// A projectile is an entity whose model is in proj_models and that moves
// fast at some point (weapons dropped by dead players use the same knife
// model, but fall slowly). A .dm2 only has what was near the recorder: a
// projectile that leaves that area disappears from the demo.

#include "client.h"

typedef enum {
    PT_ROCKET,
    PT_GRENADE,
    PT_KNIFE,
    PT_BOMB,        // D-Day air strike: rocket model falling straight down
    PT_NONE = -1,
    PT_UNKNOWN = -2
} projtype_t;

typedef struct {
    int         number;     // entity number
    projtype_t  type;
    int         first, last;    // server frames it is in the demo
    int         demo_frame;     // cls.demo.frames_read at the throw, to seek
    float       pos;            // demo file position at the throw (timeline)
    vec3_t      *pts;           // one position per frame, first to last
    int         alloc;
    float       top_speed;      // units per frame
    bool        open;           // still flying while indexing
} track_t;

#define FLY_SPEED   30      // units per frame (300 / s) = thrown, not dropped
#define MAX_GAP     5       // frames out of the demo before a flight ends
#define MAX_JUMP    400     // units between frames: another projectile

static struct {
    track_t     *tracks;
    int         num, alloc;
    int         total_pts;
    char        demo[MAX_QPATH];
    bool        indexing;
    int         last_frame;     // last frame indexed
    signed char model_type[MAX_MODELS];
} pj;

static cvar_t   *proj_models;
static cvar_t   *visor_trails;
static cvar_t   *clip_trails;
static cvar_t   *trail_time;
static cvar_t   *trail_width;
static cvar_t   *trail_color;

static const char *const type_names[] = { "cohete", "granada", "cuchillo", "bomba" };

static bool have_tracks(void)
{
    return CL_DemoView() && pj.num && !strcmp(pj.demo, CL_DemoName());
}

/*
===============================================================================

INDEX

===============================================================================
*/

static void clear_types(void)
{
    memset(pj.model_type, PT_UNKNOWN, sizeof(pj.model_type));
}

static projtype_t model_type(int index, const char *name)
{
    char words[MAX_STRING_CHARS], *w, *s;

    if (index <= 0 || index >= MAX_MODELS)
        return PT_NONE;
    if (pj.model_type[index] != PT_UNKNOWN)
        return pj.model_type[index];

    pj.model_type[index] = PT_NONE;
    if (!name || !*name || *name == '*')
        return PT_NONE;

    // the words of proj_models found in the model's path
    Q_strlcpy(words, proj_models->string, sizeof(words));
    for (w = words; *w; w = s) {
        while (*w == ' ')
            w++;
        s = w;
        while (*s && *s != ' ')
            s++;
        if (*s)
            *s++ = 0;
        if (!*w || !Q_stristr(name, w))
            continue;
        if (Q_stristr(name, "rocket"))
            pj.model_type[index] = PT_ROCKET;
        else if (Q_stristr(name, "knife"))
            pj.model_type[index] = PT_KNIFE;
        else
            pj.model_type[index] = PT_GRENADE;
        break;
    }
    return pj.model_type[index];
}

// one more position at the end of a flight (several fly at once, so each
// one keeps its own list)
static void add_point(track_t *t, int count, const vec3_t p)
{
    if (count == t->alloc) {
        t->alloc = max(t->alloc * 2, 64);
        t->pts = Z_Realloc(t->pts, t->alloc * sizeof(t->pts[0]));
    }
    VectorCopy(p, t->pts[count]);
    pj.total_pts++;
}

static track_t *new_track(void)
{
    if (pj.num == pj.alloc) {
        pj.alloc = max(pj.alloc * 2, 256);
        pj.tracks = Z_Realloc(pj.tracks, pj.alloc * sizeof(pj.tracks[0]));
    }
    return memset(&pj.tracks[pj.num++], 0, sizeof(track_t));
}

static void free_all(void)
{
    for (int i = 0; i < pj.num; i++)
        Z_Freep(&pj.tracks[i].pts);
    Z_Freep(&pj.tracks);
    pj.num = pj.alloc = pj.total_pts = 0;
    pj.demo[0] = 0;
}

// the entities of the frame being indexed
static struct {
    int         number;
    projtype_t  type;
    vec3_t      origin;
} seen[MAX_EDICTS];
static int num_seen;

void CL_ProjIndexBegin(void)
{
    free_all();
    clear_types();
    Q_strlcpy(pj.demo, CL_DemoName(), sizeof(pj.demo));
    pj.indexing = true;
    pj.last_frame = INT_MIN;
    num_seen = 0;
}

// an entity of the frame being read ahead (called before CL_ProjIndexFrame)
void CL_ProjIndexEntity(int number, int modelindex, const char *model, const vec3_t origin)
{
    projtype_t type;

    if (!pj.indexing || num_seen == MAX_EDICTS)
        return;
    type = model_type(modelindex, model);
    if (type == PT_NONE)
        return;
    seen[num_seen].number = number;
    seen[num_seen].type = type;
    VectorCopy(origin, seen[num_seen].origin);
    num_seen++;
}

// a flight that ended: keep it only if it really flew
static void close_track(track_t *t)
{
    t->open = false;
    if (t->top_speed >= FLY_SPEED) {
        // a "rocket" that only falls is a bomb of an air strike
        if (t->type == PT_ROCKET && t->last > t->first) {
            const float *a = t->pts[0], *b = t->pts[t->last - t->first];
            float drop = a[2] - b[2], side = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
            if (drop > 64 && drop > side * 3)
                t->type = PT_BOMB;
        }
        return;
    }

    // dropped weapon: forget it (CL_ProjIndexEnd drops the empty ones)
    Z_Freep(&t->pts);
    t->alloc = 0;
    t->last = t->first - 1;
}

static void extend(track_t *t, int frame, const vec3_t p)
{
    int count = t->last - t->first + 1, gap = frame - t->last;
    vec3_t prev, mid;
    float speed;

    VectorCopy(t->pts[count - 1], prev);
    speed = Distance(prev, p) / gap;

    // fill frames it was out of the demo with a straight line
    for (int i = 1; i < gap; i++) {
        LerpVector(prev, p, (float)i / gap, mid);
        add_point(t, count++, mid);
    }
    add_point(t, count, p);
    t->last = frame;
    t->top_speed = max(t->top_speed, speed);
}

/*
===============
CL_ProjIndexFrame

The entities of a frame read ahead were given: continue the flights or
start new ones. frame: demo frame (the clock of CL_DemoFrameNow);
seek_frame: what seek takes to come back; pos: position in the file.
===============
*/
void CL_ProjIndexFrame(int frame, int seek_frame, float pos)
{
    // a MVD message without a new frame repeats the last one: once is enough
    if (!pj.indexing || frame <= pj.last_frame) {
        num_seen = 0;
        return;
    }
    pj.last_frame = frame;

    for (int i = 0; i < num_seen; i++) {
        int number = seen[i].number;
        projtype_t type = seen[i].type;
        const float *origin = seen[i].origin;
        track_t *t = NULL;

        // continue its flight, or a new one
        for (int j = pj.num - 1; j >= 0; j--) {
            track_t *o = &pj.tracks[j];
            if (o->open && o->number == number) {
                const float *last = o->pts[o->last - o->first];
                if (o->type == type && o->last < frame &&
                    Distance(last, origin) < MAX_JUMP * (frame - o->last))
                    t = o;
                else
                    close_track(o);
                break;
            }
        }

        if (t) {
            extend(t, frame, origin);
            continue;
        }

        if (pj.total_pts > 4000000)
            continue;   // way too many, something is wrong
        t = new_track();
        t->number = number;
        t->type = type;
        t->first = t->last = frame;
        t->demo_frame = seek_frame;
        t->pos = pos;
        t->open = true;
        add_point(t, 0, origin);
    }
    num_seen = 0;

    // flights whose projectile is gone
    for (int j = 0; j < pj.num; j++) {
        track_t *t = &pj.tracks[j];
        if (t->open && frame - t->last > MAX_GAP)
            close_track(t);
    }
}

// a DM2 frame parsed while indexing (seek_demo reading ahead)
void CL_ProjIndexDm2Frame(void)
{
    float pos = cls.demo.file_progress;

    if (!pj.indexing || !cl.frame.valid)
        return;

    for (int i = 0; i < cl.frame.numEntities; i++) {
        const centity_state_t *st = &cl.entityStates[(cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK];
        int model = st->modelindex;

        if (st->number <= cl.maxclients || model <= 0 || model >= MAX_MODELS)
            continue;
        CL_ProjIndexEntity(st->number, model, cl.configstrings[cl.csr.models + model], st->origin);
    }

    if (cls.demo.file_size) {
        int64_t at = FS_Tell(cls.demo.playback) - cls.demo.file_offset;
        pos = Q_clipf((float)at / cls.demo.file_size, 0, 1);
    }
    CL_ProjIndexFrame(cl.frame.number, cls.demo.frames_read, pos);
}

void CL_ProjIndexEnd(void)
{
    int kept = 0;

    for (int j = pj.num - 1; j >= 0; j--)
        if (pj.tracks[j].open)
            close_track(&pj.tracks[j]);

    // drop the empty ones left in the middle
    for (int j = 0; j < pj.num; j++)
        if (pj.tracks[j].last >= pj.tracks[j].first)
            pj.tracks[kept++] = pj.tracks[j];
    pj.num = kept;
    pj.indexing = false;

    Com_DPrintf("Proyectiles en el demo: %d\n", pj.num);
}

/*
===============================================================================

QUERIES

Times are in server frames, with a fraction: the moment drawn now is
between the previous frame and this one.

===============================================================================
*/

float CL_ProjNow(void)
{
    return CL_DemoFrameNow();
}

static const track_t *get(int id)
{
    if (!have_tracks() || id < 0 || id >= pj.num)
        return NULL;
    return &pj.tracks[id];
}

static const float *point(const track_t *t, int frame)
{
    frame = Q_clip(frame, t->first, t->last);
    return t->pts[frame - t->first];
}

// position at time T, clamped to the flight
static void track_pos(const track_t *t, float T, vec3_t out)
{
    int i;

    T = Q_clipf(T, t->first, t->last);
    i = (int)floorf(T);
    LerpVector(point(t, i), point(t, i + 1), T - i, out);
}

static bool in_flight(const track_t *t, float T)
{
    return T >= t->first && T <= t->last;
}

// projectile id flying now, or -1; id = any id, for the follow camera
bool CL_ProjPos(int id, vec3_t out)
{
    const track_t *t = get(id);

    if (!t)
        return false;
    track_pos(t, CL_ProjNow(), out);
    return in_flight(t, CL_ProjNow());
}

// 0 not thrown yet, 1 flying, 2 exploded / gone (and for how many frames)
int CL_ProjState(int id, float *since_end)
{
    const track_t *t = get(id);
    float T = CL_ProjNow();

    *since_end = 0;
    if (!t)
        return 2;
    if (T < t->first)
        return 0;
    if (T <= t->last)
        return 1;
    *since_end = T - t->last;
    return 2;
}

const char *CL_ProjName(int id)
{
    const track_t *t = get(id);
    return t ? type_names[t->type] : "proyectil";
}

// flight direction around time T (keeps the last one while it lies still)
static bool track_dir(const track_t *t, float T, vec3_t dir)
{
    vec3_t a, b;

    T = Q_clipf(T, t->first, t->last);
    for (float back = 1; back < 30; back += 2) {
        track_pos(t, T - back, a);
        track_pos(t, T + 1, b);
        VectorSubtract(b, a, dir);
        if (VectorNormalize(dir) > 4)
            return true;
    }
    return false;
}

/*
===============
CL_ProjFollowPose

Camera behind projectile `id` now: `off` is the turn from straight behind
(pitch: positive = from above, looking down at it). The flight direction
is smoothed by looking at a whole frame around now.
===============
*/
bool CL_ProjFollowPose(int id, const vec2_t off, float dist, vec3_t org, vec3_t ang)
{
    vec2_t def = { Cvar_VariableValue("follow_pitch"), 0 };
    static const vec3_t mins = { -4, -4, -4 };
    static const vec3_t maxs = {  4,  4,  4 };
    const track_t *t = get(id);
    vec3_t pos, dir, fwd, wanted;
    trace_t tr;
    float T = CL_ProjNow();

    if (!t)
        return false;
    if (!off)
        off = def;
    if (dist < 0)
        dist = Cvar_VariableValue("follow_dist");
    dist = Q_clipf(dist, 16, 1000);

    track_pos(t, T, pos);
    if (!track_dir(t, T, dir))
        VectorSet(dir, 1, 0, 0);

    ang[YAW] = RAD2DEG(atan2f(dir[1], dir[0])) + off[1];
    ang[PITCH] = -RAD2DEG(atan2f(dir[2], sqrtf(dir[0] * dir[0] + dir[1] * dir[1])));
    ang[PITCH] = Q_clipf(ang[PITCH] * 0.5f + off[0], -80, 85);   // half the dive, plus the angle chosen
    ang[ROLL] = 0;

    AngleVectors(ang, fwd, NULL, NULL);
    VectorMA(pos, -dist, fwd, wanted);
    CL_Trace(&tr, pos, wanted, mins, maxs, CONTENTS_SOLID);
    VectorCopy(tr.endpos, org);
    return true;
}

// the projectile id that is entity `number` and was thrown at server frame
// `first` (camera points save them like this); -1 if not found
int CL_ProjFind(int number, int first)
{
    if (!have_tracks())
        return -1;
    for (int i = 0; i < pj.num; i++)
        if (pj.tracks[i].number == number && abs(pj.tracks[i].first - first) <= 2)
            return i;
    return -1;
}

void CL_ProjIdentity(int id, int *number, int *first)
{
    const track_t *t = get(id);

    *number = t ? t->number : 0;
    *first = t ? t->first : 0;
}

// flying projectile nearest to the line of sight from the camera through
// `dir`; -1 if none is close to it
int CL_ProjAtRay(const vec3_t dir)
{
    float T = CL_ProjNow(), best = 1e9f;
    int found = -1;

    if (!have_tracks())
        return -1;

    for (int i = 0; i < pj.num; i++) {
        const track_t *t = &pj.tracks[i];
        vec3_t pos, d, closest;
        float along, off;

        if (!in_flight(t, T))
            continue;
        track_pos(t, T, pos);
        VectorSubtract(pos, cl.refdef.vieworg, d);
        along = DotProduct(d, dir);
        if (along < 16)
            continue;
        VectorScale(dir, along, closest);
        off = Distance(d, closest);
        if (off < max(32, along * 0.06f) && off / along < best) {
            best = off / along;
            found = i;
        }
    }
    return found;
}

// timeline marks: where the throws are
int CL_ProjMarks(float *pos, int max)
{
    int n = 0;

    if (!have_tracks())
        return 0;
    for (int i = 0; i < pj.num && n < max; i++)
        pos[n++] = pj.tracks[i].pos;
    return n;
}

/*
===============================================================================

TRAILS

A glowing line behind each projectile, fading towards the tail. Built from
the kept flight, so it is there after a seek, while paused and in slow
motion. Between the positions of two frames the line follows a smooth
curve (Catmull-Rom), the head sits on the projectile drawn.

===============================================================================
*/

int CL_TrailsMode(bool clip)
{
    return Q_clip((clip ? clip_trails : visor_trails)->integer, 0, 2);
}

bool CL_TrailsOn(bool clip)
{
    return CL_TrailsMode(clip) > 0;
}

static uint32_t trail_rgba(projtype_t type)
{
    static const struct {
        const char *name;
        uint32_t color;
    } colors[] = {
        { "red",     MakeColor(255,  60,  50, 255) },
        { "green",   MakeColor( 80, 255, 120, 255) },
        { "blue",    MakeColor( 80, 150, 255, 255) },
        { "yellow",  MakeColor(255, 230,  60, 255) },
        { "magenta", MakeColor(255,  80, 255, 255) },
        { "cyan",    MakeColor( 60, 240, 255, 255) },
        { "white",   MakeColor(245, 245, 255, 255) },
        { "orange",  MakeColor(255, 140,  40, 255) },
    };
    static const uint32_t by_type[] = {
        MakeColor(255, 140,  40, 255),  // rocket
        MakeColor( 80, 255, 120, 255),  // grenade
        MakeColor(245, 245, 255, 255),  // knife
        MakeColor(255, 210,  60, 255),  // bomb
    };

    for (int i = 0; i < q_countof(colors); i++)
        if (!Q_stricmp(trail_color->string, colors[i].name))
            return colors[i].color;
    return by_type[type];
}

static float catmull(float p0, float p1, float p2, float p3, float s)
{
    float s2 = s * s, s3 = s2 * s;
    return 0.5f * (2 * p1 + (p2 - p0) * s + (2 * p0 - 5 * p1 + 4 * p2 - p3) * s2 +
                   (3 * p1 - p0 - 3 * p2 + p3) * s3);
}

// smooth position at time T (inside the flight)
static void curve_pos(const track_t *t, float T, vec3_t out)
{
    int i = (int)floorf(T);
    const float *p0 = point(t, i - 1), *p1 = point(t, i), *p2 = point(t, i + 1), *p3 = point(t, i + 2);
    float s = T - i;

    for (int k = 0; k < 3; k++)
        out[k] = catmull(p0[k], p1[k], p2[k], p3[k], s);
}

static void add_trail(const track_t *t, float T, float len, bool followed)
{
    float tail = max(T - len, t->first), head = min(T, t->last);
    float step = 0.34f, alpha_scale = followed ? 1.0f : 0.75f;
    int width = Cvar_ClampInteger(trail_width, 1, 32) * (followed ? 2 : 1);
    uint32_t rgba = trail_rgba(t->type);
    vec3_t a, b;

    if (head <= tail)
        return;

    curve_pos(t, tail, a);
    for (float s = tail + step; ; s += step) {
        bool last = s >= head;
        float age, near;

        if (last)
            track_pos(t, head, b);  // on the projectile itself
        else
            curve_pos(t, s, b);

        // newer = brighter; the tail fades out, and so does the part
        // passing by the camera (following it, the trail comes at us)
        age = (T - min(s, head)) / len;
        near = Q_clipf((Distance(b, cl.refdef.vieworg) - 24) / 140, 0, 1);
        if (near > 0)
            CL_AddBeam(a, b, rgba, (1 - age) * alpha_scale * near, width);
        VectorCopy(b, a);
        if (last)
            break;
    }
}

void CL_ProjAddTrails(void)
{
    bool clip = CL_CaptureActive();
    int mode = CL_TrailsMode(clip), followed = CL_FreecamProj();
    float T, len;

    if (!mode || !have_tracks())
        return;

    if (followed < 0)
        followed = CL_KeyCamProjNow();

    T = CL_ProjNow();
    len = Cvar_ClampValue(trail_time, 0.1f, 30) * 1000 / CL_DemoFrameMs();

    for (int i = 0; i < pj.num; i++) {
        const track_t *t = &pj.tracks[i];

        if (T < t->first || T - len > t->last)
            continue;
        if (mode == 2 && i != followed)
            continue;
        add_trail(t, T, len, i == followed);
    }
}

// trails_toggle [clip]: all / only the one followed / none (key 6)
static void Trails_Toggle_f(void)
{
    bool clip = !strcmp(Cmd_Argv(1), "clip");
    cvar_t *var = clip ? clip_trails : visor_trails;
    static const char *const names[] = { "no", "todos los proyectiles", "solo el que sigues" };
    int v = (CL_TrailsMode(clip) + 1) % 3;

    Cvar_SetInteger(var, v, FROM_CONSOLE);
    SCR_CenterPrint(va("Estelas %s: %s", clip ? "en el video" : "en pantalla", names[v]), false);
}

/*
===============================================================================

COMMANDS

===============================================================================
*/

// proj_next / proj_prev: jump to the next / previous throw and follow it
static void jump(int dir)
{
    int now = CL_DemoSeekFrame(), found = -1;

    if (!CL_DemoView()) {
        Com_Printf("Los proyectiles se pueden seguir al ver un demo.\n");
        return;
    }
    if (!have_tracks()) {
        SCR_CenterPrint("No hay granadas, cohetes ni cuchillos lanzados en este demo", false);
        return;
    }

    for (int i = 0; i < pj.num; i++) {
        int f = pj.tracks[i].demo_frame;
        if (dir > 0 && f > now + 1 && (found < 0 || f < pj.tracks[found].demo_frame))
            found = i;
        if (dir < 0 && f < now - 1 && (found < 0 || f > pj.tracks[found].demo_frame))
            found = i;
    }
    if (found < 0) {
        SCR_CenterPrint(dir > 0 ? "No hay mas proyectiles" : "No hay proyectiles antes", false);
        return;
    }

    // a moment before the throw, so it is seen leaving the hand
    {
        int f = max(pj.tracks[found].demo_frame - 2, 0);
        Cmd_ExecuteString(&cmd_buffer, va("seek %d.%d", f / 10, f % 10));
    }
    CL_FreecamFollowProj(found);
}

static void Proj_Next_f(void)
{
    jump(1);
}

static void Proj_Prev_f(void)
{
    jump(-1);
}

static void Proj_List_f(void)
{
    if (!have_tracks()) {
        Com_Printf("No hay proyectiles en este demo.\n");
        return;
    }
    for (int i = 0; i < pj.num; i++) {
        const track_t *t = &pj.tracks[i];
        int f = t->demo_frame;
        Com_Printf("%3d  %d:%02d.%d  %-8s  %.1f s\n", i + 1, f / 600, f / 10 % 60, f % 10,
                   type_names[t->type], (t->last - t->first) * CL_DemoFrameMs() * 0.001f);
    }
}

static void proj_models_changed(cvar_t *self)
{
    clear_types();
}

void CL_InitProj(void)
{
    proj_models = Cvar_Get("proj_models", "rocket masher grenade knife", CVAR_ARCHIVE);
    proj_models->changed = proj_models_changed;
    visor_trails = Cvar_Get("visor_trails", "1", CVAR_ARCHIVE);
    clip_trails = Cvar_Get("clip_trails", "1", CVAR_ARCHIVE);
    trail_time = Cvar_Get("trail_time", "2", CVAR_ARCHIVE);
    trail_width = Cvar_Get("trail_width", "3", CVAR_ARCHIVE);
    trail_color = Cvar_Get("trail_color", "tipo", CVAR_ARCHIVE);
    clear_types();

    Cmd_AddCommand("proj_next", Proj_Next_f);
    Cmd_AddCommand("proj_prev", Proj_Prev_f);
    Cmd_AddCommand("proj_list", Proj_List_f);
    Cmd_AddCommand("trails_toggle", Trails_Toggle_f);
}
