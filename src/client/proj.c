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
    PT_NONE = -1,
    PT_UNKNOWN = -2
} projtype_t;

typedef struct {
    int         number;     // entity number
    projtype_t  type;
    int         first, last;    // server frames it is in the demo
    int         demo_frame;     // cls.demo.frames_read at the throw, to seek
    float       pos;            // demo file position at the throw (timeline)
    int         start;          // first position in pj.pts
    float       top_speed;      // units per frame
    bool        open;           // still flying while indexing
} track_t;

#define FLY_SPEED   30      // units per frame (300 / s) = thrown, not dropped
#define MAX_GAP     5       // frames out of the demo before a flight ends
#define MAX_JUMP    400     // units between frames: another projectile

static struct {
    track_t     *tracks;
    int         num, alloc;
    vec3_t      *pts;
    int         npts, apts;
    char        demo[MAX_QPATH];
    bool        indexing;
    signed char model_type[MAX_MODELS];
} pj;

static cvar_t   *proj_models;
static cvar_t   *visor_trails;
static cvar_t   *clip_trails;
static cvar_t   *trail_time;
static cvar_t   *trail_width;
static cvar_t   *trail_color;

static const char *const type_names[] = { "cohete", "granada", "cuchillo" };

static bool have_tracks(void)
{
    return cls.demo.playback && cls.state == ca_active && pj.num &&
           !strcmp(pj.demo, cls.servername);
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

static projtype_t model_type(int index)
{
    const char *name;
    char words[MAX_STRING_CHARS], *w, *s;

    if (index <= 0 || index >= MAX_MODELS)
        return PT_NONE;
    if (pj.model_type[index] != PT_UNKNOWN)
        return pj.model_type[index];

    pj.model_type[index] = PT_NONE;
    name = cl.configstrings[cl.csr.models + index];
    if (!*name || *name == '*')
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

static void add_point(const vec3_t p)
{
    if (pj.npts == pj.apts) {
        pj.apts = max(pj.apts * 2, 4096);
        pj.pts = Z_Realloc(pj.pts, pj.apts * sizeof(pj.pts[0]));
    }
    VectorCopy(p, pj.pts[pj.npts]);
    pj.npts++;
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
    Z_Freep(&pj.tracks);
    Z_Freep(&pj.pts);
    pj.num = pj.alloc = pj.npts = pj.apts = 0;
    pj.demo[0] = 0;
}

void CL_ProjIndexBegin(void)
{
    free_all();
    clear_types();
    Q_strlcpy(pj.demo, cls.servername, sizeof(pj.demo));
    pj.indexing = true;
}

// a flight that ended: keep it only if it really flew
static void close_track(track_t *t)
{
    t->open = false;
    if (t->top_speed >= FLY_SPEED)
        return;

    // dropped weapon: forget it (its points are the last ones when it is
    // the last track; otherwise they stay unused)
    if (t == &pj.tracks[pj.num - 1]) {
        pj.npts = t->start;
        pj.num--;
    } else {
        t->last = t->first - 1;     // empty
    }
}

static void extend(track_t *t, int frame, const vec3_t p)
{
    const float *prev = pj.pts[t->start + t->last - t->first];
    int gap = frame - t->last;
    float speed = Distance(prev, p) / gap;
    vec3_t mid;

    // fill frames it was out of the demo with a straight line
    for (int i = 1; i < gap; i++) {
        LerpVector(prev, p, (float)i / gap, mid);
        add_point(mid);
    }
    add_point(p);
    t->last = frame;
    t->top_speed = max(t->top_speed, speed);
}

/*
===============
CL_ProjIndexFrame

A frame was parsed while indexing (seek_demo reading ahead).
===============
*/
void CL_ProjIndexFrame(void)
{
    int frame = cl.frame.number;

    if (!pj.indexing || !cl.frame.valid)
        return;

    for (int i = 0; i < cl.frame.numEntities; i++) {
        const centity_state_t *st = &cl.entityStates[(cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK];
        projtype_t type;
        track_t *t = NULL;

        if (st->number <= cl.maxclients)
            continue;
        type = model_type(st->modelindex);
        if (type == PT_NONE)
            continue;

        // continue its flight, or a new one
        for (int j = pj.num - 1; j >= 0; j--) {
            track_t *o = &pj.tracks[j];
            if (o->open && o->number == st->number) {
                const float *last = pj.pts[o->start + o->last - o->first];
                if (o->type == type && o->last < frame &&
                    Distance(last, st->origin) < MAX_JUMP * (frame - o->last))
                    t = o;
                else
                    close_track(o);
                break;
            }
        }

        if (t) {
            extend(t, frame, st->origin);
            continue;
        }

        if (pj.npts > 4000000)
            continue;   // way too many, something is wrong
        t = new_track();
        t->number = st->number;
        t->type = type;
        t->first = t->last = frame;
        t->demo_frame = cls.demo.frames_read;
        t->pos = cls.demo.file_progress;
        if (cls.demo.file_size) {
            int64_t pos = FS_Tell(cls.demo.playback) - cls.demo.file_offset;
            t->pos = Q_clipf((float)pos / cls.demo.file_size, 0, 1);
        }
        t->start = pj.npts;
        t->open = true;
        add_point(st->origin);
    }

    // flights whose projectile is gone
    for (int j = 0; j < pj.num; j++) {
        track_t *t = &pj.tracks[j];
        if (t->open && frame - t->last > MAX_GAP)
            close_track(t);
    }
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
    if (!cl.oldframe.valid)
        return cl.frame.number;
    return cl.frame.number - 1 + cl.lerpfrac;
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
    return pj.pts[t->start + frame - t->first];
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

COMMANDS

===============================================================================
*/

// proj_next / proj_prev: jump to the next / previous throw and follow it
static void jump(int dir)
{
    int now = cls.demo.frames_read, found = -1;

    if (!cls.demo.playback || cls.state != ca_active) {
        Com_Printf("Los proyectiles se pueden seguir en demos .dm2.\n");
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
                   type_names[t->type], (t->last - t->first) * CL_FRAMETIME * 0.001f);
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
}
