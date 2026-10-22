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

// freecam.c -- free (detached) camera for demo playback
//
// While active, the view no longer follows the player who recorded the demo.
// The camera flies with the usual movement binds (+forward, +moveleft,
// +moveup...) and looks around with the mouse. It keeps working while the
// demo is paused, so shots can be framed on a frozen moment.

#include "client.h"

static struct {
    bool    enabled;
    vec3_t  origin;
    vec3_t  velocity;
} fc;

static cvar_t   *freecam_speed;
static cvar_t   *freecam_fast;
static cvar_t   *freecam_smooth;
static cvar_t   *freecam_hud;

bool CL_FreecamActive(void)
{
    return fc.enabled && cls.demo.playback && cls.state == ca_active;
}

// true when the recorder's HUD (stats, layouts, crosshair) should be hidden
bool CL_FreecamHideHud(void)
{
    return CL_FreecamActive() && !freecam_hud->integer;
}

void CL_FreecamReset(void)
{
    fc.enabled = false;
    VectorClear(fc.velocity);
}

static void Freecam_Set(bool enable)
{
    if (enable && (!cls.demo.playback || cls.state != ca_active)) {
        Com_Printf("freecam: only available while playing a demo.\n");
        enable = false;
    }

    if (enable && !fc.enabled) {
        // start from wherever the view currently is
        VectorCopy(cl.refdef.vieworg, fc.origin);
        VectorCopy(cl.refdef.viewangles, cl.viewangles);
        if (cl.viewangles[PITCH] > 180)
            cl.viewangles[PITCH] -= 360;
        cl.viewangles[ROLL] = 0;
        VectorClear(fc.velocity);
    }

    fc.enabled = enable;
    IN_Activate();  // grab or release the mouse
}

static void Freecam_f(void)
{
    if (Cmd_Argc() > 1)
        Freecam_Set(Q_atoi(Cmd_Argv(1)));
    else
        Freecam_Set(!fc.enabled);
}

/*
===============
CL_FreecamMove

Called once per client frame from input code. `dir` holds -1..1 intents for
forward, right and up. Uses real time, so it works while the demo is paused.
===============
*/
void CL_FreecamMove(int msec, const vec3_t dir, bool fast)
{
    vec3_t forward, right, wish;
    float dt, speed, frac;

    if (msec <= 0)
        return;

    dt = msec * 0.001f;

    cl.viewangles[PITCH] = Q_clipf(cl.viewangles[PITCH], -89, 89);
    cl.viewangles[ROLL] = 0;

    // fly where you look; up/down is always world vertical
    AngleVectors(cl.viewangles, forward, right, NULL);
    VectorScale(forward, dir[0], wish);
    VectorMA(wish, dir[1], right, wish);
    wish[2] += dir[2];

    speed = Cvar_ClampValue(freecam_speed, 1, 10000);
    if (fast)
        speed *= Cvar_ClampValue(freecam_fast, 1, 20);

    if (VectorNormalize(wish) > 0)
        VectorScale(wish, speed, wish);
    else
        VectorClear(wish);

    // ease towards the wanted velocity for smoother camera moves
    if (freecam_smooth->value > 0)
        frac = 1.0f - expf(-dt / freecam_smooth->value);
    else
        frac = 1.0f;

    LerpVector(fc.velocity, wish, frac, fc.velocity);
    VectorMA(fc.origin, dt, fc.velocity, fc.origin);
}

/*
===============
CL_FreecamView

Overrides the computed view with the free camera. Called at the end of
CL_CalcViewValues, after the player entity position has been worked out.
===============
*/
void CL_FreecamView(void)
{
    if (!CL_FreecamActive())
        return;

    VectorCopy(fc.origin, cl.refdef.vieworg);
    VectorCopy(cl.viewangles, cl.refdef.viewangles);
    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    // the recorder's damage flashes and underwater tint don't belong here
    Vector4Clear(cl.refdef.screen_blend);
    Vector4Clear(cl.refdef.damage_blend);
}

static const cmdreg_t c_freecam[] = {
    { "freecam", Freecam_f },
    { NULL }
};

void CL_InitFreecam(void)
{
    freecam_speed = Cvar_Get("freecam_speed", "300", CVAR_ARCHIVE);
    freecam_fast = Cvar_Get("freecam_fast", "3", CVAR_ARCHIVE);
    freecam_smooth = Cvar_Get("freecam_smooth", "0.15", CVAR_ARCHIVE);
    freecam_hud = Cvar_Get("freecam_hud", "0", CVAR_ARCHIVE);

    Cmd_Register(c_freecam);
}
