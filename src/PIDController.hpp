#pragma once

#include "crsf_rc.hpp"

#include <chrono>
#include <cstdint>
#include <memory>

struct PIDControllerConfig {
    double kp = 1.2;
    double ki = 0.0;
    double kd = 0.05;
    double output_limit = 300.0;
    double integral_limit = 5000.0;
    bool tune_from_rc = false;
};

class PIDController {
public:
    using timer = std::chrono::steady_clock;

    PIDController(std::shared_ptr<CrsfRcSender> rc, const PIDControllerConfig& config);
    PIDController(std::shared_ptr<CrsfRcSender> rc, double p, double i, double d);

    void update(double current_x, double current_y, double speed_x, double speed_y);
    void setK(double p, double i, double d);

    double output_x() const;
    double output_y() const;
    double kp() const;
    double ki() const;
    double kd() const;

private:
    static uint16_t pwm_from_output(double output);
    static double clamp(double value, double min_value, double max_value);
    static uint16_t neutral_if_missing(uint16_t value);

    void set_from_rc();
    void update_rc(double x, double y);

    std::shared_ptr<CrsfRcSender> rc_;
    PIDControllerConfig config_;
    timer::time_point last_time_;
    double integral_x_ = 0.0;
    double integral_y_ = 0.0;
    double output_x_ = 0.0;
    double output_y_ = 0.0;
};
