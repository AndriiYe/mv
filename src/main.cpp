#include "local_tracker.h"
#include "kalman_filtre.hpp"
#include "PIDController.hpp"
#include "crsf_rc.hpp"
#include "screen_capture.h"
#include "Jconfig.hpp"
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <exception>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

std::string make_pi_camera_pipeline(const CaptureSettings& settings) {
    return "libcamerasrc ! "
        "video/x-raw,width=" + std::to_string(settings.width) +
        ",height=" + std::to_string(settings.height) +
        ",framerate=" + std::to_string(settings.fps) + "/1,format=NV12 ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        "appsink name=appsink drop=true max-buffers=1 sync=false";
}

bool open_capture(cv::VideoCapture& camera, const CaptureSettings& settings) {
    if (settings.use_gstreamer) {
        return camera.open(make_pi_camera_pipeline(settings), cv::CAP_GSTREAMER);
    }
    if (settings.use_camera_index) {
        return camera.open(settings.camera_index);
    }

    return camera.open(settings.source);
}

bool open_screen_capture(ScreenCapture& screen, const CaptureSettings& settings) {
    if (settings.use_screen_region) {
        return screen.open_region(
            settings.screen_left,
            settings.screen_top,
            settings.screen_width,
            settings.screen_height
        );
    }
    if (settings.use_virtual_screen) {
        return screen.open_virtual_screen();
    }

    return screen.open_primary();
}

std::optional<bool> read_bool_env(const char* name) {
    const char* raw_value = std::getenv(name);
    if (raw_value == nullptr || raw_value[0] == '\0') {
        return std::nullopt;
    }

    std::string value(raw_value);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }

    std::cerr << "Warning: ignoring unsupported CV_FULLSCREEN value '" << raw_value
              << "'. Use 1/0, true/false, yes/no, or on/off." << std::endl;
    return std::nullopt;
}

bool should_start_fullscreen(const DisplaySettings& display_settings) {
    const std::optional<bool> env_fullscreen = read_bool_env("CV_FULLSCREEN");
    return env_fullscreen.value_or(display_settings.fullscreen);
}

int make_window_flags(bool fullscreen) {
    const int size_mode = fullscreen ? cv::WINDOW_NORMAL : cv::WINDOW_AUTOSIZE;
    return size_mode | cv::WINDOW_GUI_NORMAL;
}

void apply_window_mode_after_show(const std::string& window_name, bool fullscreen) {
    if (!fullscreen) {
        return;
    }

    cv::setWindowProperty(window_name, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    cv::moveWindow(window_name, 0, 0);
}

bool move_screen_region_from_key(ScreenCapture& screen, int key) {
    constexpr int move_step = 10;
    constexpr int windows_arrow_left = 0x250000;
    constexpr int windows_arrow_up = 0x260000;
    constexpr int windows_arrow_right = 0x270000;
    constexpr int windows_arrow_down = 0x280000;

    switch (key) {
    case 'a':
    case 'A':
    case windows_arrow_left:
        screen.move_by(-move_step, 0);
        return true;
    case 'd':
    case 'D':
    case windows_arrow_right:
        screen.move_by(move_step, 0);
        return true;
    case 'w':
    case 'W':
    case windows_arrow_up:
        screen.move_by(0, -move_step);
        return true;
    case 's':
    case 'S':
    case windows_arrow_down:
        screen.move_by(0, move_step);
        return true;
    default:
        return false;
    }
}

class Cursor {
public:
    using timer = std::chrono::steady_clock;

    Cursor(std::shared_ptr<CrsfRcSender> rc, cv::Size frame_size, int sens)
        : command_input_(std::move(rc)),
          last_time_(timer::now()),
          frame_w_(std::max(1, frame_size.width)),
          frame_h_(std::max(1, frame_size.height)),
          x_(frame_w_ / 2.0f),
          y_(frame_h_ / 2.0f), 
          sens_(sens) {
    }

    cv::Point update() {
        auto time_now = timer::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time_now - last_time_);
        last_time_ = time_now;
        float dt = elapsed.count() / 1000.0f;   // seconds

        const uint16_t p = neutral_if_missing(command_input_->getChannel(3));
        const uint16_t r = neutral_if_missing(command_input_->getChannel(4));

        if (r > 1520) {
            const float x_speed = map(r - 1500.0f, 1, 500, 30, sens_); // px/sec
            x_ += x_speed * dt;
        } else if (r < 1480) {
            const float x_speed = map(1500.0f - r, 1, 500, 30, sens_); // px/sec
            x_ -= x_speed * dt;
        }

        if (p > 1520) {
            const float y_speed = map(p - 1500.0f, 1, 500, 30, sens_); // px/sec
            y_ -= y_speed * dt;
        } else if (p < 1480) {
            const float y_speed = map(1500.0f - p, 1, 500, 30, sens_); // px/sec
            y_ += y_speed * dt;
        }

        x_ = std::clamp(x_, 0.0f, static_cast<float>(frame_w_ - 1));
        y_ = std::clamp(y_, 0.0f, static_cast<float>(frame_h_ - 1));

        const bool set_switch_high = neutral_if_missing(command_input_->getChannel(7)) > 1500;
        set_requested_ = set_switch_high && !was_set_switch_high_;
        was_set_switch_high_ = set_switch_high;

        return cv::Point(cvRound(x_), cvRound(y_));
    }

    bool consume_set_request() {
        if (!set_requested_) {
            return false;
        }

        set_requested_ = false;
        return true;
    }

private:
    static uint16_t neutral_if_missing(uint16_t value) {
        return value == 0 ? 1500 : value;
    }

    static float map(float value, float fromLow, float fromHigh, float toLow, float toHigh) {
        return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
    }

    std::shared_ptr<CrsfRcSender> command_input_; // 1000 - 2000
    timer::time_point last_time_;
    int frame_w_;
    int frame_h_;
    float x_;
    float y_;
    int sens_;
    bool was_set_switch_high_ = false;
    bool set_requested_ = false;
};

} // namespace

int main() {
    AppSettings settings;
    try {
        const std::string config_path = Jconfig::find_default_config_path();
        settings = Jconfig(config_path).load();
        std::cout << "Loaded config: " << config_path << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }
    const CaptureSettings& capture_settings = settings.capture;

    cv::VideoCapture camera;
    ScreenCapture screen;
    if (capture_settings.use_screen) {
        if (!open_screen_capture(screen, capture_settings)) {
            std::cerr << "Error: could not open screen capture source." << std::endl;
            return 1;
        }
        std::cout << "Screen capture active: left=" << screen.left()
                  << " top=" << screen.top()
                  << " width=" << screen.width()
                  << " height=" << screen.height() << std::endl;
    } else if (!open_capture(camera, capture_settings)) {
        std::cerr << "Error: could not open video source." << std::endl;
        if (capture_settings.use_gstreamer) {
            std::cerr << "Tried Raspberry Pi pipeline: "
                      << make_pi_camera_pipeline(capture_settings) << std::endl;
        }
        return 1;
    }

    std::shared_ptr<CrsfRcSender> rc_sender;
    if (!settings.rc.device.empty()) {
        rc_sender = std::make_shared<CrsfRcSender>(settings.rc.device, settings.rc.baudrate);
        if (!rc_sender->start()) {
            std::cerr << "Warning: could not start CRSF RC sender on "
                      << settings.rc.device << "." << std::endl;
            rc_sender.reset();
        } else {
            std::cout << "CRSF RC sender active on " << settings.rc.device
                      << " at " << settings.rc.baudrate << " baud." << std::endl;
        }
    }

    const std::string window_name = "Lucas-Kanade Optical Flow";
    const bool fullscreen = should_start_fullscreen(settings.display);
    cv::namedWindow(window_name, make_window_flags(fullscreen));
    bool window_mode_applied = false;

    cv::Mat frame;
    cv::Mat gray;
    cv::Mat output;

    auto read_frame = [&]() {
        if (capture_settings.use_screen) {
            return screen.read(frame);
        }

        camera >> frame;
        return !frame.empty();
    };

    if (!read_frame() || frame.empty()) {
        std::cerr << "Error: captured empty first frame." << std::endl;
        return 1;
    }

    // LocalTracker expects a grayscale frame buffer reference. The same gray
    // cv::Mat object is reused every frame, so the tracker always sees the
    // current image data after cvtColor writes into it.
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    LocalTrackerConfig tracker_config;
    tracker_config.max_points = 50;
    tracker_config.tracking_region_size = 70;
    tracker_config.max_delta = 50.0F;
    tracker_config.qualityLevel = 0.01;
    tracker_config.minDistance = 7.0;
    tracker_config.winSize = cv::Size(21, 21);
    tracker_config.maxLevel = 3;
    tracker_config.criteria = cv::TermCriteria(
        cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
        30,
        0.01
    );
    tracker_config.flags = 0;
    tracker_config.minEigThreshold = 1e-4;

    LocalTracker tracker(gray, tracker_config);
    tracker.update();

    KalmanFiltreConfig kalman_config;
    kalman_config.process_noise = 0.02F;
    kalman_config.measurement_noise = 1.0F;
    kalman_config.estimate_error = 1.0F;
    Kalman_filtre kalman_filtre(kalman_config);

    PIDControllerConfig pid_config;
    pid_config.kp = 1.2;
    pid_config.ki = 0.0;
    pid_config.kd = 0.05;
    pid_config.output_limit = 300.0;
    pid_config.integral_limit = 5000.0;
    pid_config.tune_from_rc = false;
    PIDController pidController(rc_sender, pid_config);

    //FPS counter
    using Clock = std::chrono::steady_clock;
    auto fps_sample_start = Clock::now();
    int fps_sample_frames = 0;
    double display_fps = 0.0;

    //Cursor
    std::unique_ptr<Cursor> cursor;
    if (rc_sender != nullptr) {
        cursor = std::make_unique<Cursor>(rc_sender, frame.size(), 250);
    }

    while (true) {
        if (!read_frame() || frame.empty()) {
            break;
        }

        ++fps_sample_frames;
        const auto fps_now = Clock::now();
        const std::chrono::duration<double> fps_elapsed = fps_now - fps_sample_start;
        if (fps_elapsed.count() >= 0.5) {
            display_fps = fps_sample_frames / fps_elapsed.count();
            fps_sample_frames = 0;
            fps_sample_start = fps_now;
        }

        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        const cv::Point2f delta = tracker.update();
        kalman_filtre.update(delta.x, delta.y);

        const cv::Rect roi = tracker.tracking_region();
        const cv::Point2f roi_center(
            static_cast<float>(roi.x) + static_cast<float>(roi.width) * 0.5F,
            static_cast<float>(roi.y) + static_cast<float>(roi.height) * 0.5F
        );
        const cv::Point2f frame_center(
            static_cast<float>(frame.cols) * 0.5F,
            static_cast<float>(frame.rows) * 0.5F
        );
        const double present_x = roi_center.x - frame_center.x;
        const double present_y = frame_center.y - roi_center.y;
        pidController.update(
            present_x,
            present_y,
            kalman_filtre.D_speed_x,
            -kalman_filtre.D_speed_y
        );

        frame.copyTo(output);
        // Drawing stays in main: the tracker only returns data, while the UI
        // layer decides how to visualize ROI, points, counters, and movement.
        cv::rectangle(output, tracker.tracking_region(), cv::Scalar(0, 255, 0), 2);
        for (const cv::Point2f& point : tracker.valid_next_points()) {
            cv::circle(output, point, 2, cv::Scalar(0, 0, 255), -1);
        }

        cv::putText(
            output,
            "Tracking points: " + std::to_string(tracker.valid_next_points().size()) +
                " / " + std::to_string(tracker_config.max_points),
            cv::Point(10, 30),
            cv::FONT_HERSHEY_SIMPLEX,
            0.8,
            cv::Scalar(255, 255, 255),
            2
        );
        cv::putText(
            output,
            "recap = " + std::to_string(tracker.recap_counter()),
            cv::Point(10, 60),
            cv::FONT_HERSHEY_SIMPLEX,
            0.8,
            cv::Scalar(0, 255, 255),
            2
        );
        cv::putText(
            output,
            "dx = " + std::to_string(delta.x) + " dy = " + std::to_string(delta.y),
            cv::Point(10, 90),
            cv::FONT_HERSHEY_SIMPLEX,
            0.8,
            cv::Scalar(255, 255, 0),
            2
        );
        cv::putText(
            output,
            "kdx = " + std::to_string(kalman_filtre.D_speed_x) +
                " kdy = " + std::to_string(kalman_filtre.D_speed_y),
            cv::Point(10, 120),
            cv::FONT_HERSHEY_SIMPLEX,
            0.8,
            cv::Scalar(255, 180, 0),
            2
        );
        cv::putText(
            output,
            cv::format("FPS: %.1f", display_fps),
            cv::Point(10, 150),
            cv::FONT_HERSHEY_SIMPLEX,
            0.8,
            cv::Scalar(0, 255, 0),
            2
        );
        cv::putText(
            output,
            cv::format("pid x=%.1f y=%.1f", pidController.output_x(), pidController.output_y()),
            cv::Point(10, 180),
            cv::FONT_HERSHEY_SIMPLEX,
            0.8,
            cv::Scalar(0, 180, 255),
            2
        );
        if (rc_sender != nullptr) {
            cv::putText(
                output,
                "P = " + std::to_string(rc_sender->getChannel(3)) + " R = " + std::to_string(rc_sender->getChannel(4)),
                cv::Point(50, 200),
                cv::FONT_HERSHEY_SIMPLEX,
                0.8,
                cv::Scalar(255, 255, 0),
                2
            );
        }
        if (capture_settings.use_screen_region) {
            cv::putText(
                output,
                "screen left=" + std::to_string(screen.left()) +
                    " top=" + std::to_string(screen.top()),
                cv::Point(10, 230),
                cv::FONT_HERSHEY_SIMPLEX,
                0.8,
                cv::Scalar(120, 255, 120),
                2
            );
        }
        if (cursor != nullptr) {
            const cv::Point cursor_position = cursor->update();
            if (cursor->consume_set_request()) {
                tracker_config.ROI_center_x = cursor_position.x;
                tracker_config.ROI_center_y = cursor_position.y;
                tracker.set(tracker_config);
                tracker.update();
            }

            constexpr float speed_arrow_seconds = 0.20F;
            constexpr float max_speed_arrow_length = 120.0F;
            cv::Point2f speed_vector(
                kalman_filtre.D_speed_x * speed_arrow_seconds,
                kalman_filtre.D_speed_y * speed_arrow_seconds
            );
            const float speed_vector_length = std::sqrt(
                speed_vector.x * speed_vector.x +
                speed_vector.y * speed_vector.y
            );
            if (speed_vector_length > max_speed_arrow_length) {
                speed_vector *= max_speed_arrow_length / speed_vector_length;
            }

            const cv::Point2f cursor_center(
                static_cast<float>(cursor_position.x),
                static_cast<float>(cursor_position.y)
            );
            const cv::Point arrow_start = cv::Point(
                cvRound(std::clamp(cursor_center.x - speed_vector.x * 0.5F, 0.0F, static_cast<float>(output.cols - 1))),
                cvRound(std::clamp(cursor_center.y - speed_vector.y * 0.5F, 0.0F, static_cast<float>(output.rows - 1)))
            );
            const cv::Point arrow_end = cv::Point(
                cvRound(std::clamp(cursor_center.x + speed_vector.x * 0.5F, 0.0F, static_cast<float>(output.cols - 1))),
                cvRound(std::clamp(cursor_center.y + speed_vector.y * 0.5F, 0.0F, static_cast<float>(output.rows - 1)))
            );

            cv::arrowedLine(
                output,
                arrow_start,
                arrow_end,
                cv::Scalar(0, 0, 255),
                2,
                cv::LINE_AA,
                0,
                0.25
            );

            cv::circle(
                output,
                cursor_position,
                4,
                cv::Scalar(0, 200, 255),
                -1
            );
            
        }

        cv::imshow(window_name, output);
        if (!window_mode_applied) {
            apply_window_mode_after_show(window_name, fullscreen);
            window_mode_applied = true;
        }

        const int key = cv::waitKeyEx(1);
        if (key == 27 || key == 'q' || key == 'Q') {
            break;
        }
        if (key == 'r' || key == 'R') {
            tracker.set(tracker_config);
            tracker.update();
        }
        if (capture_settings.use_screen_region) {
            move_screen_region_from_key(screen, key);
        }

        if (cv::getWindowProperty(window_name, cv::WND_PROP_VISIBLE) < 1) {
            break;
        }
    }

    return 0;
}
