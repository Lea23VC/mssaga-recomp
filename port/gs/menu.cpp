/*
 * MS Saga: the port's settings menu (F1, or START + SELECT on a gamepad), drawn with Dear ImGui (MIT,
 * port/third_party/imgui, licenses/DearImGui-MIT.txt) over the picture, after it is presented (gpu_vk.c gpu_present).
 *
 * While it is open the game gets no buttons (display.c poll_pads) but keeps running. Keyboard: arrows, Enter / Space
 * to choose, Esc to close; gamepad: d-pad, Cross to choose, Circle to go back or close; the mouse works too. Every
 * setting is the one the F-keys and the settings folder already had; each is kept where it was.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

extern "C" {
int widescreen_on(void);                      /* hle/widescreen.c */
void widescreen_toggle(void);
int gpu_vsync(void);                          /* gpu_vk.c */
void gpu_set_vsync(int on);
int gpu_option(int which);
void gpu_set_option(int which, int on);
int turbo_enabled(void);                      /* runtime/turbo.c */
void turbo_toggle(void);
int turbo_factor(void);
void turbo_set_factor(int n);
void display_win_size_next(void);             /* display.c */
int display_panel_on(void);
void display_panel_set(int on);
void display_config_path(char *p, size_t n, const char *name);
int menu_open(void);
}

enum { OPT_HUD_EDGES, OPT_PORTRAITS, OPT_SIDES };   /* gpu_vk.c gpu_option */

static SDL_Window *mwin;
static int ready, open_, confirm_exit;

static int discord_setting(void) {
    char path[1100], buf[8] = "1";
    display_config_path(path, sizeof path, "discord");
    FILE *f = fopen(path, "r");
    if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = '1'; fclose(f); }
    return atoi(buf) != 0;
}

static void discord_set(int on) {
    char path[1100];
    display_config_path(path, sizeof path, "discord");
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", on); fclose(f); }
}

extern "C" int menu_init(SDL_Window *win, SDL_GPUDevice *dev, SDL_GPUTextureFormat fmt) {
    mwin = win;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = NULL;                    /* nothing written next to the program */
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding = 4.0f;
    if (!ImGui_ImplSDL3_InitForSDLGPU(win)) return -1;
    ImGui_ImplSDLGPU3_InitInfo info;
    info.Device = dev;
    info.ColorTargetFormat = fmt;
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) return -1;
    ready = 1;
    return 0;
}

extern "C" int menu_open(void) { return open_; }

extern "C" void menu_toggle(void) {
    open_ = !open_;
    confirm_exit = 0;
}

/* Every SDL event, first: 1 if the menu took it (the game must not see it) */
extern "C" int menu_event(const SDL_Event *e) {
    if (!ready) return 0;
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_F1 && !e->key.repeat) { menu_toggle(); return 1; }
    if (!open_) return 0;
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.scancode == SDL_SCANCODE_ESCAPE && !e->key.repeat) {
        if (confirm_exit) confirm_exit = 0;
        else menu_toggle();
        return 1;
    }
    ImGui_ImplSDL3_ProcessEvent(e);
    return e->type != SDL_EVENT_QUIT && e->type != SDL_EVENT_WINDOW_RESIZED && e->type != SDL_EVENT_GAMEPAD_ADDED &&
           e->type != SDL_EVENT_GAMEPAD_REMOVED;
}

/* What the menu changes is done after the picture is shown (menu_run_pending, from display.c's loop): V-Sync and the
   window size rebuild the window's swapchain, and the menu draws into its texture after choosing (done at once, the
   draw went into a texture that no longer existed: a crash in the driver). */
static void (*pending[16])(void);
static int npending;
static void later(void (*fn)(void)) { if (npending < 16) pending[npending++] = fn; }

extern "C" void menu_run_pending(void) {
    for (int i = 0; i < npending; i++) pending[i]();
    npending = 0;
}

static void toggle_row(const char *label, int on, void (*flip)(void), const char *help) {
    bool v = on != 0;
    if (ImGui::Checkbox(label, &v)) later(flip);
    if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", help);
}

static void flip_vsync(void) { gpu_set_vsync(!gpu_vsync()); }
static void flip_hud(void) { gpu_set_option(OPT_HUD_EDGES, !gpu_option(OPT_HUD_EDGES)); }
static void flip_portraits(void) { gpu_set_option(OPT_PORTRAITS, !gpu_option(OPT_PORTRAITS)); }
static void flip_sides(void) { gpu_set_option(OPT_SIDES, !gpu_option(OPT_SIDES)); }
static void flip_panel(void) { display_panel_set(!display_panel_on()); }
static void flip_discord(void) { discord_set(!discord_setting()); }

static void build(void) {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 size(io.DisplaySize.x * 0.42f < 460 ? 460 : io.DisplaySize.x * 0.42f, 0);
    float scale = io.DisplaySize.y / 720.0f;
    if (scale < 1.0f) scale = 1.0f;
    io.FontGlobalScale = scale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(size.x * scale > io.DisplaySize.x * 0.9f ? io.DisplaySize.x * 0.9f : size.x * scale, 0));
    ImGui::Begin("MS Saga: settings", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_AlwaysAutoResize);
    if (confirm_exit) {
        ImGui::Text("Quit the game? Progress since your last save is lost.");
        if (ImGui::Button("Quit")) { SDL_Quit(); _Exit(0); }
        ImGui::SameLine();
        if (ImGui::Button("Back")) confirm_exit = 0;
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Video")) {
            toggle_row("Widescreen 16:9", widescreen_on(), widescreen_toggle, "More of the scene at the sides (F2). Off: the PS2's 4:3.");
            toggle_row("V-Sync", gpu_vsync(), flip_vsync, "Waits for the screen before each picture: no tearing (F4).");
            int full = (SDL_GetWindowFlags(mwin) & SDL_WINDOW_FULLSCREEN) != 0, w = 0, h = 0;
            SDL_GetWindowSize(mwin, &w, &h);
            char label[64];
            if (full) snprintf(label, sizeof label, "Window: full screen");
            else snprintf(label, sizeof label, "Window: %d x %d", w, h);
            if (ImGui::Button(label)) later(display_win_size_next);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The next size up that fits the screen, then full screen.");
            ImGui::Separator();
            ImGui::TextDisabled("Widescreen layout");
            toggle_row("HUD at the screen's edges", gpu_option(OPT_HUD_EDGES), flip_hud,
                       "Radar, minimap, battle menus and panels at the edges, as in the HD remasters.");
            toggle_row("Portraits at the screen's edges", gpu_option(OPT_PORTRAITS), flip_portraits,
                       "Dialogue portraits (and their text box) at the left or right edge.");
            toggle_row("Picture at the sides of 4:3 screens", gpu_option(OPT_SIDES), flip_sides,
                       "sides.png from the settings folder beside menus and the title; black when off or missing.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gameplay")) {
            toggle_row("Turbo", turbo_enabled(), turbo_toggle,
                       "Gameplay faster on maps and in battles (Tab). Cutscenes, voices and music keep their speed.");
            int f = turbo_factor();
            ImGui::Text("Turbo speed");
            for (int n = 2; n <= 4; n++) {
                char b[8];
                snprintf(b, sizeof b, "x%d", n);
                ImGui::SameLine();
                if (ImGui::RadioButton(b, f == n)) turbo_set_factor(n);         /* no window or swapchain change */
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Misc")) {
            toggle_row("Performance panel", display_panel_on(), flip_panel, "Frame rate and timings at the top left (F3).");
            toggle_row("Discord status", discord_setting(), flip_discord, "Shows in Discord that you're playing. Applies at the next start.");
            ImGui::Separator();
            ImGui::TextDisabled("F9 records a few seconds of graphics for a bug report.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    if (ImGui::Button("Close")) menu_toggle();
    ImGui::SameLine();
    if (ImGui::Button("Exit game")) confirm_exit = 1;
    ImGui::SameLine();
    ImGui::TextDisabled("F1 / START+SELECT");
    ImGui::End();
}

/* gpu_vk.c gpu_present, after the picture is in the window's texture: the menu over it */
extern "C" void menu_draw(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    if (!ready || !open_) return;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    build();
    ImGui::Render();
    ImDrawData *dd = ImGui::GetDrawData();
    if (dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f) return;
    Imgui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    SDL_GPUColorTargetInfo ci = {};
    ci.texture = target;
    ci.load_op = SDL_GPU_LOADOP_LOAD;
    ci.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ci, 1, NULL);
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp);
    SDL_EndGPURenderPass(rp);
}
