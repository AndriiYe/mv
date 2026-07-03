#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

// Nonblocking TCP listener that mirrors raw CRSF frames to one local client.
class CrsfTcpPacketSender {
public:
    explicit CrsfTcpPacketSender(std::string host = "127.0.0.1", uint16_t port = 4005);
    ~CrsfTcpPacketSender();

    CrsfTcpPacketSender(const CrsfTcpPacketSender&) = delete;
    CrsfTcpPacketSender& operator=(const CrsfTcpPacketSender&) = delete;

    void sendPacket(const uint8_t* data, size_t length);
    void close();
    bool isConnected() const;

private:
#ifdef _WIN32
    using SocketHandle = std::uintptr_t;
#else
    using SocketHandle = int;
#endif

    static SocketHandle invalidSocket();

    bool startSockets();
    bool ensureListening();
    bool openListener();
    void acceptClient();
    void closeClient();
    void closeListener();
    void scheduleReconnect();

    std::string host_;
    uint16_t port_;
    SocketHandle listen_socket_;
    SocketHandle client_socket_;
    std::chrono::steady_clock::time_point next_listen_attempt_{};

#ifdef _WIN32
    bool winsock_started_ = false;
#endif
};
