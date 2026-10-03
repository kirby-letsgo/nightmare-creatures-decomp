/* Start screen and in-game menu, drawn with Dear ImGui over SDL3's renderer. Keyboard, mouse
 * and gamepad navigation all work. */
#include "port/ui/menu.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

extern "C" {
#include "port/disc.h"
#include "port/hw/gpu.h"
#include "port/hw/spu.h"
#include "port/patches.h"
#include "port/recomp.h"
#include "port/settings.h"
}

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

SDL_Window *g_window;
SDL_Renderer *g_renderer;

enum class Page { Main, Settings };

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
    static const SDL_DialogFileFilter filters[] = {{"CHD disc image", "chd"}};
    g_dialog_done = false;
    SDL_ShowOpenFileDialog(on_file_chosen, nullptr, g_window, filters, 1,
                           settings.disc_path[0] ? settings.disc_path : nullptr, false);
}

void apply_style() {
    ImGuiStyle &s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.WindowPadding = ImVec2(24, 20);
    s.ItemSpacing = ImVec2(12, 10);
    s.FramePadding = ImVec2(10, 6);
    ImVec4 *c = s.Colors;
    const ImVec4 blood(0.55f, 0.05f, 0.06f, 1.0f), blood_hi(0.75f, 0.10f, 0.10f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.04f, 0.05f, 0.94f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.10f, 0.11f, 1.0f);
    c[ImGuiCol_ButtonHovered] = blood;
    c[ImGuiCol_ButtonActive] = blood_hi;
    c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.10f, 0.11f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.12f, 0.13f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.12f, 0.13f, 1.0f);
    c[ImGuiCol_SliderGrab] = blood;
    c[ImGuiCol_SliderGrabActive] = blood_hi;
    c[ImGuiCol_CheckMark] = blood_hi;
    c[ImGuiCol_CheckboxSelectedBg] = ImVec4(0.14f, 0.10f, 0.11f, 1.0f);
    c[ImGuiCol_Header] = blood;
    c[ImGuiCol_HeaderHovered] = blood_hi;
    c[ImGuiCol_NavCursor] = blood_hi;
    c[ImGuiCol_Separator] = ImVec4(0.35f, 0.15f, 0.15f, 1.0f);
}

/* Scales the UI with the window (in points; HiDPI is handled by the framebuffer scale). */
void update_scale() {
    int w = 0, h = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    ImGui::GetStyle().FontScaleMain = h > 0 ? h / 720.0f : 1.0f;
}

/* Returns false if the window was closed. */
bool pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
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

/* Centred, auto-sized window. */
bool begin_panel(const char *id, float width_frac) {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * width_frac, 0), ImGuiCond_Always);
    return ImGui::Begin(id, nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoSavedSettings);
}

void title(const char *text) {
    float scale = 1.8f;
    ImGui::SetWindowFontScale(scale);
    float w = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - w) * 0.5f);
    ImGui::TextColored(ImVec4(0.80f, 0.12f, 0.12f, 1.0f), "%s", text);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
}

bool wide_button(const char *label) {
    return ImGui::Button(label, ImVec2(-FLT_MIN, 0));
}

void settings_page(bool *back) {
    bool changed = false;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

    ImGui::SeparatorText("Gameplay");
    changed |= ImGui::Checkbox("Adrenaline system", &settings.adrenaline);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off: the adrenaline meter never fills or drains your health,\n"
                          "and its gauge is hidden.");
    }
    int controls = settings.controls;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Controls");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Tank (original)", &controls, CONTROLS_TANK);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Modern (camera-relative)", &controls, CONTROLS_MODERN);
    ImGui::SameLine();
    ImGui::TextDisabled("(coming soon)");
    settings.controls = static_cast<ControlScheme>(controls);

    ImGui::SeparatorText("Audio");
    changed |= ImGui::SliderInt("Master volume", &settings.volume_master, 0, 100, "%d%%");
    changed |= ImGui::SliderInt("Music volume", &settings.volume_music, 0, 100, "%d%%");
    changed |= ImGui::SliderInt("Effects volume", &settings.volume_sfx, 0, 100, "%d%%");

    ImGui::SeparatorText("Video");
    changed |= ImGui::Checkbox("Fullscreen", &settings.fullscreen);
    changed |= ImGui::Checkbox("Widescreen (16:9)", &settings.widescreen);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Widens the 3D view. Movies stay 4:3; the HUD is stretched for now.");
    }
    int filter = settings.filter;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Screen filtering");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Sharp", &filter, FILTER_NEAREST);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Smooth", &filter, FILTER_BILINEAR);
    settings.filter = static_cast<TextureFilter>(filter);
    changed |= ImGui::SliderInt("Resolution", &settings.render_scale, 1, 8, "%dx native");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Internal rendering resolution. Higher values are smoother but cost\n"
                          "more CPU (the current renderer is software-based).");
    }
    if (settings.render_scale > 3) {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
                           "Above 3x the game may slow down on this renderer.");
    }
    changed |= ImGui::Checkbox("Show FPS", &settings.show_fps);

    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (wide_button("Back") || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        *back = true;
    }
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
    default:
        return "No disc selected. Choose your Nightmare Creatures .chd image.";
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
    apply_style();
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
    gte_set_widescreen(settings.widescreen);
    spu_set_gains(settings.volume_master * 256 / 100, settings.volume_music * 256 / 100,
                  settings.volume_sfx * 256 / 100);
    SDL_SetWindowFullscreen(g_window, settings.fullscreen);
}

extern "C" MenuResult menu_run_start(void) {
    const char *shot = SDL_getenv("NC_MENU_SHOT");
    Page page = shot != nullptr && std::strcmp(shot, "settings") == 0 ? Page::Settings : Page::Main;
    DiscCheck check = disc_check(settings.disc_path);
    bool dialog_open = false;

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
        if (begin_panel("##start", page == Page::Main ? 0.45f : 0.7f)) {
            title("NIGHTMARE CREATURES");
            if (page == Page::Main) {
                ImGui::TextWrapped("%s", disc_status_text(check));
                if (check != DISC_OK && settings.disc_path[0] != '\0') {
                    ImGui::TextDisabled("%s", settings.disc_path);
                }
                ImGui::Spacing();
                ImGui::BeginDisabled(check != DISC_OK);
                if (check == DISC_OK) {
                    ImGui::SetItemDefaultFocus();
                }
                bool start = wide_button("Start Game");
                ImGui::EndDisabled();
                if (start) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_START;
                }
                if (wide_button("Settings")) {
                    page = Page::Settings;
                }
                ImGui::BeginDisabled(dialog_open);
                if (wide_button(check == DISC_OK ? "Change Disc..." : "Choose Disc...")) {
                    dialog_open = true;
                    open_disc_dialog();
                }
                ImGui::EndDisabled();
                if (wide_button("Quit")) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_QUIT;
                }
            } else {
                bool back = false;
                settings_page(&back);
                if (back) {
                    page = Page::Main;
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
    Page page = Page::Main;
    bool first = true;
    for (;;) {
        if (!pump_events()) {
            return MENU_QUIT;
        }
        begin_frame();
        if (begin_panel("##pause", page == Page::Main ? 0.35f : 0.7f)) {
            title("PAUSED");
            if (page == Page::Main) {
                if (first) {
                    ImGui::SetItemDefaultFocus();
                }
                bool resume = wide_button("Resume");
                /* Esc closes the menu, but not on the frame that opened it. */
                if (!first && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                               ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false))) {
                    resume = true;
                }
                if (resume) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_RESUME;
                }
                if (wide_button("Settings")) {
                    page = Page::Settings;
                }
                if (wide_button("Quit to Desktop")) {
                    ImGui::End();
                    ImGui::EndFrame();
                    return MENU_QUIT;
                }
            } else {
                bool back = false;
                settings_page(&back);
                if (back) {
                    page = Page::Main;
                }
            }
        }
        ImGui::End();
        first = false;

        draw_background();
        end_frame();
    }
}
