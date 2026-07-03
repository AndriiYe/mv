#include "kalman_filtre.hpp"

#include <algorithm>

Kalman_filtre::Kalman_filtre(const KalmanFiltreConfig& config)
    : config_(config),
      last_time_(timer::now()) {
    config_.process_noise = std::max(config_.process_noise, 0.0F);
    config_.measurement_noise = std::max(config_.measurement_noise, 0.000001F);
    config_.estimate_error = std::max(config_.estimate_error, 0.000001F);

    x_.estimate = config_.initial_speed_x;
    x_.error = config_.estimate_error;
    y_.estimate = config_.initial_speed_y;
    y_.error = config_.estimate_error;

    D_speed_x = x_.estimate;
    D_speed_y = y_.estimate;
}

void Kalman_filtre::update(float dx, float dy) {
    const auto time_now = timer::now();
    const std::chrono::duration<float> elapsed = time_now - last_time_;
    last_time_ = time_now;

    const float dt = elapsed.count();
    if (dt <= 0.000001F) {
        return;
    }

    const float speed_x = dx / dt;
    const float speed_y = dy / dt;

    D_speed_x = update_axis(x_, speed_x);
    D_speed_y = update_axis(y_, speed_y);
}

float Kalman_filtre::update_axis(AxisState& axis, float measurement) {
    axis.error += config_.process_noise;

    const float kalman_gain = axis.error / (axis.error + config_.measurement_noise);
    axis.estimate += kalman_gain * (measurement - axis.estimate);
    axis.error *= 1.0F - kalman_gain;

    return axis.estimate;
}
