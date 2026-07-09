#pragma once

#include <chrono>

struct KalmanFiltreConfig {
    // Q/process_noise controls how quickly the speed estimate is allowed to
    // change. Higher Q follows motion faster but passes more noise.
    float process_noise = 0.01F;

    // R/measurement_noise controls how much to trust measured optical-flow
    // speed. Higher R gives smoother output but adds more lag.
    float measurement_noise = 1.0F;

    float estimate_error = 1.0F;
    float initial_speed_x = 0.0F;
    float initial_speed_y = 0.0F;
};

class Kalman_filtre {
public:
    using timer = std::chrono::steady_clock;

    explicit Kalman_filtre(const KalmanFiltreConfig& config);

    void update(float dx, float dy);
    void set_noise(float process_noise, float measurement_noise);

    float process_noise() const;
    float measurement_noise() const;

    float D_speed_x = 0.0F;
    float D_speed_y = 0.0F;

private:
    struct AxisState {
        float estimate = 0.0F;
        float error = 1.0F;
    };

    float update_axis(AxisState& axis, float measurement);

    KalmanFiltreConfig config_;
    timer::time_point last_time_;
    AxisState x_;
    AxisState y_;
};
