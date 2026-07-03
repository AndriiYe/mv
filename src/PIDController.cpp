#include "PIDController.hpp"

#include <algorithm>
#include <cmath>

PIDController::PIDController(std::shared_ptr<CrsfRcSender> rc, const PIDControllerConfig& config)
    : rc_(std::move(rc)),
      config_(config),
      last_time_(timer::now()) {
    config_.output_limit = std::max(0.0, config_.output_limit);
    config_.integral_limit = std::max(0.0, config_.integral_limit);
}

PIDController::PIDController(std::shared_ptr<CrsfRcSender> rc, double p, double i, double d)
    : PIDController(std::move(rc), PIDControllerConfig{p, i, d}) {
}

void PIDController::update(double current_x, double current_y, double speed_x, double speed_y) {
    const auto time_now = timer::now();
    const std::chrono::duration<double> elapsed = time_now - last_time_;
    last_time_ = time_now;

    const double dt = elapsed.count();
    if (dt <= 0.000001) {
        return;
    }

    if (config_.tune_from_rc) {
        set_from_rc();
    }

    // Target is fixed at the frame center, represented as coordinate 0,0.
    const double error_x = -current_x;
    const double error_y = -current_y;

    integral_x_ = clamp(
        integral_x_ + error_x * dt,
        -config_.integral_limit,
        config_.integral_limit
    );
    integral_y_ = clamp(
        integral_y_ + error_y * dt,
        -config_.integral_limit,
        config_.integral_limit
    );

    output_x_ = config_.kp * error_x +
        config_.ki * integral_x_ -
        config_.kd * speed_x;
    output_y_ = config_.kp * error_y +
        config_.ki * integral_y_ -
        config_.kd * speed_y;

    output_x_ = clamp(output_x_, -config_.output_limit, config_.output_limit);
    output_y_ = clamp(output_y_, -config_.output_limit, config_.output_limit);

    update_rc(output_x_, output_y_);
}

void PIDController::setK(double p, double i, double d) {
    config_.kp = p;
    config_.ki = i;
    config_.kd = d;
}

double PIDController::output_x() const {
    return output_x_;
}

double PIDController::output_y() const {
    return output_y_;
}

double PIDController::kp() const {
    return config_.kp;
}

double PIDController::ki() const {
    return config_.ki;
}

double PIDController::kd() const {
    return config_.kd;
}

uint16_t PIDController::pwm_from_output(double output) {
    return static_cast<uint16_t>(std::lround(clamp(1500.0 + output, 1000.0, 2000.0)));
}

double PIDController::clamp(double value, double min_value, double max_value) {
    return std::clamp(value, min_value, max_value);
}

uint16_t PIDController::neutral_if_missing(uint16_t value) {
    return value == 0 ? 1500 : value;
}

void PIDController::set_from_rc() {
    if (rc_ == nullptr) {
        return;
    }

    const double ch8 = static_cast<double>(neutral_if_missing(rc_->getChannel(8)));
    const double ch9 = static_cast<double>(neutral_if_missing(rc_->getChannel(9)));
    const double ch10 = static_cast<double>(neutral_if_missing(rc_->getChannel(10)));

    config_.kp = (ch8 - 1000.0) / 500.0;
    config_.ki = (ch9 - 1000.0) / 2000.0;
    config_.kd = (ch10 - 1000.0) / 1000.0;
}

void PIDController::update_rc(double x, double y) {
    if (rc_ == nullptr) {
        return;
    }

    // Cursor uses channel 4 for horizontal movement and channel 3 for vertical.
    rc_->setChennel(4, pwm_from_output(x));
    rc_->setChennel(3, pwm_from_output(y));
}
