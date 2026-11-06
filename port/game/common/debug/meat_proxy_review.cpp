// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "debug/overlay.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include "common/env.h"
#include "common/json.h"
#include "common/log.h"
#include "common/staged_file.h"
#include "openrac/game_host.h"

namespace openrac::frontend::debug_overlay {
namespace {

struct Task {
    std::string id, title, description;
    int level;
    scene::Vec3 position, forward;
    std::string verdict, feedback, saved_feedback;
};

struct Review {
    std::vector<Task> tasks;
    std::string selected;
    std::string message;
    std::optional<Task> destination;
    std::uint64_t deadline = 0;
    bool travel_started = false;
    bool loaded = false;
};

Review review;

const std::filesystem::path& review_path() {
    static const auto path = [] {
        const auto configured = env::value(env::Var::MeatProxyReview);
        if (configured && !configured->empty()) {
            return std::filesystem::path(std::u8string(configured->begin(), configured->end()));
        }
        std::error_code error;
        const auto temp = std::filesystem::temp_directory_path(error);
        return error ? std::filesystem::path{} : temp / "meat-proxy-review.json";
    }();
    return path;
}

bool read_document(json::Value& doc, std::string& error) {
    if (review_path().empty()) {
        error = "Cannot locate the system temp folder.";
        return false;
    }
    std::ifstream file(review_path(), std::ios::binary);
    if (!file) {
        error = "Cannot open the review JSON.";
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (!json::parse(contents.str(), doc, error, json::DuplicateKeys::Reject)) {
        return false;
    }
    if (!doc["tasks"].is_array() || (!doc["reviews"].is_null() && !doc["reviews"].is_object())) {
        error = "Expected a tasks array and an optional reviews object.";
        return false;
    }
    if (!doc["game"].is_null() && doc["game"].string() != openrac_game.id) {
        error = "This review queue belongs to a different game.";
        return false;
    }
    return true;
}

bool read_vector(const json::Value& value, scene::Vec3& out) {
    if (value.items().size() != 3) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const double component = value[i].number();
        if (!value[i].is_number() || !std::isfinite(component)
            || std::abs(component) > std::numeric_limits<float>::max()) {
            return false;
        }
        out[i] = static_cast<float>(component);
    }
    return true;
}

void reload() {
    review.loaded = true;
    json::Value doc;
    if (!read_document(doc, review.message)) {
        return;
    }
    std::vector<Task> tasks;
    std::set<std::string> ids;
    const auto* play = openrac_game_play_host;
    for (const auto& item : doc["tasks"].items()) {
        Task task{};
        task.id = item["id"].string();
        task.title = item["title"].string();
        task.description = item["description"].string();
        const double level = item["level"].number(-1);
        if (task.id.empty() || task.title.empty() || !ids.insert(task.id).second
            || !std::isfinite(level) || level < 0 || level >= play->levels
            || level != std::floor(level)
            || !read_vector(item["camera"]["position"], task.position)
            || !read_vector(item["camera"]["forward"], task.forward)
            || task.forward == scene::Vec3{}) {
            review.message = "Invalid or duplicate task at row " + std::to_string(tasks.size() + 1);
            return;
        }
        task.level = static_cast<int>(level);
        const auto& saved = doc["reviews"][task.id];
        task.verdict = saved["verdict"].string();
        task.feedback = task.saved_feedback = saved["feedback"].string();
        tasks.push_back(std::move(task));
    }
    review.tasks = std::move(tasks);
    if (!ids.contains(review.selected)) {
        review.selected = review.tasks.empty() ? "" : review.tasks.front().id;
    }
    review.message.clear();
}

void quote(std::ostream& out, std::string_view value) {
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out << '\\' << static_cast<char>(c);
        } else if (c < 0x20) {
            out << std::format("\\u{:04x}", c);
        } else {
            out << static_cast<char>(c);
        }
    }
    out << '"';
}

void write_json(std::ostream& out, const json::Value& value, unsigned depth);

bool write_members(
    std::ostream& out, const json::Value& object, unsigned depth, std::string_view skip = {},
    std::string_view also_skip = {}
) {
    bool wrote_member = false;
    for (const auto& [key, value] : object.members()) {
        if ((!skip.empty() && key == skip) || (!also_skip.empty() && key == also_skip)) {
            continue;
        }
        if (wrote_member) {
            out << ',';
        }
        wrote_member = true;
        out << '\n' << std::string(depth * 2, ' ');
        quote(out, key);
        out << ": ";
        write_json(out, value, depth);
    }
    return wrote_member;
}

void write_json(std::ostream& out, const json::Value& value, unsigned depth) {
    switch (value.type()) {
        case json::Value::Type::Null: out << "null"; break;
        case json::Value::Type::Bool: out << (value.boolean() ? "true" : "false"); break;
        case json::Value::Type::Number: out << std::format("{}", value.number()); break;
        case json::Value::Type::String: quote(out, value.string()); break;
        case json::Value::Type::Array: {
            out << '[';
            bool first = true;
            for (const auto& item : value.items()) {
                if (!first) {
                    out << ',';
                }
                first = false;
                out << '\n' << std::string((depth + 1) * 2, ' ');
                write_json(out, item, depth + 1);
            }
            if (!first) {
                out << '\n' << std::string(depth * 2, ' ');
            }
            out << ']';
            break;
        }
        case json::Value::Type::Object: {
            out << '{';
            if (write_members(out, value, depth + 1)) {
                out << '\n' << std::string(depth * 2, ' ');
            }
            out << '}';
            break;
        }
    }
}

bool read_for_edit(const Task& task, json::Value& doc) {
    if (!read_document(doc, review.message)) {
        return false;
    }
    const auto& tasks = doc["tasks"].items();
    if (std::none_of(tasks.begin(), tasks.end(), [&](const auto& item) {
            return item["id"].string() == task.id;
        })) {
        review.message = "This task was removed from the file. Reload the queue.";
        return false;
    }
    const auto& saved = doc["reviews"][task.id];
    if (saved["verdict"].string() != task.verdict || saved["feedback"].string() != task.saved_feedback) {
        review.message = "This task's review changed on disk. Reload the queue.";
        return false;
    }
    return true;
}

bool publish(std::string_view contents) {
    auto staged = StagedFile::create(review_path());
    if (!staged) {
        review.message = staged.error().message;
        return false;
    }
    std::ofstream out(staged->path(), std::ios::binary);
    out << contents;
    out.close();
    if (!out) {
        review.message = "Cannot write the review JSON.";
        return false;
    }
    if (const auto result = staged->publish(); !result) {
        review.message = result.error().message;
        return false;
    }
    return true;
}

bool save(Task& task, std::string_view verdict) {
    // Read the current file so adding feedback preserves tasks and other reviews edited externally.
    json::Value doc;
    if (!read_for_edit(task, doc)) {
        return false;
    }
    std::ostringstream out;
    out << '{';
    if (write_members(out, doc, 1, "reviews")) {
        out << ',';
    }
    out << "\n  \"reviews\": {";
    if (write_members(out, doc["reviews"], 2, task.id)) {
        out << ',';
    }
    out << "\n    ";
    quote(out, task.id);
    out << ": {\n      \"verdict\": ";
    quote(out, verdict);
    out << ",\n      \"feedback\": ";
    quote(out, task.feedback);
    out << "\n    }\n  }\n}\n";
    if (!publish(out.str())) {
        return false;
    }
    task.verdict = verdict;
    task.saved_feedback = task.feedback;
    review.message = "Review saved.";
    return true;
}

void save_camera(Task& task, const renderer::Camera& camera) {
    const auto& position = camera.position;
    const auto forward = camera_forward(camera);
    const auto finite = [](const auto& v) {
        return std::all_of(v.begin(), v.end(), [](float n) { return std::isfinite(n); });
    };
    if (!finite(position) || !finite(forward) || forward == scene::Vec3{}) {
        review.message = "The current camera pose is unavailable.";
        return;
    }
    json::Value doc;
    if (!read_document(doc, review.message)) {
        return;
    }
    const auto& tasks = doc["tasks"].items();
    const auto selected = std::find_if(tasks.begin(), tasks.end(), [&](const auto& item) {
        return item["id"].string() == task.id;
    });
    if (selected == tasks.end()) {
        review.message = "This task was removed from the file. Reload the queue.";
        return;
    }
    const auto& saved = (*selected)["camera"];
    scene::Vec3 saved_position{}, saved_forward{};
    if ((*selected)["level"].number(-1) != task.level
        || !read_vector(saved["position"], saved_position) || saved_position != task.position
        || !read_vector(saved["forward"], saved_forward) || saved_forward != task.forward) {
        review.message = "This task's camera or level changed on disk. Reload the queue.";
        return;
    }

    std::ostringstream out;
    out << '{';
    if (write_members(out, doc, 1, "tasks")) {
        out << ',';
    }
    out << "\n  \"tasks\": [";
    for (auto item = tasks.begin(); item != tasks.end(); ++item) {
        out << (item == tasks.begin() ? "\n    " : ",\n    ");
        if (item != selected) {
            write_json(out, *item, 2);
            continue;
        }
        out << '{';
        write_members(out, *item, 3, "camera");
        out << ",\n      \"camera\": {";
        if (write_members(out, saved, 4, "position", "forward")) {
            out << ',';
        }
        out << std::format(
            "\n        \"position\": [{}, {}, {}],\n        \"forward\": [{}, {}, {}]",
            position[0], position[1], position[2], forward[0], forward[1], forward[2]
        );
        out << "\n      }\n    }";
    }
    out << "\n  ]\n}\n";
    if (publish(out.str())) {
        task.position = position;
        task.forward = forward;
        review.message = "Camera saved.";
    }
}

bool delete_task(const Task& task) {
    json::Value doc;
    if (!read_for_edit(task, doc)) {
        return false;
    }
    std::ostringstream out;
    out << '{';
    if (write_members(out, doc, 1, "tasks", "reviews")) {
        out << ',';
    }
    out << "\n  \"tasks\": [";
    bool first = true;
    for (const auto& item : doc["tasks"].items()) {
        if (item["id"].string() == task.id) {
            continue;
        }
        out << (first ? "\n    " : ",\n    ");
        first = false;
        write_json(out, item, 2);
    }
    out << (first ? "]," : "\n  ],") << "\n  \"reviews\": {";
    if (write_members(out, doc["reviews"], 2, task.id)) {
        out << "\n  ";
    }
    out << "}\n}\n";
    return publish(out.str());
}

void finish_view(const Task& task) {
    place_review_camera(task.level, task.position, task.forward);
    log::info("meat proxy review: placed freecam for {} in level {}", task.id, task.level);
    review.message = "Freecam placed.";
}

int resize_feedback(ImGuiInputTextCallbackData* data) {
    auto& feedback = *static_cast<std::string*>(data->UserData);
    feedback.resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = feedback.data();
    return 0;
}

}  // namespace

bool meat_proxy_review_skips_landing() {
    const auto* front_end = openrac_game_front_end;
    if (!review.destination || front_end == nullptr || front_end->ship_state == 0
        || openrac_game_loaded_overlay() != review.destination->level
        || openrac_game_mode == nullptr) {
        return false;
    }
    // The ship's landing is state 8 of the level's flight mode (see rac1/host/boot.c).
    std::uint32_t mode = 0;
    return openrac_game_mode(&mode) != 0 && mode == 6
           && GREF(int, front_end->ship_state) == 8;
}

bool meat_proxy_review_skips_flight() {
    const auto* front_end = openrac_game_front_end;
    return review.destination && review.travel_started && front_end != nullptr
           && front_end->ship_state != 0 && GREF(int, front_end->ship_state) == 4;
}

void meat_proxy_review_update() {
    if (!review.destination) {
        return;
    }
    const auto& task = *review.destination;
    const auto* play = openrac_game_play_host;
    std::int32_t level = -1;
    const int phase = play->phase(&level);
    if (level == task.level && phase == OPENRAC_GAME_PHASE_PLAYING) {
        finish_view(task);
        review.destination.reset();
        return;
    }
    if (SDL_GetTicks() >= review.deadline) {
        review.message = "Timed out waiting for the level to be playable.";
        review.destination.reset();
        return;
    }
    // Freecam suppresses the game's pad input, including the landing skip. Re-enable it
    // at the saved pose once the destination is playable.
    s.camera_tools.enabled = false;
    if (review.travel_started || phase == OPENRAC_GAME_PHASE_LOADING
        || phase == OPENRAC_GAME_PHASE_NONE
        || (level == task.level && phase == OPENRAC_GAME_PHASE_SCENE)) {
        return;
    }
    const bool started = phase == OPENRAC_GAME_PHASE_FRONT_END
                             ? play->start(task.level, nullptr, 0) != 0
                             : play->travel(task.level) != 0;
    if (started) {
        review.travel_started = true;
    } else {
        review.message = "The game refused to open this level.";
        review.destination.reset();
    }
}

namespace {
void draw_review(const Shown& shown) {
    const auto* play = openrac_game_play_host;
    if (play == nullptr || play->phase == nullptr || play->start == nullptr
        || play->travel == nullptr) {
        ImGui::TextUnformatted("Level travel is unavailable in this game.");
        return;
    }
    if (!review.loaded || ImGui::Button("Reload JSON")) {
        reload();
    }
    const auto path = review_path().u8string();
    ImGui::TextWrapped("%s", reinterpret_cast<const char*>(path.c_str()));
    if (!review.message.empty()) {
        ImGui::TextWrapped("%s", review.message.c_str());
    }
    const auto selected = std::find_if(review.tasks.begin(), review.tasks.end(), [](const Task& task) {
        return task.id == review.selected;
    });
    if (selected == review.tasks.end()) {
        if (review.message.empty()) {
            ImGui::TextUnformatted("No review tasks.");
        }
        return;
    }
    if (ImGui::BeginCombo("Task", selected->title.c_str())) {
        const bool opening = ImGui::IsWindowAppearing();
        for (const auto& task : review.tasks) {
            ImGui::PushID(task.id.c_str());
            const std::string label = task.title + " [" + (task.verdict.empty() ? "Pending" : task.verdict) + "]";
            const bool is_selected = task.id == review.selected;
            if (ImGui::Selectable(label.c_str(), is_selected)) {
                review.selected = task.id;
            }
            if (opening && is_selected) {
                ImGui::SetScrollHereY();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    auto& task = *selected;
    ImGui::PushID(task.id.c_str());
    ImGui::Text("Level %d | %s", task.level, task.verdict.empty() ? "Pending" : task.verdict.c_str());
    ImGui::TextWrapped("%s", task.description.c_str());
    ImGui::BeginDisabled(review.destination.has_value());
    if (ImGui::Button("Open view")) {
        review.destination = task;
        review.deadline = SDL_GetTicks() + 60000;
        review.travel_started = false;
        review.message = "Opening level " + std::to_string(task.level) + "...";
        s.paused = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    std::int32_t playing_level = -1;
    const bool can_save_camera = !review.destination && shown.camera != nullptr
                                 && shown.camera_level == task.level
                                 && play->phase(&playing_level) == OPENRAC_GAME_PHASE_PLAYING
                                 && playing_level == task.level;
    ImGui::BeginDisabled(!can_save_camera);
    if (ImGui::Button("Save current camera")) {
        save_camera(task, *shown.camera);
    }
    ImGui::EndDisabled();
    if (!can_save_camera && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Open the task's level before saving its camera.");
    }
    ImGui::TextUnformatted("Feedback");
    ImGui::InputTextMultiline(
        "##feedback", task.feedback.data(), task.feedback.capacity() + 1, ImVec2(-1, dp(65.0f)),
        ImGuiInputTextFlags_CallbackResize, resize_feedback, &task.feedback
    );
    if (ImGui::Button("Yay")) {
        save(task, "yay");
    }
    ImGui::SameLine();
    if (ImGui::Button("Nay")) {
        save(task, "nay");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save feedback")) {
        save(task, task.verdict);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(review.destination.has_value());
    if (ImGui::Button("Delete task")) {
        ImGui::OpenPopup("Delete review task?");
    }
    ImGui::EndDisabled();
    bool deleted = false;
    if (ImGui::BeginPopupModal("Delete review task?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this task and its saved feedback?");
        ImGui::TextUnformatted(task.title.c_str());
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            deleted = delete_task(task);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (deleted) {
        const auto next = review.tasks.erase(selected);
        review.selected = next != review.tasks.end() ? next->id
                          : review.tasks.empty()      ? ""
                                                      : review.tasks.back().id;
        review.message = "Task deleted.";
    }
}
}  // namespace

void meat_proxy_review_window(const Shown& shown) {
    if (!s.meat_proxy_review) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(dp(410), dp(360)), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Meat Proxy Review", &s.meat_proxy_review)) {
        draw_review(shown);
    }
    ImGui::End();
}

}  // namespace openrac::frontend::debug_overlay
