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
    bool tcp_mirror = true;
};

struct DisplaySettings {
    bool fullscreen = false;
};

struct PidSettings {
    double kp = 1.2;
    double ki = 0.0;
    double kd = 0.05;
    double output_limit = 300.0;
    double integral_limit = 5000.0;
    bool tune_from_rc = false;
};

struct KalmanSettings {
    float process_noise = 0.02F;
    float measurement_noise = 1.0F;
    float estimate_error = 1.0F;
    bool tune_from_rc = false;
};

struct AppSettings {
    CaptureSettings capture;
    RcSettings rc;
    DisplaySettings display;
    PidSettings pid;
    KalmanSettings kalman;
};

class Jconfig {
public:
    explicit Jconfig(std::string config_path);

    AppSettings load() const;

    static std::string find_default_config_path();

private:
    std::string config_path_;
};
