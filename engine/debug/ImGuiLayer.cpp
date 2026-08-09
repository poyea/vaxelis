// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Law

#include "engine/debug/ImGuiLayer.hpp"

#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

namespace vaxelis {

void ImGuiLayer::init(SDL_Window* window, void* gl_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
#ifdef __EMSCRIPTEN__
    ImGui_ImplOpenGL3_Init("#version 300 es"); // WebGL2 / GLES 3.0
#else
    ImGui_ImplOpenGL3_Init("#version 450");
#endif
}

void ImGuiLayer::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::process_event(const SDL_Event& ev) {
    ImGui_ImplSDL3_ProcessEvent(&ev);
}

void ImGuiLayer::begin_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::end_frame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace vaxelis
