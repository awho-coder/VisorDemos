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

// freecam.c -- detached cameras for demo playback
//
// Three camera modes while watching a DM2 demo:
//   off    - the view of the player who recorded the demo (normal q2pro)
//   free   - fly anywhere with the movement binds, look with the mouse
//   chase  - follow any player from behind; the mouse orbits around them
//            and the camera eases back behind the player on its own
// Both detached modes keep working while the demo is paused.

#include "client.h"

typedef enum {
    CAM_OFF,
    CAM_FREE,
    CAM_CHASE
} cammode_t;

static struct {
    cammode_t   mode;
    vec3_t      origin;     // current camera position
    vec3_t      velocity;   // free camera
    int         target;     // chase: entity number
    vec3_t      target_pos; // chase: last known position of the target
    bool        target_seen;
} fc;

static cvar_t   *freecam_speed;
static cvar_t   *freecam_fast;
static cvar_t   *freecam_smooth;
static cvar_t   *freecam_hud;
static cvar_t   *freecam_fov;
static cvar_t   *chase_dist;
static cvar_t   *chase_height;
static cvar_t   *chase_auto;

static bool     path_driving;   // a recorded camera path moves the view

// camera moved by the user right now (free or chase)
static bool live_active(void)
{
    return fc.mode != CAM_OFF && cls.demo.playback && cls.state == ca_active;
}

// any detached camera: live, or played back from a recorded path
bool CL_FreecamActive(void)
{
    return live_active() || (path_driving && cls.demo.playback && cls.state == ca_active);
}

static bool free_active(void)
{
    return fc.mode == CAM_FREE && live_active();
}

static bool chase_active(void)
{
    return fc.mode == CAM_CHASE && live_active();
}

// true when the recorder's HUD (stats, layouts, crosshair) should be hidden
bool CL_FreecamHideHud(void)
{
    return CL_FreecamActive() && !freecam_hud->integer;
}

// shown on the timeline; NULL when not chasing
const char *CL_ChaseTargetName(void)
{
    const char *name;

    if (!chase_active())
        return NULL;

    name = cl.clientinfo[(fc.target - 1) & 0xff].name;
    if (!*name)
        name = va("jugador %d", fc.target);
    if (!fc.target_seen)
        return va("%s (fuera de vista)", name);
    return name;
}

bool CL_FreecamFlying(void)
{
    return free_active();
}

void CL_FreecamReset(void)
{
    fc.mode = CAM_OFF;
    fc.target = 0;
    VectorClear(fc.velocity);
}

// MVD playback runs through a local MVD server: camera control there is
// done with its own spectator commands
static bool mvd_playing(void)
{
#if USE_MVD_CLIENT
    return sv_running->integer == ss_broadcast && cls.state == ca_active;
#else
    return false;
#endif
}

static bool mvd_command(const char *cmd, const char *msg)
{
    if (!mvd_playing())
        return false;
    CL_ClientCommand(cmd);
    if (msg)
        SCR_CenterPrint(msg, false);
    return true;
}

static bool can_detach(void)
{
    if (!cls.demo.playback || cls.state != ca_active) {
        Com_Printf("La camara solo se puede mover al ver un demo .dm2.\n");
        return false;
    }
    return true;
}

static void normalize_view_angles(void)
{
    if (cl.viewangles[PITCH] > 180)
        cl.viewangles[PITCH] -= 360;
    cl.viewangles[PITCH] = Q_clipf(cl.viewangles[PITCH], -89, 89);
    cl.viewangles[ROLL] = 0;
}

static void set_mode(cammode_t mode)
{
    if (mode != CAM_OFF && fc.mode == CAM_OFF) {
        // start from wherever the view currently is
        VectorCopy(cl.refdef.vieworg, fc.origin);
        VectorCopy(cl.refdef.viewangles, cl.viewangles);
        normalize_view_angles();
    }

    if (mode == CAM_FREE)
        VectorClear(fc.velocity);

    fc.mode = mode;
    IN_Activate();  // grab or release the mouse
}

/*
===============================================================================

FREE CAMERA

===============================================================================
*/

static void Freecam_f(void)
{
    bool enable;

    if (mvd_command("observe", "MVD: camara libre / seguir jugador"))
        return;

    if (Cmd_Argc() > 1)
        enable = Q_atoi(Cmd_Argv(1));
    else
        enable = fc.mode != CAM_FREE;

    if (enable && !can_detach())
        return;

    // from chase mode the free camera starts where the chase camera was
    set_mode(enable ? CAM_FREE : CAM_OFF);
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

    normalize_view_angles();

    if (!free_active())
        return;     // chase camera only uses the mouse

    dt = msec * 0.001f;

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
===============================================================================

CHASE CAMERA

===============================================================================
*/

static bool is_player_entity(int num)
{
    const centity_t *ent;

    if (num < 1 || num > cl.maxclients)
        return false;   // bodies and other entities

    ent = &cl_entities[num];
    return ent->serverframe == cl.frame.number &&
           ent->current.modelindex == MODELINDEX_PLAYER;
}

// position of a player entity this frame, false if not in the demo now
static bool player_position(int num, vec3_t out, float *yaw)
{
    const centity_t *ent;

    if (!is_player_entity(num))
        return false;

    ent = &cl_entities[num];
    if (num == cl.frame.clientNum + 1) {
        VectorCopy(cl.playerEntityOrigin, out);
        *yaw = cl.playerEntityAngles[YAW];
    } else {
        LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, out);
        *yaw = LerpAngle(ent->prev.angles[YAW], ent->current.angles[YAW], cl.lerpfrac);
    }
    return true;
}

static void chase_cycle(int dir)
{
    int start, num, max_ent;
    vec3_t pos;
    float yaw;

    if (!can_detach())
        return;

    max_ent = cl.maxclients;
    if (max_ent < 1) {
        Com_Printf("No hay jugadores en este demo.\n");
        return;
    }

    start = chase_active() ? fc.target : (dir > 0 ? 0 : max_ent + 1);
    num = start;

    for (int i = 0; i < max_ent; i++) {
        num += dir;
        if (num > max_ent)
            num = 1;
        else if (num < 1)
            num = max_ent;

        if (player_position(num, pos, &yaw)) {
            set_mode(CAM_CHASE);
            fc.target = num;
            fc.target_seen = true;
            VectorCopy(pos, fc.target_pos);
            cl.viewangles[YAW] = yaw;
            cl.viewangles[PITCH] = 15;
            return;
        }
    }

    Com_Printf("No hay jugadores a la vista en este momento.\n"
               "(El demo solo trae a los jugadores que veia quien lo grabo.)\n");
    SCR_CenterPrint("No hay jugadores a la vista", false);
}

static void Chase_Next_f(void)
{
    if (!mvd_command("invnext", NULL))
        chase_cycle(1);
}

static void Chase_Prev_f(void)
{
    if (!mvd_command("invprev", NULL))
        chase_cycle(-1);
}

static void Camera_Reset_f(void)
{
    if (mvd_command("follow", NULL))
        return;
    set_mode(CAM_OFF);
}

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

static void chase_view(void)
{
    static const vec3_t mins = { -4, -4, -4 };
    static const vec3_t maxs = {  4,  4,  4 };
    static unsigned last_time;
    vec3_t focus, forward, wanted;
    trace_t trace;
    float yaw, dt;

    dt = (cls.realtime - last_time) * 0.001f;
    last_time = cls.realtime;
    dt = Q_clipf(dt, 0, 0.1f);

    fc.target_seen = player_position(fc.target, fc.target_pos, &yaw);

    // drift back behind the player unless the mouse is pointing elsewhere
    if (fc.target_seen && chase_auto->value > 0) {
        float delta = angle_delta(yaw, cl.viewangles[YAW]);
        cl.viewangles[YAW] += delta * (1.0f - expf(-dt / chase_auto->value));
    }

    VectorCopy(fc.target_pos, focus);
    focus[2] += chase_height->value;

    AngleVectors(cl.viewangles, forward, NULL, NULL);
    VectorMA(focus, -Cvar_ClampValue(chase_dist, 16, 1000), forward, wanted);

    // don't go through walls
    CL_Trace(&trace, focus, wanted, mins, maxs, CONTENTS_SOLID);
    VectorCopy(trace.endpos, fc.origin);
}

/*
===============
CL_FreecamView

Overrides the computed view with the detached camera. Called at the end of
CL_CalcViewValues, after the player entity position has been worked out.
===============
*/
void CL_FreecamView(void)
{
    vec3_t org, ang;

    path_driving = false;

    if (!cls.demo.playback || cls.state != ca_active)
        return;

    if (live_active()) {
        // the user is moving the camera
        if (fc.mode == CAM_CHASE)
            chase_view();
        VectorCopy(fc.origin, cl.refdef.vieworg);
        VectorCopy(cl.viewangles, cl.refdef.viewangles);
    } else if (CL_CamPathView(org, ang)) {
        // a recorded camera path covers this moment
        path_driving = true;
        VectorCopy(org, cl.refdef.vieworg);
        VectorCopy(ang, cl.refdef.viewangles);
    }

    if (CL_FreecamActive()) {
        AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

        // own field of view: ignore the recorder's zoom (sniper scope etc.)
        cl.fov_x = Cvar_ClampValue(freecam_fov, 10, 160);
        cl.fov_y = V_CalcFov(cl.fov_x, 4, 3);

        // the recorder's damage flashes and underwater tint don't belong here
        Vector4Clear(cl.refdef.screen_blend);
        Vector4Clear(cl.refdef.damage_blend);
    }

    CL_CamPathRecord(live_active());
}

static const cmdreg_t c_freecam[] = {
    { "freecam", Freecam_f },
    { "chase_next", Chase_Next_f },
    { "chase_prev", Chase_Prev_f },
    { "camera_reset", Camera_Reset_f },
    { NULL }
};

void CL_InitFreecam(void)
{
    freecam_speed = Cvar_Get("freecam_speed", "300", CVAR_ARCHIVE);
    freecam_fast = Cvar_Get("freecam_fast", "3", CVAR_ARCHIVE);
    freecam_smooth = Cvar_Get("freecam_smooth", "0.15", CVAR_ARCHIVE);
    freecam_hud = Cvar_Get("freecam_hud", "0", CVAR_ARCHIVE);
    freecam_fov = Cvar_Get("freecam_fov", "90", CVAR_ARCHIVE);
    chase_dist = Cvar_Get("chase_dist", "110", CVAR_ARCHIVE);
    chase_height = Cvar_Get("chase_height", "28", CVAR_ARCHIVE);
    chase_auto = Cvar_Get("chase_auto", "0.6", CVAR_ARCHIVE);

    Cmd_Register(c_freecam);
}
