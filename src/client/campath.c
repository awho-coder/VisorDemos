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

// campath.c -- recorded camera paths for demo playback
//
// While recording, the final camera of every frame (free, chase or the
// recorder's own view) is stored against demo time. Playing the path back
// reproduces those camera moves, smoothed, which is what clip_export uses to
// render moving shots at a fixed frame rate. Re-recording a stretch only
// replaces that stretch. Paths are saved per demo in campaths/<demo>.cam
// and loaded again automatically.

#include "client.h"

typedef struct {
    int     time;       // demo_time(), ms
    float   pos;        // demo file position, for the timeline
    bool    detached;   // false: the recorder's own view
    vec3_t  org, ang;
} campoint_t;

typedef struct {
    campoint_t  *pts;
    int         num, alloc;
} campath_t;

static campath_t    path, rec;
static bool         recording;
static bool         playing = true;
static char         loaded_for[MAX_QPATH];

static cvar_t       *campath_smooth;

// Demo time that stays put across seeks. cl.time is kept continuous when
// seeking (serverdelta absorbs the jump), so add the delta back to get a
// position tied to the server frame number.
static int demo_time(void)
{
    return cl.time + cl.serverdelta * CL_FRAMETIME;
}

static void path_push(campath_t *p, const campoint_t *pt)
{
    if (p->num == p->alloc) {
        p->alloc = max(p->alloc * 2, 1024);
        p->pts = Z_Realloc(p->pts, p->alloc * sizeof(p->pts[0]));
    }
    p->pts[p->num++] = *pt;
}

static void path_free(campath_t *p)
{
    Z_Freep(&p->pts);
    p->num = p->alloc = 0;
}

static void demo_basename(char *buf, size_t size)
{
    char *p;

    Q_strlcpy(buf, COM_SkipPath(cls.servername), size);
    p = strrchr(buf, '.');
    if (p)
        *p = 0;
    for (p = buf; *p; p++) {
        if (!Q_isalnum(*p) && *p != '-' && *p != '_')
            *p = '_';
    }
}

/*
===============================================================================

SAVE / LOAD

===============================================================================
*/

static void path_save(void)
{
    char name[MAX_QPATH], file[MAX_OSPATH];
    qhandle_t f;

    demo_basename(name, sizeof(name));
    Q_snprintf(file, sizeof(file), "campaths/%s.cam", name);

    if (!path.num) {
        // an empty path: make sure an old file doesn't come back
        if (FS_OpenFile(file, &f, FS_MODE_WRITE | FS_FLAG_TEXT) >= 0) {
            FS_FPrintf(f, "VISORCAM 1\n");
            FS_CloseFile(f);
        }
        return;
    }

    if (FS_OpenFile(file, &f, FS_MODE_WRITE | FS_FLAG_TEXT) < 0) {
        Com_EPrintf("No se pudo guardar el recorrido de camara en %s\n", file);
        return;
    }

    FS_FPrintf(f, "VISORCAM 1\n");
    for (int i = 0; i < path.num; i++) {
        const campoint_t *p = &path.pts[i];
        FS_FPrintf(f, "%d %.5f %d %.2f %.2f %.2f %.3f %.3f %.3f\n",
                   p->time, p->pos, p->detached,
                   p->org[0], p->org[1], p->org[2],
                   p->ang[0], p->ang[1], p->ang[2]);
    }
    FS_CloseFile(f);
}

static void path_load(void)
{
    char name[MAX_QPATH], file[MAX_OSPATH];
    const char *data, *line;
    char *buf;
    campoint_t pt;
    int det;

    path_free(&path);

    demo_basename(name, sizeof(name));
    Q_snprintf(file, sizeof(file), "campaths/%s.cam", name);

    if (FS_LoadFile(file, (void **)&buf) < 0)
        return;

    data = buf;
    if (strncmp(data, "VISORCAM 1", 10)) {
        FS_FreeFile(buf);
        return;
    }

    for (line = strchr(data, '\n'); line; line = strchr(line, '\n')) {
        line++;
        if (sscanf(line, "%d %f %d %f %f %f %f %f %f", &pt.time, &pt.pos, &det,
                   &pt.org[0], &pt.org[1], &pt.org[2],
                   &pt.ang[0], &pt.ang[1], &pt.ang[2]) != 9)
            continue;
        pt.detached = det;
        if (path.num && pt.time <= path.pts[path.num - 1].time)
            continue;   // keep it sorted
        path_push(&path, &pt);
    }

    FS_FreeFile(buf);

    if (path.num)
        Com_Printf("Recorrido de camara cargado (%d puntos). V lo activa o desactiva.\n", path.num);
}

// switching demos: drop the old path and pick up this demo's saved one
static void check_demo(void)
{
    if (!strcmp(loaded_for, cls.servername))
        return;

    Q_strlcpy(loaded_for, cls.servername, sizeof(loaded_for));
    recording = false;
    path_free(&rec);
    path_load();
}

/*
===============================================================================

RECORDING

===============================================================================
*/

static void record_stop(void)
{
    campath_t merged = { 0 };
    int t0, t1, i;

    recording = false;

    if (!rec.num) {
        path_free(&rec);
        return;
    }

    // the new stretch replaces whatever was recorded for the same time
    t0 = rec.pts[0].time;
    t1 = rec.pts[rec.num - 1].time;

    for (i = 0; i < path.num && path.pts[i].time < t0; i++)
        path_push(&merged, &path.pts[i]);
    for (int j = 0; j < rec.num; j++)
        path_push(&merged, &rec.pts[j]);
    for (; i < path.num; i++)
        if (path.pts[i].time > t1)
            path_push(&merged, &path.pts[i]);

    path_free(&path);
    path_free(&rec);
    path = merged;
    playing = true;

    path_save();

    Com_Printf("Recorrido de camara guardado (%.1f s).\n", (t1 - t0) * 0.001f);
    SCR_CenterPrint(va("Recorrido de camara guardado (%.1f s)", (t1 - t0) * 0.001f), false);
}

/*
===============
CL_CamPathRecord

Called every frame with the camera that was just decided on.
===============
*/
void CL_CamPathRecord(bool detached)
{
    campoint_t pt;

    if (!cls.demo.playback || cls.state != ca_active)
        return;

    check_demo();

    if (!recording)
        return;

    pt.time = demo_time();
    pt.pos = cls.demo.file_progress;
    pt.detached = detached;
    VectorCopy(cl.refdef.vieworg, pt.org);
    VectorCopy(cl.refdef.viewangles, pt.ang);

    if (rec.num) {
        campoint_t *last = &rec.pts[rec.num - 1];

        if (pt.time == last->time) {
            // paused: keep only the latest camera for this moment
            *last = pt;
            return;
        }
        // jumped back in the demo: re-record from here
        while (rec.num && rec.pts[rec.num - 1].time >= pt.time)
            rec.num--;
    }

    path_push(&rec, &pt);
}

// angle difference wrapped to -180..180
static float AngleMod180(float a)
{
    a = fmodf(a, 360.0f);
    if (a > 180)
        a -= 360;
    else if (a < -180)
        a += 360;
    return a;
}

/*
===============================================================================

PLAYBACK

===============================================================================
*/

// interpolated camera at time t; false outside the path
static bool sample_at(int t, campoint_t *out)
{
    const campoint_t *a, *b;
    int lo, hi;
    float frac;

    if (path.num < 2 || t < path.pts[0].time || t > path.pts[path.num - 1].time)
        return false;

    // first point with time > t
    lo = 0;
    hi = path.num - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (path.pts[mid].time > t)
            hi = mid;
        else
            lo = mid + 1;
    }
    if (hi == 0)
        hi = 1;

    a = &path.pts[hi - 1];
    b = &path.pts[hi];

    if (!a->detached || !b->detached || b->time <= a->time) {
        *out = *a;
        return true;
    }

    frac = Q_clipf((float)(t - a->time) / (b->time - a->time), 0, 1);
    out->time = t;
    out->detached = true;
    LerpVector(a->org, b->org, frac, out->org);
    LerpAngles(a->ang, b->ang, frac, out->ang);
    return true;
}

/*
===============
CL_CamPathView

Camera from the recorded path for the current demo time. Returns false when
the path doesn't cover this moment, or covers it with the recorder's view.
===============
*/
bool CL_CamPathView(vec3_t org, vec3_t ang)
{
    campoint_t center, s;
    vec3_t sum_org, sum_ang;
    float total, window;
    const int taps = 8;

    if (!cls.demo.playback || cls.state != ca_active)
        return false;

    check_demo();

    if (!playing || recording)
        return false;
    if (!sample_at(demo_time(), &center) || !center.detached)
        return false;

    // gaussian-ish smoothing over neighbouring moments, hides mouse jitter
    window = Cvar_ClampValue(campath_smooth, 0, 2) * 1000;
    VectorCopy(center.org, sum_org);
    VectorClear(sum_ang);
    total = 1;

    if (window > 0) {
        for (int i = -taps; i <= taps; i++) {
            float w;

            if (!i)
                continue;
            if (!sample_at(demo_time() + i * window / taps, &s) || !s.detached)
                continue;

            w = expf(-2.0f * (float)(i * i) / (taps * taps));
            VectorMA(sum_org, w, s.org, sum_org);
            for (int k = 0; k < 3; k++)
                sum_ang[k] += w * AngleMod180(s.ang[k] - center.ang[k]);
            total += w;
        }
    }

    VectorScale(sum_org, 1.0f / total, org);
    for (int k = 0; k < 3; k++)
        ang[k] = center.ang[k] + sum_ang[k] / total;

    return true;
}

bool CL_CamPathRecording(void)
{
    return recording && cls.demo.playback;
}

bool CL_CamPathPlaying(void)
{
    return playing && !recording && path.num >= 2 && cls.demo.playback;
}

// demo positions covered by the path, for the timeline
bool CL_CamPathRange(float *start, float *end)
{
    const campath_t *p = recording ? &rec : &path;

    if (p->num < 2 || (!recording && !playing))
        return false;
    *start = p->pts[0].pos;
    *end = p->pts[p->num - 1].pos;
    return true;
}

/*
===============================================================================

COMMANDS

===============================================================================
*/

static void CamPath_Record_f(void)
{
    if (!cls.demo.playback || cls.state != ca_active) {
        Com_Printf("Abre un demo .dm2 para grabar un recorrido de camara.\n");
        return;
    }

    check_demo();

    if (recording) {
        record_stop();
        return;
    }

    if (CL_CaptureActive()) {
        Com_Printf("No se puede grabar la camara mientras se exporta un clip.\n");
        return;
    }

    path_free(&rec);
    recording = true;
    SCR_CenterPrint("Grabando recorrido de camara (C para terminar)", false);
}

static void CamPath_Play_f(void)
{
    check_demo();

    if (Cmd_Argc() > 1)
        playing = Q_atoi(Cmd_Argv(1));
    else
        playing = !playing;

    if (playing && path.num < 2) {
        SCR_CenterPrint("No hay recorrido de camara grabado (C para grabar)", false);
        return;
    }
    SCR_CenterPrint(playing ? "Usando el recorrido de camara grabado"
                            : "Recorrido de camara desactivado", false);
}

static void CamPath_Clear_f(void)
{
    check_demo();
    recording = false;
    path_free(&rec);
    path_free(&path);
    if (cls.demo.playback)
        path_save();
    SCR_CenterPrint("Recorrido de camara borrado", false);
}

static const cmdreg_t c_campath[] = {
    { "campath_record", CamPath_Record_f },
    { "campath_play", CamPath_Play_f },
    { "campath_clear", CamPath_Clear_f },
    { NULL }
};

void CL_InitCamPath(void)
{
    campath_smooth = Cvar_Get("campath_smooth", "0.25", CVAR_ARCHIVE);
    Cmd_Register(c_campath);
}
