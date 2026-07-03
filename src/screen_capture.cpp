#include "screen_capture.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

int system_metric(int index) {
    return GetSystemMetrics(index);
}

} // namespace

ScreenCapture::~ScreenCapture() {
    close();
}

bool ScreenCapture::open_primary() {
    return open(
        0,
        0,
        system_metric(SM_CXSCREEN),
        system_metric(SM_CYSCREEN)
    );
}

bool ScreenCapture::open_virtual_screen() {
    return open(
        system_metric(SM_XVIRTUALSCREEN),
        system_metric(SM_YVIRTUALSCREEN),
        system_metric(SM_CXVIRTUALSCREEN),
        system_metric(SM_CYVIRTUALSCREEN)
    );
}

bool ScreenCapture::open_region(int left, int top, int width, int height) {
    return open(left, top, width, height);
}

bool ScreenCapture::read(cv::Mat& frame) {
    if (!screen_dc_ || !memory_dc_ || !bitmap_ || !pixels_) {
        return false;
    }

    const BOOL copied = BitBlt(
        static_cast<HDC>(memory_dc_),
        0,
        0,
        width_,
        height_,
        static_cast<HDC>(screen_dc_),
        left_,
        top_,
        SRCCOPY | CAPTUREBLT
    );
    if (!copied) {
        frame.release();
        return false;
    }

    cv::Mat bgra(height_, width_, CV_8UC4, pixels_);
    cv::cvtColor(bgra, frame, cv::COLOR_BGRA2BGR);
    return true;
}

void ScreenCapture::move_by(int dx, int dy) {
    set_position(left_ + dx, top_ + dy);
}

void ScreenCapture::set_position(int left, int top) {
    left_ = left;
    top_ = top;
}

int ScreenCapture::left() const {
    return left_;
}

int ScreenCapture::top() const {
    return top_;
}

int ScreenCapture::width() const {
    return width_;
}

int ScreenCapture::height() const {
    return height_;
}

void ScreenCapture::close() {
    if (memory_dc_ && previous_object_) {
        SelectObject(static_cast<HDC>(memory_dc_), static_cast<HGDIOBJ>(previous_object_));
    }

    if (bitmap_) {
        DeleteObject(static_cast<HBITMAP>(bitmap_));
    }
    if (memory_dc_) {
        DeleteDC(static_cast<HDC>(memory_dc_));
    }
    if (screen_dc_) {
        ReleaseDC(nullptr, static_cast<HDC>(screen_dc_));
    }

    screen_dc_ = nullptr;
    memory_dc_ = nullptr;
    bitmap_ = nullptr;
    previous_object_ = nullptr;
    pixels_ = nullptr;
    left_ = 0;
    top_ = 0;
    width_ = 0;
    height_ = 0;
}

bool ScreenCapture::open(int left, int top, int width, int height) {
    close();

    if (width <= 0 || height <= 0) {
        return false;
    }

    SetProcessDPIAware();

    HDC screen_dc = GetDC(nullptr);
    if (!screen_dc) {
        return false;
    }

    HDC memory_dc = CreateCompatibleDC(screen_dc);
    if (!memory_dc) {
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        memory_dc,
        &bitmap_info,
        DIB_RGB_COLORS,
        &pixels,
        nullptr,
        0
    );
    if (!bitmap || !pixels) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    HGDIOBJ previous_object = SelectObject(memory_dc, bitmap);
    if (!previous_object) {
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    left_ = left;
    top_ = top;
    width_ = width;
    height_ = height;
    screen_dc_ = screen_dc;
    memory_dc_ = memory_dc;
    bitmap_ = bitmap;
    previous_object_ = previous_object;
    pixels_ = static_cast<unsigned char*>(pixels);
    return true;
}

#else

ScreenCapture::~ScreenCapture() = default;

bool ScreenCapture::open_primary() {
    return false;
}

bool ScreenCapture::open_virtual_screen() {
    return false;
}

bool ScreenCapture::open_region(int, int, int, int) {
    return false;
}

bool ScreenCapture::read(cv::Mat& frame) {
    frame.release();
    return false;
}

void ScreenCapture::move_by(int, int) {
}

void ScreenCapture::set_position(int, int) {
}

int ScreenCapture::left() const {
    return 0;
}

int ScreenCapture::top() const {
    return 0;
}

int ScreenCapture::width() const {
    return 0;
}

int ScreenCapture::height() const {
    return 0;
}

void ScreenCapture::close() {
}

bool ScreenCapture::open(int, int, int, int) {
    return false;
}

#endif
