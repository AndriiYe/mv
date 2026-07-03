#pragma once

#include <opencv2/opencv.hpp>

#include <vector>

// All tunable parameters for LocalTracker live in one object so callers can
// either configure the tracker once at construction or replace the full setup
// later with LocalTracker::set(config).
struct LocalTrackerConfig {
    int max_points = 20;
    int tracking_region_size = 50;
    float max_delta = 20.0F;

    // Negative values mean "use the frame center on initialization".
    int ROI_center_x = -1;
    int ROI_center_y = -1;

    double qualityLevel = 0.01;
    double minDistance = 5.0;
    cv::Size winSize = cv::Size(21, 21);
    int maxLevel = 3;
    cv::TermCriteria criteria = cv::TermCriteria(
        cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
        30,
        0.01
    );
    int flags = 0;
    double minEigThreshold = 1e-4;
};

// LocalTracker tracks feature points inside a local ROI in an already-prepared
// grayscale frame buffer. It does not capture frames, convert color, draw, or
// show windows; callers own all input/output presentation.
class LocalTracker {
public:
    explicit LocalTracker(const cv::Mat& frame_buffer, LocalTrackerConfig config = {});

    // Processes the current contents of frame_buffer and returns the accepted
    // median ROI displacement for this frame. A rejected fast jump returns 0,0.
    cv::Point2f update();

    // Replace the whole configuration and force the tracker to initialize again
    // from the next non-empty frame.
    void set(const LocalTrackerConfig& config);

    // Individual setters are intentionally small and explicit so tuning code can
    // change one parameter without rebuilding the whole config object.
    void set_max_points(int value);
    void set_tracking_region_size(int value);
    void set_max_delta(float value);
    void set_ROI_center_x(int value);
    void set_ROI_center_y(int value);
    void set_qualityLevel(double value);
    void set_minDistance(double value);
    void set_winSize(cv::Size value);
    void set_maxLevel(int value);
    void set_criteria(cv::TermCriteria value);
    void set_flags(int value);
    void set_minEigThreshold(double value);

    const cv::Rect& tracking_region() const;
    int recap_counter() const;
    const std::vector<cv::Point2f>& valid_next_points() const;

private:
    void reset_state();
    void initialize_from_current_frame();
    void reserve_buffers();
    void recap_at_current_roi();
    cv::Rect make_tracking_region(float center_x, float center_y) const;
    bool touches_frame_border(const cv::Rect& region) const;
    std::vector<cv::Point2f> find_tracking_points(const cv::Rect& region) const;
    cv::Point2f median_delta(
        const std::vector<cv::Point2f>& previous_points,
        const std::vector<cv::Point2f>& current_points
    ) const;

    const cv::Mat& frame_buffer_;
    LocalTrackerConfig config_;

    cv::Mat previous_gray_;
    bool initialized_ = false;

    float origin_center_x_ = 0.0F;
    float origin_center_y_ = 0.0F;
    float tracking_region_center_x_ = 0.0F;
    float tracking_region_center_y_ = 0.0F;
    cv::Rect tracking_region_;
    int recap_counter_ = 0;

    std::vector<cv::Point2f> previous_points_;
    std::vector<cv::Point2f> next_points_;
    std::vector<uchar> status_;
    std::vector<float> errors_;
    std::vector<cv::Point2f> valid_previous_points_;
    std::vector<cv::Point2f> valid_next_points_;
};
