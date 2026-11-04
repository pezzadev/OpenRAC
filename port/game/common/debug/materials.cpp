// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Unified material authoring window, using the shared schema and explicit sparse property edits.
#include <algorithm>
#include <array>
#include <limits>

#include "debug/overlay.h"
#include "debug/material_presets.h"
#include "enhanced/renderer/foliage.h"
#include "enhanced/renderer/material_runtime.h"
#include "frontend.h"

namespace openrac::frontend::debug_overlay {
namespace {
// Retains inherited values until a widget changes; displayed defaults never become authored overrides implicitly.
materials::Properties displayed_properties(const scene::MaterialBinding& binding) {
    auto properties = binding.values.properties;
    properties.try_emplace("surface.tint", std::array<double, 3>{1, 1, 1});
    properties
        .try_emplace("surface.diffuse_scale", static_cast<double>(binding.surface.diffuse_scale));
    properties.try_emplace(
        "emission.runtime_intensity",
        static_cast<double>(
            *std::max_element(binding.surface.emission.begin(), binding.surface.emission.end())
        )
    );
    properties.try_emplace(
        "emission.bake_intensity",
        static_cast<double>(binding.surface.bake_intensity.value_or(
            *std::max_element(binding.surface.emission.begin(), binding.surface.emission.end())
        ))
    );
    properties
        .try_emplace("emission.contrast", static_cast<double>(binding.surface.emission_contrast));
    properties.try_emplace("emission.vertex_colour", binding.surface.emission_vertex_colour);
    properties.try_emplace(
        "emission.use_base_texture", binding.surface.emission_image == binding.surface.image
    );
    return properties;
}

// Presents only implemented families; changing a family cannot manufacture water geometry or reflective packets.
bool family_widget(std::string& value) {
    int selected = value == "foliage" ? 1 : 0;
    if (!ImGui::Combo("Shading", &selected, "Standard\0Foliage\0")) {
        return false;
    }
    value = selected == 1 ? "foliage" : "standard";
    return true;
}

// Narrows the shared roughness range for thin leaves without changing chrome/glass controls.
materials::Field applicable_field(materials::Field field, const materials::Properties& properties) {
    if (field.path == "surface.roughness"
        && materials::text_value(properties, "shading") == "foliage") {
        field.minimum = .35;
    }
    return field;
}

// Diagnostics remain session-only; authoring files contain material response rather than debug views.
void foliage_diagnostics() {
    auto controls = frontend::foliage_controls();
    if (ImGui::Combo(
            "Foliage view", &controls.debug, "Shaded\0Material mask\0Transmission\0Normals\0"
        )) {
        const bool applied = frontend::set_foliage_controls(controls, s.material_error);
        if (!applied) {
            ImGui::TextWrapped("%s", s.material_error.c_str());
        }
    }
}

// Edits one supported scalar, retaining the schema's bounds and HDR input range.
bool number_widget(const materials::Field& field, materials::Value& value) {
    double number = std::get<double>(value);
    const bool hdr = field.maximum > std::numeric_limits<float>::max() / 2.0;
    bool changed = false;
    if (hdr) {
        changed = ImGui::InputDouble(std::string(field.label).c_str(), &number);
        number = std::clamp(number, field.minimum, field.maximum);
    } else {
        changed = ImGui::SliderScalar(
            std::string(field.label).c_str(),
            ImGuiDataType_Double,
            &number,
            &field.minimum,
            &field.maximum,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp
        );
    }
    if (changed) {
        value = number;
    }
    return changed;
}

// Text asset references are read-only here; importing unverified coverage or missing images requires an explicit reload.
bool property_widget(const materials::Field& field, materials::Value& value) {
    const std::string label(field.label);
    switch (field.kind) {
        case materials::Kind::Boolean:
            return ImGui::Checkbox(label.c_str(), &std::get<bool>(value));
        case materials::Kind::Number:
            return number_widget(field, value);
        case materials::Kind::Colour: {
            auto& colour = std::get<std::array<double, 3>>(value);
            std::array<float, 3> edit{
                static_cast<float>(colour[0]),
                static_cast<float>(colour[1]),
                static_cast<float>(colour[2])
            };
            if (!ImGui::ColorEdit3(label.c_str(), edit.data())) {
                return false;
            }
            colour = {edit[0], edit[1], edit[2]};
            return true;
        }
        case materials::Kind::Text: {
            auto& text = std::get<std::string>(value);
            if (field.path == "shading" && (text == "standard" || text == "foliage")) {
                return family_widget(text);
            }
            ImGui::Text("%s: %s", label.c_str(), text.c_str());
            return false;
        }
    }
    return false;
}

// Keeps the click picker's model/slot selection stable when sorted authoring rows change between levels.
void pick_binding(const scene::MaterialBindings& bindings) {
    if (s.material_slot < 0) {
        return;
    }
    const auto found = bindings.lookup.find({s.material_model, s.material_slot});
    if (found != bindings.lookup.end()) {
        s.material_selection = found->second;
    }
    s.material_slot = -1;
    s.material_preset_edit = false;
}

// Lists loaded identities without matching selectors or hashing images in the frame loop.
void binding_combo(const scene::MaterialBindings& bindings) {
    const auto& sources = bindings.assets->sources;
    s.material_selection = std::min(s.material_selection, sources.size() - 1);
    const auto label = [](const scene::MaterialSource& source) {
        auto name = source.model_name + " / " + source.identity.material_name;
        // Repeated or empty glTF names still need visibly distinct choices.
        if (source.identity.material_slot) {
            name += " [slot " + std::to_string(*source.identity.material_slot) + "]";
        }
        return name;
    };
    if (!ImGui::BeginCombo("Material", label(sources[s.material_selection]).c_str())) {
        return;
    }
    for (std::size_t n = 0; n < sources.size(); ++n) {
        ImGui::PushID(static_cast<int>(n));
        if (ImGui::Selectable(label(sources[n]).c_str(), n == s.material_selection)) {
            s.material_selection = n;
        }
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

// Older exports still expose the visor's compiled defaults as sparse material edits.
void show_visor_defaults(materials::Properties& properties, const scene::MaterialSource& source) {
    const auto response = enhanced::material_response(source, properties);
    const auto& reflection = response.reflection;
    const auto colour = [](const std::array<float, 3>& rgb) {
        return std::array<double, 3>{rgb[0], rgb[1], rgb[2]};
    };
    properties.try_emplace("surface.roughness", double(reflection.roughness));
    properties.try_emplace("surface.reflectivity", double(reflection.reflectivity));
    properties.try_emplace("reflection.strength", double(reflection.strength));
    properties.try_emplace("reflection.opacity", double(reflection.opacity));
    properties.try_emplace("reflection.tint", colour(reflection.tint));
    properties.try_emplace("glass.transmission", double(response.glass.transmission));
    properties.try_emplace("glass.tint", colour(response.glass.tint));
}

const char* preset_group(PresetSurface surface) {
    switch (surface) {
        case PresetSurface::Opaque: return "Opaque surfaces";
        case PresetSurface::Metal: return "Bare metals";
        case PresetSurface::Glass: return "Thin glass";
        case PresetSurface::Water:
        case PresetSurface::Ocean: return "Water";
        case PresetSurface::Foliage: return "Foliage";
        case PresetSurface::DryFur:
        case PresetSurface::WetFur: return "Fur";
    }
    return "";
}

const char* preset_requirement(PresetSurface surface, const materials::Identity& identity) {
    const bool water = identity.material_name == "@water";
    switch (surface) {
        case PresetSurface::Opaque:
        case PresetSurface::Foliage:
            return !water && !identity.metal && identity.layer != "sky"
                       ? nullptr : "Select a regular colour material.";
        case PresetSurface::Metal:
            return identity.metal && identity.material_name == "metal_chrome"
                       ? nullptr : "Select a chrome packet, such as Clank's bare metal.";
        case PresetSurface::Glass:
            return identity.material_name == enhanced::kShipVisorMaterial
                       ? nullptr : "Select a thin ship visor pane.";
        case PresetSurface::Water:
        case PresetSurface::Ocean:
            return water ? nullptr : "Select the level's water material.";
        case PresetSurface::DryFur:
        case PresetSurface::WetFur:
            for (const auto* profile : {&enhanced::rac1_fur_profile(), &enhanced::rac2_fur_profile()}) {
                if (identity.layer == "moby" && !identity.metal && identity.class_id == profile->class_id
                    && identity.material_name == profile->material) {
                    return nullptr;
                }
            }
            return "Select Ratchet's fur material. The renderer verifies the coat coverage.";
    }
    return "Select a compatible material.";
}

materials::Properties preset_properties(const MaterialPreset& preset) {
    using Colour = std::array<double, 3>;
    if (preset.surface == PresetSurface::Ocean) {
        return {{"ocean.roughness", preset.roughness}, {"ocean.reflectivity", preset.reflectivity}};
    }
    if (preset.surface == PresetSurface::DryFur) {
        const enhanced::FurMaterialSettings fur;
        return {{"fur.enabled", fur.enabled}, {"fur.strength", double(fur.strength)},
                {"fur.contrast", double(fur.contrast)}, {"fur.fade_distance", double(fur.fade_distance)},
                {"fur.lighting_enabled", fur.lighting_enabled}, {"fur.highlight", double(fur.highlight)},
                {"fur.softness", double(fur.softness)}, {"fur.directionality", double(fur.directionality)},
                {"fur.scattering", double(fur.scattering)},
                {"fur.environment_strength", double(fur.environment_strength)}};
    }
    if (preset.surface == PresetSurface::WetFur) {
        const enhanced::WetnessMaterialSettings wet;
        return {{"wetness.diffuse_scale", double(wet.diffuse_scale)},
                {"wetness.specular_strength", double(wet.specular_strength)},
                {"wetness.dry_roughness", double(wet.dry_roughness)},
                {"wetness.wet_roughness", double(wet.wet_roughness)},
                {"wetness.reflectivity", double(wet.reflectivity)}};
    }
    materials::Properties properties{
        {"surface.roughness", preset.roughness}, {"surface.reflectivity", preset.reflectivity},
        {"reflection.strength", 1.}, {"reflection.tint", Colour{1, 1, 1}},
    };
    switch (preset.surface) {
        case PresetSurface::Opaque:
            properties["shading"] = std::string("standard");
            break;
        case PresetSurface::Metal: {
            const auto& [r, g, b] = preset.colour;
            const double f0 = std::max({r, g, b});
            properties["shading"] = std::string("chrome");
            properties["surface.reflectivity"] = f0;
            properties["reflection.tint"] = Colour{r / f0, g / f0, b / f0};
            properties["reflection.opacity"] = 1.;
            break;
        }
        case PresetSurface::Glass:
            properties["shading"] = std::string("glass");
            properties["reflection.opacity"] = 1.;
            properties["glass.transmission"] = preset.transmission;
            properties["glass.tint"] = preset.colour;
            break;
        case PresetSurface::Water:
            properties["shading"] = std::string("water");
            break;
        case PresetSurface::Foliage:
            properties["shading"] = std::string("foliage");
            properties["foliage.transmission"] = preset.transmission;
            properties["foliage.thickness"] = .7;
            properties["foliage.normal_bend"] = .2;
            break;
        default:
            break;
    }
    return properties;
}

void library_controls(const materials::Identity& identity) {
    auto& selection = s.material_library_preset;
    if (ImGui::BeginCombo("Preset library", selection < 0 ? "Choose a finish" : kMaterialPresets[selection].name)) {
        std::string_view last_group;
        for (int i = 0; i < static_cast<int>(std::size(kMaterialPresets)); ++i) {
            const auto& preset = kMaterialPresets[i];
            const std::string_view group = preset_group(preset.surface);
            if (group != last_group) {
                ImGui::TextDisabled("%s", group.data());
                last_group = group;
            }
            if (ImGui::Selectable(preset.name, i == selection)) {
                selection = i;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", preset.description);
            }
        }
        ImGui::EndCombo();
    }
    if (selection < 0) {
        return;
    }
    const auto& preset = kMaterialPresets[selection];
    ImGui::TextWrapped("%s", preset.description);
    const auto* requirement = preset_requirement(preset.surface, identity);
    if (requirement) {
        ImGui::TextDisabled("%s", requirement);
    }
    ImGui::BeginDisabled(requirement != nullptr);
    if (ImGui::Button("Apply preset to material")) {
        frontend::patch_material(s.material_selection, preset_properties(preset), s.material_error);
    }
    ImGui::EndDisabled();
}

// Shows exact provenance and sends only changed properties; validation failures leave all previous bindings intact.
void binding_controls(const scene::MaterialBindings& bindings) {
    const auto& binding = bindings.bindings[s.material_selection];
    const auto& source = bindings.assets->sources[s.material_selection];
    const bool water = source.identity.material_name == "@water";
    const bool sky = source.identity.layer == "sky";
    const bool visor = source.identity.material_name == enhanced::kShipVisorMaterial;
    auto properties = water || visor ? binding.values.properties : displayed_properties(binding);
    // Transmission applies only to the thin pane's blended pass.
    if (visor) {
        show_visor_defaults(properties, source);
    }
    // Older exports have no authored water fields, but still use the renderer defaults.
    if (water) {
        const auto defaults =
            enhanced::default_reflection_material(enhanced::ReflectionSurface::Water);
        properties.try_emplace("surface.roughness", static_cast<double>(defaults.roughness));
        properties.try_emplace("surface.reflectivity", static_cast<double>(defaults.reflectivity));
        properties.try_emplace("reflection.strength", static_cast<double>(defaults.strength));
    }
    if (!source.identity.metal && !water && !sky) {
        properties.try_emplace("shading", std::string("standard"));
    }
    if (materials::text_value(properties, "shading") == "foliage") {
        properties.try_emplace("surface.roughness", .65);
        properties.try_emplace("surface.reflectivity", .04);
        properties.try_emplace("foliage.transmission", .35);
        properties.try_emplace("foliage.thickness", .7);
        properties.try_emplace("foliage.normal_bend", .2);
    }
    ImGui::Text("Layer: %s | Class: %d", source.identity.layer.c_str(), source.identity.class_id);
    ImGui::TextWrapped("Edits apply to every instance of this model and material slot.");
    library_controls(source.identity);
    for (const auto& field : materials::fields()) {
        // The sky shader consumes only tint.
        if (sky && field.path != "surface.tint") {
            continue;
        }
        if (!water && field.path.starts_with("ocean.")) {
            continue;
        }
        const auto found = properties.find(field.path);
        if (found == properties.end()) {
            continue;
        }
        ImGui::PushID(field.path.data(), field.path.data() + field.path.size());
        auto value = found->second;
        if (property_widget(applicable_field(field, properties), value)) {
            const bool applied = frontend::patch_material(
                s.material_selection, std::string(field.path), std::move(value), s.material_error
            );
            if (!applied) {
                ImGui::TextWrapped("%s", s.material_error.c_str());
            }
        }
        if (ImGui::IsItemHovered()) {
            const auto origin = binding.values.sources.find(std::string(field.path));
            ImGui::SetTooltip(
                "%s",
                origin == binding.values.sources.end() ? "Original material/default"
                                                       : origin->second.c_str()
            );
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Undo")) {
            const bool restored = frontend::undo_material(
                s.material_selection, std::string(field.path), s.material_error
            );
            if (!restored) {
                ImGui::TextWrapped("%s", s.material_error.c_str());
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Discard this field's unsaved edit and inherit its loaded/saved value."
            );
        }
        ImGui::PopID();
    }
}

// Shared presets update every rule using them, while higher-priority object fields remain explicit exceptions.
void preset_controls(const scene::MaterialBindings& bindings) {
    const auto& presets = bindings.catalog.presets;
    if (presets.empty()) {
        ImGui::TextDisabled("No shared presets loaded.");
        return;
    }
    if (!presets.contains(s.material_preset)) {
        s.material_preset = presets.begin()->first;
    }
    if (ImGui::BeginCombo("Preset", s.material_preset.c_str())) {
        for (const auto& [id, properties] : presets) {
            if (ImGui::Selectable(id.c_str(), id == s.material_preset)) {
                s.material_preset = id;
            }
        }
        ImGui::EndCombo();
    }
    for (const auto& [key, current] : presets.at(s.material_preset)) {
        const auto* field = materials::field(key);
        if (!field) {
            continue;
        }
        auto value = current;
        ImGui::PushID(key.c_str());
        if (property_widget(applicable_field(*field, presets.at(s.material_preset)), value)) {
            const bool applied = frontend::patch_material_preset(
                s.material_preset, key, std::move(value), s.material_error
            );
            if (!applied) {
                ImGui::TextWrapped("%s", s.material_error.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Undo")) {
            const bool restored =
                frontend::undo_material_preset(s.material_preset, key, s.material_error);
            if (!restored) {
                ImGui::TextWrapped("%s", s.material_error.c_str());
            }
        }
        ImGui::PopID();
    }
}

// Offers explicit saves and session reset, with a separate warning for transport edits.
void save_controls(const enhanced::MaterialEditor& editor) {
    const auto path = editor.path.u8string();
    ImGui::TextWrapped("Save to: %s", reinterpret_cast<const char*>(path.c_str()));
    ImGui::BeginDisabled(!editor.dirty);
    if (ImGui::Button("Save material overrides")) {
        const bool saved = frontend::save_materials(s.material_error);
        if (!saved) {
            ImGui::TextWrapped("%s", s.material_error.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard unsaved edits")) {
        frontend::reset_materials();
        s.material_error.clear();
    }
    ImGui::EndDisabled();
    if (editor.dirty) {
        ImGui::TextDisabled("Unsaved changes");
    }
    if (editor.transport_dirty) {
        ImGui::TextWrapped("Baked lighting changed. Rebake this level to update probe lighting.");
    }
    if (!s.material_error.empty()) {
        ImGui::TextWrapped("%s", s.material_error.c_str());
    }
}
}  // namespace

void material_settings_window() {
    if (!s.material_settings) {
        return;
    }
    if (!ImGui::Begin("Material inspector", &s.material_settings)) {
        ImGui::End();
        return;
    }
    const auto editor = frontend::material_editor();
    if (!editor) {
        ImGui::TextDisabled("Waiting for the render worker.");
    } else if (!editor->error.empty()) {
        ImGui::TextWrapped("%s", editor->error.c_str());
    } else if (editor->bindings && !editor->bindings->bindings.empty()) {
        pick_binding(*editor->bindings);
        ImGui::Checkbox("Edit shared preset", &s.material_preset_edit);
        if (!s.material_preset_edit) {
            binding_combo(*editor->bindings);
        }
        save_controls(*editor);
        foliage_diagnostics();
        ImGui::Separator();
        if (s.material_preset_edit) {
            preset_controls(*editor->bindings);
        } else {
            binding_controls(*editor->bindings);
        }
    }
    ImGui::End();
}
}  // namespace openrac::frontend::debug_overlay
