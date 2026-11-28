/*
 * MS Saga: the controls, rebindable. Each PS2 button has one keyboard key and one controller button (or trigger);
 * the settings menu (menu.cpp, Controls) changes them and they are kept in the settings folder (file `controls`,
 * one line per button: name, key, controller button, tab-separated; SDL's names for both). Bound per button, not per
 * action: the game only reads the pad, and what a button does depends on the screen.
 *
 * Defaults: arrows = d-pad, Z / X / A / S = Cross / Circle / Square / Triangle, Q / W = L1 / R1, E / R = L2 / R2,
 * Enter = START, Backspace = SELECT; a controller in its usual layout (south = Cross, triggers = L2 / R2, the
 * stick clicks = L3 / R3). The sticks are not rebound: the game reads them as they are.
 *
 * Rebinding (controls_capture): the next key (or controller button) pressed is the button's; one already bound to
 * another button swaps with it, so nothing is left doubled. Esc leaves it as it was, Delete clears it. The keys the
 * port uses itself (F1-F12, Tab, Esc) can't be bound. A controller wait ends by itself after 5 seconds.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "r5900.h"
#include "runtime.h"
#include "pad.h"

void display_config_path(char *p, size_t n, const char *name);     /* display.c */
SDL_Gamepad *display_gamepad(int k);

enum { TRIG_L = 1000, TRIG_R, NONE = -1 };            /* controller "buttons" past SDL's: the triggers */

typedef struct { const char *name, *label; u16 bit; int key, pad, def_key, def_pad; } Bind;
static Bind binds[] = {
    {"up", "Up", PAD_UP, 0, 0, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"down", "Down", PAD_DOWN, 0, 0, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"left", "Left", PAD_LEFT, 0, 0, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"right", "Right", PAD_RIGHT, 0, 0, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"cross", "Cross", PAD_CROSS, 0, 0, SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH},
    {"circle", "Circle", PAD_CIRCLE, 0, 0, SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_EAST},
    {"square", "Square", PAD_SQUARE, 0, 0, SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_WEST},
    {"triangle", "Triangle", PAD_TRIANGLE, 0, 0, SDL_SCANCODE_S, SDL_GAMEPAD_BUTTON_NORTH},
    {"l1", "L1", PAD_L1, 0, 0, SDL_SCANCODE_Q, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"r1", "R1", PAD_R1, 0, 0, SDL_SCANCODE_W, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"l2", "L2", PAD_L2, 0, 0, SDL_SCANCODE_E, TRIG_L},
    {"r2", "R2", PAD_R2, 0, 0, SDL_SCANCODE_R, TRIG_R},
    {"l3", "L3", PAD_L3, 0, 0, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"r3", "R3", PAD_R3, 0, 0, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"start", "START", PAD_START, 0, 0, SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START},
    {"select", "SELECT", PAD_SELECT, 0, 0, SDL_SCANCODE_BACKSPACE, SDL_GAMEPAD_BUTTON_BACK},
};
enum { NB = sizeof binds / sizeof *binds };
static int loaded;

static const char *pad_string(int p) {
    if (p == TRIG_L) return "lefttrigger";
    if (p == TRIG_R) return "righttrigger";
    const char *s = p >= 0 ? SDL_GetGamepadStringForButton((SDL_GamepadButton)p) : NULL;
    return s ? s : "none";
}

static int pad_from_string(const char *s) {
    if (!strcmp(s, "lefttrigger")) return TRIG_L;
    if (!strcmp(s, "righttrigger")) return TRIG_R;
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(s);
    return b == SDL_GAMEPAD_BUTTON_INVALID ? NONE : (int)b;
}

static void save(void) {
    char path[1100];
    display_config_path(path, sizeof path, "controls");
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < NB; i++)
        fprintf(f, "%s\t%s\t%s\n", binds[i].name, binds[i].key ? SDL_GetScancodeName((SDL_Scancode)binds[i].key) : "none",
                pad_string(binds[i].pad));
    fclose(f);
}

static void load(void) {
    if (loaded) return;
    loaded = 1;
    for (int i = 0; i < NB; i++) { binds[i].key = binds[i].def_key; binds[i].pad = binds[i].def_pad; }
    char path[1100], line[200];
    display_config_path(path, sizeof path, "controls");
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *k = strchr(line, '\t'), *p = k ? strchr(k + 1, '\t') : NULL;
        if (!p) continue;
        *k++ = 0;
        *p++ = 0;
        for (int i = 0; i < NB; i++)
            if (!strcmp(binds[i].name, line)) {
                binds[i].key = strcmp(k, "none") ? (int)SDL_GetScancodeFromName(k) : 0;
                binds[i].pad = pad_from_string(p);
            }
    }
    fclose(f);
}

static int pad_down(SDL_Gamepad *g, int p) {
    if (!g || p == NONE) return 0;
    if (p == TRIG_L) return SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
    if (p == TRIG_R) return SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
    return SDL_GetGamepadButton(g, (SDL_GamepadButton)p);
}

/* display.c: the PS2 buttons held now, from the keyboard / from a controller */
u16 controls_keyboard(void) {
    load();
    const bool *k = SDL_GetKeyboardState(NULL);
    u16 b = 0;
    for (int i = 0; i < NB; i++)
        if (binds[i].key && k[binds[i].key]) b |= binds[i].bit;
    return b;
}

u16 controls_gamepad(SDL_Gamepad *g) {
    load();
    u16 b = 0;
    for (int i = 0; i < NB; i++)
        if (pad_down(g, binds[i].pad)) b |= binds[i].bit;
    return b;
}

/* ---- the settings menu ---- */
static int cap_i = -1, cap_kind;                  /* waiting for a key (1) or a controller button (2) for cap_i */
static Uint64 cap_since;
static int last_pad = NONE;                        /* the controller button just bound: the menu ignores it until let go */

int controls_count(void) { load(); return NB; }
const char *controls_label(int i) { return i >= 0 && i < NB ? binds[i].label : ""; }

const char *controls_key_name(int i) {
    load();
    if (i < 0 || i >= NB || !binds[i].key) return "-";
    const char *s = SDL_GetScancodeName((SDL_Scancode)binds[i].key);
    return s && *s ? s : "?";
}

/* a controller button's name as written on the controller connected (PlayStation, Xbox, Nintendo), or both
   usual names when none is connected or its kind is unknown */
static const char *pad_name_for(int p, SDL_Gamepad *g) {
    SDL_GamepadType t = g ? SDL_GetGamepadType(g) : SDL_GAMEPAD_TYPE_UNKNOWN;
    int ps = t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5;
    int xb = t == SDL_GAMEPAD_TYPE_XBOX360 || t == SDL_GAMEPAD_TYPE_XBOXONE;
    int nin = t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
              t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    if (p == SDL_GAMEPAD_BUTTON_SOUTH || p == SDL_GAMEPAD_BUTTON_EAST || p == SDL_GAMEPAD_BUTTON_WEST ||
        p == SDL_GAMEPAD_BUTTON_NORTH) {
        if (ps || xb || nin)
            switch (SDL_GetGamepadButtonLabelForType(t, (SDL_GamepadButton)p)) {
            case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
            case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
            case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
            case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
            case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
            case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
            case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
            case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
            default: break;
            }
        return p == SDL_GAMEPAD_BUTTON_SOUTH ? "A / Cross" : p == SDL_GAMEPAD_BUTTON_EAST ? "B / Circle"
             : p == SDL_GAMEPAD_BUTTON_WEST ? "X / Square" : "Y / Triangle";
    }
    switch (p) {
    case NONE: return "-";
    case TRIG_L: return ps ? "L2" : xb ? "LT" : nin ? "ZL" : "LT / L2";
    case TRIG_R: return ps ? "R2" : xb ? "RT" : nin ? "ZR" : "RT / R2";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ps ? "L1" : xb ? "LB" : nin ? "L" : "LB / L1";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ps ? "R1" : xb ? "RB" : nin ? "R" : "RB / R1";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return ps ? "L3" : xb ? "LS (left stick click)" : "Left stick click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return ps ? "R3" : xb ? "RS (right stick click)" : "Right stick click";
    case SDL_GAMEPAD_BUTTON_BACK:
        return t == SDL_GAMEPAD_TYPE_PS5 ? "Create" : t == SDL_GAMEPAD_TYPE_PS4 ? "Share" : t == SDL_GAMEPAD_TYPE_PS3 ? "Select"
             : t == SDL_GAMEPAD_TYPE_XBOXONE ? "View" : t == SDL_GAMEPAD_TYPE_XBOX360 ? "Back" : nin ? "-  (minus)" : "Back / Select";
    case SDL_GAMEPAD_BUTTON_START:
        return t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5 ? "Options" : t == SDL_GAMEPAD_TYPE_XBOXONE ? "Menu"
             : nin ? "+  (plus)" : "Start";
    case SDL_GAMEPAD_BUTTON_GUIDE: return ps ? "PS button" : xb ? "Xbox button" : nin ? "Home" : "Guide";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad right";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    case SDL_GAMEPAD_BUTTON_MISC1: return nin ? "Capture" : xb ? "Share" : ps ? "Mic" : "Misc";
    default: return pad_string(p);
    }
}

const char *controls_pad_name(int i) {
    load();
    return i >= 0 && i < NB ? pad_name_for(binds[i].pad, display_gamepad(0)) : "";
}

/* the first controller's name (it plays as the PS2's first pad), or NULL */
const char *controls_gamepad_name(void) {
    SDL_Gamepad *g = display_gamepad(0);
    if (!g) return NULL;
    const char *n = SDL_GetGamepadName(g);
    return n && *n ? n : "Controller";
}

/* the row's button held now (keyboard or the first controller): the menu lights it */
int controls_held(int i) {
    load();
    if (i < 0 || i >= NB) return 0;
    const bool *k = SDL_GetKeyboardState(NULL);
    return (binds[i].key && k[binds[i].key]) || pad_down(display_gamepad(0), binds[i].pad);
}

void controls_capture(int i, int kind) { cap_i = i; cap_kind = kind; cap_since = SDL_GetTicks(); }
void controls_cancel(void) { cap_i = -1; }

/* 1 key, 2 controller button, 0 none: what controls_capture waits for (for i, or any if i < 0) */
int controls_capturing(int i) {
    if (cap_i >= 0 && cap_kind == 2 && SDL_GetTicks() - cap_since > 5000) cap_i = -1;
    return cap_i >= 0 && (i < 0 || i == cap_i) ? cap_kind : 0;
}

/* the menu leaves the controller alone (no navigation) while one is waited for, and until the one just bound is let go */
int controls_pad_busy(void) {
    if (controls_capturing(-1) == 2) return 1;
    if (last_pad != NONE && pad_down(display_gamepad(0), last_pad)) return 1;
    last_pad = NONE;
    return 0;
}

void controls_reset(void) {
    load();
    for (int i = 0; i < NB; i++) { binds[i].key = binds[i].def_key; binds[i].pad = binds[i].def_pad; }
    cap_i = -1;
    save();
    rt_log("controls", "back to the defaults");
}

static int reserved_key(SDL_Scancode s) {
    return (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) || s == SDL_SCANCODE_TAB || s == SDL_SCANCODE_ESCAPE;
}

static void bind_key(int i, int key) {
    for (int j = 0; j < NB; j++)
        if (j != i && key && binds[j].key == key) binds[j].key = binds[i].key;   /* swapped */
    binds[i].key = key;
    save();
    rt_log("controls", "%s: key %s", binds[i].label, controls_key_name(i));
}

static void bind_pad(int i, int pad) {
    for (int j = 0; j < NB; j++)
        if (j != i && pad != NONE && binds[j].pad == pad) binds[j].pad = binds[i].pad;
    binds[i].pad = pad;
    last_pad = pad;
    save();
    rt_log("controls", "%s: controller %s", binds[i].label, controls_pad_name(i));
}

/* menu.cpp, each SDL event first while the menu is open: 1 if it was the binding being waited for (or its Esc) */
int controls_event(const SDL_Event *e) {
    int kind = controls_capturing(-1);
    if (!kind) return 0;
    int i = cap_i;
    if (e->type == SDL_EVENT_KEY_DOWN) {
        if (e->key.repeat) return 1;
        SDL_Scancode s = e->key.scancode;
        if (s == SDL_SCANCODE_ESCAPE) { cap_i = -1; return 1; }
        if (s == SDL_SCANCODE_DELETE) {
            cap_i = -1;
            if (kind == 1) bind_key(i, 0);
            else bind_pad(i, NONE);
            return 1;
        }
        if (kind == 1 && !reserved_key(s)) { cap_i = -1; bind_key(i, (int)s); }
        return 1;
    }
    if (e->type == SDL_EVENT_KEY_UP) return 1;
    if (kind != 2) return 0;
    SDL_Gamepad *g = display_gamepad(0);
    SDL_JoystickID first = g ? SDL_GetGamepadID(g) : 0;
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.which == first) {
        cap_i = -1;
        bind_pad(i, e->gbutton.button);
        return 1;
    }
    if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e->gaxis.which == first && e->gaxis.value > 16000 &&
        (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        cap_i = -1;
        bind_pad(i, e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? TRIG_L : TRIG_R);
        return 1;
    }
    return e->type == SDL_EVENT_GAMEPAD_BUTTON_UP || e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
}

/* START + SELECT on the first controller opens the menu: the buttons bound to them */
int controls_menu_combo(SDL_Gamepad *g) {
    load();
    int start = NONE, select = NONE;
    for (int i = 0; i < NB; i++) {
        if (binds[i].bit == PAD_START) start = binds[i].pad;
        if (binds[i].bit == PAD_SELECT) select = binds[i].pad;
    }
    return pad_down(g, start) && pad_down(g, select);
}
