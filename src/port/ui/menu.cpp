/* Start screen and in-game menu, drawn with Dear ImGui over SDL3's renderer. Keyboard, mouse
 * and gamepad navigation all work. */
#include "port/ui/menu.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "menu_font.h"

extern "C" {
#include "port/disc.h"
#include "port/hw/gpu.h"
#include "port/hw/spu.h"
#include "port/hw/widescreen.h"
#include "port/input.h"
#include "port/patches.h"
#include "port/recomp.h"
#include "port/savestate.h"
#include "port/settings.h"
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

SDL_Window *g_window;
SDL_Renderer *g_renderer;

enum class Page { Main, Settings, Bindings };

/* File dialog results arrive via callback (possibly on another thread). */
std::atomic<bool> g_dialog_done{false};
char g_dialog_path[1024];

void SDLCALL on_file_chosen(void *, const char *const *files, int) {
    if (files != nullptr && files[0] != nullptr) {
        std::snprintf(g_dialog_path, sizeof g_dialog_path, "%s", files[0]);
    } else {
        g_dialog_path[0] = '\0';
    }
    g_dialog_done = true;
}

void open_disc_dialog() {
    g_dialog_done = false;
#ifdef __ANDROID__
    // Providers assign different MIME types to CUE sheets and raw BIN files.
    SDL_ShowOpenFileDialog(on_file_chosen, nullptr, g_window, nullptr, 0, nullptr, true);
#else
    static const SDL_DialogFileFilter filters[] = {{"Disc image or cue sheet", "chd;cue;bin"}};
    SDL_ShowOpenFileDialog(on_file_chosen, nullptr, g_window, filters, 1,
                           settings.disc_path[0] ? settings.disc_path : nullptr, false);
#endif
}

const ImVec4 ink(0.95f, 0.91f, 0.84f, 1.0f), muted(0.75f, 0.68f, 0.62f, 1.0f);
const ImVec4 copper(0.93f, 0.67f, 0.44f, 1.0f), wine(0.40f, 0.14f, 0.16f, 1.0f);
float ui_scale = 1.0f;
SDL_Rect safe_area;
SDL_FRect scroll_area;
float scroll_delta;

void apply_style(float scale) {
    ImGuiStyle &s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.FontSizeBase = 18;
    s.FontScaleMain = scale;
    s.WindowPadding = ImVec2(20, 18);
    s.ItemSpacing = ImVec2(6, 6);
    s.FramePadding = ImVec2(12, 13);
    s.GrabMinSize = 18;
    s.ScrollbarSize = 14;
    s.FrameRounding = 3;
    s.WindowRounding = 6;
    s.ScaleAllSizes(scale);
    ImVec4 *c = s.Colors;
    c[ImGuiCol_Text] = ink;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_WindowBg] = ImVec4(0.085f, 0.065f, 0.076f, 1);
    c[ImGuiCol_Border] = ImVec4(0.32f, 0.24f, 0.23f, 1);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.125f, 0.14f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.18f, 0.19f, 1);
    c[ImGuiCol_ButtonActive] = wine;
    c[ImGuiCol_FrameBg] = c[ImGuiCol_Button];
    c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_ButtonHovered];
    c[ImGuiCol_FrameBgActive] = wine;
    c[ImGuiCol_SliderGrab] = ImVec4(0.57f, 0.27f, 0.22f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.64f, 0.31f, 0.24f, 1);
    c[ImGuiCol_NavCursor] = copper;
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
}

void update_scale() {
    if (!SDL_GetWindowSafeArea(g_window, &safe_area) || safe_area.w <= 0 || safe_area.h <= 0) {
        safe_area = {};
        SDL_GetWindowSize(g_window, &safe_area.w, &safe_area.h);
    }
    // SDL reports mouse and window coordinates in points on Retina, in pixels on Android.
    float density = SDL_GetWindowPixelDensity(g_window);
    float factor = density > 0 ? SDL_GetWindowDisplayScale(g_window) / density : 1;
    float scale = std::max(
        0.5f, std::min({std::max(1.0f, factor), safe_area.w / 300.0f, safe_area.h / 240.0f}));
    if (scale != ui_scale) {
        apply_style(scale);
        ui_scale = scale;
    }
}

/* Button mapping: while waiting for a key or gamepad button, input goes to the binding instead of
 * the menu. */
enum class Capture { None, Key, Pad, HotkeyKey, HotkeyPad };
Capture g_capture = Capture::None;
int g_capture_button;

/* Assigns a game-button binding, removing it from any other button or hotkey that had it. */
void bind(int *table, int value) {
    bool keys = table == settings.key_bind;
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        if (table[i] == value) {
            table[i] = keys ? 0 : INPUT_UNBOUND;
        }
    }
    for (int i = 0; i < HOTKEY_COUNT; i++) {
        if (keys && settings.hotkey_key[i] == value && settings.hotkey_mods[i] == 0) {
            settings.hotkey_key[i] = 0;
        } else if (!keys && settings.hotkey_pad[i] == value) {
            settings.hotkey_pad[i] = INPUT_UNBOUND;
        }
    }
    table[g_capture_button] = value;
    settings_save();
}

/* Assigns a hotkey key (with modifiers) or gamepad button, removing conflicts. */
void bind_hotkey_key(int key, int mods) {
    for (int i = 0; i < HOTKEY_COUNT; i++) {
        if (settings.hotkey_key[i] == key && settings.hotkey_mods[i] == mods) {
            settings.hotkey_key[i] = 0;
        }
    }
    if (mods == 0) {
        for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
            if (settings.key_bind[i] == key) {
                settings.key_bind[i] = 0;
            }
        }
    }
    settings.hotkey_key[g_capture_button] = key;
    settings.hotkey_mods[g_capture_button] = mods;
    settings_save();
}

void bind_hotkey_pad(int button) {
    for (int i = 0; i < HOTKEY_COUNT; i++) {
        if (settings.hotkey_pad[i] == button) {
            settings.hotkey_pad[i] = INPUT_UNBOUND;
        }
    }
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        if (settings.pad_bind[i] == button) {
            settings.pad_bind[i] = INPUT_UNBOUND;
        }
    }
    settings.hotkey_pad[g_capture_button] = button;
    settings_save();
}

/* Returns true if the event was consumed by a pending capture. */
bool capture_event(const SDL_Event &e) {
    if (g_capture == Capture::None) {
        return false;
    }
    if (e.type == SDL_EVENT_KEY_DOWN &&
        (e.key.scancode == SDL_SCANCODE_ESCAPE || e.key.scancode == SDL_SCANCODE_AC_BACK)) {
        g_capture = Capture::None; /* cancel */
        return true;
    }
    if (g_capture == Capture::HotkeyKey && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        /* Wait for the main key; modifiers held with it become part of the hotkey. */
        if (!input_is_modifier_key(e.key.scancode) && !input_key_reserved(e.key.scancode)) {
            bind_hotkey_key((int)e.key.scancode, input_mods_from_sdl(e.key.mod));
            g_capture = Capture::None;
        }
        return true;
    }
    if (g_capture == Capture::HotkeyPad) {
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            e.gbutton.button != SDL_GAMEPAD_BUTTON_GUIDE) {
            bind_hotkey_pad((int)e.gbutton.button);
            g_capture = Capture::None;
            return true;
        }
        return e.type == SDL_EVENT_GAMEPAD_BUTTON_UP || e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION ||
               e.type == SDL_EVENT_KEY_UP;
    }
    if (g_capture == Capture::Key && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (!input_key_reserved(e.key.scancode)) {
            bind(settings.key_bind, (int)e.key.scancode);
            g_capture = Capture::None;
        }
        return true;
    }
    if (g_capture == Capture::Pad) {
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            e.gbutton.button != SDL_GAMEPAD_BUTTON_GUIDE) {
            bind(settings.pad_bind, (int)e.gbutton.button);
            g_capture = Capture::None;
            return true;
        }
        if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.value > 16000 &&
            (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
             e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            bind(settings.pad_bind, e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                        ? INPUT_PAD_LEFT_TRIGGER
                                        : INPUT_PAD_RIGHT_TRIGGER);
            g_capture = Capture::None;
            return true;
        }
        /* Keep other gamepad input (sticks, releases) away from menu navigation meanwhile. */
        return e.type == SDL_EVENT_GAMEPAD_BUTTON_UP || e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    }
    return e.type == SDL_EVENT_KEY_UP;
}

// Delay touch presses in a scrollable list until a tap or a horizontal slider drag
// is clear. A vertical swipe must not activate the row it starts on.
bool scroll_touch_list(const SDL_Event &event) {
    enum class Gesture { Idle, Pending, Scrolling };
    static Gesture gesture = Gesture::Idle;
    static SDL_Event down;
    static float previous_y;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.which == SDL_TOUCH_MOUSEID) {
        SDL_FPoint pos = {event.button.x, event.button.y};
        if (scroll_area.h > 0 && SDL_PointInRectFloat(&pos, &scroll_area)) {
            down = event;
            previous_y = pos.y;
            gesture = Gesture::Pending;
            return true;
        }
    } else if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID &&
               gesture != Gesture::Idle) {
        if (gesture == Gesture::Pending) {
            float dx = std::fabs(event.motion.x - down.button.x);
            float dy = std::fabs(event.motion.y - down.button.y);
            if (std::max(dx, dy) < 8 * ui_scale)
                return true;
            if (dx > dy) {
                gesture = Gesture::Idle;
                ImGui_ImplSDL3_ProcessEvent(&down);
                return false;
            }
            gesture = Gesture::Scrolling;
        }
        scroll_delta += previous_y - event.motion.y;
        previous_y = event.motion.y;
        return true;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.which == SDL_TOUCH_MOUSEID) {
        if (gesture == Gesture::Pending)
            ImGui_ImplSDL3_ProcessEvent(&down);
        bool consumed = gesture == Gesture::Scrolling;
        gesture = Gesture::Idle;
        return consumed;
    }
    return false;
}

/* Returns false if the window was closed. */
bool pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type != SDL_EVENT_QUIT && (capture_event(e) || scroll_touch_list(e))) {
            continue;
        }
        ImGui_ImplSDL3_ProcessEvent(&e);
        if (e.type == SDL_EVENT_QUIT) {
            return false;
        }
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            SDL_OpenGamepad(e.gdevice.which);
        }
    }
    return true;
}

void begin_frame() {
    update_scale();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

/* Debugging aid: NC_MENU_SHOT=1 saves the menu to build/shots/menu.bmp after ~1 second. */
void debug_shot() {
    static int frames = -1;
    if (frames < 0) {
        frames = SDL_getenv("NC_MENU_SHOT") != nullptr ? 0 : -2;
    }
    if (frames < 0 || ++frames != 60) {
        return;
    }
    SDL_Surface *surf = SDL_RenderReadPixels(g_renderer, nullptr);
    if (surf != nullptr) {
        SDL_CreateDirectory("build/shots");
        SDL_SaveBMP(surf, "build/shots/menu.bmp");
        SDL_DestroySurface(surf);
    }
}

void end_frame() {
    ImGui::Render();
    /* ImGui works in window points; scale its draw data to the pixel framebuffer. */
    const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
    SDL_SetRenderScale(g_renderer, fb.x, fb.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer);
    SDL_SetRenderScale(g_renderer, 1.0f, 1.0f);
    debug_shot();
    SDL_RenderPresent(g_renderer);
}

bool begin_panel(const char *id, Page page) {
    float gutter = 10 * ui_scale;
    float width = std::min(safe_area.w - 2 * gutter, (page == Page::Main ? 440 : 540) * ui_scale);
    float height = std::max(1.0f, safe_area.h - 2 * gutter);
    ImGui::SetNextWindowPos(
        ImVec2(safe_area.x + safe_area.w * 0.5f, safe_area.y + safe_area.h * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, height));
    ImGui::SetNextWindowSize(
        ImVec2(std::max(1.0f, width), page == Page::Main ? 0 : std::min(height, 590 * ui_scale)));
    scroll_area = {};
    return ImGui::Begin(id, nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
}

bool wide_button(const char *label) {
    return ImGui::Button(label, ImVec2(-FLT_MIN, 0));
}

void heading(const char *label, bool subpage) {
    if (subpage) {
        ImGui::PushFont(nullptr, 14);
        ImGui::TextColored(copper, "NIGHTMARE CREATURES");
        ImGui::PopFont();
    }
    ImGui::PushFont(nullptr, 32);
    ImGui::TextWrapped("%s", label);
    ImGui::PopFont();
    ImGui::Separator();
}

bool back_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
           ImGui::IsKeyPressed(ImGuiKey_AppBack, false) ||
           ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
}

// A full-width target keeps the name and current value together for touch and gamepad users.
// A long binding or a narrow window moves the value below the name.
bool value_row(const char *label, const char *value, bool accent = true) {
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    float inner = std::max(1.0f, width - 2 * pad.x);
    ImVec2 name_size = ImGui::CalcTextSize(label, nullptr, false, inner);
    ImVec2 value_size = ImGui::CalcTextSize(value, nullptr, false, inner);
    bool stack = name_size.x + value_size.x + pad.x > inner;
    float height = 2 * pad.y + (stack ? name_size.y + value_size.y : name_size.y);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool pressed = ImGui::Button("##row", ImVec2(width, height));
    ImGui::PopID();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddText(nullptr, 0, ImVec2(pos.x + pad.x, pos.y + pad.y), ImGui::GetColorU32(ink), label,
                  nullptr, inner);
    float x = stack ? pad.x : width - pad.x - value_size.x;
    float y = pad.y + (stack ? name_size.y : 0);
    draw->AddText(nullptr, 0, ImVec2(pos.x + x, pos.y + y),
                  ImGui::GetColorU32(accent ? copper : muted), value, nullptr, inner);
    return pressed;
}

bool toggle_row(const char *label, bool &value) {
    if (!value_row(label, value ? "On" : "Off", value))
        return false;
    value = !value;
    return true;
}

bool slider_row(const char *label, int *value, int lo, int hi, const char *format) {
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::SliderInt("##slider", value, lo, hi, format);
    ImGui::PopID();
    return changed;
}

int tabs(const char *const *labels, int count, int selected) {
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float width = (ImGui::GetContentRegionAvail().x - (count - 1) * gap) / count;
    for (int i = 0; i < count; i++) {
        if (i)
            ImGui::SameLine();
        bool active = i == selected;
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, wine);
        if (ImGui::Button(labels[i], ImVec2(i == count - 1 ? -FLT_MIN : width, 0)))
            selected = i;
        if (active) {
            ImGui::PopStyleColor();
            ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, hi.y - ui_scale),
                                                ImVec2(hi.x, hi.y - ui_scale),
                                                ImGui::GetColorU32(copper), 2 * ui_scale);
        }
    }
    return selected;
}

void begin_list(const char *id) {
    // Reserve a fixed Back button below the independently scrolling list.
    ImGui::BeginChild(id, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    if (ImGui::GetScrollMaxY() > 0) {
        scroll_area = {pos.x, pos.y, size.x - ImGui::GetStyle().ScrollbarSize, size.y};
        if (scroll_delta)
            ImGui::SetScrollY(ImGui::GetScrollY() + scroll_delta);
    }
    scroll_delta = 0;
}

void bindings_page(bool *back) {
    static int device = 0;
    const char *devices[] = {"Keyboard", "Gamepad"};
    heading("Button mapping", true);
    int next = tabs(devices, 2, device);
    if (next != device)
        g_capture = Capture::None;
    device = next;
    bool keyboard = device == 0;
    begin_list(devices[device]);
    ImGui::TextWrapped("Select an action, then press the %s to assign.",
                       keyboard ? "key" : "gamepad button");
    for (int i = 0; i < INPUT_BUTTON_COUNT + HOTKEY_COUNT; i++) {
        bool hotkey = i >= INPUT_BUTTON_COUNT;
        int index = hotkey ? i - INPUT_BUTTON_COUNT : i;
        if (index == 0)
            ImGui::SeparatorText(hotkey ? "Shortcuts" : "PS1 buttons");
        Capture kind = hotkey ? (keyboard ? Capture::HotkeyKey : Capture::HotkeyPad)
                              : (keyboard ? Capture::Key : Capture::Pad);
        const char *value;
        if (g_capture == kind && g_capture_button == index) {
            value = keyboard ? "Press a key..." : "Press a button...";
        } else if (hotkey && keyboard) {
            value = input_hotkey_label(settings.hotkey_key[index], settings.hotkey_mods[index]);
        } else if (keyboard) {
            value = input_key_label(settings.key_bind[index]);
        } else {
            value = input_pad_label(hotkey ? settings.hotkey_pad[index] : settings.pad_bind[index]);
        }
        if (value_row(hotkey ? input_hotkeys[index].name : input_buttons[index].name, value)) {
            g_capture = kind;
            g_capture_button = index;
        }
    }
    ImGui::TextWrapped(
        "F5 and F9 always quick save and load. The left stick also works as the D-pad.");
    if (wide_button("Reset bindings to defaults")) {
        for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
            settings.key_bind[i] = input_default_key(i);
            settings.pad_bind[i] = input_default_pad(i);
        }
        for (int i = 0; i < HOTKEY_COUNT; i++) {
            input_default_hotkey(i, &settings.hotkey_key[i], &settings.hotkey_mods[i],
                                 &settings.hotkey_pad[i]);
        }
        g_capture = Capture::None;
        settings_save();
    }
    ImGui::EndChild();
    if (wide_button("Back") || (g_capture == Capture::None && back_pressed())) {
        g_capture = Capture::None;
        *back = true;
    }
}

void settings_page(bool *back, bool *open_bindings) {
    static int section = 0;
    const char *sections[] = {"Play", "Audio", "Video"};
    heading("Settings", true);
    section = tabs(sections, 3, section);
    begin_list(sections[section]);
    bool changed = false;
    switch (section) {
    case 0:
        ImGui::SeparatorText("Movement");
        if (value_row("Control scheme", settings.controls == CONTROLS_TANK ? "Tank" : "Modern")) {
            settings.controls =
                settings.controls == CONTROLS_TANK ? CONTROLS_MODERN : CONTROLS_TANK;
            changed = true;
        }
        if (settings.controls == CONTROLS_MODERN)
            ImGui::TextWrapped("Modern controls are camera-relative and experimental.");
        changed |= toggle_row("Adrenaline system", settings.adrenaline);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When off, the meter is hidden and never drains health.");
        ImGui::SeparatorText("Camera");
        changed |= toggle_row("Mouse / right stick camera", settings.mouse_camera);
        if (settings.mouse_camera)
            changed |= slider_row("Mouse sensitivity", &settings.mouse_sensitivity, 1, 100, "%d");
        ImGui::Spacing();
        *open_bindings = value_row("Button mapping", ">");
        break;
    case 1:
        ImGui::SeparatorText("Volume");
        changed |= slider_row("Master", &settings.volume_master, 0, 100, "%d%%");
        changed |= slider_row("Music", &settings.volume_music, 0, 100, "%d%%");
        changed |= slider_row("Effects", &settings.volume_sfx, 0, 100, "%d%%");
        break;
    case 2: {
        ImGui::SeparatorText("Display");
        changed |= toggle_row("Fullscreen", settings.fullscreen);
        changed |= toggle_row("Widescreen (16:9)", settings.widescreen);
        changed |= toggle_row("Show FPS", settings.show_fps);
        ImGui::SeparatorText("Rendering");
        if (value_row("Screen filtering", settings.filter == FILTER_NEAREST ? "Sharp" : "Smooth")) {
            settings.filter = settings.filter == FILTER_NEAREST ? FILTER_BILINEAR : FILTER_NEAREST;
            changed = true;
        }
        changed |= slider_row("Resolution", &settings.render_scale, 1, 8, "%dx native");
        if (settings.render_scale > 3)
            ImGui::TextWrapped("Above 3x, the game may slow down.");
        static const char *modes[] = {"Off", "xBRZ 2x", "xBRZ 3x", "xBRZ 4x"};
        ImGui::BeginDisabled(settings.render_scale <= 1);
        if (value_row("Texture upscaling", modes[settings.texture_scale - 1])) {
            settings.texture_scale = settings.texture_scale % 4 + 1;
            changed = true;
        }
        ImGui::EndDisabled();
        if (settings.render_scale <= 1)
            ImGui::TextWrapped("Texture upscaling needs a resolution above 1x.");
        break;
    }
    }
    ImGui::EndChild();
    *back = wide_button("Back") || back_pressed();
    if (changed) {
        menu_apply_settings();
        settings_save();
    }
}

const char *disc_status_text(DiscCheck check) {
    switch (check) {
    case DISC_OK:
        return "Disc verified: Nightmare Creatures (USA, SLUS-00582)";
    case DISC_WRONG_IMAGE:
        return "This image is not the supported disc (Nightmare Creatures USA, SLUS-00582).";
    case DISC_BAD_CUE:
        return "The CUE sheet or track sizes do not match the 26-track USA disc.";
    case DISC_MISSING_TRACK:
#ifdef __ANDROID__
        return "Missing BIN track. Select the CUE and all its BIN files together.";
#else
        return "Missing BIN track. Check the filenames and paths listed in the CUE sheet.";
#endif
    default:
#ifdef __ANDROID__
        return "Choose a CHD, a full-disc BIN, or select the CUE and all its BIN files together.";
#else
        return "Choose a CHD, a full-disc BIN, or a CUE sheet.";
#endif
    }
}

} // namespace

extern "C" void menu_init(SDL_Window *window, SDL_Renderer *renderer) {
    g_window = window;
    g_renderer = renderer;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    ImFontConfig font;
    font.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(menu_font), sizeof menu_font, 18,
                                   &font);
    apply_style(ui_scale);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
}

extern "C" void menu_shutdown(void) {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

extern "C" void menu_apply_settings(void) {
    patches_apply();
    gpu_set_scale(settings.render_scale);
    gpu_set_texture_scale(settings.texture_scale);
    ws_set_enabled(settings.widescreen);
    spu_set_gains(settings.volume_master * 256 / 100, settings.volume_music * 256 / 100,
                  settings.volume_sfx * 256 / 100);
    SDL_SetWindowFullscreen(g_window, settings.fullscreen);
#ifndef __ANDROID__
    /* Reshape a windowed game to the chosen aspect ratio (keeping its height). */
    static int applied_widescreen = -1;
    if (applied_widescreen != (int)settings.widescreen) {
        applied_widescreen = settings.widescreen;
        if (!settings.fullscreen) {
            int w = 0, h = 0;
            SDL_GetWindowSize(g_window, &w, &h);
            SDL_SetWindowSize(g_window, settings.widescreen ? h * 16 / 9 : h * 4 / 3, h);
        }
    }
#endif
}

/* Menus need the cursor: leave free-look's relative mouse mode. */
static void release_mouse() {
    SDL_SetWindowRelativeMouseMode(g_window, false);
}

extern "C" MenuResult menu_run_start(void) {
    release_mouse();
    const char *shot = SDL_getenv("NC_MENU_SHOT");
    Page page = Page::Main;
    if (shot != nullptr && std::strcmp(shot, "settings") == 0) {
        page = Page::Settings;
    } else if (shot != nullptr && std::strcmp(shot, "bindings") == 0) {
        page = Page::Bindings;
    }
    DiscCheck check = disc_check(settings.disc_path);
    bool dialog_open = false;
    bool has_resume = savestate_has_resume();

    for (;;) {
        if (!pump_events()) {
            return MENU_QUIT;
        }
        if (dialog_open && g_dialog_done) {
            dialog_open = false;
            if (g_dialog_path[0] != '\0') {
                std::snprintf(settings.disc_path, sizeof settings.disc_path, "%s", g_dialog_path);
                check = disc_check(settings.disc_path);
                settings_save();
            }
        }

        begin_frame();
        if (begin_panel("##start", page)) {
            if (page == Page::Main) {
                heading("Nightmare Creatures", false);
                ImGui::TextWrapped("%s", disc_status_text(check));
                if (check != DISC_OK && settings.disc_path[0] != '\0') {
                    ImGui::TextWrapped("%s", settings.disc_path);
                }
                ImGui::Spacing();
                bool ready = check == DISC_OK;
                ImGui::PushStyleColor(ImGuiCol_Button, wine);
                ImGui::BeginDisabled(dialog_open);
                bool primary =
                    wide_button(ready ? (has_resume ? "Continue" : "Start Game") : "Choose Disc");
                ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
                ImGui::PopStyleColor();
                if (ready && (primary || (has_resume && wide_button("Start Game")))) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return primary && has_resume ? MENU_CONTINUE : MENU_START;
                }
                bool pick_disc = !ready && primary;
                if (ready) {
                    ImGui::BeginDisabled(dialog_open);
                    pick_disc = wide_button("Change Disc");
                    ImGui::EndDisabled();
                }
                if (pick_disc) {
                    dialog_open = true;
                    open_disc_dialog();
                }
                if (wide_button("Settings"))
                    page = Page::Settings;
                if (wide_button("Quit")) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_QUIT;
                }
            } else {
                bool back = false, bindings = false;
                if (page == Page::Bindings) {
                    bindings_page(&back);
                    if (back) {
                        page = Page::Settings;
                    }
                } else {
                    settings_page(&back, &bindings);
                    if (back) {
                        page = Page::Main;
                    } else if (bindings) {
                        page = Page::Bindings;
                    }
                }
            }
        }
        ImGui::End();

        SDL_SetRenderDrawColor(g_renderer, 8, 6, 8, 255);
        SDL_RenderClear(g_renderer);
        end_frame();
    }
}

extern "C" MenuResult menu_run_pause(MenuDrawBackground draw_background) {
    release_mouse();
    Page page = Page::Main;
    bool first = true;
    for (;;) {
        if (!pump_events()) {
            return MENU_QUIT;
        }
        begin_frame();
        if (begin_panel("##pause", page)) {
            if (page == Page::Main) {
                heading("Paused", true);
                bool resume = wide_button("Resume");
                if (first)
                    ImGui::SetItemDefaultFocus();
                /* Esc closes the menu, but not on the frame that opened it. */
                if (!first && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                               ImGui::IsKeyPressed(ImGuiKey_AppBack, false) ||
                               ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false))) {
                    resume = true;
                }
                if (resume) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_RESUME;
                }
                /* Save states: the request runs at the next safe point after resuming. */
                static int slot = 0;
                ImGui::SeparatorText("Save state");
                for (int i = 0; i < SAVESTATE_SLOTS - 1; i++) {
                    char label[32];
                    std::snprintf(label, sizeof label,
                                  savestate_exists(i) ? "Slot %d" : "Slot %d (empty)", i + 1);
                    if (i % 2)
                        ImGui::SameLine();
                    float w =
                        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                    if (slot == i)
                        ImGui::PushStyleColor(ImGuiCol_Button, wine);
                    bool clicked = ImGui::Button(label, ImVec2(i % 2 ? -FLT_MIN : w, 0));
                    if (slot == i)
                        ImGui::PopStyleColor();
                    if (clicked)
                        slot = i;
                }
                float half =
                    (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
                bool save = ImGui::Button("Save", ImVec2(half, 0));
                ImGui::SameLine();
                ImGui::BeginDisabled(!savestate_exists(slot));
                bool load = ImGui::Button("Load", ImVec2(half, 0));
                ImGui::EndDisabled();
                if (save || load) {
                    if (save) {
                        savestate_request_save(slot);
                    } else {
                        savestate_request_load(slot);
                    }
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_RESUME;
                }
                ImGui::Spacing();
                if (wide_button("Settings")) {
                    page = Page::Settings;
                }
                if (savestate_can_save_quit() && wide_button("Save & Quit")) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_SAVE_QUIT;
                }
                if (wide_button("Quit")) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_QUIT;
                }
            } else {
                bool back = false, bindings = false;
                if (page == Page::Bindings) {
                    bindings_page(&back);
                    if (back) {
                        page = Page::Settings;
                    }
                } else {
                    settings_page(&back, &bindings);
                    if (back) {
                        page = Page::Main;
                    } else if (bindings) {
                        page = Page::Bindings;
                    }
                }
            }
        }
        ImGui::End();
        first = false;

        draw_background();
        end_frame();
    }
}
