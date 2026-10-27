#include "core/config/deprecation.h"

namespace yasb::config::deprecation {

namespace {

constexpr const char* kGone = "No longer supported and can be removed from the config.";
constexpr const char* kOffsetTop = "Use 'offset_top' instead.";

Tables buildTables() {
    Tables t;
    t.deprecated = {
        {"container_padding", "Use CSS padding instead."},
        {"label_shadow", "Use CSS text-shadow instead."},
        {"container_shadow", "Use CSS box-shadow instead."},
        {"btn_shadow", "Use CSS box-shadow instead."},
        {"app_title_shadow", "Use CSS text-shadow instead."},
        {"app_icon_shadow", "Use CSS box-shadow instead."},
        {"unpinned_shadow", "Use CSS box-shadow instead."},
        {"pinned_shadow", "Use CSS box-shadow instead."},
        {"unpinned_vis_btn_shadow", "Use CSS box-shadow instead."},
        {"animation", "Use CSS animations instead."},
    };
    auto& d = t.scopedDeprecated;
    d["BarBlurEffect"] = {{"acrylic", kGone}};
    d["BarAlignment"] = {{"center", "Use 'align' instead."}};
    d["VSCodeMenuConfig"] = {{"distance", kOffsetTop}};
    d["VSCodeConfig"] = {
        {"max_field_size", "No longer supported and can be removed from the config. Handled dynamically by "
                           "font-aware elision."},
        {"folder_icon", "Use icons.folder instead."},
        {"file_icon", "Use icons.file instead."},
        {"hide_folder_icon", "No longer supported. Set icons.folder to an empty string instead."},
        {"hide_file_icon", "No longer supported. Set icons.file to an empty string instead."},
    };
    d["BrightnessMenuConfig"] = {{"distance", kOffsetTop}};
    d["ClockCalendarConfig"] = {{"distance", kOffsetTop}};
    d["GroupLabelConfig"] = {{"distance", kOffsetTop}};
    d["ServerMonitorMenuConfig"] = {{"distance", kOffsetTop}};
    d["NotesMenuConfig"] = {
        {"max_title_size",
         "No longer supported and can be removed from the config. Handled by font-aware elision."}};
    d["AudioMenuConfig"] = {{"distance", kOffsetTop}};
    d["WeatherCardConfig"] = {{"distance", kOffsetTop}};
    d["HomeConfig"] = {{"distance", kOffsetTop}};
    d["GlazewmWorkspacesConfig"] = {{"hide_empty_workspaces", kGone}};
    d["BrightnessConfig"] = {{"hide_unsupported", kGone}};
    d["YasbConfig"] = {
        {"env_file", "No longer supported. Place a .env file in the config folder instead."}};
    for (const char* key : {"lazy_load_delay", "enable_cache", "lazy_load", "lazy_load_fadein", "enabled",
                            "blur", "image_per_page", "gallery_columns", "show_buttons", "image_spacing",
                            "respect_work_area", "horizontal_position", "vertical_position",
                            "position_offset"}) {
        d["GalleryConfig"][key] = kGone;
    }
    d["AnimationConfig"] = {
        {"type", "Animation type is no longer supported in TaskbarWidget, remove it from your config."}};
    d["PowerPlanConfig"] = {
        {"update_interval", "Power plan changes are now detected via Windows events. Remove "
                            "'update_interval' from your config."}};

    t.scopedRenamed["ActiveLayoutIconsConfig"] = {
        {"maximised", {"maximized", "Use 'maximized' instead."}}};
    t.scopedRenamed["VolumeConfig"] = {
        {"volume_icons", {"icons", "Use 'icons' with the dictionary format instead."}}};
    return t;
}

}  // namespace

const Tables& tables() {
    static const Tables t = buildTables();
    return t;
}

void apply(std::string_view model, nlohmann::ordered_json& object,
           const std::function<bool(const std::string&)>& isKnownField, std::vector<std::string>& warnings) {
    if (!object.is_object()) return;
    const Tables& t = tables();
    const std::string name(model);

    auto deprecated = t.deprecated;
    if (auto it = t.scopedDeprecated.find(name); it != t.scopedDeprecated.end()) {
        for (const auto& [k, v] : it->second) deprecated[k] = v;
    }
    auto renamed = t.renamed;
    if (auto it = t.scopedRenamed.find(name); it != t.scopedRenamed.end()) {
        for (const auto& [k, v] : it->second) renamed[k] = v;
    }
    if (deprecated.empty() && renamed.empty()) return;

    std::vector<std::string> keys;
    for (auto it = object.begin(); it != object.end(); ++it) keys.push_back(it.key());

    for (const auto& key : keys) {
        if (isKnownField(key)) continue;
        if (auto d = deprecated.find(key); d != deprecated.end()) {
            warnings.push_back("[DEPRECATED] " + name + ": '" + key + "' - " + d->second);
            object.erase(key);
        } else if (auto r = renamed.find(key); r != renamed.end()) {
            const auto& [newName, message] = r->second;
            warnings.push_back("[DEPRECATED] " + name + ": '" + key + "' renamed to '" + newName + "' - " +
                               message);
            if (!object.contains(newName)) object[newName] = object[key];
            object.erase(key);
        }
    }
}

}  // namespace yasb::config::deprecation
