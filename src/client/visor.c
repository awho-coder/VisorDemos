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

// visor.c -- small demo viewer helpers: help overlay and playback speed

#include "client.h"
#include "server/server.h"

static bool     help_visible;

static const float speeds[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };

bool CL_VisorHelpVisible(void)
{
    return help_visible;
}

static void Visor_Help_f(void)
{
    help_visible = !help_visible;
}

float CL_DemoSpeed(void)
{
    float ts = Cvar_VariableValue("timescale");
    return ts > 0 ? ts : 1.0f;
}

static void set_speed(float value)
{
    if (!CL_CheatsOK()) {
        Com_Printf("La velocidad solo se puede cambiar al ver un demo.\n");
        return;
    }
    Cvar_Set("timescale", va("%g", value));
    SCR_CenterPrint(va("Velocidad x%g", value), false);
}

// demo_speed up | down | reset | <value>
static void Demo_Speed_f(void)
{
    const char *arg = Cmd_Argv(1);
    float cur = CL_DemoSpeed();
    int i, n = q_countof(speeds);

    if (!*arg) {
        Com_Printf("Velocidad actual: x%g\nUso: %s up | down | reset | <valor>\n", cur, Cmd_Argv(0));
        return;
    }

    if (!Q_stricmp(arg, "up")) {
        for (i = 0; i < n - 1 && speeds[i] <= cur + 0.001f; i++)
            ;
        set_speed(speeds[i]);
    } else if (!Q_stricmp(arg, "down")) {
        for (i = n - 1; i > 0 && speeds[i] >= cur - 0.001f; i--)
            ;
        set_speed(speeds[i]);
    } else if (!Q_stricmp(arg, "reset")) {
        set_speed(1);
    } else {
        float v = Q_atof(arg);
        if (v < 0.05f || v > 10) {
            Com_Printf("Velocidad entre 0.05 y 10.\n");
            return;
        }
        set_speed(v);
    }
}

/*
===============
CL_DemoPosition

Current position for DM2 playback or local MVD playback. Frames are 1/10 s.
===============
*/
bool CL_DemoPosition(int *frame, float *progress, bool *paused)
{
    bool mvd_paused = false;
    float p = 0;
    int f = 0;

    if (cls.state != ca_active)
        return false;

    if (cls.demo.playback) {
        if (cls.demo.compat || !cls.demo.file_size)
            return false;
        f = cls.demo.frames_read;
        p = cls.demo.file_progress;
    } else if (!MVD_GetDemoStatus(&p, &mvd_paused, &f)) {
        return false;
    }

    if (frame)
        *frame = f;
    if (progress)
        *progress = Q_clipf(p, 0, 1);
    if (paused)
        *paused = mvd_paused || (sv_paused->integer && cl_paused->integer);
    return true;
}

// demo finished or closed
bool CL_DemoAtEnd(void)
{
    if (cls.demo.playback)
        return cls.demo.eof;
    return !CL_DemoPosition(NULL, NULL, NULL);
}

/*
===============================================================================

X-RAY

Players hidden behind walls are drawn as a flat colored silhouette. Only for
demos played from a file (.dm2, or .mvd2 through the local MVD player):
never while connected to a server or watching a live GTV stream.

===============================================================================
*/

static cvar_t   *xray;
static cvar_t   *xray_color;
static cvar_t   *xray_color_allies;
static cvar_t   *xray_color_axis;
static cvar_t   *xray_alpha;

// demo read from a file, not a game or a live stream
static bool xray_allowed(void)
{
    if (cls.state != ca_active)
        return false;
    if (cls.demo.playback)
        return true;
    return MVD_GetDemoStatus(NULL, NULL, NULL);
}

bool CL_XrayWants(const centity_state_t *s1)
{
    if (!xray->integer || !xray_allowed())
        return false;
    if (s1->modelindex != MODELINDEX_PLAYER)
        return false;
    // players only: bodies left behind use the same model
    return s1->number >= 1 && s1->number <= cl.maxclients;
}

static uint64_t color_bits(const char *name)
{
    static const struct {
        const char *name;
        uint64_t bits;
    } colors[] = {
        { "red",     RF_SHELL_RED },
        { "green",   RF_SHELL_GREEN },
        { "blue",    RF_SHELL_BLUE },
        { "yellow",  RF_SHELL_RED | RF_SHELL_GREEN },
        { "magenta", RF_SHELL_RED | RF_SHELL_BLUE },
        { "cyan",    RF_SHELL_GREEN | RF_SHELL_BLUE },
        { "white",   RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE },
    };

    for (int i = 0; i < q_countof(colors); i++)
        if (!Q_stricmp(name, colors[i].name))
            return colors[i].bits;
    return RF_SHELL_RED | RF_SHELL_BLUE;
}

// D-Day: the player model is the nation; these ones fight for the Axis
static bool is_axis_model(const char *model)
{
    static const char *const axis[] = { "grm", "ger", "ita", "jpn", "axis" };

    for (int i = 0; i < q_countof(axis); i++)
        if (!Q_stricmp(model, axis[i]))
            return true;
    return false;
}

// silhouette color: fixed (xray_color <color>) or by team (xray_color team)
uint64_t CL_XrayShell(const centity_state_t *s1)
{
    const clientinfo_t *ci;

    if (Q_stricmp(xray_color->string, "team"))
        return color_bits(xray_color->string);

    ci = &cl.clientinfo[s1->skinnum & 0xff];   // same lookup the renderer uses
    return color_bits(is_axis_model(ci->model_name) ? xray_color_axis->string
                                                   : xray_color_allies->string);
}

float CL_XrayAlpha(void)
{
    return Cvar_ClampValue(xray_alpha, 0.1f, 1);
}

static void Visor_Xray_f(void)
{
    bool on = Cmd_Argc() > 1 ? Q_atoi(Cmd_Argv(1)) : !xray->integer;

    if (on && !xray_allowed()) {
        Com_Printf("El x-ray solo funciona al ver un demo desde un archivo.\n");
        on = false;
    }
    Cvar_SetInteger(xray, on, FROM_CODE);
    SCR_CenterPrint(on ? "X-ray: si" : "X-ray: no", false);
}

static void Visor_Players_f(void)
{
    for (int i = 0; i < cl.maxclients && i < MAX_CLIENTS; i++) {
        const clientinfo_t *ci = &cl.clientinfo[i];
        if (!ci->name[0])
            continue;
        Com_Printf("%2d  %-24s %-8s %s/%s\n", i + 1, ci->name,
                   is_axis_model(ci->model_name) ? "Eje" : "Aliados",
                   ci->model_name, ci->skin_name);
    }
}

static const cmdreg_t c_visor[] = {
    { "visor_help", Visor_Help_f },
    { "demo_speed", Demo_Speed_f },
    { "visor_xray", Visor_Xray_f },
    { "visor_players", Visor_Players_f },
    { NULL }
};

void CL_InitVisor(void)
{
    xray = Cvar_Get("xray", "0", 0);
    xray_color = Cvar_Get("xray_color", "team", CVAR_ARCHIVE);
    xray_color_allies = Cvar_Get("xray_color_allies", "blue", CVAR_ARCHIVE);
    xray_color_axis = Cvar_Get("xray_color_axis", "red", CVAR_ARCHIVE);
    xray_alpha = Cvar_Get("xray_alpha", "0.45", CVAR_ARCHIVE);

    Cmd_Register(c_visor);
}
