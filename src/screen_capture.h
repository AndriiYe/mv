#pragma once

#include <opencv2/opencv.hpp>

class ScreenCapture {
public:
    ScreenCapture() = default;
    ~ScreenCapture();

    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    bool open_primary();
    bool open_virtual_screen();
    bool open_region(int left, int top, int width, int height);
    bool read(cv::Mat& frame);
    void move_by(int dx, int dy);
    void set_position(int left, int top);

    int left() const;
    int top() const;
    int width() const;
    int height() const;

private:
    void close();
    bool open(int left, int top, int width, int height);

    int left_ = 0;
    int top_ = 0;
    int width_ = 0;
    int height_ = 0;
    void* screen_dc_ = nullptr;
    void* memory_dc_ = nullptr;
    void* bitmap_ = nullptr;
    void* previous_object_ = nullptr;
    unsigned char* pixels_ = nullptr;
};
