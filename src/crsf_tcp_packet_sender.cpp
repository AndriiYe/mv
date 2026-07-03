#include "crsf_tcp_packet_sender.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kReconnectDelay = std::chrono::seconds(1);

#ifdef _WIN32
SOCKET nativeSocket(std::uintptr_t socket) {
    return static_cast<SOCKET>(socket);
}

int lastSocketError() {
    return WSAGetLastError();
}

bool isWouldBlock(int error) {
    return error == WSAEWOULDBLOCK;
}

void closeNativeSocket(SOCKET socket) {
    closesocket(socket);
}
#else
int nativeSocket(int socket) {
    return socket;
}

int lastSocketError() {
    return errno;
}

bool isWouldBlock(int error) {
    return error == EAGAIN || error == EWOULDBLOCK;
}

void closeNativeSocket(int socket) {
    close(socket);
}
#endif

} // namespace

CrsfTcpPacketSender::CrsfTcpPacketSender(std::string host, uint16_t port)
    : host_(std::move(host)),
      port_(port),
      listen_socket_(invalidSocket()),
      client_socket_(invalidSocket()) {
}

CrsfTcpPacketSender::~CrsfTcpPacketSender() {
    closeClient();
    closeListener();

#ifdef _WIN32
    if (winsock_started_) {
        WSACleanup();
    }
#endif
}

void CrsfTcpPacketSender::sendPacket(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return;
    }

    if (!ensureListening()) {
        return;
    }

    acceptClient();
    if (client_socket_ == invalidSocket()) {
        return;
    }

    size_t offset = 0;
    while (offset < length) {
        const auto socket = nativeSocket(client_socket_);

#ifdef _WIN32
        const int remaining = static_cast<int>(std::min<size_t>(length - offset, 16384));
        const int sent = send(socket, reinterpret_cast<const char*>(data + offset), remaining, 0);
        if (sent == SOCKET_ERROR) {
            closeClient();
            return;
        }
#else
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const ssize_t sent = send(socket, data + offset, length - offset, flags);
        if (sent < 0) {
            closeClient();
            return;
        }
#endif

        if (sent == 0) {
            closeClient();
            return;
        }

        offset += static_cast<size_t>(sent);
    }
}

void CrsfTcpPacketSender::close() {
    closeClient();
    closeListener();
    next_listen_attempt_ = Clock::now();
}

bool CrsfTcpPacketSender::isConnected() const {
    return client_socket_ != invalidSocket();
}

CrsfTcpPacketSender::SocketHandle CrsfTcpPacketSender::invalidSocket() {
#ifdef _WIN32
    return static_cast<SocketHandle>(INVALID_SOCKET);
#else
    return -1;
#endif
}

bool CrsfTcpPacketSender::startSockets() {
#ifdef _WIN32
    if (winsock_started_) {
        return true;
    }

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        scheduleReconnect();
        return false;
    }

    winsock_started_ = true;
#endif

    return true;
}

bool CrsfTcpPacketSender::ensureListening() {
    if (listen_socket_ != invalidSocket()) {
        return true;
    }

    if (Clock::now() < next_listen_attempt_) {
        return false;
    }

    return openListener();
}

bool CrsfTcpPacketSender::openListener() {
    if (!startSockets()) {
        return false;
    }

    const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == nativeSocket(invalidSocket())) {
        scheduleReconnect();
        return false;
    }

    SocketHandle new_socket = static_cast<SocketHandle>(socket);

#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) {
        closeNativeSocket(socket);
        scheduleReconnect();
        return false;
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0) {
        closeNativeSocket(socket);
        scheduleReconnect();
        return false;
    }
#endif

    int reuse_address = 1;
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse_address), sizeof(reuse_address));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1) {
        closeNativeSocket(socket);
        scheduleReconnect();
        return false;
    }

    if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(socket, 1) != 0) {
        closeNativeSocket(socket);
        scheduleReconnect();
        return false;
    }

    listen_socket_ = new_socket;
    return true;
}

void CrsfTcpPacketSender::acceptClient() {
    if (client_socket_ != invalidSocket() || listen_socket_ == invalidSocket()) {
        return;
    }

    sockaddr_in client_address{};
#ifdef _WIN32
    int address_length = sizeof(client_address);
#else
    socklen_t address_length = sizeof(client_address);
#endif

    const auto client = accept(
        nativeSocket(listen_socket_),
        reinterpret_cast<sockaddr*>(&client_address),
        &address_length
    );
    if (client == nativeSocket(invalidSocket())) {
        if (!isWouldBlock(lastSocketError())) {
            closeListener();
            scheduleReconnect();
        }
        return;
    }

    int no_delay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));

#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(client, FIONBIO, &nonblocking) != 0) {
        closeNativeSocket(client);
        return;
    }
#else
    const int flags = fcntl(client, F_GETFL, 0);
    if (flags < 0 || fcntl(client, F_SETFL, flags | O_NONBLOCK) != 0) {
        closeNativeSocket(client);
        return;
    }
#endif

    client_socket_ = static_cast<SocketHandle>(client);
}

void CrsfTcpPacketSender::closeClient() {
    if (client_socket_ == invalidSocket()) {
        return;
    }

    closeNativeSocket(nativeSocket(client_socket_));
    client_socket_ = invalidSocket();
}

void CrsfTcpPacketSender::closeListener() {
    if (listen_socket_ == invalidSocket()) {
        return;
    }

    closeNativeSocket(nativeSocket(listen_socket_));
    listen_socket_ = invalidSocket();
}

void CrsfTcpPacketSender::scheduleReconnect() {
    next_listen_attempt_ = Clock::now() + kReconnectDelay;
}
