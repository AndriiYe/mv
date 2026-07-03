#pragma once

#include <chrono>

struct KalmanFiltreConfig {
    float process_noise = 0.01F;
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
