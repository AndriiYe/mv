#pragma once

#include <string>

struct CaptureSettings {
    std::string source = "C:/VScode_projects/cv/src/w22.mp4";
    int camera_index = 0;
    int width = 640;
    int height = 480;
    int fps = 30;
    bool use_camera_index = false;
    bool use_gstreamer = false;
    bool use_screen = false;
    bool use_virtual_screen = false;
    bool use_screen_region = false;
    int screen_left = 0;
    int screen_top = 0;
    int screen_width = 0;
    int screen_height = 0;
};

struct RcSettings {
    std::string device;
    int baudrate = 420000;
};

struct AppSettings {
    CaptureSettings capture;
    RcSettings rc;
};

class Jconfig {
public:
    explicit Jconfig(std::string config_path);

    AppSettings load() const;

    static std::string find_default_config_path();

private:
    std::string config_path_;
};
