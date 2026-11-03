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

// names.c -- nicks above the players while watching a demo
//
// Every player in the demo gets their nick above the head, in their team's
// color, also through walls. Closer players get bigger text, between
// names_min and names_max (character height in pixels on a 1080 lines
// screen). On screen (visor_names, key Y) and in the videos (clip_names)
// they are switched separately.

#include "client.h"

static cvar_t   *visor_names;
static cvar_t   *clip_names;
static cvar_t   *names_min;
static cvar_t   *names_max;
static cvar_t   *names_dist;

static bool watching_demo(void)
{
    if (cls.state != ca_active)
        return false;
    return cls.demo.playback || MVD_GetDemoStatus(NULL, NULL, NULL);
}

bool CL_NamesOn(bool clip)
{
    return (clip ? clip_names : visor_names)->integer;
}

static uint32_t name_color(const char *name)
{
    static const struct {
        const char *name;
        uint32_t color;
    } colors[] = {
        { "red",     MakeColor(255,  90,  90, 255) },
        { "green",   MakeColor( 90, 230,  90, 255) },
        { "blue",    MakeColor( 90, 160, 255, 255) },
        { "yellow",  MakeColor(255, 220,  60, 255) },
        { "magenta", MakeColor(255,  90, 255, 255) },
        { "cyan",    MakeColor( 60, 230, 240, 255) },
        { "white",   MakeColor(240, 240, 240, 255) },
    };

    for (int i = 0; i < q_countof(colors); i++)
        if (!Q_stricmp(name, colors[i].name))
            return colors[i].color;
    return MakeColor(240, 240, 240, 255);
}

// world point to window pixels; false when behind the camera
static bool project(const vec3_t p, float *sx, float *sy)
{
    const refdef_t *rd = &cl.refdef;
    vec3_t d, f, r, u;
    float x, y, z;

    AngleVectors(rd->viewangles, f, r, u);
    VectorSubtract(p, rd->vieworg, d);
    z = DotProduct(d, f);
    if (z < 4)
        return false;
    x = DotProduct(d, r) / (z * tanf(DEG2RAD(rd->fov_x) * 0.5f));
    y = DotProduct(d, u) / (z * tanf(DEG2RAD(rd->fov_y) * 0.5f));
    if (fabsf(x) > 1.2f || fabsf(y) > 1.2f)
        return false;
    *sx = rd->x + rd->width * 0.5f * (1 + x);
    *sy = rd->y + rd->height * 0.5f * (1 - y);
    return true;
}

/*
===============
CL_DrawNames

Called after the 3D view is drawn, before the HUD and the video capture.
===============
*/
void CL_DrawNames(qhandle_t font)
{
    const refdef_t *rd = &cl.refdef;
    float lines, lo, hi, near;
    uint32_t allies, axis;

    if (!watching_demo() || !CL_NamesOn(CL_CaptureActive()))
        return;
    if (rd->width <= 0 || rd->height <= 0)
        return;

    // sizes are given for a 1080 lines picture
    lines = rd->height / 1080.0f;
    lo = Cvar_ClampValue(names_min, 4, 64) * lines;
    hi = max(Cvar_ClampValue(names_max, 4, 96) * lines, lo);
    near = Cvar_ClampValue(names_dist, 32, 4000);
    allies = name_color(Cvar_VariableString("xray_color_allies"));
    axis = name_color(Cvar_VariableString("xray_color_axis"));

    for (int num = 1; num <= cl.maxclients && num <= MAX_CLIENTS; num++) {
        const centity_t *e = &cl_entities[num];
        const clientinfo_t *ci = &cl.clientinfo[num - 1];
        vec3_t head;
        float dist, size, scale, sx, sy, top;

        if (e->serverframe != cl.frame.number || e->current.modelindex != MODELINDEX_PLAYER)
            continue;
        if (!ci->name[0])
            continue;

        if (num == cl.frame.clientNum + 1 && !cls.demo.playback)
            VectorCopy(cl.playerEntityOrigin, head);
        else
            LerpVector(e->prev.origin, e->current.origin, cl.lerpfrac, head);

        dist = Distance(head, rd->vieworg);
        head[2] += 20;
        if (Distance(head, rd->vieworg) < 16)
            continue;   // the camera is in their eyes
        head[2] -= 20;

        top = e->maxs[2] - e->mins[2] > 1 ? e->maxs[2] : 32;
        head[2] += top + 12;
        if (!project(head, &sx, &sy))
            continue;

        // smaller far away
        size = Q_clipf(hi * near / dist, lo, hi);
        scale = CONCHAR_HEIGHT / size;

        R_SetScale(scale);
        R_SetColor(CL_IsAxisPlayer(num - 1) ? axis : allies);
        R_DrawString(Q_rint(sx * scale), Q_rint((sy - size) * scale),
                     UI_CENTER | UI_DROPSHADOW, MAX_CLIENT_NAME, ci->name, font);
    }

    R_ClearColor();
    R_SetScale(1.0f);
}

// names_toggle [clip]: nicks above the players, on screen or in the videos (Y)
static void Names_Toggle_f(void)
{
    bool clip = !strcmp(Cmd_Argv(1), "clip");
    cvar_t *var = clip ? clip_names : visor_names;

    Cvar_SetInteger(var, !var->integer, FROM_CONSOLE);
    SCR_CenterPrint(va("Nombres %s: %s", clip ? "en el video" : "en pantalla",
                       var->integer ? "si" : "no"), false);
}

void CL_InitNames(void)
{
    visor_names = Cvar_Get("visor_names", "1", CVAR_ARCHIVE);
    clip_names = Cvar_Get("clip_names", "0", CVAR_ARCHIVE);
    names_min = Cvar_Get("names_min", "12", CVAR_ARCHIVE);
    names_max = Cvar_Get("names_max", "24", CVAR_ARCHIVE);
    names_dist = Cvar_Get("names_dist", "400", CVAR_ARCHIVE);
    Cmd_AddCommand("names_toggle", Names_Toggle_f);
}
