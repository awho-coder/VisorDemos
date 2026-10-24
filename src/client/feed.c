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

// feed.c -- kill feed and chat panel for demo playback
//
// The server prints every death and chat line, and demos keep those prints.
// Deaths are turned into short "killer  verb  victim" lines (top right) and
// chat goes to its own panel (bottom left). Both run on demo time, so they
// stay up while paused, last longer in slow motion and come out the same in
// video clips rendered at a fixed frame rate.
//
// D-Day sends an obituary in pieces: victim, " was sniped by ", killer, tail
// ("'s rifle - ** HEADSHOT **\n"). Mods that send one whole line are parsed
// by looking for the players' names in it.

#include "client.h"

#define FEED_KILLS      16
#define FEED_CHATS      32
#define FEED_NAME       32
#define FEED_TEXT       160
#define MAX_PIECES      8

typedef enum {
    TEAM_NONE,
    TEAM_ALLIES,
    TEAM_AXIS
} team_t;

typedef struct {
    int     time;
    char    killer[FEED_NAME];  // empty: died on their own
    char    victim[FEED_NAME];
    char    label[FEED_NAME];   // "sniped", "grenade", "suicide"...
    team_t  kteam, vteam;
    bool    headshot;
    bool    teamkill;
} kill_t;

typedef struct {
    int     time;
    bool    server;             // a server message, not chat
    char    tag[8];             // "usa", "grm"... (D-Day team chat)
    char    name[FEED_NAME];
    team_t  team;
    char    text[FEED_TEXT];
} chat_t;

static kill_t   kills[FEED_KILLS];
static int      kill_head;
static chat_t   chats[FEED_CHATS];
static int      chat_head;
static int      newest_time;

// obituary pieces of the current server frame
static struct {
    int     frame;
    int     count;
    char    text[MAX_PIECES][MAX_STRING_CHARS];
} acc;

static cvar_t   *visor_killfeed;
static cvar_t   *visor_chat;
static cvar_t   *clip_killfeed;
static cvar_t   *clip_chat;
static cvar_t   *visor_killfeed_time;
static cvar_t   *visor_killfeed_lines;
static cvar_t   *visor_chat_time;
static cvar_t   *visor_chat_lines;
static cvar_t   *visor_feed_size;

// same clock as the camera paths: follows the demo through seeks
static int demo_time(void)
{
    return cl.time + cl.serverdelta * CL_FRAMETIME;
}

static bool watching_demo(void)
{
    if (cls.state != ca_active)
        return false;
#if USE_MVD_CLIENT
    if (sv_running->integer == ss_broadcast)
        return true;
#endif
    return cls.demo.playback;
}

void CL_FeedClear(void)
{
    memset(kills, 0, sizeof(kills));
    memset(chats, 0, sizeof(chats));
    kill_head = chat_head = 0;
    newest_time = 0;
    acc.count = 0;
}

// went back in the demo (or a new map started): what we have is from the future
static void check_rewind(void)
{
    int now = demo_time();

    if (now + 500 < newest_time)
        CL_FeedClear();
}

/*
===============================================================================

PARSING

===============================================================================
*/

// printable copy for our colored drawing: no control characters, no high
// bit (that would ignore the color), spaces trimmed at both ends
static void clean_copy(char *dst, size_t size, const char *src, size_t len)
{
    size_t n = 0;

    while (len && *src && n + 1 < size) {
        int c = *src++ & 127;
        len--;
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c < 32 && c != 0x10 && c != 0x11)
            continue;   // D-Day puts a \x02 in front of some names
        if (c == ' ' && !n)
            continue;
        dst[n++] = c;
    }
    while (n && dst[n - 1] == ' ')
        n--;
    dst[n] = 0;
}

static int find_player(const char *name)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (cl.clientinfo[i].name[0] && !strcmp(cl.clientinfo[i].name, name))
            return i;
    return -1;
}

// D-Day teams by model folder (usa/infantry...)
static team_t player_team(const char *name)
{
    static const char *const allies[] = { "usa", "gbr", "rus", "pol", "aus", "fra" };
    static const char *const axis[] = { "grm", "ita", "jpn" };
    int i = find_player(name);
    const char *model;

    if (i < 0)
        return TEAM_NONE;
    model = cl.clientinfo[i].model_name;
    for (int k = 0; k < q_countof(allies); k++)
        if (!Q_stricmp(model, allies[k]))
            return TEAM_ALLIES;
    for (int k = 0; k < q_countof(axis); k++)
        if (!Q_stricmp(model, axis[k]))
            return TEAM_AXIS;
    return TEAM_NONE;
}

// longest player name that s starts with (raw names, as the server sends them)
static size_t name_prefix(const char *s)
{
    size_t best = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        const char *n = cl.clientinfo[i].name;
        size_t len = strlen(n);
        if (len > best && !strncmp(s, n, len))
            best = len;
    }
    return best;
}

static const char *short_weapon(const char *w)
{
    if (!Q_stricmp(w, "handgrenade"))
        return "grenade";
    if (!Q_stricmp(w, "explosive attack"))
        return "explosive";
    return w;
}

// tail "'s light machine gun - ** HEADSHOT **" -> "light machine gun"
static void tail_weapon(char *dst, size_t size, const char *tail)
{
    const char *end;

    *dst = 0;
    if (strncmp(tail, "'s ", 3))
        return;
    tail += 3;
    for (end = tail; *end && *end != '\n' && *end != ')' &&
         strncmp(end, " - ", 3) && strncmp(end, " (", 2); end++)
        ;
    clean_copy(dst, size, tail, end - tail);
}

static void make_label(char *dst, size_t size, const char *mid, const char *weapon)
{
    size_t len = strlen(mid);

    // "was sniped by" -> "sniped"; "was killed by X's airstrike" -> "airstrike"
    if (!strncmp(mid, "was ", 4) && len > 7 && !strcmp(mid + len - 3, " by")) {
        if (*weapon && !strcmp(mid, "was killed by")) {
            Q_strlcpy(dst, short_weapon(weapon), size);
            return;
        }
        clean_copy(dst, size, mid + 4, len - 7);
        return;
    }
    if (Q_stristr(mid, "wounds")) {
        Q_strlcpy(dst, "wounds", size);
        return;
    }
    if (Q_stristr(mid, "fell to")) {
        Q_strlcpy(dst, "fell", size);
        return;
    }
    // "did not survive", "didn't see", "ate"... say the weapon instead
    if (*weapon) {
        Q_strlcpy(dst, short_weapon(weapon), size);
        return;
    }
    clean_copy(dst, size, mid, len);
}

static kill_t *new_kill(void)
{
    kill_t *k = &kills[kill_head++ & (FEED_KILLS - 1)];

    memset(k, 0, sizeof(*k));
    k->time = newest_time = demo_time();
    return k;
}

static bool add_kill(const char *victim, const char *mid, const char *killer, const char *tail)
{
    char v[FEED_NAME], m[MAX_STRING_CHARS], kname[FEED_NAME], weapon[FEED_NAME];
    kill_t *k;

    clean_copy(v, sizeof(v), victim, SIZE_MAX);
    clean_copy(m, sizeof(m), mid, SIZE_MAX);
    clean_copy(kname, sizeof(kname), killer, SIZE_MAX);
    if (!*v || !*m || !*kname)
        return false;

    // what follows the killer's name has to look like the end of an obituary
    if (*tail && *tail != '\n' && strncmp(tail, "'s ", 3) &&
        strncmp(tail, " - ", 3) && strncmp(tail, " (", 2) && *tail != ')')
        return false;

    tail_weapon(weapon, sizeof(weapon), tail);

    k = new_kill();
    Q_strlcpy(k->victim, v, sizeof(k->victim));
    Q_strlcpy(k->killer, kname, sizeof(k->killer));
    make_label(k->label, sizeof(k->label), m, weapon);
    k->vteam = player_team(v);
    k->kteam = player_team(kname);
    k->headshot = Q_stristr(tail, "HEADSHOT");
    k->teamkill = Q_stristr(tail, "Friendly fire") ||
        (k->kteam && k->kteam == k->vteam && strcmp(v, kname));
    return true;
}

// "X killed himself." and the like: a death with no killer
static bool add_death(const char *victim, const char *tail)
{
    static const char *const marks[] = {
        "himself", "herself", "itself", "death", "suicide", "drown", "squish",
        "sank", "melt", "lava", "cratered", "smaller gun", "pin back",
        "saw the light", "found a way out", "blew up"
    };
    char v[FEED_NAME], t[FEED_NAME];
    kill_t *k;
    int i;

    for (i = 0; i < q_countof(marks); i++)
        if (Q_stristr(tail, marks[i]))
            break;
    if (i == q_countof(marks))
        return false;

    clean_copy(v, sizeof(v), victim, SIZE_MAX);
    clean_copy(t, sizeof(t), tail, SIZE_MAX);
    if (!*v || !*t)
        return false;

    k = new_kill();
    Q_strlcpy(k->victim, v, sizeof(k->victim));
    k->vteam = player_team(v);
    if (Q_stristr(t, "killed himself") || Q_stristr(t, "suicide"))
        Q_strlcpy(k->label, "suicide", sizeof(k->label));
    else if (Q_stristr(t, "blew himself up"))
        Q_strlcpy(k->label, "blew up", sizeof(k->label));
    else if (Q_stristr(t, "fell to"))
        Q_strlcpy(k->label, "fell", sizeof(k->label));
    else {
        size_t len = strlen(t);
        if (len && t[len - 1] == '.')
            t[len - 1] = 0;
        Q_strlcpy(k->label, t, sizeof(k->label));
    }
    return true;
}

// a whole obituary in one line: look for the two players' names in it
static bool parse_line(const char *s)
{
    char mid[MAX_STRING_CHARS];
    size_t vlen = name_prefix(s), klen;
    const char *p;

    if (!vlen || s[vlen] != ' ')
        return false;

    for (p = s + vlen + 1; *p; p++) {
        if (p[-1] != ' ' || !(klen = name_prefix(p)))
            continue;
        Q_strlcpy(mid, s + vlen, min(p - (s + vlen) + 1, sizeof(mid)));
        if (add_kill(va("%.*s", (int)vlen, s), mid, va("%.*s", (int)klen, p), p + klen))
            return true;
    }
    return add_death(va("%.*s", (int)vlen, s), s + vlen);
}

static chat_t *new_chat(void)
{
    chat_t *c = &chats[chat_head++ & (FEED_CHATS - 1)];

    memset(c, 0, sizeof(*c));
    c->time = newest_time = demo_time();
    return c;
}

static void add_server_message(const char *s)
{
    char text[FEED_TEXT];
    chat_t *c;

    clean_copy(text, sizeof(text), s, SIZE_MAX);
    if (!*text)
        return;
    c = new_chat();
    c->server = true;
    Q_strlcpy(c->text, text, sizeof(c->text));
}

// obituary pieces collected, a full line is ready
static void flush_pieces(void)
{
    char line[MAX_STRING_CHARS];
    char (*f)[MAX_STRING_CHARS] = acc.text;
    bool done = false;
    int i, n = acc.count;

    acc.count = 0;
    if (!n)
        return;

    if (n == 4)
        done = add_kill(f[0], f[1], f[2], f[3]);
    else if (n == 2)
        done = add_death(f[0], f[1]);

    if (done)
        return;

    line[0] = 0;
    for (i = 0; i < n; i++)
        Q_strlcat(line, f[i], sizeof(line));
    if (!parse_line(line))
        add_server_message(line);
}

// "usa +=EC=+ZeRo: na" or "[MR]+LolPaoAsi+: hola"
static void add_chat(const char *s)
{
    char raw[MAX_STRING_CHARS];
    const char *p = s;
    size_t len;
    chat_t *c = new_chat();

    // D-Day team chat starts with the team: "usa ", "grm "...
    if (strlen(s) > 4 && Q_isalpha(s[0]) && Q_isalpha(s[1]) && Q_isalpha(s[2]) &&
        s[3] == ' ' && (len = name_prefix(s + 4)) && !strncmp(s + 4 + len, ": ", 2)) {
        clean_copy(c->tag, sizeof(c->tag), s, 3);
        p = s + 4;
    }

    len = name_prefix(p);
    if (!len || strncmp(p + len, ": ", 2)) {
        // unknown name: split at the first ": " anyway
        const char *colon = strstr(p, ": ");
        len = colon && colon - p < FEED_NAME ? colon - p : 0;
    }

    if (len) {
        Q_strlcpy(raw, p, min(len + 1, sizeof(raw)));
        clean_copy(c->name, sizeof(c->name), raw, SIZE_MAX);
        c->team = player_team(raw);
        p += len + 2;
    }
    clean_copy(c->text, sizeof(c->text), p, SIZE_MAX);
}

/*
===============
CL_FeedPrint

Every svc_print of a demo goes through here (the console still gets it).
===============
*/
void CL_FeedPrint(int level, const char *s)
{
    if (!watching_demo() || cls.demo.seeking)
        return;

    check_rewind();

    if (level == PRINT_CHAT) {
        flush_pieces();
        add_chat(s);
        return;
    }

    if (level != PRINT_MEDIUM && level != PRINT_HIGH)
        return;

    // pieces of one obituary come in the same frame
    if (acc.count && acc.frame != cl.frame.number)
        flush_pieces();

    if (level == PRINT_HIGH) {
        flush_pieces();
        add_server_message(s);
        return;
    }

    acc.frame = cl.frame.number;
    if (acc.count < MAX_PIECES)
        Q_strlcpy(acc.text[acc.count++], s, sizeof(acc.text[0]));

    if (strchr(s, '\n') || acc.count == MAX_PIECES)
        flush_pieces();
}

/*
===============================================================================

DRAWING

===============================================================================
*/

#define COLOR_ALLIES    MakeColor(120, 175, 255, 255)
#define COLOR_AXIS      MakeColor(255, 115, 95, 255)
#define COLOR_NONE      MakeColor(235, 235, 235, 255)
#define COLOR_LABEL     MakeColor(185, 185, 185, 255)
#define COLOR_HEADSHOT  MakeColor(255, 180, 40, 255)
#define COLOR_TEAMKILL  MakeColor(255, 70, 70, 255)
#define COLOR_SERVER    MakeColor(160, 160, 160, 255)
#define COLOR_TAG       MakeColor(150, 150, 150, 255)
#define COLOR_BG        MakeColor(0, 0, 0, 120)
#define COLOR_BG_MINE   MakeColor(130, 95, 0, 150)

static uint32_t team_color(team_t t)
{
    return t == TEAM_ALLIES ? COLOR_ALLIES : t == TEAM_AXIS ? COLOR_AXIS : COLOR_NONE;
}

static uint32_t with_alpha(uint32_t color, float alpha)
{
    color_t c = { .u32 = color };
    c.u8[3] = Q_rint(c.u8[3] * alpha);
    return c.u32;
}

// 1 while fresh, fades out over the last half second
static float fade(int time, float seconds)
{
    int age = demo_time() - time;
    int life = Q_rint(seconds * 1000);

    if (age < 0)
        age = 0;
    if (age >= life)
        return 0;
    if (life - age < 500)
        return (life - age) / 500.0f;
    return 1;
}

static int draw_text(int x, int y, int flags, const char *s, size_t len,
                     uint32_t color, float alpha, qhandle_t font)
{
    R_SetColor(with_alpha(color, alpha));
    return R_DrawString(x, y, flags, len, s, font);
}

bool CL_FeedKillsOn(void)
{
    return (CL_CaptureActive() ? clip_killfeed : visor_killfeed)->integer;
}

bool CL_FeedChatOn(void)
{
    return (CL_CaptureActive() ? clip_chat : visor_chat)->integer;
}

// our panels replace the console lines at the top while watching a demo
bool CL_FeedHidesNotify(void)
{
    return watching_demo() && (visor_killfeed->integer || visor_chat->integer);
}

static void draw_kills(qhandle_t font, int right, int y)
{
    const char *watched;
    viewkind_t kind;
    int i, lines, max_lines = Cvar_ClampInteger(visor_killfeed_lines, 1, FEED_KILLS);
    const kill_t *list[FEED_KILLS];
    int only_mine = (CL_CaptureActive() ? clip_killfeed : visor_killfeed)->integer == 2;
    float time = Cvar_ClampValue(visor_killfeed_time, 1, 60);

    watched = CL_ViewedPlayer(&kind);

    // newest ones that are still up, oldest first
    lines = 0;
    for (i = 1; i <= FEED_KILLS && lines < max_lines; i++) {
        const kill_t *k = &kills[(kill_head - i) & (FEED_KILLS - 1)];
        if (!k->time || !fade(k->time, time))
            break;
        if (only_mine && watched && strcmp(k->killer, watched) && strcmp(k->victim, watched))
            continue;
        list[lines++] = k;
    }

    for (i = lines - 1; i >= 0; i--) {
        const kill_t *k = list[i];
        float a = fade(k->time, time);
        bool mine = watched && (!strcmp(k->killer, watched) || !strcmp(k->victim, watched));
        // [killer  ]label[ HS][ TK]  victim
        size_t len = strlen(k->label) + 2 + strlen(k->victim);
        int x;

        if (*k->killer)
            len += strlen(k->killer) + 2;
        if (k->headshot)
            len += 3;
        if (k->teamkill)
            len += 3;

        x = right - CONCHAR_WIDTH - len * CONCHAR_WIDTH;
        R_DrawFill32(x - 3, y - 1, len * CONCHAR_WIDTH + 6, CONCHAR_HEIGHT + 2,
                     with_alpha(mine ? COLOR_BG_MINE : COLOR_BG, a));

        if (*k->killer)
            x = draw_text(x, y, 0, k->killer, SIZE_MAX, team_color(k->kteam), a, font) +
                CONCHAR_WIDTH * 2;
        x = draw_text(x, y, 0, k->label, SIZE_MAX,
                      k->teamkill ? COLOR_TEAMKILL : COLOR_LABEL, a, font);
        if (k->headshot)
            x = draw_text(x, y, 0, " HS", SIZE_MAX, COLOR_HEADSHOT, a, font);
        if (k->teamkill)
            x = draw_text(x, y, 0, " TK", SIZE_MAX, COLOR_TEAMKILL, a, font);
        draw_text(x + CONCHAR_WIDTH * 2, y, 0, k->victim, SIZE_MAX, team_color(k->vteam), a, font);

        y += CONCHAR_HEIGHT + 3;
    }
}

// one chat entry, wrapped to cols characters; returns how many rows it
// takes. Drawn with its last row at y_bottom when draw is set.
static int draw_chat(const chat_t *c, int x, int y_bottom, int cols, float a,
                     qhandle_t font, bool draw)
{
    const char *text = c->text;
    size_t width = cols, len = strlen(text), head = 0, first;
    uint32_t color = c->server ? COLOR_SERVER : COLOR_NONE;
    int rows, y;

    if (!c->server) {
        if (*c->tag)
            head += strlen(c->tag) + 3;     // "(usa) "
        if (*c->name)
            head += strlen(c->name) + 2;    // "name: "
    }
    if (head + 8 > width)
        head = 0;   // no room for the name, just the text
    first = width - head;

    rows = 1 + (len > first ? (len - first + width - 1) / width : 0);
    if (!draw)
        return rows;

    y = y_bottom - (rows - 1) * CONCHAR_HEIGHT;

    // see-through band behind each row, like the kill feed
    for (int r = 0; r < rows; r++) {
        size_t n = r ? min(width, len - first - (r - 1) * width) : head + min(first, len);
        R_DrawFill32(x - 3, y + r * CONCHAR_HEIGHT - 1, n * CONCHAR_WIDTH + 6,
                     CONCHAR_HEIGHT + 1, with_alpha(COLOR_BG, a * 0.8f));
    }

    if (head) {
        if (*c->tag) {
            x = draw_text(x, y, UI_DROPSHADOW, "(", 1, COLOR_TAG, a, font);
            x = draw_text(x, y, UI_DROPSHADOW, c->tag, SIZE_MAX, COLOR_TAG, a, font);
            x = draw_text(x, y, UI_DROPSHADOW, ") ", 2, COLOR_TAG, a, font);
        }
        if (*c->name) {
            x = draw_text(x, y, UI_DROPSHADOW, c->name, SIZE_MAX, team_color(c->team), a, font);
            x = draw_text(x, y, UI_DROPSHADOW, ": ", 2, COLOR_NONE, a, font);
        }
        x -= head * CONCHAR_WIDTH;
    }

    draw_text(x + head * CONCHAR_WIDTH, y, UI_DROPSHADOW, text, first, color, a, font);
    text += min(first, len);
    while (*text) {
        y += CONCHAR_HEIGHT;
        draw_text(x, y, UI_DROPSHADOW, text, width, color, a, font);
        text += min(width, strlen(text));
    }
    return rows;
}

static void draw_chats(qhandle_t font, int left, int w, int y_bottom)
{
    int i, rows = 0, max_rows = Cvar_ClampInteger(visor_chat_lines, 1, FEED_CHATS);
    int cols = max(w / 2 / CONCHAR_WIDTH, 20);
    int with_server = (CL_CaptureActive() ? clip_chat : visor_chat)->integer == 2;
    float time = Cvar_ClampValue(visor_chat_time, 1, 120);
    int x = left + CONCHAR_WIDTH;

    // newest at the bottom, older ones above
    for (i = 1; i <= FEED_CHATS; i++) {
        const chat_t *c = &chats[(chat_head - i) & (FEED_CHATS - 1)];
        float a;
        int n;

        if (!c->time || !(a = fade(c->time, time)))
            break;
        if (c->server && !with_server)
            continue;
        n = draw_chat(c, x, 0, cols, a, font, false);
        if (rows + n > max_rows)
            break;
        draw_chat(c, x, y_bottom - rows * CONCHAR_HEIGHT, cols, a, font, true);
        rows += n;
    }
}

/*
===============
CL_FeedDraw

scale: HUD scale (larger = smaller characters). game_hud: the game's own
status bar is on screen, keep clear of it. bottom_rows: rows taken at the
bottom by the viewer's timeline.
===============
*/
void CL_FeedDraw(qhandle_t font, float scale, bool game_hud, int bottom_rows)
{
    int x = 0, y = 0, w = r_config.width, h = r_config.height;
    float size = Cvar_ClampValue(visor_feed_size, 0.5f, 3);
    int top, bottom;

    if (!watching_demo())
        return;

    check_rewind();

    // in a clip, stay inside the part of the window that is recorded
    CL_CaptureRect(&x, &y, &w, &h);

    // D-Day keeps kills and points at the top right, the health at the
    // bottom left. These gaps are in HUD rows, the HUD doesn't change size.
    top = game_hud ? 12 : 2;
    bottom = (game_hud ? 10 : 3) + bottom_rows;

    scale /= size;
    R_SetScale(scale);
    x = Q_rint(x * scale);
    y = Q_rint(y * scale);
    w = Q_rint(w * scale);
    h = Q_rint(h * scale);

    if (CL_FeedKillsOn())
        draw_kills(font, x + w, y + Q_rint(CONCHAR_HEIGHT * top / size));

    if (CL_FeedChatOn())
        draw_chats(font, x, w, y + h - Q_rint(CONCHAR_HEIGHT * bottom / size));

    R_ClearColor();
    R_SetScale(1.0f);
}

void CL_InitFeed(void)
{
    visor_killfeed = Cvar_Get("visor_killfeed", "1", CVAR_ARCHIVE);
    visor_chat = Cvar_Get("visor_chat", "1", CVAR_ARCHIVE);
    clip_killfeed = Cvar_Get("clip_killfeed", "1", CVAR_ARCHIVE);
    clip_chat = Cvar_Get("clip_chat", "0", CVAR_ARCHIVE);
    visor_killfeed_time = Cvar_Get("visor_killfeed_time", "6", CVAR_ARCHIVE);
    visor_killfeed_lines = Cvar_Get("visor_killfeed_lines", "5", CVAR_ARCHIVE);
    visor_chat_time = Cvar_Get("visor_chat_time", "10", CVAR_ARCHIVE);
    visor_chat_lines = Cvar_Get("visor_chat_lines", "6", CVAR_ARCHIVE);
    visor_feed_size = Cvar_Get("visor_feed_size", "1", CVAR_ARCHIVE);
}
