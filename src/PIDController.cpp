#include "PIDController.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kPwmMin = 1000.0;
constexpr double kPwmNeutral = 1500.0;
constexpr double kPwmMax = 2000.0;
constexpr int kOutputHorizontalChannel = 1;
constexpr int kOutputVerticalChannel = 2;
constexpr int kTunePChannel = 9;
constexpr int kTuneIChannel = 10;
constexpr int kTuneDChannel = 11;
constexpr double kTunePDivisor = 500.0;  // 1000-2000 us -> 0.0-2.0
constexpr double kTuneIDivisor = 2000.0; // 1000-2000 us -> 0.0-0.5
constexpr double kTuneDDivisor = 1000.0; // 1000-2000 us -> 0.0-1.0

} // namespace

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

    // The D term uses measured target speed instead of a finite difference of
    // error. This avoids derivative spikes when the tracker is reset/reacquired.
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

uint16_t PIDController::output_roll_channel() const {
    return output_roll_channel_;
}

uint16_t PIDController::output_pitch_channel() const {
    return output_pitch_channel_;
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
    return static_cast<uint16_t>(std::lround(clamp(kPwmNeutral + output, kPwmMin, kPwmMax)));
}

double PIDController::clamp(double value, double min_value, double max_value) {
    return std::clamp(value, min_value, max_value);
}

void PIDController::set_from_rc() {
    if (rc_ == nullptr) {
        return;
    }

    const uint16_t p_channel = rc_->getChannel(kTunePChannel);
    const uint16_t i_channel = rc_->getChannel(kTuneIChannel);
    const uint16_t d_channel = rc_->getChannel(kTuneDChannel);
    if (p_channel == 0 || i_channel == 0 || d_channel == 0) {
        return;
    }

    // Aux channels act as absolute gain knobs. Keep channel 10 low at first:
    // even small integral gain can accumulate while the target is off-center.
    config_.kp = (static_cast<double>(p_channel) - kPwmMin) / kTunePDivisor;
    config_.ki = (static_cast<double>(i_channel) - kPwmMin) / kTuneIDivisor;
    config_.kd = (static_cast<double>(d_channel) - kPwmMin) / kTuneDDivisor;
}

void PIDController::update_rc(double x, double y) {
    output_roll_channel_ = pwm_from_output(-x);
    output_pitch_channel_ = pwm_from_output(-y);

    if (rc_ == nullptr) {
        return;
    }

    // P/R servos expect the opposite sign from the image-space controller.
    // Keep the PID math/display unchanged and invert only at the RC boundary.
    rc_->setChennel(kOutputHorizontalChannel, output_roll_channel_);
    rc_->setChennel(kOutputVerticalChannel, output_pitch_channel_);
}
