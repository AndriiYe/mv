#include "Jconfig.hpp"

#include <opencv2/core/persistence.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace {

bool file_exists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

std::string directory_name(const std::string& path) {
    const std::size_t directory_end = path.find_last_of("/\\");
    if (directory_end == std::string::npos) {
        return std::string();
    }

    return path.substr(0, directory_end);
}

void add_candidate(std::vector<std::string>& candidates, const std::string& path) {
    if (path.empty()) {
        return;
    }
    if (std::find(candidates.begin(), candidates.end(), path) != candidates.end()) {
        return;
    }

    candidates.push_back(path);
}

std::string current_executable_path() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(sizeof(path)));
    if (length == 0 || length >= sizeof(path)) {
        return std::string();
    }

    return std::string(path, length);
#else
    char path[PATH_MAX] = {};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) {
        return std::string();
    }

    return std::string(path, static_cast<std::size_t>(length));
#endif
}

std::vector<std::string> default_config_candidates() {
    std::vector<std::string> candidates;
    add_candidate(candidates, "config.json");

    const std::string executable = current_executable_path();
    const std::string executable_dir = directory_name(executable);
    add_candidate(candidates, executable_dir + "/config.json");

    const std::string executable_parent = directory_name(executable_dir);
    add_candidate(candidates, executable_parent + "/config.json");

    return candidates;
}

std::string read_config_string(
    const cv::FileNode& object_node,
    const std::string& key,
    const std::string& config_path
) {
    const cv::FileNode value_node = object_node[key];
    if (value_node.empty()) {
        return std::string();
    }
    if (!value_node.isString()) {
        throw std::runtime_error("Config file '" + config_path + "' field '" + key + "' must be a string.");
    }

    std::string value;
    value_node >> value;
    return value;
}

void read_config_int(
    const cv::FileNode& object_node,
    const std::string& key,
    int& target,
    const std::string& config_path
) {
    const cv::FileNode value_node = object_node[key];
    if (value_node.empty()) {
        return;
    }
    if (!value_node.isInt()) {
        throw std::runtime_error("Config file '" + config_path + "' field '" + key + "' must be an integer.");
    }

    value_node >> target;
}

void read_config_double(
    const cv::FileNode& object_node,
    const std::string& key,
    double& target,
    const std::string& config_path
) {
    const cv::FileNode value_node = object_node[key];
    if (value_node.empty()) {
        return;
    }
    if (!value_node.isInt() && !value_node.isReal()) {
        throw std::runtime_error("Config file '" + config_path + "' field '" + key + "' must be a number.");
    }

    value_node >> target;
}

void read_config_float(
    const cv::FileNode& object_node,
    const std::string& key,
    float& target,
    const std::string& config_path
) {
    double value = target;
    read_config_double(object_node, key, value, config_path);
    target = static_cast<float>(value);
}

void read_config_bool(
    const cv::FileNode& object_node,
    const std::string& key,
    bool& target,
    const std::string& config_path
) {
    const cv::FileNode value_node = object_node[key];
    if (value_node.empty()) {
        return;
    }
    if (value_node.isInt()) {
        int value = 0;
        value_node >> value;
        target = value != 0;
        return;
    }
    if (value_node.isString()) {
        std::string value;
        value_node >> value;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (value == "true" || value == "1" || value == "yes" || value == "on") {
            target = true;
            return;
        }
        if (value == "false" || value == "0" || value == "no" || value == "off") {
            target = false;
            return;
        }
    }

    throw std::runtime_error(
        "Config file '" + config_path + "' field '" + key + "' must be a boolean, 0/1, or true/false string."
    );
}

void apply_capture_mode(CaptureSettings& capture, const std::string& mode, const std::string& config_path) {
    if (mode == "video") {
        capture.use_camera_index = false;
        capture.use_gstreamer = false;
        capture.use_screen = false;
        return;
    }
    if (mode == "camera") {
        capture.use_camera_index = true;
        capture.use_gstreamer = false;
        capture.use_screen = false;
        return;
    }
    if (mode == "pi-camera") {
        capture.use_camera_index = false;
        capture.use_gstreamer = true;
        capture.use_screen = false;
        return;
    }
    if (mode == "screen") {
        capture.use_camera_index = false;
        capture.use_gstreamer = false;
        capture.use_screen = true;
        capture.use_virtual_screen = false;
        capture.use_screen_region = false;
        return;
    }
    if (mode == "screen-virtual") {
        capture.use_camera_index = false;
        capture.use_gstreamer = false;
        capture.use_screen = true;
        capture.use_virtual_screen = true;
        capture.use_screen_region = false;
        return;
    }
    if (mode == "screen-region") {
        capture.use_camera_index = false;
        capture.use_gstreamer = false;
        capture.use_screen = true;
        capture.use_virtual_screen = false;
        capture.use_screen_region = true;
        return;
    }

    throw std::runtime_error(
        "Config file '" + config_path + "' has unsupported capture mode '" + mode +
        "'. Use video, camera, pi-camera, screen, screen-virtual, or screen-region."
    );
}

void apply_capture_config(AppSettings& settings, const cv::FileNode& capture_node, const std::string& config_path) {
    if (!capture_node.isMap()) {
        throw std::runtime_error("Config file '" + config_path + "' field 'capture' must be an object.");
    }

    CaptureSettings& capture = settings.capture;
    const std::string mode = read_config_string(capture_node, "mode", config_path);
    if (mode.empty()) {
        throw std::runtime_error("Config file '" + config_path + "' field 'capture.mode' is required.");
    }
    apply_capture_mode(capture, mode, config_path);

    const std::string source = read_config_string(capture_node, "source", config_path);
    if (!source.empty()) {
        capture.source = source;
    }
    const std::string video_source = read_config_string(capture_node, "video_source", config_path);
    if (!video_source.empty()) {
        capture.source = video_source;
    }

    read_config_int(capture_node, "camera_index", capture.camera_index, config_path);
    read_config_int(capture_node, "width", capture.width, config_path);
    read_config_int(capture_node, "height", capture.height, config_path);
    read_config_int(capture_node, "fps", capture.fps, config_path);
    read_config_int(capture_node, "screen_left", capture.screen_left, config_path);
    read_config_int(capture_node, "screen_top", capture.screen_top, config_path);
    read_config_int(capture_node, "screen_width", capture.screen_width, config_path);
    read_config_int(capture_node, "screen_height", capture.screen_height, config_path);
}

void apply_rc_config(AppSettings& settings, const cv::FileNode& rc_node, const std::string& config_path) {
    if (!rc_node.isMap()) {
        throw std::runtime_error("Config file '" + config_path + "' field 'crsf' must be an object.");
    }

    const std::string device = read_config_string(rc_node, "device", config_path);
    settings.rc.device = device;

    read_config_int(rc_node, "baudrate", settings.rc.baudrate, config_path);
    read_config_int(rc_node, "baud", settings.rc.baudrate, config_path);
    read_config_bool(rc_node, "tcp_mirror", settings.rc.tcp_mirror, config_path);
    read_config_bool(rc_node, "mirror_tcp_packets", settings.rc.tcp_mirror, config_path);
}

void apply_display_config(AppSettings& settings, const cv::FileNode& display_node, const std::string& config_path) {
    if (!display_node.isMap()) {
        throw std::runtime_error("Config file '" + config_path + "' field 'display' must be an object.");
    }

    read_config_bool(display_node, "fullscreen", settings.display.fullscreen, config_path);
}

void apply_pid_config(AppSettings& settings, const cv::FileNode& pid_node, const std::string& config_path) {
    if (!pid_node.isMap()) {
        throw std::runtime_error("Config file '" + config_path + "' field 'pid' must be an object.");
    }

    read_config_double(pid_node, "kp", settings.pid.kp, config_path);
    read_config_double(pid_node, "ki", settings.pid.ki, config_path);
    read_config_double(pid_node, "kd", settings.pid.kd, config_path);
    read_config_double(pid_node, "output_limit", settings.pid.output_limit, config_path);
    read_config_double(pid_node, "integral_limit", settings.pid.integral_limit, config_path);
    read_config_bool(pid_node, "tune_from_rc", settings.pid.tune_from_rc, config_path);
}

void apply_kalman_config(AppSettings& settings, const cv::FileNode& kalman_node, const std::string& config_path) {
    if (!kalman_node.isMap()) {
        throw std::runtime_error("Config file '" + config_path + "' field 'kalman' must be an object.");
    }

    read_config_float(kalman_node, "process_noise", settings.kalman.process_noise, config_path);
    read_config_float(kalman_node, "q", settings.kalman.process_noise, config_path);
    read_config_float(kalman_node, "measurement_noise", settings.kalman.measurement_noise, config_path);
    read_config_float(kalman_node, "r", settings.kalman.measurement_noise, config_path);
    read_config_float(kalman_node, "estimate_error", settings.kalman.estimate_error, config_path);
    read_config_bool(kalman_node, "tune_from_rc", settings.kalman.tune_from_rc, config_path);
}

} // namespace

Jconfig::Jconfig(std::string config_path)
    : config_path_(std::move(config_path)) {
}

std::string Jconfig::find_default_config_path() {
    for (const std::string& candidate : default_config_candidates()) {
        if (file_exists(candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error(
        "Could not find config.json in the current working directory, next to cv.exe, or one directory above cv.exe."
    );
}

AppSettings Jconfig::load() const {
    cv::FileStorage config(config_path_, cv::FileStorage::READ);
    if (!config.isOpened()) {
        throw std::runtime_error("Could not open config file '" + config_path_ + "'.");
    }

    AppSettings settings;

    const cv::FileNode capture_node = config["capture"];
    if (capture_node.empty()) {
        throw std::runtime_error("Config file '" + config_path_ + "' must contain a 'capture' object.");
    }
    apply_capture_config(settings, capture_node, config_path_);

    const cv::FileNode crsf_node = config["crsf"];
    if (!crsf_node.empty()) {
        apply_rc_config(settings, crsf_node, config_path_);
    }

    const cv::FileNode display_node = config["display"];
    if (!display_node.empty()) {
        apply_display_config(settings, display_node, config_path_);
    }

    const cv::FileNode pid_node = config["pid"];
    if (!pid_node.empty()) {
        apply_pid_config(settings, pid_node, config_path_);
    }

    const cv::FileNode kalman_node = config["kalman"];
    if (!kalman_node.empty()) {
        apply_kalman_config(settings, kalman_node, config_path_);
    }

    return settings;
}
