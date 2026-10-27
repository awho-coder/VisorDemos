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
//   chase  - follow any player, in third person (from behind; the mouse
//            orbits around them and the camera eases back behind the player
//            on its own) or in first person (through their eyes)
// Both detached modes keep working while the demo is paused.
//
// F switches first / third person. The choice sticks: E / Q keep it when
// moving to another player, and leaving the free camera goes back to the
// player you were following. In first person the recorder is shown with the
// demo's own view (CAM_OFF), which has the real view angles and the gun.

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
    bool        third;      // follow players in third person (F)
    int         back;       // free camera: player to go back to, 0 = recorder
    float       eye_height; // first person: smoothed view height
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

static bool mvd_playing(void);

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

// following someone through their eyes
static bool eyes_active(void)
{
    return chase_active() && !fc.third;
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

/*
===============
spectator_target

The recorder of a DM2 was a spectator chasing someone: who? Game mods put
the chased player's skin configstring index in a stat (STAT_CHASE in baseq2,
another slot in D-Day), so look for a stat pointing at a player. Mods that
don't: the chased player's body is where the camera is.
===============
*/
static int chase_stat_index(void)
{
    const player_state_t *ps = &cl.frame.ps;

    for (int i = 16; i < MAX_STATS_OLD; i++) {
        int n = ps->stats[i] - cl.csr.playerskins;
        if (n >= 0 && n < cl.maxclients && n < MAX_CLIENTS && cl.clientinfo[n].name[0])
            return i;
    }
    return -1;
}

static int spectator_target(void)
{
    const player_state_t *ps = &cl.frame.ps;
    int best = -1;
    float bestd = 64;
    vec3_t org;
    int stat = chase_stat_index();

    if (stat >= 0)
        return ps->stats[stat] - cl.csr.playerskins;

    for (int k = 0; k < 3; k++)
        org[k] = SHORT2COORD(ps->pmove.origin[k]);

    for (int i = 1; i <= cl.maxclients; i++) {
        const centity_t *e = &cl_entities[i];
        float d;

        if (e->serverframe != cl.frame.number || e->current.modelindex != MODELINDEX_PLAYER)
            continue;
        d = Distance(org, e->current.origin);
        if (d < bestd) {
            bestd = d;
            best = i - 1;
        }
    }
    return best;
}

/*
===============
CL_SpectatorChaseStat

Stat the game uses to name the player a spectator recorder was chasing
(the HUD prints it with stat_string), or -1. The viewer shows its own label
instead, so the HUD block that prints it can be left out.
===============
*/
int CL_SpectatorChaseStat(void)
{
    viewkind_t kind;

    if (!CL_ViewedPlayer(&kind) || kind != VIEW_CHASED)
        return -1;
    return chase_stat_index();
}

/*
===============
CL_ChasedBodyToHide

The recorder was a spectator watching through another player's eyes: that
player's own body sits on the camera and fills the screen from the inside.
Returns its entity number so it is not drawn (0 = nothing to hide).
===============
*/
int CL_ChasedBodyToHide(void)
{
    const centity_t *e;
    vec3_t org;
    int n;

    // our first person camera sits inside the followed player
    if (eyes_active())
        return fc.target;

    if (!cls.demo.playback || cls.state != ca_active || CL_FreecamActive())
        return 0;
    if (cl.frame.ps.pmove.pm_type != PM_FREEZE)
        return 0;

    n = spectator_target();
    if (n < 0)
        return 0;

    e = &cl_entities[n + 1];
    if (e->serverframe != cl.frame.number)
        return 0;

    for (int k = 0; k < 3; k++)
        org[k] = SHORT2COORD(cl.frame.ps.pmove.origin[k]);

    // only "in the eyes" chasing; a camera behind the player keeps the body
    return Distance(org, e->current.origin) < 40 ? n + 1 : 0;
}

/*
===============
CL_ViewedPlayer

Nick of the player the view belongs to, and what kind of view it is:
VIEW_OWN    through their own eyes (the recorder, or the player a MVD chases)
VIEW_CHASED the recorder was a spectator chasing this player
VIEW_FOLLOW we follow them with the chase camera (E / Q)
VIEW_OBSERVER the recorder was a free-flying spectator (returns NULL)
NULL and VIEW_NONE with our free camera or a recorded path.
===============
*/
const char *CL_ViewedPlayer(viewkind_t *kind)
{
    const char *name;
    int num;

    *kind = VIEW_NONE;

    if (cls.state != ca_active || (!cls.demo.playback && !mvd_playing()))
        return NULL;

    if (chase_active()) {
        *kind = VIEW_FOLLOW;
        num = fc.target - 1;
    } else if (CL_FreecamActive()) {
        return NULL;
    } else if (mvd_playing()) {
        // flying around as a MVD spectator: not looking through anyone
        if (cl.frame.ps.pmove.pm_type == PM_SPECTATOR)
            return NULL;
        *kind = VIEW_OWN;
        num = cl.frame.clientNum;
    } else if (cl.frame.ps.pmove.pm_type == PM_SPECTATOR) {
        *kind = VIEW_OBSERVER;      // recorder flying around as a spectator
        return NULL;
    } else if (cl.frame.ps.pmove.pm_type == PM_FREEZE &&
               (num = spectator_target()) >= 0) {
        *kind = VIEW_CHASED;        // recorder chasing someone as a spectator
    } else {
        *kind = VIEW_OWN;
        num = cl.frame.clientNum;
    }

    if (num < 0 || num >= MAX_CLIENTS) {
        *kind = VIEW_NONE;
        return NULL;
    }
    name = cl.clientinfo[num].name;
    if (!*name) {
        *kind = VIEW_NONE;
        return NULL;
    }
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
    fc.back = 0;
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

static bool follow(int num);

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

    if (enable) {
        // remember who we were watching, to come back to them
        if (fc.mode != CAM_FREE)
            fc.back = chase_active() ? fc.target : 0;
        // from chase mode the free camera starts where the chase camera was
        set_mode(CAM_FREE);
        return;
    }

    if (fc.mode == CAM_FREE && fc.back && follow(fc.back))
        return;
    set_mode(CAM_OFF);
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

// the recorder was playing, not spectating: the demo view is their eyes
static bool recorder_playing(void)
{
    int t = cl.frame.ps.pmove.pm_type;

    return t != PM_SPECTATOR && t != PM_FREEZE;
}

// player the view belongs to now (entity number), 0 = nobody
static int current_player(void)
{
    int n;

    if (chase_active())
        return fc.target;
    if (fc.mode != CAM_OFF)
        return 0;
    if (recorder_playing())
        return cl.frame.clientNum + 1;
    if (cl.frame.ps.pmove.pm_type == PM_FREEZE && (n = spectator_target()) >= 0)
        return n + 1;   // the recorder was a spectator chasing this player
    return 0;
}

// eye height from the top of the player's bounding box (measured in demos:
// D-Day standing 16 -> 20, crouched or prone -16 -> -17; baseq2 standing
// 32 -> 22, crouched 4 -> -2). No box (dead...): keep the current height.
static float player_eye_height(int num, float current)
{
    const centity_t *ent = &cl_entities[num];
    float top = ent->maxs[2];

    if (top - ent->mins[2] < 1)
        return current;
    if (!Q_stricmp(Cvar_VariableString("game"), "dday"))
        return top >= 0 ? top + 4 : top - 1;
    return top >= 16 ? top - 10 : top - 6;
}

/*
===============
follow

Watch player `num` in the current perspective (fc.third). False when the
player isn't in the demo at this moment.
===============
*/
static bool follow(int num)
{
    vec3_t pos;
    float yaw;

    // the recorder in first person: the demo's own view
    if (!fc.third && num == cl.frame.clientNum + 1 && recorder_playing()) {
        set_mode(CAM_OFF);
        return true;
    }

    if (!player_position(num, pos, &yaw))
        return false;

    set_mode(CAM_CHASE);
    fc.target = num;
    fc.target_seen = true;
    VectorCopy(pos, fc.target_pos);
    fc.eye_height = player_eye_height(num, 20);
    cl.viewangles[YAW] = yaw;
    cl.viewangles[PITCH] = 15;
    return true;
}

static void chase_cycle(int dir)
{
    int start, num, max_ent;

    if (!can_detach())
        return;

    max_ent = cl.maxclients;
    if (max_ent < 1) {
        Com_Printf("No hay jugadores en este demo.\n");
        return;
    }

    start = current_player();
    if (!start && dir < 0)
        start = max_ent + 1;
    num = start;

    for (int i = 0; i < max_ent; i++) {
        num += dir;
        if (num > max_ent)
            num = 1;
        else if (num < 1)
            num = max_ent;

        if (follow(num))
            return;
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

/*
===============
Camera_View_f

camera_view [1 | 3]: first or third person when watching a player (F).
===============
*/
static void Camera_View_f(void)
{
    bool old = fc.third;
    const char *msg;
    int num;

    if (Cmd_Argc() > 1)
        fc.third = Q_atoi(Cmd_Argv(1)) == 3;
    else
        fc.third = !fc.third;
    msg = fc.third ? "Tercera persona" : "Primera persona";

    // MVD: the view of the followed player comes from the local MVD
    // server; CL_MvdChaseView moves it behind them
    if (mvd_playing()) {
        SCR_CenterPrint(msg, false);
        return;
    }

    if (!can_detach()) {
        fc.third = old;
        return;
    }

    num = current_player();
    if (!num) {
        // free camera, or nobody to look at: used when following someone
        SCR_CenterPrint(va("%s al seguir jugadores (E / Q)", msg), false);
        return;
    }

    if (!follow(num)) {
        fc.third = old;
        SCR_CenterPrint("El jugador no esta a la vista", false);
        return;
    }
    SCR_CenterPrint(msg, false);
}

/*
===============
Camera_Speed_f

camera_speed up | down | reset | <value>
Free camera: flying speed. Chase camera: distance to the player (the mouse
wheel is bound to this, so it "zooms" when following someone).
===============
*/
static const float camera_speeds[] = { 50, 100, 150, 200, 300, 450, 650, 900, 1300, 2000 };
static const float chase_dists[] = { 40, 60, 80, 110, 150, 200, 260, 340, 450 };

static float step_value(const float *steps, int n, float cur, int dir)
{
    int i;

    if (dir > 0) {
        for (i = 0; i < n - 1 && steps[i] <= cur + 0.5f; i++)
            ;
    } else {
        for (i = n - 1; i > 0 && steps[i] >= cur - 0.5f; i--)
            ;
    }
    return steps[i];
}

static void Camera_Speed_f(void)
{
    const char *arg = Cmd_Argv(1);
    bool chase = (chase_active() && fc.third) || CL_MvdThirdPerson();
    cvar_t *var = chase ? chase_dist : freecam_speed;
    const float *steps = chase ? chase_dists : camera_speeds;
    int n = chase ? q_countof(chase_dists) : q_countof(camera_speeds);
    float value;

    if (!*arg) {
        Com_Printf("Velocidad de la camara libre: %g\nDistancia al seguir jugadores: %g\n"
                   "Uso: %s up | down | reset | <valor>\n",
                   freecam_speed->value, chase_dist->value, Cmd_Argv(0));
        return;
    }

    if (eyes_active()) {
        SCR_CenterPrint("Primera persona (F: tercera persona)", false);
        return;
    }

    if (!Q_stricmp(arg, "up"))
        value = step_value(steps, n, var->value, 1);
    else if (!Q_stricmp(arg, "down"))
        value = step_value(steps, n, var->value, -1);
    else if (!Q_stricmp(arg, "reset"))
        value = Q_atof(var->default_string);
    else
        value = Q_atof(arg);

    value = Q_clipf(value, chase ? 16 : 1, chase ? 1000 : 10000);
    Cvar_SetValue(var, value, FROM_CONSOLE);

    if (chase)
        SCR_CenterPrint(va("Distancia de la camara: %g", value), false);
    else
        SCR_CenterPrint(va("Velocidad de la camara: %g", value), false);
}

// shown on the timeline next to the camera mode
float CL_FreecamSpeed(void)
{
    return freecam_speed->value;
}

float CL_ChaseDistance(void)
{
    return chase_dist->value;
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
eyes_view

First person of a player other than the recorder. The demo only has their
entity: position, yaw, and pitch / 3 (the game stores a third of the view
pitch so the model doesn't bend too much). No gun: their weapon frames
aren't in the demo.
===============
*/
static void eyes_view(void)
{
    static unsigned last_time;
    const centity_t *ent = &cl_entities[fc.target];
    float yaw, pitch, dt;

    dt = (cls.realtime - last_time) * 0.001f;
    last_time = cls.realtime;
    dt = Q_clipf(dt, 0, 0.1f);

    fc.target_seen = player_position(fc.target, fc.target_pos, &yaw);
    if (fc.target_seen) {
        pitch = LerpAngle(ent->prev.angles[PITCH], ent->current.angles[PITCH], cl.lerpfrac);
        if (pitch > 180)
            pitch -= 360;
        cl.viewangles[PITCH] = Q_clipf(pitch * 3, -89, 89);
        cl.viewangles[YAW] = yaw;
        cl.viewangles[ROLL] = 0;

        // crouching / standing up: ease the height like the game does
        fc.eye_height += (player_eye_height(fc.target, fc.eye_height) - fc.eye_height) *
                         (1.0f - expf(-dt / 0.1f));
    }

    VectorCopy(fc.target_pos, fc.origin);
    fc.origin[2] += fc.eye_height;
}

// shown on the timeline
bool CL_ChaseFirstPerson(void)
{
    return eyes_active();
}

/*
===============
CL_MvdThirdPerson / CL_MvdChaseView

MVD: following a player goes through the local MVD server, which sends the
player's own first person view. In third person the client moves that view
behind them, turning smoothly after where they look.
===============
*/
bool CL_MvdThirdPerson(void)
{
    return fc.third && mvd_playing() && cl.frame.ps.pmove.pm_type != PM_SPECTATOR;
}

void CL_MvdChaseView(void)
{
    static const vec3_t mins = { -4, -4, -4 };
    static const vec3_t maxs = {  4,  4,  4 };
    static vec3_t   angles;
    static int      last_client = -1;
    static unsigned last_time;
    vec3_t focus, forward, wanted;
    trace_t trace;
    float pitch, frac, dt;

    dt = (cls.realtime - last_time) * 0.001f;
    last_time = cls.realtime;

    pitch = cl.refdef.viewangles[PITCH];
    if (pitch > 180)
        pitch -= 360;
    pitch = Q_clipf(pitch * 0.5f + 10, -60, 70);

    if (cl.frame.clientNum != last_client || dt > 0.5f) {
        // another player, or the first frame: no easing
        angles[PITCH] = pitch;
        angles[YAW] = cl.refdef.viewangles[YAW];
        last_client = cl.frame.clientNum;
    } else {
        frac = 1.0f - expf(-dt / 0.15f);
        angles[PITCH] += (pitch - angles[PITCH]) * frac;
        angles[YAW] += angle_delta(cl.refdef.viewangles[YAW], angles[YAW]) * frac;
    }
    angles[ROLL] = 0;

    VectorCopy(cl.playerEntityOrigin, focus);
    focus[2] += chase_height->value;

    AngleVectors(angles, forward, NULL, NULL);
    VectorMA(focus, -Cvar_ClampValue(chase_dist, 16, 1000), forward, wanted);
    CL_Trace(&trace, focus, wanted, mins, maxs, CONTENTS_SOLID);

    VectorCopy(trace.endpos, cl.refdef.vieworg);
    VectorCopy(angles, cl.refdef.viewangles);
    AngleVectors(angles, cl.v_forward, cl.v_right, cl.v_up);

    // own field of view: no sniper zoom from behind the player
    cl.fov_x = Cvar_ClampValue(freecam_fov, 10, 160);
    cl.fov_y = V_CalcFov(cl.fov_x, 4, 3);

    cl.thirdPersonView = true;
    cl.thirdPersonAlpha = 1.0f;
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
        if (fc.mode == CAM_CHASE && fc.third)
            chase_view();
        else if (fc.mode == CAM_CHASE)
            eyes_view();
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
    { "camera_speed", Camera_Speed_f },
    { "camera_view", Camera_View_f },
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
