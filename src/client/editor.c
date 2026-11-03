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

// editor.c -- mouse driven editor mode for demo playback (Keygrip style)
//
// Tab (visor_editor) swaps the keyboard timeline for a panel at the bottom:
// the timeline, a fixed row with playback, and tabs (CAMARA, PUNTOS, VER,
// VIDEO, AJUSTES) with the rest of the buttons. Two side panels sit on the
// image: on the left a list of camera points or players, on the right the
// settings of the point here (or of the player followed). The mouse cursor
// stays free: a click on a button runs the same command as its key, a click
// or drag on the timeline seeks, a click on a camera point jumps to it.
// Holding the right mouse button flies the camera (WASD + mouse) until it is
// released. All of it is drawn after the frame is captured, so it never
// shows up in video clips.

#include "client.h"

#define ED_MAX_ITEMS    256

typedef enum {
    IT_BUTTON,
    IT_ON,          // a button for something that is switched on
    IT_RED,         // record
    IT_OFF,         // not available now
    IT_LABEL,       // group name
    IT_VALUE,       // the number between - and +
    IT_TAB,         // a tab
    IT_TAB_ON,      // the open tab
    IT_ROW,         // a line of a side list
    IT_ROW_ON,      // the line of what is here / followed now
    IT_TEXT,        // plain text in a side panel
    IT_TITLE        // title of a side panel
} itemkind_t;

typedef struct {
    int         x, y, w, h;
    itemkind_t  kind;
    uint32_t    color;      // text color of a list line, 0 = default
    char        label[96];
    char        cmd[64];
    char        tip[128];
} item_t;

typedef struct {
    int x, y, w, h;     // w = 0: not shown
} rect_t;

static struct {
    // mouse, in screen pixels
    int         mx, my;
    bool        flying;         // right button held
    bool        dragging;       // seeking on the timeline
    bool        clicked_panel;  // left button went down on the panel
    float       drag_frac;
    unsigned    last_seek;
    unsigned    confirm_until;  // "BORRAR TODO" asks for a second click

    // layout of the last drawn frame, for hit testing
    item_t      items[ED_MAX_ITEMS];
    int         num_items;
    int         panel_y;        // top of the panel, 0 = no panel
    rect_t      side[2];        // left list, right settings
    int         tl_x, tl_y, tl_w, tl_h;     // timeline track
    float       key_pos[256];
    int         num_keys;

    // drawing
    qhandle_t   font;
    float       scale;
    int         cw, ch;         // character size in pixels
} ed;

static cvar_t   *visor_editor;
static cvar_t   *visor_editor_size;
static cvar_t   *visor_editor_tab;
static cvar_t   *visor_editor_list;
static cvar_t   *visor_editor_sides;

enum { TAB_CAMERA, TAB_POINTS, TAB_VIEW, TAB_VIDEO, TAB_SETTINGS, NUM_TABS };

#define COLOR_PANEL     MakeColor( 16,  17,  22, 215)
#define COLOR_SIDE      MakeColor( 16,  17,  22, 200)
#define COLOR_BTN       MakeColor( 58,  60,  72, 240)
#define COLOR_BTN_HOVER MakeColor( 92,  96, 116, 250)
#define COLOR_BTN_ON    MakeColor(  0, 118, 135, 250)
#define COLOR_BTN_ON_H  MakeColor(  0, 150, 170, 255)
#define COLOR_BTN_RED   MakeColor(165,  30,  30, 250)
#define COLOR_BTN_RED_H MakeColor(205,  45,  45, 255)
#define COLOR_BTN_OFF   MakeColor( 38,  39,  46, 230)
#define COLOR_VALUE     MakeColor( 28,  29,  36, 250)
#define COLOR_TAB_ON    MakeColor( 30,  32,  42, 255)
#define COLOR_TEXT      MakeColor(240, 240, 240, 255)
#define COLOR_TEXT_OFF  MakeColor(115, 115, 125, 255)
#define COLOR_LABEL     MakeColor(150, 155, 170, 255)
#define COLOR_TIP       MakeColor(255, 200,  90, 255)
#define COLOR_TRACK     MakeColor( 60,  62,  70, 255)
#define COLOR_PLAYED    MakeColor(170, 172, 180, 255)
#define COLOR_CLIP      MakeColor(255, 170,   0, 140)
#define COLOR_IN        MakeColor( 60, 220,  60, 255)
#define COLOR_OUT       MakeColor(230,  60,  60, 255)
#define COLOR_HEAD      MakeColor(255, 255, 255, 255)
#define COLOR_HOVER     MakeColor(255, 255, 255, 110)
#define COLOR_PATH      MakeColor( 60, 140, 255, 255)
#define COLOR_FRAG      MakeColor(255, 210,   0, 255)
#define COLOR_DEATH     MakeColor(255,  45,  45, 255)
#define COLOR_KEY       MakeColor(  0, 220, 230, 255)
#define COLOR_KEY_HERE  MakeColor(255, 255, 255, 255)
#define COLOR_EDGE      MakeColor(  0,   0,   0, 255)
#define COLOR_SLOW      MakeColor(180, 108, 255, 200)
#define COLOR_FAST      MakeColor(255, 150,  60, 200)
#define COLOR_PROJ      MakeColor(255, 122,  60, 255)
#define COLOR_ALLIES    MakeColor( 90, 160, 255, 255)
#define COLOR_AXIS      MakeColor(255,  90,  90, 255)

/*
===============================================================================

STATE

===============================================================================
*/

// the editor is on and there is a demo to edit
bool CL_EditorActive(void)
{
    return visor_editor && visor_editor->integer &&
           cls.state == ca_active && CL_DemoPosition(NULL, NULL, NULL);
}

// clicks do something (not while a clip is being recorded)
static bool interactive(void)
{
    return CL_EditorActive() && !CL_CaptureActive() &&
           !(cls.key_dest & (KEY_MENU | KEY_CONSOLE | KEY_MESSAGE));
}

bool CL_EditorFlying(void)
{
    return ed.flying && CL_EditorActive();
}

// height of the panel at the bottom, in pixels (0 = none)
int CL_EditorPanelHeight(void)
{
    if (!CL_EditorActive() || !ed.panel_y)
        return 0;
    return r_config.height - ed.panel_y;
}

static bool in_rect(const rect_t *r, int x, int y)
{
    return r->w > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static bool over_panel(void)
{
    return ed.panel_y && ed.my >= ed.panel_y;
}

// on the panel or a side panel: clicks are for the editor, not the image
static bool over_ui(void)
{
    return over_panel() || in_rect(&ed.side[0], ed.mx, ed.my) || in_rect(&ed.side[1], ed.mx, ed.my);
}

static bool clickable(const item_t *it)
{
    return it->cmd[0] && it->kind != IT_OFF && it->kind != IT_LABEL &&
           it->kind != IT_VALUE && it->kind != IT_TEXT && it->kind != IT_TITLE;
}

static const item_t *item_at(int x, int y)
{
    for (int i = 0; i < ed.num_items; i++) {
        const item_t *it = &ed.items[i];
        if (it->kind != IT_LABEL && x >= it->x && x < it->x + it->w &&
            y >= it->y && y < it->y + it->h)
            return it;
    }
    return NULL;
}

static bool over_timeline(int pad)
{
    return ed.tl_w > 0 && ed.mx >= ed.tl_x - pad && ed.mx < ed.tl_x + ed.tl_w + pad &&
           ed.my >= ed.tl_y - pad && ed.my < ed.tl_y + ed.tl_h + pad;
}

static float timeline_frac(int x)
{
    return Q_clipf((float)(x - ed.tl_x) / max(ed.tl_w, 1), 0, 1);
}

// camera point under the mouse on the timeline, -1 if none
static int key_at_mouse(void)
{
    int best = -1, best_d = max(ed.ch / 2, 6) + 1;

    if (!over_timeline(ed.ch / 2))
        return -1;
    for (int i = 0; i < ed.num_keys; i++) {
        int d = abs(ed.tl_x + Q_rint(ed.tl_w * ed.key_pos[i]) - ed.mx);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/*
===============================================================================

INPUT

===============================================================================
*/

// a button's command, next in line (ahead of whatever is queued)
static void run(const char *cmd)
{
    Cbuf_InsertText(&cmd_buffer, va("%s\n", cmd));
}

static void seek_to(float frac)
{
    run(va("seek %.3f%%", frac * 100));
    ed.last_seek = cls.realtime;
}

static void stop_flying(void)
{
    if (!ed.flying)
        return;
    ed.flying = false;
    IN_Activate();
}

int CL_EditorMouseX(void)
{
    return ed.mx;
}

int CL_EditorMouseY(void)
{
    return ed.my;
}

// mouse moved (absolute position, window pixels); called by the video code
// through UI_MouseEvent whenever the mouse isn't grabbed
void CL_EditorMouseMove(int x, int y)
{
    ed.mx = Q_clip(x, 0, r_config.width - 1);
    ed.my = Q_clip(y, 0, r_config.height - 1);

    if (ed.dragging && interactive()) {
        ed.drag_frac = timeline_frac(ed.mx);
        // seeking is not free: keep up with the mouse a few times a second
        if (cls.realtime - ed.last_seek > 120)
            seek_to(ed.drag_frac);
    }
}

/*
===============
CL_EditorKeyEvent

Mouse buttons in editor mode. True when the editor used the event.
===============
*/
bool CL_EditorKeyEvent(unsigned key, bool down)
{
    // let go of the buttons even if the editor was switched off meanwhile
    if (!down) {
        if (key == K_MOUSE2 && ed.flying) {
            stop_flying();
            return true;
        }
        if (key == K_MOUSE1 && (ed.dragging || ed.clicked_panel)) {
            if (ed.dragging && interactive())
                seek_to(ed.drag_frac);
            ed.dragging = ed.clicked_panel = false;
            return true;
        }
    }

    // recording: only the stop button answers
    if (CL_CaptureActive() && CL_EditorActive() && key == K_MOUSE1 && down &&
        !(cls.key_dest & (KEY_MENU | KEY_CONSOLE | KEY_MESSAGE))) {
        const item_t *it = over_ui() ? item_at(ed.mx, ed.my) : NULL;

        if (it && !strcmp(it->cmd, "clip_stop"))
            run(it->cmd);
        ed.clicked_panel = true;
        return true;
    }

    if (!interactive())
        return false;

    switch (key) {
    case K_MOUSE1:
        if (!down)
            return true;    // a click that started on the 3D view
        if (over_ui()) {
            const item_t *it = item_at(ed.mx, ed.my);
            int k;

            ed.clicked_panel = true;
            if (it && clickable(it)) {
                if (strncmp(it->cmd, "editor_", 7))
                    ed.confirm_until = 0;   // any other button cancels "BORRAR TODO"
                run(it->cmd);
            } else if ((k = key_at_mouse()) >= 0) {
                run(va("keycam_goto %d", k + 1));
            } else if (over_timeline(ed.ch / 2)) {
                ed.dragging = true;
                ed.drag_frac = timeline_frac(ed.mx);
                seek_to(ed.drag_frac);
            }
        } else {
            CL_EditorClickView(ed.mx, ed.my);
        }
        return true;        // never fire the game's +attack

    case K_MOUSE2:
        if (!down)
            return true;
        ed.flying = true;
        // following a projectile the mouse turns around it; otherwise fly
        if (cls.demo.playback && !CL_FreecamFlying() && CL_FreecamProj() < 0)
            run("freecam 1");
        IN_Activate();      // grab the mouse to look around
        return true;

    case K_MWHEELUP:
    case K_MWHEELDOWN:
        return over_ui();   // on the 3D view: camera speed, as usual

    default:
        return false;
    }
}

static void editor_changed(cvar_t *self)
{
    stop_flying();
    ed.dragging = ed.clicked_panel = false;
    IN_Activate();
}

/*
===============================================================================

LAYOUT

Buttons flow left to right and wrap. The bottom panel is laid out from its
own top and moved down once its height is known; the side panels use the
same flow inside their own box.

===============================================================================
*/

static struct {
    int x, y, row_h, gap, margin, right;
    int indent;     // where a wrapped line starts (after the group names)
} lay;

static item_t *add_sized(itemkind_t kind, const char *label, const char *cmd, const char *tip, int w)
{
    item_t *it;

    if (ed.num_items == ED_MAX_ITEMS)
        return NULL;

    if (lay.x > lay.indent && lay.x + w > lay.right) {
        // no room left on this line: wrap
        lay.x = lay.indent;
        lay.y += lay.row_h + lay.gap;
    }

    it = &ed.items[ed.num_items++];
    memset(it, 0, sizeof(*it));
    it->x = lay.x;
    it->y = lay.y;
    it->w = w;
    it->h = lay.row_h;
    it->kind = kind;
    Q_strlcpy(it->label, label, sizeof(it->label));
    if (cmd)
        Q_strlcpy(it->cmd, cmd, sizeof(it->cmd));
    if (tip)
        Q_strlcpy(it->tip, tip, sizeof(it->tip));
    lay.x += w + lay.gap;
    return it;
}

static item_t *add_item(itemkind_t kind, const char *label, const char *cmd, const char *tip)
{
    int w = strlen(label) * ed.cw + (kind == IT_LABEL || kind == IT_TEXT ? 0 : ed.cw * 2);
    return add_sized(kind, label, cmd, tip, w);
}

static void button(const char *label, const char *cmd, const char *tip, itemkind_t kind)
{
    add_item(kind, label, cmd, tip);
}

// group name; at the start of a row the names line up in a column
#define LABEL_CHARS 12

static void group(const char *name)
{
    item_t *it;

    if (lay.x > lay.margin)
        lay.x += lay.gap * 4;
    it = add_item(IT_LABEL, name, NULL, NULL);
    if (it && it->x == lay.margin) {
        lay.x = lay.indent = lay.margin + LABEL_CHARS * ed.cw + lay.gap;
        it->w = LABEL_CHARS * ed.cw;
    } else if (it) {
        lay.x += ed.cw;
    }
}

static void new_row(void)
{
    if (lay.x > lay.margin) {
        lay.x = lay.indent = lay.margin;
        lay.y += lay.row_h + lay.gap;
    }
}

static itemkind_t on_if(bool on)
{
    return on ? IT_ON : IT_BUTTON;
}

// [-] value [+]: down / up are the commands of the two buttons
static void value(const char *name, const char *shown, const char *down, const char *up,
                  const char *tip, itemkind_t kind)
{
    if (name)
        group(name);
    // keep the three together on one line
    if (lay.x > lay.indent && lay.x + ed.cw * 14 + lay.gap * 2 > lay.right) {
        lay.x = lay.indent;
        lay.y += lay.row_h + lay.gap;
    }
    add_item(kind == IT_OFF ? IT_OFF : IT_BUTTON, "-", down, tip);
    add_sized(IT_VALUE, shown, NULL, tip, ed.cw * 8);
    add_item(kind == IT_OFF ? IT_OFF : IT_BUTTON, "+", up, tip);
}

// - / + for a setting: visor_step <cvar> <step> <min> <max>
static void setting(const char *name, const char *cvar, float step, float lo, float hi,
                    const char *shown, const char *tip)
{
    char down[64], up[64];

    Q_snprintf(down, sizeof(down), "visor_step %s %g %g %g", cvar, -step, lo, hi);
    Q_snprintf(up, sizeof(up), "visor_step %s %g %g %g", cvar, step, lo, hi);
    if (!shown)
        shown = va("%g", Cvar_VariableValue(cvar));
    value(name, shown, down, up, tip, IT_BUTTON);
}

// kill feed and chat, on screen or in the videos (each has its own setting)
static void feed_buttons(bool clip)
{
    int kills = Cvar_VariableInteger(clip ? "clip_killfeed" : "visor_killfeed");
    int chat = Cvar_VariableInteger(clip ? "clip_chat" : "visor_chat");

    button(kills == 2 ? "Muertes: jugador" : "Muertes",
           clip ? "feed_toggle killfeed clip" : "feed_toggle killfeed",
           clip ? "Kill feed en los videos: si / solo el jugador / no (en pantalla no cambia)"
                : "Kill feed en pantalla: si / solo el jugador / no (en el video no cambia)  [K]",
           on_if(kills));
    button(chat == 2 ? "Chat + avisos" : "Chat",
           clip ? "feed_toggle chat clip" : "feed_toggle chat",
           clip ? "Chat en los videos: si / con avisos del servidor / no (en pantalla no cambia)"
                : "Chat en pantalla: si / con avisos del servidor / no (en el video no cambia)  [J]",
           on_if(chat));
}

// the same switches on screen and in the videos, in the same order
static void overlay_buttons(bool clip)
{
    button("Nombres", clip ? "names_toggle clip" : "names_toggle",
           clip ? "Nicks sobre los jugadores en los videos (en pantalla no cambia)"
                : "Nicks sobre los jugadores en pantalla (en el video no cambia)  [Y]",
           on_if(CL_NamesOn(clip)));
    feed_buttons(clip);
    button("HUD", clip ? "toggle clip_hud" : "toggle freecam_hud",
           clip ? "HUD del juego (vida, municion) en los videos"
                : "HUD del juego en pantalla con la camara libre o siguiendo a alguien",
           on_if(Cvar_VariableInteger(clip ? "clip_hud" : "freecam_hud")));
    button("Nick arriba", clip ? "toggle clip_name" : "toggle visor_name",
           clip ? "Nick del jugador que ves, arriba, en los videos"
                : "Nick del jugador que ves, arriba en la pantalla",
           on_if(Cvar_VariableInteger(clip ? "clip_name" : "visor_name")));
}

static void tab_camera(bool mvd)
{
    button("Libre", "freecam", "Camara libre: volar por el mapa. Tambien: clic derecho mantenido  [flecha arriba]",
           on_if(CL_FreecamFlying()));
    button("< Jugador", "chase_prev", "Seguir al jugador anterior  [Q]", IT_BUTTON);
    button("Jugador >", "chase_next", "Seguir al siguiente jugador  [E]", IT_BUTTON);
    button("1a / 3a", "camera_view", "Primera / tercera persona al seguir a alguien  [F]", on_if(CL_ChaseFirstPerson()));
    button("Original", "camera_reset", "Volver a la vista de quien grabo el demo  [R]", IT_BUTTON);
    group("PROYECTILES");
    button("< Proyectil", "proj_prev", "Ir a la granada, cohete o cuchillo anterior y seguirlo", mvd ? IT_OFF : IT_BUTTON);
    button("Seguir proyectil", "follow",
           "Seguir el proyectil del centro de la pantalla. Tambien: clic sobre el en la imagen  [5]",
           mvd ? IT_OFF : on_if(CL_FreecamProj() >= 0));
    button("Proyectil >", "proj_next", "Ir a la siguiente granada, cohete o cuchillo lanzado y seguirlo", mvd ? IT_OFF : IT_BUTTON);
    button("Angulo: atras", "follow_angle",
           "La camara vuelve justo detras del proyectil y un poco arriba (clic derecho mantenido la gira)",
           CL_FreecamProj() >= 0 ? IT_BUTTON : IT_OFF);
    new_row();

    setting("DISTANCIA", "chase_dist", 10, 16, 1000, NULL,
            "Distancia de la camara al seguir a un jugador en tercera persona (tambien la rueda del mouse)");
    setting("ALTURA", "chase_height", 4, -64, 128, NULL,
            "Altura de la camara al seguir a un jugador en tercera persona");
    setting("FOV", "freecam_fov", 5, 10, 160, NULL,
            "Campo de vision de la camara libre: menos = mas zoom");
    setting("VUELO", "freecam_speed", 50, 50, 3000, NULL,
            "Velocidad de la camara libre (tambien la rueda del mouse)");
    setting("PROYECTIL", "follow_dist", 10, 16, 1000, NULL,
            "Distancia de la camara al seguir un proyectil");
}

static void point_buttons(bool mvd, int here, bool line)
{
    button(line ? "Recta" : "Curva", "keycam_mode",
           "Del punto al siguiente: curva suave o linea recta  [M]", here && !mvd ? IT_BUTTON : IT_OFF);
    button("Mirar jugador", "keycam_target",
           "El punto mira al jugador del centro de la pantalla / mirada libre  [L]",
           mvd ? IT_OFF : on_if(here == 2));
    button("Borrar punto", "keycam_delete", "Borrar el punto de camara de aqui  [U]",
           here && !mvd ? IT_BUTTON : IT_OFF);
}

static void tab_points(bool mvd)
{
    bool line;
    int here = CL_KeyCamHere(&line);
    itemkind_t dm2 = mvd ? IT_OFF : IT_BUTTON;

    button(here ? "Actualizar punto" : "+ Punto", "keycam_add",
           "Poner un punto de camara aqui con lo que ves (o actualizar el de aqui)  [P]", dm2);
    button("< Punto", "keycam_prev", "Ir al punto anterior  [1]", dm2);
    button("Punto >", "keycam_next", "Ir al punto siguiente  [2]", dm2);
    button("Usar camara", "campath_play", "Usar los puntos y el recorrido grabado si / no  [V]",
           mvd ? IT_OFF : on_if(CL_KeyCamPlaying() || CL_CamPathPlaying()));
    button(CL_CamPathRecording() ? "Terminar" : "Grabar recorrido", "campath_record",
           "Grabar la camara mientras corre el demo / terminar  [C]",
           mvd ? IT_OFF : CL_CamPathRecording() ? IT_RED : IT_BUTTON);
    button("Ver recorrido", "toggle keycam_show",
           "Dibujar en el mapa la linea del recorrido y una marca donde mira cada punto (no sale en el video)",
           mvd ? IT_OFF : on_if(Cvar_VariableInteger("keycam_show")));

    bool have = !mvd && (CL_KeyCamCount() > 0 || CL_CamPathHave());
    if (!have)
        ed.confirm_until = 0;
    if (have && cls.realtime < ed.confirm_until)
        button("SI, BORRAR TODO", "camera_clear",
               "Clic otra vez para borrar todos los puntos y el recorrido de este demo", IT_RED);
    else
        button("BORRAR TODO", "editor_clear_ask",
               "Borrar todos los puntos de camara y el recorrido grabado de este demo (pide confirmar)",
               have ? IT_RED : IT_OFF);
    new_row();

    if (!visor_editor_sides->integer) {
        // the point's settings are in the right panel; without it, here
        group("ESTE PUNTO");
        point_buttons(mvd, here, line);
    } else if (mvd) {
        add_item(IT_TEXT, "Los puntos de camara funcionan con demos .dm2", NULL, NULL);
    } else {
        add_item(IT_TEXT, "Lista de puntos a la izquierda, ajustes del punto a la derecha", NULL, NULL);
    }
}

static void tab_view(void)
{
    group("EN PANTALLA");
    button("X-ray", "visor_xray", "Siluetas de los jugadores detras de las paredes  [T]",
           on_if(Cvar_VariableInteger("xray")));
    overlay_buttons(false);
}

static const char *quality_text(void)
{
    int q = Cvar_VariableInteger("clip_quality");

    return va("%s %d", q <= 18 ? "alta" : q <= 26 ? "media" : "baja", q);
}

static void tab_video(void)
{
    group("CLIP");
    button("Inicio", "clip_in", "Marcar el inicio del clip aqui  [I]", IT_BUTTON);
    button("Fin", "clip_out", "Marcar el fin del clip aqui  [O]", IT_BUTTON);
    button("Borrar marcas", "clip_clear", "Borrar las marcas de inicio y fin  [Supr]", IT_BUTTON);
    button("GRABAR CLIP", "clip_export", "Grabar el clip a video, de la marca de inicio a la de fin  [G]", IT_RED);
    button("DEMO COMPLETO", "clip_export_all",
           "Grabar todo el demo a video, del principio al final, sin marcar inicio ni fin", IT_RED);
    new_row();

    group("EN EL VIDEO");
    overlay_buttons(true);
    value("CALIDAD", quality_text(), "visor_step clip_quality 2 0 51", "visor_step clip_quality -2 0 51",
          "Calidad del video: mas alta = archivo mas grande (clip_quality, menos = mejor)", IT_BUTTON);
    setting("FPS", "clip_fps", 10, 10, 240, NULL, "Cuadros por segundo del video");
}

static void tab_settings(void)
{
    setting("PANEL", "visor_editor_size", 0.1f, 0.5f, 3, NULL, "Tamano de la letra del editor");
    setting("KILL FEED", "visor_feed_size", 0.1f, 0.5f, 3, NULL, "Tamano de la letra del kill feed y del chat");
    setting("MOUSE", "sensitivity", 0.5f, 0.5f, 30, NULL, "Sensibilidad del mouse al mover la camara");
    new_row();

    setting("NOMBRES MIN", "names_min", 1, 4, 64, NULL, "Tamano de los nombres lejos (pixeles en 1080p)");
    setting("MAX", "names_max", 1, 4, 96, NULL, "Tamano de los nombres cerca (pixeles en 1080p)");
    new_row();

    setting("X-RAY", "xray_alpha", 0.05f, 0.1f, 1, NULL, "Opacidad de las siluetas del x-ray");
    button(Q_stricmp(Cvar_VariableString("xray_color"), "team") ? "Color: fijo" : "Color: por equipo",
           "editor_xray_color", "Color de las siluetas: por equipo (azul / rojo) o uno solo", IT_BUTTON);
    group("");
    button("Paneles a los lados", "toggle visor_editor_sides",
           "Lista de puntos / jugadores a la izquierda y ajustes del punto a la derecha", on_if(visor_editor_sides->integer));
}

// the fixed row: playback, speed, volume; menu buttons at the right end
static void fixed_row(void)
{
    bool paused = false;
    float speed = CL_DemoSpeed();
    int first;

    CL_DemoPosition(NULL, NULL, &paused);

    button("-10 s", "demo_step -", "Retroceder 10 segundos  [flecha izquierda]", IT_BUTTON);
    button("< Frag", "frag_prev", "Frag o muerte anterior del jugador que ves  [B]", IT_BUTTON);
    button(paused ? "> Seguir" : "|| Pausa", "pause", "Pausa / seguir  [flecha abajo]", on_if(paused));
    button("Frag >", "frag_next", "Siguiente frag o muerte del jugador que ves  [N]", IT_BUTTON);
    button("+10 s", "demo_step +", "Adelantar 10 segundos  [flecha derecha]", IT_BUTTON);
    group("VELOCIDAD");
    button("-", "demo_speed down", "Mas lento, camara lenta  [Z]", IT_BUTTON);
    button(va("x%g", speed), "demo_speed reset", "Velocidad normal  [Retroceso]", on_if(speed != 1.0f));
    button("+", "demo_speed up", "Mas rapido  [X]", IT_BUTTON);
    value("VOLUMEN", va("%d%%", Q_rint(Cvar_VariableValue("s_volume") * 100)),
          "visor_volume down", "visor_volume up", "Volumen  [, y .]", IT_BUTTON);

    first = ed.num_items;
    lay.x += lay.gap * 4;
    button("Abrir demo", "pushmenu demos", "Elegir otro demo  [Esc]", IT_BUTTON);
    button("Ayuda", "visor_help", "Teclas y ajustes  [H]", on_if(CL_VisorHelpVisible()));
    button("Ocultar editor", "toggle visor_editor", "Volver a la barra simple y al teclado  [Tab]", IT_BUTTON);

    // push them to the right end when they fit on the same line
    if (first < ed.num_items) {
        item_t *a = &ed.items[first], *b = &ed.items[ed.num_items - 1];
        int dx = lay.right - (b->x + b->w);
        if (a->y == b->y && dx > 0)
            for (int i = first; i < ed.num_items; i++)
                ed.items[i].x += dx;
    }
    new_row();
}

static const char *const tab_names[NUM_TABS] = { "CAMARA", "PUNTOS", "VER", "VIDEO", "AJUSTES" };
static const char *const tab_tips[NUM_TABS] = {
    "Modos de camara, seguir jugadores, distancia y zoom",
    "Puntos de camara y recorrido grabado",
    "Lo que sale en la pantalla: x-ray, kill feed, chat, HUD",
    "Grabar clips y lo que sale en los videos",
    "Tamanos, mouse y colores",
};

// the bottom panel's buttons, top to bottom; positions relative to its top
static void build_panel(void)
{
    if (CL_CaptureActive()) {
        // recording: the only thing to do is stopping it
        group("GRABANDO");
        button("DETENER GRABACION", "clip_stop",
               "Terminar la grabacion aqui: el video se guarda hasta este momento  [G]", IT_RED);
        return;
    }

    bool mvd = !cls.demo.playback;
    int tab = Q_clip(visor_editor_tab->integer, 0, NUM_TABS - 1);
    int body_y;

    fixed_row();

    // tabs, close together
    for (int i = 0; i < NUM_TABS; i++) {
        item_t *it = add_item(i == tab ? IT_TAB_ON : IT_TAB, tab_names[i],
                              va("set visor_editor_tab %d", i), tab_tips[i]);
        if (it) {
            it->w += ed.cw * 2;
            lay.x += ed.cw * 2 - lay.gap + 1;
        }
    }
    new_row();
    lay.y += lay.gap;

    body_y = lay.y;
    switch (tab) {
    case TAB_CAMERA:   tab_camera(mvd); break;
    case TAB_POINTS:   tab_points(mvd); break;
    case TAB_VIEW:     tab_view(); break;
    case TAB_VIDEO:    tab_video(); break;
    case TAB_SETTINGS: tab_settings(); break;
    }
    new_row();
    // same height for every tab, so the timeline doesn't jump
    lay.y = max(lay.y, body_y + (lay.row_h + lay.gap) * 2);
    lay.y -= lay.row_h + lay.gap;
}

/*
===============================================================================

SIDE PANELS

===============================================================================
*/

#define SIDE_CHARS  30

static void side_begin(int x, int y, int chars)
{
    int pad = lay.gap * 2;

    lay.margin = lay.indent = lay.x = x + pad;
    lay.right = x + chars * ed.cw - pad;
    lay.y = y + pad;
}

// a full width line of a list
static item_t *list_row(itemkind_t kind, const char *label, const char *cmd, const char *tip, uint32_t color)
{
    item_t *it;
    char buf[96];
    int chars = (lay.right - lay.margin) / ed.cw - 1;

    Q_strlcpy(buf, label, min(chars + 1, (int)sizeof(buf)));
    new_row();
    it = add_sized(kind, buf, cmd, tip, lay.right - lay.margin);
    if (it)
        it->color = color;
    new_row();
    return it;
}

// a setting line of the right panel: name in a column, then the controls
static void prop(const char *name)
{
    new_row();
    add_sized(IT_TEXT, name, NULL, NULL, ed.cw * 11);
    lay.indent = lay.x;
}

static void format_frame(char *buf, size_t size, int frame)
{
    Q_snprintf(buf, size, "%d:%02d.%d", frame / 600, frame / 10 % 60, frame % 10);
}

static void points_list(int max_rows)
{
    int n = CL_KeyCamCount(), here = CL_KeyCamIndex(), first;
    keyinfo_t k;
    char t[16];

    if (!cls.demo.playback) {
        list_row(IT_TEXT, "Solo con demos .dm2", NULL, NULL, 0);
        return;
    }
    if (!n) {
        list_row(IT_TEXT, "No hay puntos.", NULL, NULL, 0);
        list_row(IT_TEXT, "+ Punto o P pone uno", NULL, NULL, 0);
        list_row(IT_TEXT, "con lo que ves.", NULL, NULL, 0);
        return;
    }

    // a window of the list around the point here
    first = 0;
    if (n > max_rows)
        first = Q_clip((here >= 0 ? here : 0) - max_rows / 2, 0, n - max_rows);

    for (int i = first; i < n && i < first + max_rows; i++) {
        if (!CL_KeyCamGet(i, &k))
            break;
        format_frame(t, sizeof(t), k.frame);
        list_row(i == here ? IT_ROW_ON : IT_ROW,
                 va("%2d %8s %s%s%s%s", i + 1, t, k.line ? "recta" : "curva",
                    k.speed != 1 && i < n - 1 ? va(" x%g", k.speed) : "",
                    k.follow ? " sigue " : k.target ? " mira " : "", k.target_name ? k.target_name : ""),
                 va("keycam_goto %d", i + 1), va("Ir al punto %d", i + 1), 0);
    }
    if (n > max_rows)
        list_row(IT_TEXT, va("(%d puntos en total)", n), NULL, NULL, 0);
}

static void players_list(int max_rows)
{
    int followed = CL_ChaseTarget(), rows = 0;
    viewkind_t kind;
    const char *viewed = CL_ViewedPlayer(&kind);

    for (int i = 0; i < cl.maxclients && i < MAX_CLIENTS && rows < max_rows; i++) {
        const clientinfo_t *ci = &cl.clientinfo[i];
        const centity_t *e = &cl_entities[i + 1];
        bool seen = e->serverframe == cl.frame.number && e->current.modelindex == MODELINDEX_PLAYER;
        bool on = followed ? followed == i + 1 : viewed && !strcmp(viewed, ci->name);
        uint32_t color;

        if (!ci->name[0])
            continue;
        color = !seen ? COLOR_TEXT_OFF : CL_IsAxisPlayer(i) ? COLOR_AXIS : COLOR_ALLIES;
        list_row(on ? IT_ROW_ON : IT_ROW, ci->name, va("chase_player %d", i + 1),
                 seen ? va("Seguir a %s", ci->name)
                      : va("%s no esta a la vista ahora (el demo solo trae a los que veia quien grabo)", ci->name),
                 color);
        rows++;
    }
    if (!rows)
        list_row(IT_TEXT, "No hay jugadores", NULL, NULL, 0);
}

static void build_left(int top, int bottom)
{
    int x = lay.gap * 4, first = ed.num_items, list = visor_editor_list->integer;
    int max_rows;

    side_begin(x, top, SIDE_CHARS);
    button("PUNTOS", "set visor_editor_list 0", "Lista de puntos de camara: clic para ir a uno", on_if(!list));
    button("JUGADORES", "set visor_editor_list 1", "Lista de jugadores: clic para seguirlo", on_if(list));
    new_row();
    lay.y += lay.gap;

    max_rows = Q_clip((bottom - lay.y) / (lay.row_h + lay.gap) - 1, 3, 16);
    if (list)
        players_list(max_rows);
    else
        points_list(max_rows);

    ed.side[0].x = x;
    ed.side[0].y = top;
    ed.side[0].w = SIDE_CHARS * ed.cw;
    ed.side[0].h = lay.y - top + lay.gap;
    if (ed.num_items == first)
        ed.side[0].w = 0;
}

static void point_settings(int i, int n)
{
    keyinfo_t k;

    if (!CL_KeyCamGet(i, &k))
        return;

    add_item(IT_TITLE, va("PUNTO %d DE %d", i + 1, n), NULL, NULL);
    prop("Movimiento");
    button(k.line ? "Recta" : "Curva", "keycam_mode", "Del punto al siguiente: curva suave o linea recta  [M]", IT_BUTTON);
    prop("Objetivo");
    button(k.follow == 2 ? va("Sigue: %s", k.target_name) :
           k.follow ? va("Sigue a %s", k.target_name) :
           k.target ? va("Mira a %s", k.target_name) : "Nada", "keycam_target",
           "Nada / mirar al jugador del centro de la pantalla / seguirlo hasta el punto siguiente  [L]",
           on_if(k.target || k.follow));
    prop("");
    button("Seguir proyectil", "keycam_follow_proj",
           "Del punto al siguiente, seguir la granada, cohete o cuchillo que sigues o del centro de la pantalla",
           on_if(k.follow == 2));
    prop("Velocidad");
    value(NULL, va("x%g", k.speed), "keycam_speed down", "keycam_speed up",
          i < n - 1 ? "Camara lenta de este punto al siguiente: la camara se mueve normal y el juego lento  [3 / 4]"
                    : "La velocidad vale del punto al siguiente: este es el ultimo",
          i < n - 1 ? IT_BUTTON : IT_OFF);
    prop("FOV");
    value(NULL, va("%g", k.fov), "keycam_fov -5", "keycam_fov 5",
          "Campo de vision del punto: menos = mas zoom", IT_BUTTON);
    prop("");
    button("Actualizar", "keycam_add", "Guardar en este punto lo que ves ahora  [P]", IT_BUTTON);
    button("Borrar", "keycam_delete", "Borrar este punto  [U]", IT_BUTTON);
}

static void follow_settings(int num)
{
    const char *name = cl.clientinfo[(num - 1) & 0xff].name;

    add_item(IT_TITLE, va("SIGUIENDO: %s", *name ? name : "jugador"), NULL, NULL);
    prop("Vista");
    button(CL_ChaseFirstPerson() ? "1a persona" : "3a persona", "camera_view",
           "Primera / tercera persona  [F]", IT_BUTTON);
    prop("Distancia");
    setting(NULL, "chase_dist", 10, 16, 1000, NULL, "Distancia de la camara (tambien la rueda del mouse)");
    prop("Altura");
    setting(NULL, "chase_height", 4, -64, 128, NULL, "Altura de la camara");
    prop("");
    button("Vista original", "camera_reset", "Volver a la vista de quien grabo el demo  [R]", IT_BUTTON);
}

static void proj_settings(int id)
{
    add_item(IT_TITLE, va("SIGUIENDO: %s", CL_ProjName(id)), NULL, NULL);
    prop("Angulo");
    button("Atras", "follow_angle", "Justo detras del proyectil y un poco arriba. Clic derecho mantenido: girarla", IT_BUTTON);
    prop("Distancia");
    setting(NULL, "follow_dist", 10, 16, 1000, NULL, "Distancia de la camara al proyectil");
    prop("");
    button("Camara libre", "freecam 1", "Dejar de seguirlo: camara libre desde aqui", IT_BUTTON);
    button("Original", "camera_reset", "Volver a la vista de quien grabo el demo  [R]", IT_BUTTON);
}

static void no_point_settings(void)
{
    int n = CL_KeyCamCount();

    add_item(IT_TITLE, "SIN PUNTO AQUI", NULL, NULL);
    new_row();
    add_item(IT_TEXT, "Pon uno con lo que ves:", NULL, NULL);
    new_row();
    button("+ Punto", "keycam_add", "Poner un punto de camara aqui con lo que ves  [P]", IT_BUTTON);
    button("< Punto", "keycam_prev", "Ir al punto anterior  [1]", n ? IT_BUTTON : IT_OFF);
    button("Punto >", "keycam_next", "Ir al punto siguiente  [2]", n ? IT_BUTTON : IT_OFF);
}

// laid out from y = 0, then moved to sit on the bottom panel
static void build_right(int bottom)
{
    int chars = SIDE_CHARS + 4, x = r_config.width - lay.gap * 4 - chars * ed.cw;
    int first = ed.num_items, here = CL_KeyCamIndex(), dy;

    side_begin(x, 0, chars);
    if (cls.demo.playback && here >= 0)
        point_settings(here, CL_KeyCamCount());
    else if (CL_FreecamProj() >= 0)
        proj_settings(CL_FreecamProj());
    else if (CL_ChaseTarget())
        follow_settings(CL_ChaseTarget());
    else if (cls.demo.playback)
        no_point_settings();
    new_row();

    if (ed.num_items == first) {
        ed.side[1].w = 0;
        return;
    }
    ed.side[1].x = x;
    ed.side[1].w = chars * ed.cw;
    ed.side[1].h = lay.y + lay.gap;
    dy = bottom - ed.side[1].h;
    ed.side[1].y = dy;
    for (int i = first; i < ed.num_items; i++)
        ed.items[i].y += dy;
}

/*
===============================================================================

DRAWING

===============================================================================
*/

static void draw_text(int x, int y, uint32_t color, int flags, const char *s)
{
    R_SetColor(color);
    R_DrawString(Q_rint(x * ed.scale), Q_rint(y * ed.scale), flags, MAX_STRING_CHARS, s, ed.font);
}

static void draw_timeline(int frame, float prog)
{
    int x0 = ed.tl_x, y = ed.tl_y, w = ed.tl_w, h = ed.tl_h;
    int tick = max(ed.ch / 2, 3), in = CL_ClipMarkIn(), out = CL_ClipMarkOut();
    float head = ed.dragging ? ed.drag_frac : prog, p0, p1;
    static float marks[1024];
    int here, n;

#define TX(f)   (x0 + Q_rint(w * Q_clipf(f, 0, 1)))

    R_DrawFill32(x0, y, w, h, COLOR_TRACK);
    R_DrawFill32(x0, y, TX(prog) - x0, h, COLOR_PLAYED);

    if (in >= 0) {
        int xi = TX(CL_ClipMarkPos(false));
        int xo = out >= 0 ? TX(CL_ClipMarkPos(true)) : x0 + w;
        R_DrawFill32(xi, y, max(xo - xi, 1), h, COLOR_CLIP);
        R_DrawFill32(xi - 1, y - tick, 3, h + tick * 2, COLOR_IN);
    }
    if (out >= 0)
        R_DrawFill32(TX(CL_ClipMarkPos(true)) - 1, y - tick, 3, h + tick * 2, COLOR_OUT);

    if (CL_CamPathRange(&p0, &p1))
        R_DrawFill32(TX(p0), y + h, max(TX(p1) - TX(p0), 1), max(tick / 2, 2),
                     CL_CamPathRecording() ? COLOR_OUT : COLOR_PATH);

    // the watched player's frags above the track, deaths below
    n = CL_FeedMarks(false, marks, q_countof(marks));
    for (int i = 0; i < n; i++)
        R_DrawFill32(TX(marks[i]) - 1, y - tick, 2, tick, COLOR_FRAG);
    n = CL_FeedMarks(true, marks, q_countof(marks));
    for (int i = 0; i < n; i++)
        R_DrawFill32(TX(marks[i]) - 1, y + h, 2, tick, COLOR_DEATH);

    // throws of grenades, rockets and knives: small orange marks below
    n = CL_ProjMarks(marks, q_countof(marks));
    for (int i = 0; i < n; i++)
        R_DrawFill32(TX(marks[i]) - 1, y + h + tick / 2, 3, tick, COLOR_PROJ);

    // camera points: squares on the track, joined by a line
    ed.num_keys = CL_KeyCamMarks(ed.key_pos, q_countof(ed.key_pos), &here);

    // stretches in slow motion (purple) or fast (orange), over the track
    for (int i = 0; i + 1 < ed.num_keys; i++) {
        keyinfo_t k;
        if (!CL_KeyCamGet(i, &k) || k.speed == 1)
            continue;
        R_DrawFill32(TX(ed.key_pos[i]), y - tick, max(TX(ed.key_pos[i + 1]) - TX(ed.key_pos[i]), 1),
                     h + tick * 2, k.speed < 1 ? COLOR_SLOW : COLOR_FAST);
    }
    if (ed.num_keys >= 2) {
        int lh = max(h / 4, 2);
        R_DrawFill32(TX(ed.key_pos[0]), y + (h - lh) / 2,
                     max(TX(ed.key_pos[ed.num_keys - 1]) - TX(ed.key_pos[0]), 1), lh,
                     CL_KeyCamPlaying() ? COLOR_KEY : COLOR_TRACK);
    }
    {
        int hover_key = interactive() ? key_at_mouse() : -1;
        for (int i = 0; i < ed.num_keys; i++) {
            int sz = h + (i == hover_key ? 6 : 2);
            int kx = TX(ed.key_pos[i]) - sz / 2, ky = y + (h - sz) / 2;
            R_DrawFill32(kx - 1, ky - 1, sz + 2, sz + 2, COLOR_EDGE);
            R_DrawFill32(kx, ky, sz, sz, i == here ? COLOR_KEY_HERE : COLOR_KEY);
        }
    }

    // where a click would go
    if (interactive() && !ed.dragging && over_timeline(ed.ch / 2))
        R_DrawFill32(Q_clip(ed.mx, x0, x0 + w) - 1, y - tick, 2, h + tick * 2, COLOR_HOVER);

    R_DrawFill32(TX(head) - 2, y - tick, 4, h + tick * 2, COLOR_HEAD);
#undef TX
}

static void draw_item_box(const item_t *it, bool hover)
{
    uint32_t c;

    switch (it->kind) {
    case IT_LABEL:
    case IT_TEXT:
    case IT_TITLE:
        return;
    case IT_ON:     c = hover ? COLOR_BTN_ON_H : COLOR_BTN_ON; break;
    case IT_RED:    c = hover ? COLOR_BTN_RED_H : COLOR_BTN_RED; break;
    case IT_OFF:    c = COLOR_BTN_OFF; break;
    case IT_VALUE:  c = COLOR_VALUE; break;
    case IT_TAB:
        if (hover)
            R_DrawFill32(it->x, it->y, it->w, it->h, COLOR_TAB_ON);
        return;
    case IT_TAB_ON:
        R_DrawFill32(it->x, it->y, it->w, it->h, COLOR_TAB_ON);
        R_DrawFill32(it->x, it->y + it->h - 2, it->w, 2, COLOR_KEY);
        return;
    case IT_ROW:
        if (hover)
            R_DrawFill32(it->x, it->y, it->w, it->h, COLOR_BTN);
        return;
    case IT_ROW_ON: c = hover ? COLOR_BTN_ON_H : COLOR_BTN_ON; break;
    default:        c = hover ? COLOR_BTN_HOVER : COLOR_BTN; break;
    }
    R_DrawFill32(it->x, it->y, it->w, it->h, c);
}

static void draw_item_text(const item_t *it, bool hover)
{
    int ty = it->y + (it->h - ed.ch) / 2, tx = it->x + ed.cw;
    uint32_t c = COLOR_TEXT;

    switch (it->kind) {
    case IT_LABEL:
    case IT_TEXT:
        tx = it->x;
        c = COLOR_LABEL;
        break;
    case IT_TITLE:
        tx = it->x;
        c = COLOR_TIP;
        break;
    case IT_OFF:
        c = COLOR_TEXT_OFF;
        break;
    case IT_VALUE:
        tx = it->x + (it->w - (int)strlen(it->label) * ed.cw) / 2;
        break;
    case IT_TAB:
        c = hover ? COLOR_TEXT : COLOR_LABEL;
        tx = it->x + (it->w - (int)strlen(it->label) * ed.cw) / 2;
        break;
    case IT_TAB_ON:
        tx = it->x + (it->w - (int)strlen(it->label) * ed.cw) / 2;
        break;
    case IT_ROW:
    case IT_ROW_ON:
        tx = it->x + ed.cw / 2;
        if (it->color)
            c = it->kind == IT_ROW_ON ? COLOR_TEXT : it->color;
        break;
    default:
        break;
    }
    draw_text(tx, ty, c, 0, it->label);
}

/*
===============
CL_EditorDraw

The panel. left / mid / right: the timeline texts (time, state, clip marks)
made by the screen code.
===============
*/
void CL_EditorDraw(qhandle_t font, float scale, const char *left, const char *mid, const char *right)
{
    int W = r_config.width, H = r_config.height;
    int pad, total, top, frame, side_top;
    float prog;
    bool paused;
    const item_t *hover = NULL;
    const char *tip;

    ed.panel_y = 0;
    ed.side[0].w = ed.side[1].w = 0;
    if (!CL_EditorActive() || (cls.key_dest & KEY_MENU))
        return;
    if (!CL_DemoPosition(&frame, &prog, &paused))
        return;

    ed.font = font;
    ed.scale = scale / Cvar_ClampValue(visor_editor_size, 0.5f, 3);
    ed.cw = Q_rint(CONCHAR_WIDTH / ed.scale);
    ed.ch = Q_rint(CONCHAR_HEIGHT / ed.scale);
    pad = max(ed.ch / 2, 3);

    // layout, from the panel top: tip line, texts, timeline, buttons
    ed.num_items = 0;
    lay.margin = pad * 2;
    lay.right = W - pad * 2;
    lay.gap = max(pad / 2, 2);
    lay.row_h = ed.ch + pad * 2;
    lay.x = lay.indent = lay.margin;
    lay.y = pad + ed.ch + pad + ed.ch + pad * 2;    // below the two text lines
    ed.tl_h = max(ed.ch, 8);
    lay.y += ed.tl_h + pad * 3;                     // below the timeline
    build_panel();
    total = lay.y + lay.row_h + pad;

    top = H - total;
    ed.panel_y = top;
    for (int i = 0; i < ed.num_items; i++)
        ed.items[i].y += top;
    ed.tl_x = pad * 2;
    ed.tl_w = W - pad * 4;
    ed.tl_y = top + pad + ed.ch + pad + ed.ch + pad * 2;

    // side panels on the image, clear of the console lines at the top
    side_top = ed.ch * 6;
    if (!CL_CaptureActive() && visor_editor_sides->integer && top - side_top > lay.row_h * 6) {
        build_right(top - pad * 2);
        build_left(side_top, min(top - pad * 2, ed.side[1].w ? H : H));
    }

    if ((interactive() || CL_CaptureActive()) && over_ui() && !ed.dragging)
        hover = item_at(ed.mx, ed.my);

    // backgrounds, timeline and buttons
    R_DrawFill32(0, top, W, total, COLOR_PANEL);
    R_DrawFill32(0, top, W, 1, COLOR_BTN_HOVER);
    for (int s = 0; s < 2; s++) {
        const rect_t *r = &ed.side[s];
        if (r->w > 0) {
            R_DrawFill32(r->x, r->y, r->w, r->h, COLOR_SIDE);
            R_DrawFill32(r->x, r->y, r->w, 1, COLOR_BTN_HOVER);
        }
    }
    draw_timeline(frame, prog);

    for (int i = 0; i < ed.num_items; i++) {
        const item_t *it = &ed.items[i];
        draw_item_box(it, it == hover && clickable(it));
        if (it->kind == IT_TAB_ON || it->kind == IT_TAB)
            R_DrawFill32(it->x, it->y + it->h, it->w, 1, COLOR_BTN);
    }

    // texts
    R_SetScale(ed.scale);

    // first line: what the thing under the mouse does, or how to use the editor
    if (CL_CaptureActive() && hover && hover->tip[0])
        tip = hover->tip;
    else if (CL_CaptureActive())
        tip = "Grabando el video. El panel no sale en el video";
    else if (ed.flying && CL_FreecamProj() >= 0)
        tip = "Girando la camara alrededor del proyectil: mueve el mouse (Angulo: atras la deja como al principio)";
    else if (ed.flying)
        tip = "Moviendo la camara: mouse para mirar, W A S D para volar, Espacio / Ctrl subir / bajar, Shift rapido";
    else if (hover && hover->tip[0])
        tip = hover->tip;
    else if (interactive() && key_at_mouse() >= 0)
        tip = va("Punto de camara %d: clic para ir a el", key_at_mouse() + 1);
    else if (interactive() && over_timeline(ed.ch / 2))
        tip = "Clic o arrastrar: ir a ese momento del demo";
    else
        tip = CL_EditorViewTip();
    draw_text(pad * 2, top + pad, hover || over_timeline(ed.ch / 2) ? COLOR_TIP : COLOR_LABEL, 0, tip);

    // second line: time, state, clip marks
    {
        int y = top + pad + ed.ch + pad;
        draw_text(pad * 2, y, COLOR_TEXT, 0, left);
        draw_text(W / 2 - strlen(mid) * ed.cw / 2, y, COLOR_TIP, 0, mid);
        draw_text(W - pad * 2 - strlen(right) * ed.cw, y, COLOR_TEXT, 0, right);
    }

    for (int i = 0; i < ed.num_items; i++)
        draw_item_text(&ed.items[i], &ed.items[i] == hover);

    R_ClearColor();
    R_SetScale(1.0f);
}

/*
===============================================================================

COMMANDS

===============================================================================
*/

// first click on "BORRAR TODO": the button asks to click again
static void Editor_ClearAsk_f(void)
{
    ed.confirm_until = cls.realtime + 4000;
    SCR_CenterPrint("Clic otra vez en SI, BORRAR TODO para confirmar", false);
}

// x-ray color by team or a single one
static void Editor_XrayColor_f(void)
{
    if (Q_stricmp(Cvar_VariableString("xray_color"), "team"))
        Cvar_Set("xray_color", "team");
    else
        Cvar_Set("xray_color", "magenta");
}

// visor_step <cvar> <change> <min> <max>: the - / + buttons of a setting
static void Visor_Step_f(void)
{
    cvar_t *var;
    float v;

    if (Cmd_Argc() < 5) {
        Com_Printf("Uso: %s <ajuste> <cambio> <minimo> <maximo>\n", Cmd_Argv(0));
        return;
    }
    var = Cvar_FindVar(Cmd_Argv(1));
    if (!var) {
        Com_Printf("No existe el ajuste %s\n", Cmd_Argv(1));
        return;
    }
    v = Q_clipf(var->value + Q_atof(Cmd_Argv(2)), Q_atof(Cmd_Argv(3)), Q_atof(Cmd_Argv(4)));
    v = roundf(v * 1000) / 1000;
    Cvar_SetByVar(var, va("%g", v), FROM_CONSOLE);
}

#if USE_DEBUG
// editor_mouse <x> <y> [key] [down|up|click]: fake mouse input, for tests.
// Goes through Key_Event like a real button.
static void Editor_Mouse_f(void)
{
    int key = Cmd_Argc() > 3 ? Key_StringToKeynum(Cmd_Argv(3)) : -1;
    const char *what = Cmd_Argc() > 4 ? Cmd_Argv(4) : "click";

    CL_EditorMouseMove(Q_atoi(Cmd_Argv(1)), Q_atoi(Cmd_Argv(2)));
    if (key < 0)
        return;
    if (strcmp(what, "up"))
        Key_Event(key, true, com_eventTime);
    if (strcmp(what, "down"))
        Key_Event(key, false, com_eventTime);
}
#endif

void CL_InitEditor(void)
{
    visor_editor = Cvar_Get("visor_editor", "0", CVAR_ARCHIVE);
    visor_editor->changed = editor_changed;
    visor_editor_size = Cvar_Get("visor_editor_size", "1", CVAR_ARCHIVE);
    visor_editor_tab = Cvar_Get("visor_editor_tab", "1", CVAR_ARCHIVE);
    visor_editor_list = Cvar_Get("visor_editor_list", "0", CVAR_ARCHIVE);
    visor_editor_sides = Cvar_Get("visor_editor_sides", "1", CVAR_ARCHIVE);
    Cmd_AddCommand("editor_clear_ask", Editor_ClearAsk_f);
    Cmd_AddCommand("editor_xray_color", Editor_XrayColor_f);
    Cmd_AddCommand("visor_step", Visor_Step_f);
#if USE_DEBUG
    Cmd_AddCommand("editor_mouse", Editor_Mouse_f);
#endif
}
