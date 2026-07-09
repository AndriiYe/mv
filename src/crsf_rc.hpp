#pragma once

#include "crsf_tcp_packet_sender.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

// Stores one complete CRSF RC channel set.
//
// A struct is used instead of exposing a raw std::array because the channels
// have domain names: CRSF channel 1, channel 2, ... channel 16. The helper
// functions still allow indexed access, but the stored state remains explicit.
// Each field is atomic because it is shared between the main thread and the
// serial worker threads without locking.
struct CrsfChannelBank {
    std::atomic<uint16_t> ch1{1500};
    std::atomic<uint16_t> ch2{1500};
    std::atomic<uint16_t> ch3{1500};
    std::atomic<uint16_t> ch4{1500};
    std::atomic<uint16_t> ch5{1500};
    std::atomic<uint16_t> ch6{1500};
    std::atomic<uint16_t> ch7{1500};
    std::atomic<uint16_t> ch8{1500};
    std::atomic<uint16_t> ch9{1500};
    std::atomic<uint16_t> ch10{1500};
    std::atomic<uint16_t> ch11{1500};
    std::atomic<uint16_t> ch12{1500};
    std::atomic<uint16_t> ch13{1500};
    std::atomic<uint16_t> ch14{1500};
    std::atomic<uint16_t> ch15{1500};
    std::atomic<uint16_t> ch16{1500};

    std::atomic<uint16_t>* select(int channel);
    const std::atomic<uint16_t>* select(int channel) const;
    std::array<uint16_t, 16> snapshot() const;
    void store(const std::array<uint16_t, 16>& values);
    void fill(uint16_t value);
};

// Opens one serial port for CRSF and runs one background I/O loop:
// - reads available CRSF bytes from the port
// - sends RC_CHANNELS_PACKED every 20 ms
// - optionally mirrors transmitted packets to a local TCP debug client
//
// Main/OpenCV code only calls setChennel() and getChannel(). Those functions
// touch atomics and return immediately, so serial I/O cannot interrupt the CV
// frame loop.
class CrsfRcSender {
public:
    explicit CrsfRcSender(
        const std::string& device,
        int baudrate = 420000,
        bool tcp_mirror_enabled = true
    );
    ~CrsfRcSender();

    bool start();
    void stop();

    bool isRunning() const;

    // Public setter intentionally kept simple as requested. Channels are
    // 1-based: channel=1 updates CRSF ch1, channel=16 updates CRSF ch16.
    // Invalid channel numbers are ignored.
    void setChennel(int channel, uint16_t value);

    // Returns the latest received CRSF channel value in PWM microseconds.
    // Returns 0 for an invalid channel number or before a valid packet arrives.
    uint16_t getChannel(int channel) const;

private:
    int openSerial();
    void closeSerial();
    int readSerial(uint8_t* buffer, size_t length);
    bool writeSerial(const uint8_t* buffer, size_t length);

    void ioLoop();
    void sendPacket(const std::array<uint16_t, 16>& channels_us);
    void processFrame(const uint8_t* frame, size_t frame_size);

    static uint16_t clampPwm(uint16_t value);
    static uint16_t pwmToCrsf(uint16_t value);
    static uint16_t crsfToPwm(uint16_t value);
    static uint8_t crc8DvbS2(const uint8_t* data, size_t length);

private:
    std::string device_;
    int baudrate_;
    bool tcp_mirror_enabled_;

#ifdef _WIN32
    void* serial_handle_ = nullptr;
#else
    int fd_ = -1;
#endif

    // running_ is the only lifetime flag shared by the main thread and the
    // worker thread. stop() clears it, then waits for the I/O loop to exit
    // before closing the serial handle.
    std::atomic<bool> running_{false};

    CrsfChannelBank tx_channels_;
    CrsfChannelBank rx_channels_;
    std::atomic<bool> has_rx_channels_{false};
    CrsfTcpPacketSender tcp_packet_sender_;

    std::thread io_thread_;
};
