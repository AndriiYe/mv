#include "crsf_rc.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <asm/termbits.h>
#include <sys/ioctl.h>
#else
#include <termios.h>
#endif
#endif

namespace {

constexpr uint8_t kCrsfAddressFlightController = 0xC8;
constexpr uint8_t kCrsfFrameTypeRcChannelsPacked = 0x16;
constexpr size_t kCrsfChannelCount = 16;
constexpr size_t kCrsfPayloadSize = 22;
constexpr size_t kCrsfFrameSize = 26;
constexpr size_t kCrsfMaxFrameSize = 64;
constexpr size_t kReadBufferSize = 256;
constexpr uint8_t kCrsfLength = 24; // type + 22-byte RC payload + crc
constexpr uint16_t kCrsfMin = 192;
constexpr uint16_t kCrsfMid = 992;
constexpr uint16_t kCrsfMax = 1792;

} // namespace

std::atomic<uint16_t>* CrsfChannelBank::select(int channel) {
    switch (channel) {
        case 1: return &ch1;
        case 2: return &ch2;
        case 3: return &ch3;
        case 4: return &ch4;
        case 5: return &ch5;
        case 6: return &ch6;
        case 7: return &ch7;
        case 8: return &ch8;
        case 9: return &ch9;
        case 10: return &ch10;
        case 11: return &ch11;
        case 12: return &ch12;
        case 13: return &ch13;
        case 14: return &ch14;
        case 15: return &ch15;
        case 16: return &ch16;
        default: return nullptr;
    }
}

const std::atomic<uint16_t>* CrsfChannelBank::select(int channel) const {
    return const_cast<CrsfChannelBank*>(this)->select(channel);
}

std::array<uint16_t, 16> CrsfChannelBank::snapshot() const {
    // Each channel is loaded separately. That means a snapshot can contain a
    // mix of old and new channel values if main updates a channel at the same
    // instant the I/O thread is reading. For RC output this is acceptable: the
    // next 20 ms packet will carry the latest value, and no thread ever blocks.
    return {
        ch1.load(),
        ch2.load(),
        ch3.load(),
        ch4.load(),
        ch5.load(),
        ch6.load(),
        ch7.load(),
        ch8.load(),
        ch9.load(),
        ch10.load(),
        ch11.load(),
        ch12.load(),
        ch13.load(),
        ch14.load(),
        ch15.load(),
        ch16.load()
    };
}

void CrsfChannelBank::store(const std::array<uint16_t, 16>& values) {
    // RX commits a decoded packet channel-by-channel. Readers can observe part
    // of a new packet during this short loop, but each channel value is always
    // valid and atomic. This avoids a mutex in the OpenCV/main path.
    ch1.store(values[0]);
    ch2.store(values[1]);
    ch3.store(values[2]);
    ch4.store(values[3]);
    ch5.store(values[4]);
    ch6.store(values[5]);
    ch7.store(values[6]);
    ch8.store(values[7]);
    ch9.store(values[8]);
    ch10.store(values[9]);
    ch11.store(values[10]);
    ch12.store(values[11]);
    ch13.store(values[12]);
    ch14.store(values[13]);
    ch15.store(values[14]);
    ch16.store(values[15]);
}

void CrsfChannelBank::fill(uint16_t value) {
    std::array<uint16_t, 16> values{};
    values.fill(value);
    store(values);
}

CrsfRcSender::CrsfRcSender(const std::string& device, int baudrate, bool tcp_mirror_enabled)
    : device_(device),
      baudrate_(baudrate),
      tcp_mirror_enabled_(tcp_mirror_enabled) {
    tx_channels_.fill(1500);
    rx_channels_.fill(0);
}

CrsfRcSender::~CrsfRcSender() {
    stop();
}

bool CrsfRcSender::start() {
    if (running_.load()) {
        return true;
    }

#ifdef _WIN32
    if (openSerial() < 0) {
        return false;
    }
#else
    fd_ = openSerial();
    if (fd_ < 0) {
        return false;
    }
#endif

    // Set running_ before launching the I/O thread. The loop checks this flag
    // on every iteration; no other shared lifetime state is needed.
    running_.store(true);
    io_thread_ = std::thread(&CrsfRcSender::ioLoop, this);
    return true;
}

void CrsfRcSender::stop() {
    if (!running_.load()) {
        closeSerial();
        return;
    }

    // Tell the worker loop to finish. Serial reads are configured as
    // nonblocking/short-timeout so the loop wakes up quickly.
    running_.store(false);

    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    // The serial handle is closed only after the worker thread has stopped.
    closeSerial();
}

bool CrsfRcSender::isRunning() const {
    return running_.load();
}

void CrsfRcSender::setChennel(int channel, uint16_t value) {
    std::atomic<uint16_t>* selected = tx_channels_.select(channel);
    if (selected == nullptr) {
        return;
    }

    // This store is the whole main-thread update path. No serial write happens
    // here; ioLoop will pick the value up on its next 20 ms tick.
    selected->store(clampPwm(value));
}

uint16_t CrsfRcSender::getChannel(int channel) const {
    const std::atomic<uint16_t>* selected = rx_channels_.select(channel);
    if (selected == nullptr || !has_rx_channels_.load()) {
        return 0;
    }

    return selected->load();
}

uint16_t CrsfRcSender::clampPwm(uint16_t value) {
    if (value < 1000) {
        return 1000;
    }

    if (value > 2000) {
        return 2000;
    }

    return value;
}

uint16_t CrsfRcSender::pwmToCrsf(uint16_t value) {
    const int crsf_value = ((static_cast<int>(clampPwm(value)) - 1500) * 8) / 5 + kCrsfMid;
    return static_cast<uint16_t>(std::clamp(crsf_value, static_cast<int>(kCrsfMin), static_cast<int>(kCrsfMax)));
}

uint16_t CrsfRcSender::crsfToPwm(uint16_t value) {
    const int pwm_value = ((static_cast<int>(value) - kCrsfMid) * 5) / 8 + 1500;
    return clampPwm(static_cast<uint16_t>(std::clamp(pwm_value, 1000, 2000)));
}

uint8_t CrsfRcSender::crc8DvbS2(const uint8_t* data, size_t length) {
    uint8_t crc = 0;

    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xD5) : static_cast<uint8_t>(crc << 1);
        }
    }

    return crc;
}

int CrsfRcSender::openSerial() {
#ifdef _WIN32
    std::string port_name = device_;
    if (port_name.rfind("\\\\.\\", 0) != 0) {
        port_name = "\\\\.\\" + port_name;
    }

    HANDLE handle = CreateFileA(
        port_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open CRSF serial device: " << device_ << std::endl;
        serial_handle_ = nullptr;
        return -1;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(handle, &dcb)) {
        std::cerr << "GetCommState failed" << std::endl;
        CloseHandle(handle);
        return -1;
    }

    dcb.BaudRate = static_cast<DWORD>(baudrate_);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle, &dcb)) {
        std::cerr << "SetCommState failed" << std::endl;
        CloseHandle(handle);
        return -1;
    }

    // Nonblocking reads let one I/O loop drain RX immediately before each TX
    // packet without delaying the 20 ms send cadence.
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 20;
    SetCommTimeouts(handle, &timeouts);

    serial_handle_ = handle;
    return 0;
#else
    int fd = open(device_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);

    if (fd < 0) {
        std::cerr << "Failed to open CRSF serial device: " << device_ << std::endl;
        return -1;
    }

#if defined(__linux__)
    termios2 tty{};
    if (ioctl(fd, TCGETS2, &tty) != 0) {
        std::cerr << "TCGETS2 failed" << std::endl;
        close(fd);
        return -1;
    }

    // Linux termios2+BOTHER is used because CRSF commonly runs at 420000 baud,
    // which is not a traditional POSIX baud constant.
    tty.c_cflag &= ~CBAUD;
    tty.c_cflag |= BOTHER;
    tty.c_ispeed = static_cast<unsigned int>(baudrate_);
    tty.c_ospeed = static_cast<unsigned int>(baudrate_);

    tty.c_cflag = static_cast<unsigned int>((tty.c_cflag & ~CSIZE) | CS8);
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif

    if (ioctl(fd, TCSETS2, &tty) != 0) {
        std::cerr << "TCSETS2 failed" << std::endl;
        close(fd);
        return -1;
    }
#else
    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "tcgetattr failed" << std::endl;
        close(fd);
        return -1;
    }

    speed_t speed = B115200;
    switch (baudrate_) {
        case 57600:
            speed = B57600;
            break;
        case 115200:
            speed = B115200;
            break;
        case 460800:
            speed = B460800;
            break;
        case 921600:
            speed = B921600;
            break;
        default:
            std::cerr << "Unsupported POSIX baudrate: " << baudrate_ << std::endl;
            close(fd);
            return -1;
    }

    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    tty.c_cflag = static_cast<unsigned int>((tty.c_cflag & ~CSIZE) | CS8);
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "tcsetattr failed" << std::endl;
        close(fd);
        return -1;
    }
#endif

    return fd;
#endif
}

void CrsfRcSender::closeSerial() {
#ifdef _WIN32
    if (serial_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(serial_handle_));
        serial_handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

int CrsfRcSender::readSerial(uint8_t* buffer, size_t length) {
#ifdef _WIN32
    if (serial_handle_ == nullptr) {
        return -1;
    }

    DWORD bytes_read = 0;
    if (!ReadFile(static_cast<HANDLE>(serial_handle_), buffer, static_cast<DWORD>(length), &bytes_read, nullptr)) {
        return -1;
    }

    return static_cast<int>(bytes_read);
#else
    if (fd_ < 0) {
        return -1;
    }

    const ssize_t bytes_read = read(fd_, buffer, length);
    return bytes_read < 0 ? -1 : static_cast<int>(bytes_read);
#endif
}

bool CrsfRcSender::writeSerial(const uint8_t* buffer, size_t length) {
#ifdef _WIN32
    if (serial_handle_ == nullptr) {
        return false;
    }

    DWORD written = 0;
    return WriteFile(static_cast<HANDLE>(serial_handle_), buffer, static_cast<DWORD>(length), &written, nullptr) &&
        written == length;
#else
    if (fd_ < 0) {
        return false;
    }

    const ssize_t written = write(fd_, buffer, length);
    return written == static_cast<ssize_t>(length);
#endif
}

void CrsfRcSender::ioLoop() {
    using clock = std::chrono::steady_clock;

    constexpr auto period = std::chrono::milliseconds(20);
#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    auto next_time = clock::now();
    std::array<uint8_t, kCrsfMaxFrameSize> frame{};
    size_t index = 0;
    size_t expected_size = 0;

    while (running_.load()) {
        const auto now = clock::now();
        if (next_time + period < now) {
            next_time = now;
        }

        std::this_thread::sleep_until(next_time);

        // Read available RX bytes once, then transmit. Keeping this bounded is
        // important: RX parsing must not stretch the TX period.
        std::array<uint8_t, kReadBufferSize> buffer{};
        const int read_count = readSerial(buffer.data(), buffer.size());
        if (read_count > 0) {
            for (int byte_index = 0; byte_index < read_count; ++byte_index) {
                const uint8_t byte = buffer[byte_index];

                if (index == 0) {
                    if (byte != kCrsfAddressFlightController) {
                        continue;
                    }

                    frame[index++] = byte;
                    continue;
                }

                if (index == 1) {
                    if (byte < 2 || byte > kCrsfMaxFrameSize - 2) {
                        index = 0;
                        expected_size = 0;
                        continue;
                    }

                    frame[index++] = byte;
                    expected_size = static_cast<size_t>(byte) + 2;
                    continue;
                }

                frame[index++] = byte;

                if (expected_size != 0 && index >= expected_size) {
                    processFrame(frame.data(), expected_size);
                    index = 0;
                    expected_size = 0;
                }
            }
        }

        // The I/O thread makes a local copy of all output channels before
        // building a frame. Main never waits for this copy to finish.
        sendPacket(tx_channels_.snapshot());

        next_time += period;
    }

    // On shutdown, send a few neutral frames. This is done from the I/O thread
    // after running_ is false, before the serial handle is closed.
    std::array<uint16_t, 16> neutral_channels{};
    neutral_channels.fill(1500);
    for (int i = 0; i < 10; ++i) {
        sendPacket(neutral_channels);
        std::this_thread::sleep_for(period);
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

void CrsfRcSender::sendPacket(const std::array<uint16_t, 16>& channels_us) {
    std::array<uint16_t, 16> channels_crsf{};
    for (size_t i = 0; i < channels_crsf.size(); ++i) {
        channels_crsf[i] = pwmToCrsf(channels_us[i]);
    }

    std::array<uint8_t, kCrsfFrameSize> frame{};
    frame[0] = kCrsfAddressFlightController;
    frame[1] = kCrsfLength;
    frame[2] = kCrsfFrameTypeRcChannelsPacked;

    uint32_t bit_buffer = 0;
    int bits_in_buffer = 0;
    size_t payload_index = 3;

    // CRSF packs sixteen 11-bit channel values little-endian into 22 bytes.
    // This loop appends 11 bits per channel to a rolling bit buffer and emits
    // full bytes as soon as they are available.
    for (size_t channel_index = 0; channel_index < kCrsfChannelCount; ++channel_index) {
        bit_buffer |= static_cast<uint32_t>(channels_crsf[channel_index] & 0x07FF) << bits_in_buffer;
        bits_in_buffer += 11;

        while (bits_in_buffer >= 8 && payload_index < 3 + kCrsfPayloadSize) {
            frame[payload_index++] = static_cast<uint8_t>(bit_buffer & 0xFF);
            bit_buffer >>= 8;
            bits_in_buffer -= 8;
        }
    }

    // CRC covers frame type and payload, not the address or length bytes.
    frame[kCrsfFrameSize - 1] = crc8DvbS2(&frame[2], kCrsfPayloadSize + 1);

    if (!writeSerial(frame.data(), frame.size())) {
        std::cerr << "Failed to write CRSF RC packet" << std::endl;
    }

    if (tcp_mirror_enabled_) {
        tcp_packet_sender_.sendPacket(frame.data(), frame.size());
    }
}

void CrsfRcSender::processFrame(const uint8_t* frame, size_t frame_size) {
    if (frame_size != kCrsfFrameSize ||
        frame[0] != kCrsfAddressFlightController ||
        frame[1] != kCrsfLength ||
        frame[2] != kCrsfFrameTypeRcChannelsPacked) {
        return;
    }

    const uint8_t expected_crc = crc8DvbS2(&frame[2], kCrsfPayloadSize + 1);
    if (expected_crc != frame[kCrsfFrameSize - 1]) {
        return;
    }

    std::array<uint16_t, 16> decoded_channels{};
    uint32_t bit_buffer = 0;
    int bits_in_buffer = 0;
    size_t payload_index = 3;

    for (size_t channel_index = 0; channel_index < kCrsfChannelCount; ++channel_index) {
        while (bits_in_buffer < 11 && payload_index < 3 + kCrsfPayloadSize) {
            bit_buffer |= static_cast<uint32_t>(frame[payload_index++]) << bits_in_buffer;
            bits_in_buffer += 8;
        }

        const uint16_t crsf_value = static_cast<uint16_t>(bit_buffer & 0x07FF);
        decoded_channels[channel_index] = crsfToPwm(crsf_value);
        bit_buffer >>= 11;
        bits_in_buffer -= 11;
    }

    rx_channels_.store(decoded_channels);
    has_rx_channels_.store(true);
}
