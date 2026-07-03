#include "local_tracker.h"

#include <algorithm>
#include <cmath>

LocalTracker::LocalTracker(const cv::Mat& frame_buffer, LocalTrackerConfig config)
    : frame_buffer_(frame_buffer),
      config_(config) {
    reserve_buffers();
}

cv::Point2f LocalTracker::update() {
    if (frame_buffer_.empty()) {
        return cv::Point2f(0.0F, 0.0F);
    }

    // First valid frame: choose the ROI origin, detect initial feature points,
    // and store this frame as the "previous" LK input.
    if (!initialized_) {
        initialize_from_current_frame();
        return cv::Point2f(0.0F, 0.0F);
    }

    // If all points were lost on the previous update, re-seed from the current
    // ROI and wait for the next frame before estimating motion again.
    if (previous_points_.empty()) {
        recap_at_current_roi();
        frame_buffer_.copyTo(previous_gray_);
        return cv::Point2f(0.0F, 0.0F);
    }

    next_points_.clear();
    status_.clear();
    errors_.clear();
    valid_previous_points_.clear();
    valid_next_points_.clear();

    cv::calcOpticalFlowPyrLK(
        previous_gray_,
        frame_buffer_,
        previous_points_,
        next_points_,
        status_,
        errors_,
        config_.winSize,
        config_.maxLevel,
        config_.criteria,
        config_.flags,
        config_.minEigThreshold
    );

    // Keep matched LK pairs only when OpenCV reports a valid track. Border/ROI
    // filtering happens after the ROI has been shifted by median motion.
    for (size_t i = 0; i < next_points_.size() && i < status_.size(); ++i) {
        if (!status_[i]) {
            continue;
        }

        valid_previous_points_.push_back(previous_points_[i]);
        valid_next_points_.push_back(next_points_[i]);
    }

    const cv::Point2f delta = median_delta(valid_previous_points_, valid_next_points_);
    const bool is_fast_jump =
        std::abs(delta.x) > config_.max_delta ||
        std::abs(delta.y) > config_.max_delta;

    cv::Point2f accepted_delta(0.0F, 0.0F);
    bool reinitialized_tracking = false;

    // A large median jump is treated as an invalid motion estimate. The ROI is
    // kept at its current center and features are re-detected there.
    if (is_fast_jump) {
        recap_at_current_roi();
        reinitialized_tracking = true;
    } else {
        tracking_region_center_x_ += delta.x;
        tracking_region_center_y_ += delta.y;
        accepted_delta = delta;
    }

    // Build the ROI after motion was accepted or rejected. This rectangle is
    // the public output returned by tracking_region().
    tracking_region_ = make_tracking_region(
        tracking_region_center_x_,
        tracking_region_center_y_
    );

    // If the ROI reaches the frame edge, snap back to the initial origin center
    // and re-detect points. This prevents the local tracker from sliding out of
    // the observable image area.
    if (touches_frame_border(tracking_region_)) {
        tracking_region_center_x_ = origin_center_x_;
        tracking_region_center_y_ = origin_center_y_;
        tracking_region_ = make_tracking_region(
            tracking_region_center_x_,
            tracking_region_center_y_
        );
        recap_at_current_roi();
        reinitialized_tracking = true;
        accepted_delta = cv::Point2f(0.0F, 0.0F);
    }

    if (!is_fast_jump && !reinitialized_tracking) {
        previous_points_.clear();

        // Keep only LK points still inside the moved ROI. Those points become
        // the previous_points_ input on the next update().
        for (const cv::Point2f& point : valid_next_points_) {
            if (point.x < tracking_region_.x ||
                point.x >= tracking_region_.x + tracking_region_.width ||
                point.y < tracking_region_.y ||
                point.y >= tracking_region_.y + tracking_region_.height) {
                continue;
            }

            previous_points_.push_back(point);
        }

        valid_next_points_ = previous_points_;
    }

    frame_buffer_.copyTo(previous_gray_);
    return accepted_delta;
}

void LocalTracker::set(const LocalTrackerConfig& config) {
    config_ = config;
    reset_state();
}

void LocalTracker::set_max_points(int value) {
    config_.max_points = value;
    reset_state();
}

void LocalTracker::set_tracking_region_size(int value) {
    config_.tracking_region_size = value;
    reset_state();
}

void LocalTracker::set_max_delta(float value) {
    config_.max_delta = value;
}

void LocalTracker::set_ROI_center_x(int value) {
    config_.ROI_center_x = value;
    reset_state();
}

void LocalTracker::set_ROI_center_y(int value) {
    config_.ROI_center_y = value;
    reset_state();
}

void LocalTracker::set_qualityLevel(double value) {
    config_.qualityLevel = value;
    reset_state();
}

void LocalTracker::set_minDistance(double value) {
    config_.minDistance = value;
    reset_state();
}

void LocalTracker::set_winSize(cv::Size value) {
    config_.winSize = value;
}

void LocalTracker::set_maxLevel(int value) {
    config_.maxLevel = value;
}

void LocalTracker::set_criteria(cv::TermCriteria value) {
    config_.criteria = value;
}

void LocalTracker::set_flags(int value) {
    config_.flags = value;
}

void LocalTracker::set_minEigThreshold(double value) {
    config_.minEigThreshold = value;
}

const cv::Rect& LocalTracker::tracking_region() const {
    return tracking_region_;
}

int LocalTracker::recap_counter() const {
    return recap_counter_;
}

const std::vector<cv::Point2f>& LocalTracker::valid_next_points() const {
    return valid_next_points_;
}

void LocalTracker::reset_state() {
    reserve_buffers();
    previous_gray_.release();
    previous_points_.clear();
    next_points_.clear();
    status_.clear();
    errors_.clear();
    valid_previous_points_.clear();
    valid_next_points_.clear();
    tracking_region_ = cv::Rect();
    initialized_ = false;
}

void LocalTracker::initialize_from_current_frame() {
    const int default_center_x = frame_buffer_.cols / 2;
    const int default_center_y = frame_buffer_.rows / 2;

    origin_center_x_ = config_.ROI_center_x >= 0 ? config_.ROI_center_x : default_center_x;
    origin_center_y_ = config_.ROI_center_y >= 0 ? config_.ROI_center_y : default_center_y;
    tracking_region_center_x_ = origin_center_x_;
    tracking_region_center_y_ = origin_center_y_;
    tracking_region_ = make_tracking_region(
        tracking_region_center_x_,
        tracking_region_center_y_
    );

    recap_at_current_roi();
    frame_buffer_.copyTo(previous_gray_);
    initialized_ = true;
}

void LocalTracker::reserve_buffers() {
    const size_t capacity = static_cast<size_t>(std::max(0, config_.max_points));
    previous_points_.reserve(capacity);
    next_points_.reserve(capacity);
    status_.reserve(capacity);
    errors_.reserve(capacity);
    valid_previous_points_.reserve(capacity);
    valid_next_points_.reserve(capacity);
}

void LocalTracker::recap_at_current_roi() {
    tracking_region_ = make_tracking_region(
        tracking_region_center_x_,
        tracking_region_center_y_
    );
    previous_points_ = find_tracking_points(tracking_region_);
    valid_next_points_ = previous_points_;
    ++recap_counter_;
}

cv::Rect LocalTracker::make_tracking_region(float center_x, float center_y) const {
    const int width = std::min(config_.tracking_region_size, frame_buffer_.cols);
    const int height = std::min(config_.tracking_region_size, frame_buffer_.rows);
    const int x = std::clamp(
        static_cast<int>(std::lround(center_x - static_cast<float>(width / 2))),
        0,
        frame_buffer_.cols - width
    );
    const int y = std::clamp(
        static_cast<int>(std::lround(center_y - static_cast<float>(height / 2))),
        0,
        frame_buffer_.rows - height
    );
    return cv::Rect(x, y, width, height);
}

bool LocalTracker::touches_frame_border(const cv::Rect& region) const {
    return region.x <= 0 ||
        region.y <= 0 ||
        region.x + region.width >= frame_buffer_.cols ||
        region.y + region.height >= frame_buffer_.rows;
}

std::vector<cv::Point2f> LocalTracker::find_tracking_points(const cv::Rect& region) const {
    std::vector<cv::Point2f> points;
    std::vector<cv::Point2f> region_points;
    region_points.reserve(static_cast<size_t>(std::max(0, config_.max_points)));

    cv::goodFeaturesToTrack(
        frame_buffer_(region),
        region_points,
        config_.max_points,
        config_.qualityLevel,
        config_.minDistance
    );

    points.reserve(region_points.size());
    for (const cv::Point2f& point : region_points) {
        points.emplace_back(point.x + region.x, point.y + region.y);
    }

    return points;
}

cv::Point2f LocalTracker::median_delta(
    const std::vector<cv::Point2f>& previous_points,
    const std::vector<cv::Point2f>& current_points
) const {
    const size_t count = std::min(previous_points.size(), current_points.size());
    if (count == 0) {
        return cv::Point2f(0.0F, 0.0F);
    }

    std::vector<float> dx_values;
    std::vector<float> dy_values;
    dx_values.reserve(count);
    dy_values.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        dx_values.push_back(current_points[i].x - previous_points[i].x);
        dy_values.push_back(current_points[i].y - previous_points[i].y);
    }

    const size_t middle = count / 2;
    std::nth_element(dx_values.begin(), dx_values.begin() + middle, dx_values.end());
    std::nth_element(dy_values.begin(), dy_values.begin() + middle, dy_values.end());

    return cv::Point2f(dx_values[middle], dy_values[middle]);
}
