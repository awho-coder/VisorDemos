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
// Tab (visor_editor) swaps the keyboard timeline for a panel with buttons
// and a large timeline: the mouse cursor stays free, a click on a button
// runs the same command as its key, a click or drag on the timeline seeks,
// a click on a camera point jumps to it. Holding the right mouse button
// flies the camera (WASD + mouse) until it is released. The panel is drawn
// after the frame is captured, so it never shows up in video clips.

#include "client.h"

#define MAX_ITEMS   64

typedef enum {
    IT_BUTTON,
    IT_ON,          // a button for something that is switched on
    IT_RED,         // record
    IT_OFF,         // not available now
    IT_LABEL        // group name
} itemkind_t;

typedef struct {
    int         x, y, w, h;
    itemkind_t  kind;
    char        label[32];
    const char  *cmd;
    const char  *tip;
} item_t;

static struct {
    // mouse, in screen pixels
    int         mx, my;
    bool        flying;         // right button held
    bool        dragging;       // seeking on the timeline
    bool        clicked_panel;  // left button went down on the panel
    float       drag_frac;
    unsigned    last_seek;

    // layout of the last drawn frame, for hit testing
    item_t      items[MAX_ITEMS];
    int         num_items;
    int         panel_y;        // top of the panel, 0 = no panel
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

#define COLOR_PANEL     MakeColor( 16,  17,  22, 215)
#define COLOR_BTN       MakeColor( 58,  60,  72, 240)
#define COLOR_BTN_HOVER MakeColor( 92,  96, 116, 250)
#define COLOR_BTN_ON    MakeColor(  0, 118, 135, 250)
#define COLOR_BTN_ON_H  MakeColor(  0, 150, 170, 255)
#define COLOR_BTN_RED   MakeColor(165,  30,  30, 250)
#define COLOR_BTN_RED_H MakeColor(205,  45,  45, 255)
#define COLOR_BTN_OFF   MakeColor( 38,  39,  46, 230)
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

static bool over_panel(void)
{
    return ed.panel_y && ed.my >= ed.panel_y;
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

    if (!interactive())
        return false;

    switch (key) {
    case K_MOUSE1:
        if (!down)
            return true;    // a click that started on the 3D view
        if (over_panel()) {
            const item_t *it = item_at(ed.mx, ed.my);
            int k;

            ed.clicked_panel = true;
            if (it && it->kind != IT_OFF && it->cmd) {
                run(it->cmd);
            } else if ((k = key_at_mouse()) >= 0) {
                run(va("keycam_goto %d", k + 1));
            } else if (over_timeline(ed.ch / 2)) {
                ed.dragging = true;
                ed.drag_frac = timeline_frac(ed.mx);
                seek_to(ed.drag_frac);
            }
        }
        return true;        // never fire the game's +attack

    case K_MOUSE2:
        if (!down)
            return true;
        ed.flying = true;
        if (cls.demo.playback && !CL_FreecamFlying())
            run("freecam 1");
        IN_Activate();      // grab the mouse to look around
        return true;

    case K_MWHEELUP:
    case K_MWHEELDOWN:
        return over_panel();    // on the 3D view: camera speed, as usual

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

===============================================================================
*/

static struct {
    int x, y, row_h, gap, margin, right;
    int indent;     // where a wrapped line starts (after the group names)
} lay;

static item_t *add_item(itemkind_t kind, const char *label, const char *cmd, const char *tip)
{
    item_t *it;
    int w;

    if (ed.num_items == MAX_ITEMS)
        return NULL;

    w = strlen(label) * ed.cw + (kind == IT_LABEL ? 0 : ed.cw * 2);
    if (lay.x > lay.indent && lay.x + w > lay.right) {
        // no room left on this line: wrap
        lay.x = lay.indent;
        lay.y += lay.row_h + lay.gap;
    }

    it = &ed.items[ed.num_items++];
    it->x = lay.x;
    it->y = lay.y;
    it->w = w;
    it->h = lay.row_h;
    it->kind = kind;
    Q_strlcpy(it->label, label, sizeof(it->label));
    it->cmd = cmd;
    it->tip = tip;
    lay.x += w + lay.gap;
    return it;
}

static void button(const char *label, const char *cmd, const char *tip, itemkind_t kind)
{
    add_item(kind, label, cmd, tip);
}

// group name; at the start of a row the names line up in a column
#define LABEL_CHARS 17

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

// all the buttons, top to bottom; positions relative to the panel top
static void build_buttons(void)
{
    bool paused = false, line, mvd = !cls.demo.playback;
    int here = CL_KeyCamHere(&line);
    float speed = CL_DemoSpeed();
    itemkind_t dm2 = mvd ? IT_OFF : IT_BUTTON;

    CL_DemoPosition(NULL, NULL, &paused);

    group("REPRODUCIR");
    button("-10 s", "demo_step -", "Retroceder 10 segundos  [flecha izquierda]", IT_BUTTON);
    button("< Frag", "frag_prev", "Frag o muerte anterior del jugador que ves  [B]", IT_BUTTON);
    button(paused ? "> Seguir" : "|| Pausa", "pause", "Pausa / seguir  [flecha abajo]", on_if(paused));
    button("Frag >", "frag_next", "Siguiente frag o muerte del jugador que ves  [N]", IT_BUTTON);
    button("+10 s", "demo_step +", "Adelantar 10 segundos  [flecha derecha]", IT_BUTTON);
    group("VELOCIDAD");
    button("-", "demo_speed down", "Mas lento, camara lenta  [Z]", IT_BUTTON);
    button(va("x%g", speed), "demo_speed reset", "Velocidad normal  [Retroceso]", on_if(speed != 1.0f));
    button("+", "demo_speed up", "Mas rapido  [X]", IT_BUTTON);
    group("VOLUMEN");
    button("-", "visor_volume down", "Bajar el volumen  [,]", IT_BUTTON);
    button("+", "visor_volume up", "Subir el volumen  [.]", IT_BUTTON);
    new_row();

    group("CAMARA");
    button("Libre", "freecam", "Camara libre: volar por el mapa. Tambien: clic derecho mantenido  [flecha arriba]",
           on_if(CL_FreecamFlying()));
    button("< Jugador", "chase_prev", "Seguir al jugador anterior  [Q]", IT_BUTTON);
    button("Jugador >", "chase_next", "Seguir al siguiente jugador  [E]", IT_BUTTON);
    button("1a / 3a", "camera_view", "Primera / tercera persona al seguir a alguien  [F]", on_if(CL_ChaseFirstPerson()));
    button("Original", "camera_reset", "Volver a la vista de quien grabo el demo  [R]", IT_BUTTON);
    button("X-ray", "visor_xray", "Siluetas de los jugadores detras de las paredes  [T]",
           on_if(Cvar_VariableInteger("xray")));
    button("Muertes", "feed_toggle killfeed", "Kill feed: si / solo el jugador / no  [K]",
           on_if(Cvar_VariableInteger("visor_killfeed")));
    button("Chat", "feed_toggle chat", "Chat: si / con avisos del servidor / no  [J]",
           on_if(Cvar_VariableInteger("visor_chat")));
    new_row();

    group("PUNTOS DE CAMARA");
    button(here ? "Actualizar punto" : "+ Punto", "keycam_add",
           "Poner un punto de camara aqui con lo que ves (o actualizar el de aqui)  [P]", dm2);
    button("< Punto", "keycam_prev", "Ir al punto anterior  [1]", dm2);
    button("Punto >", "keycam_next", "Ir al punto siguiente  [2]", dm2);
    button("Mirar jugador", "keycam_target",
           "El punto mira al jugador del centro de la pantalla / mirada libre  [L]",
           mvd ? IT_OFF : on_if(here == 2));
    button(line ? "Recta" : "Curva", "keycam_mode",
           "Del punto al siguiente: curva suave o linea recta  [M]", here && !mvd ? IT_BUTTON : IT_OFF);
    button("Borrar punto", "keycam_delete", "Borrar el punto de camara de aqui  [U]",
           here && !mvd ? IT_BUTTON : IT_OFF);
    button("Usar camara", "campath_play", "Usar los puntos y el recorrido grabado si / no  [V]",
           mvd ? IT_OFF : on_if(CL_KeyCamPlaying() || CL_CamPathPlaying()));
    button(CL_CamPathRecording() ? "Terminar" : "Grabar recorrido", "campath_record",
           "Grabar la camara mientras corre el demo / terminar  [C]",
           mvd ? IT_OFF : CL_CamPathRecording() ? IT_RED : IT_BUTTON);
    new_row();

    group("CLIP");
    button("Inicio", "clip_in", "Marcar el inicio del clip aqui  [I]", IT_BUTTON);
    button("Fin", "clip_out", "Marcar el fin del clip aqui  [O]", IT_BUTTON);
    button("Borrar marcas", "clip_clear", "Borrar las marcas de inicio y fin  [Supr]", IT_BUTTON);
    button("GRABAR CLIP", "clip_export", "Grabar el clip a video, de la marca de inicio a la de fin  [G]", IT_RED);
    group("");
    button("Abrir demo", "pushmenu demos", "Elegir otro demo  [Esc]", IT_BUTTON);
    button("Ayuda", "visor_help", "Teclas y ajustes  [H]", on_if(CL_VisorHelpVisible()));
    button("Ocultar editor", "toggle visor_editor", "Volver a la barra simple y al teclado  [Tab]", IT_BUTTON);
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

    // camera points: squares on the track, joined by a line
    ed.num_keys = CL_KeyCamMarks(ed.key_pos, q_countof(ed.key_pos), &here);
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
    int pad, total, top, frame;
    float prog;
    bool paused;
    const item_t *hover = NULL;
    const char *tip;

    ed.panel_y = 0;
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
    build_buttons();
    total = lay.y + lay.row_h + pad;

    top = H - total;
    ed.panel_y = top;
    for (int i = 0; i < ed.num_items; i++)
        ed.items[i].y += top;
    ed.tl_x = pad * 2;
    ed.tl_w = W - pad * 4;
    ed.tl_y = top + pad + ed.ch + pad + ed.ch + pad * 2;

    if (interactive() && over_panel() && !ed.dragging)
        hover = item_at(ed.mx, ed.my);

    // background, timeline and buttons
    R_DrawFill32(0, top, W, total, COLOR_PANEL);
    R_DrawFill32(0, top, W, 1, COLOR_BTN_HOVER);
    draw_timeline(frame, prog);

    for (int i = 0; i < ed.num_items; i++) {
        const item_t *it = &ed.items[i];
        uint32_t c;

        switch (it->kind) {
        case IT_LABEL:  continue;
        case IT_ON:     c = it == hover ? COLOR_BTN_ON_H : COLOR_BTN_ON; break;
        case IT_RED:    c = it == hover ? COLOR_BTN_RED_H : COLOR_BTN_RED; break;
        case IT_OFF:    c = COLOR_BTN_OFF; break;
        default:        c = it == hover ? COLOR_BTN_HOVER : COLOR_BTN; break;
        }
        R_DrawFill32(it->x, it->y, it->w, it->h, c);
    }

    // texts
    R_SetScale(ed.scale);

    // first line: what the thing under the mouse does, or how to use the editor
    if (CL_CaptureActive())
        tip = "Grabando el clip...";
    else if (ed.flying)
        tip = "Moviendo la camara: mouse para mirar, W A S D para volar, Espacio / Ctrl subir / bajar, Shift rapido";
    else if (hover && hover->tip)
        tip = hover->tip;
    else if (interactive() && key_at_mouse() >= 0)
        tip = va("Punto de camara %d: clic para ir a el", key_at_mouse() + 1);
    else if (interactive() && over_timeline(ed.ch / 2))
        tip = "Clic o arrastrar: ir a ese momento del demo";
    else
        tip = "Clic derecho mantenido sobre la imagen: mover la camara.  Tab: ocultar el editor";
    draw_text(pad * 2, top + pad, hover || over_timeline(ed.ch / 2) ? COLOR_TIP : COLOR_LABEL, 0, tip);

    // second line: time, state, clip marks
    {
        int y = top + pad + ed.ch + pad;
        draw_text(pad * 2, y, COLOR_TEXT, 0, left);
        draw_text(W / 2 - strlen(mid) * ed.cw / 2, y, COLOR_TIP, 0, mid);
        draw_text(W - pad * 2 - strlen(right) * ed.cw, y, COLOR_TEXT, 0, right);
    }

    for (int i = 0; i < ed.num_items; i++) {
        const item_t *it = &ed.items[i];
        int ty = it->y + (it->h - ed.ch) / 2;

        if (it->kind == IT_LABEL)
            draw_text(it->x, ty, COLOR_LABEL, 0, it->label);
        else
            draw_text(it->x + ed.cw, ty, it->kind == IT_OFF ? COLOR_TEXT_OFF : COLOR_TEXT, 0, it->label);
    }

    R_ClearColor();
    R_SetScale(1.0f);
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
#if USE_DEBUG
    Cmd_AddCommand("editor_mouse", Editor_Mouse_f);
#endif
}
