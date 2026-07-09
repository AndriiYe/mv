#pragma once

#include "crsf_rc.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

struct PIDControllerConfig {
    // Gains operate on frame-centered pixel coordinates:
    // error_x/error_y are pixels from the frame center, speed_x/speed_y are
    // pixels per second from the Kalman-filtered tracker motion.
    double kp = 1.2;
    double ki = 0.0;
    double kd = 0.05;

    // Controller output is added to 1500 us RC neutral and clamped to the
    // normal 1000-2000 us channel range before transmission.
    double output_limit = 300.0;

    // Integral clamp limits wind-up when the tracker stays off-center.
    double integral_limit = 5000.0;

    // When true, each update reads CRSF RX ch9/ch10/ch11 as live P/I/D knobs.
    bool tune_from_rc = false;
};

class PIDController {
public:
    using timer = std::chrono::steady_clock;

    PIDController(std::shared_ptr<CrsfRcSender> rc, const PIDControllerConfig& config);
    PIDController(std::shared_ptr<CrsfRcSender> rc, double p, double i, double d);

    // current_x/current_y are target offsets from frame center. Positive X is
    // right of center; positive Y is above center, matching main.cpp.
    void update(double current_x, double current_y, double speed_x, double speed_y);
    void setK(double p, double i, double d);

    double output_x() const;
    double output_y() const;
    uint16_t output_roll_channel() const;
    uint16_t output_pitch_channel() const;
    double kp() const;
    double ki() const;
    double kd() const;

private:
    static uint16_t pwm_from_output(double output);
    static double clamp(double value, double min_value, double max_value);

    void set_from_rc();
    void update_rc(double x, double y);

    std::shared_ptr<CrsfRcSender> rc_;
    PIDControllerConfig config_;
    timer::time_point last_time_;
    double integral_x_ = 0.0;
    double integral_y_ = 0.0;
    double output_x_ = 0.0;
    double output_y_ = 0.0;
    uint16_t output_roll_channel_ = 1500;
    uint16_t output_pitch_channel_ = 1500;
};
