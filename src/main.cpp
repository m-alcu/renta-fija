// renta-fija - seguimiento y backtesting del momento de pasar de un fondo
// monetario a uno de renta fija a 2 años, con el cuadro de cinco indicadores
// de explicacion.md y las series que publica el BCE.
//
// SDL3 + Dear ImGui (backend SDL_Renderer), C++17. Como msmoney, pensionplan
// y dca.
//
// Reparto de las fuentes:
//   model.h/.cpp    calendario, series en cache, tabla diaria de mercado
//   engine.h/.cpp   indicadores, puntuacion, fondos sinteticos y backtests
//                   (puro, con tests)
//   ecb.h/.cpp      descarga de series del portal de datos del BCE
//   updater.h/.cpp  la actualizacion en segundo plano
//   ui.h/.cpp       estado compartido, paleta, grafica y marco de la ventana
//   tabs.cpp        las seis vistas
//   main.cpp        SDL y el bucle de eventos
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "model.h"
#include "ui.h"

namespace {

std::string trim(std::string s) {
    size_t b = s.find_first_not_of(" \t");
    size_t e = s.find_last_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// An optional config.ini in the working directory says where the data lives
// and whether to refresh it on start:
//
//   data_root = ~/Documentos/renta-fija   ; carpeta de la cache y los parametros
//   update_on_start = si                  ; no | si (por omision, si)
//
// Lines starting with # or ; are comments; ~/ expands to $HOME.
struct Config {
    std::string dataRoot = "data/";
    bool updateOnStart = true;
};

// The program is as often started from build/ as from the project folder.
// config.ini and the relative data folder are looked for next to the
// executable and one level up as well, and the first place that has a
// config.ini becomes the working directory, so every relative path means the
// same thing wherever the launch came from.
void enterProjectFolder() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> places = {fs::current_path(ec)};
    if (const char* base = SDL_GetBasePath()) {
        places.push_back(fs::path(base));
        places.push_back(fs::path(base) / "..");
    }
    for (const fs::path& dir : places) {
        if (dir.empty() || !fs::exists(dir / "config.ini", ec)) continue;
        fs::current_path(dir, ec);
        return;
    }
}

Config readConfig() {
    Config cfg;
    std::ifstream f("config.ini");
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (key == "data_root" && !val.empty()) {
            if (val[0] == '~' && (val.size() == 1 || val[1] == '/' || val[1] == '\\')) {
                const char* home = getenv("HOME");
                if (!home) home = getenv("USERPROFILE");
                if (home) val = home + val.substr(1);
            }
            if (val.back() != '/' && val.back() != '\\') val += '/';
            cfg.dataRoot = val;
        } else if (key == "update_on_start") {
            cfg.updateOnStart = !(val == "no" || val == "0" || val == "false");
        }
    }
    return cfg;
}

}  // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("renta fija - del monetario al bono a 2 años", 1400, 880,
                                       SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (!win || !ren) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);  // best effort; frame pacing does not rely on it

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed fullscreen layout, nothing to persist
    applyStyle();
    loadFonts();
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    enterProjectFolder();
    Config cfg = readConfig();
    App a;
    a.dataRoot = cfg.dataRoot;
    a.updateOnStart = cfg.updateOnStart;
    std::error_code ec;
    std::filesystem::create_directories(dataDir(a.dataRoot), ec);
    a.p.load(paramsPath(a));
    // whatever is already cached, so there is something to look at before the
    // network answers
    reloadData(a);
    if (!a.mk.empty()) setStatus(a, "Cargada la caché de " + a.dataRoot);

    // debug hooks for headless testing / screenshots
    const char* shotPath = SDL_getenv("RF_SHOT");
    if (const char* t = SDL_getenv("RF_TAB")) a.forceTab = std::clamp(atoi(t), 0, kTabs - 1);
    if (SDL_getenv("RF_NO_UPDATE")) a.updateOnStart = false;
    int frame = 0;

    // Every run starts by asking the ECB what it has published since the last
    // one, so the scoreboard is never read on stale rates. It runs on its own
    // thread - see updater.h - so the window is usable meanwhile.
    if (a.updateOnStart && !shotPath) startRefresh(a);

    bool running = true;
    Uint64 lastFrame = 0;
    while (running) {
        // Event-driven loop: sleep until input arrives instead of redrawing at
        // full speed. The timeout keeps slow animations alive (caret blink,
        // status expiry) and, while a refresh is in flight, keeps the progress
        // line moving.
        if (!shotPath) SDL_WaitEventTimeout(nullptr, a.updater.busy() ? 100 : 250);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
        }
        Uint64 now = SDL_GetTicks();
        if (!shotPath && now - lastFrame < 16) SDL_Delay((Uint32)(16 - (now - lastFrame)));
        lastFrame = SDL_GetTicks();

        // a finished refresh is merged here, on the main thread, where nothing
        // else is reading the tables
        std::vector<UpdateOutcome> outcomes;
        if (a.updater.collect(outcomes)) applyUpdate(a, outcomes);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawUI(a);
        ImGui::Render();

        SDL_SetRenderDrawColor(ren, (Uint8)(C_BG.x * 255), (Uint8)(C_BG.y * 255),
                               (Uint8)(C_BG.z * 255), 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);

        if (shotPath && ++frame == 5) {
            SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr);
            if (s) {
                SDL_SaveBMP(s, shotPath);
                SDL_DestroySurface(s);
            }
            running = false;
        }
        SDL_RenderPresent(ren);
    }

    a.p.save(paramsPath(a));
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
