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

static const cmdreg_t c_visor[] = {
    { "visor_help", Visor_Help_f },
    { "demo_speed", Demo_Speed_f },
    { NULL }
};

void CL_InitVisor(void)
{
    Cmd_Register(c_visor);
}
