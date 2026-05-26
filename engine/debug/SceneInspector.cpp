#include "engine/debug/SceneInspector.hpp"

#include <cstdio>

#include <imgui.h>

#include "engine/scene/Components.hpp"
#include "engine/scene/Scene.hpp"

namespace vaxelis {

void SceneInspector::draw(Scene& scene) {
    ImGui::Begin("Scene");
    if (ImGui::BeginChild("hierarchy", ImVec2(220, 0), ImGuiChildFlags_Borders)) {
        draw_hierarchy(scene);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("properties", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        draw_properties(scene);
    }
    ImGui::EndChild();
    ImGui::End();

    if (pending_destroy_ != entt::null) {
        if (selected_ == pending_destroy_) selected_ = entt::null;
        scene.destroy_node(pending_destroy_);
        pending_destroy_ = entt::null;
    }
}

void SceneInspector::draw_hierarchy(Scene& scene) {
    if (ImGui::Button("+ Node")) {
        auto parent = (selected_ != entt::null) ? selected_ : scene.root();
        selected_ = scene.create_node("Node", parent);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && selected_ != entt::null && selected_ != scene.root()) {
        pending_destroy_ = selected_;
    }
    ImGui::Separator();
    draw_node_recursive(scene, scene.root());
}

void SceneInspector::draw_node_recursive(Scene& scene, entt::entity e) {
    auto& reg = scene.registry();
    if (!reg.valid(e)) return;
    const auto& name = reg.get<Name>(e).value;
    const auto& h    = reg.get<Hierarchy>(e);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (h.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (e == selected_)     flags |= ImGuiTreeNodeFlags_Selected;
    if (e == scene.root())  flags |= ImGuiTreeNodeFlags_DefaultOpen;

    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e)),
                                        flags, "%s", name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selected_ = e;
    if (open) {
        for (auto c : h.children) draw_node_recursive(scene, c);
        ImGui::TreePop();
    }
}

void SceneInspector::draw_properties(Scene& scene) {
    if (selected_ == entt::null || !scene.registry().valid(selected_)) {
        ImGui::TextDisabled("No selection.");
        return;
    }
    auto& reg = scene.registry();
    auto e = selected_;

    auto& name = reg.get<Name>(e);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", name.value.c_str());
    if (ImGui::InputText("Name", buf, sizeof(buf))) name.value = buf;

    if (auto* t = reg.try_get<Transform2D>(e); t && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat2("Position", &t->position.x, 1.0f);
        ImGui::DragFloat ("Rotation", &t->rotation, 0.01f);
        ImGui::DragFloat2("Scale",    &t->scale.x, 0.01f);
    }

    if (auto* s = reg.try_get<SpriteComponent>(e)) {
        if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen)) {
            char tk[128];
            std::snprintf(tk, sizeof(tk), "%s", s->texture_key.c_str());
            if (ImGui::InputText("Texture key", tk, sizeof(tk))) s->texture_key = tk;
            ImGui::DragFloat2("Size",    &s->size.x, 1.0f);
            ImGui::DragFloat4("UV rect", &s->uv_rect.x, 0.01f, 0.0f, 1.0f);
            ImGui::ColorEdit4("Color",   &s->color.x);
            ImGui::DragInt   ("Z order", &s->z_order);
            ImGui::Checkbox  ("Visible", &s->visible);
            if (!s->texture.valid()) {
                ImGui::TextDisabled("(unresolved texture)");
            }
        }
    } else {
        if (ImGui::Button("+ Sprite component")) {
            reg.emplace<SpriteComponent>(e);
        }
    }
}

}  // namespace vaxelis
