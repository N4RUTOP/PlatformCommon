#include "PlatformEasySocket.h"
#include <mutex>
#ifdef _MSC_VER
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define INVALID_SOCKET_VALUE  0

#ifndef SOCKET_ERROR
#define SOCKET_ERROR  -1
#endif

#ifdef _MSC_VER
static std::once_flag s_wsaInitFlag;
class InitializeWSAMgr
{
public:
    InitializeWSAMgr() {
        WSADATA wsaData;
        if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            // LOG
        }
    }
    ~InitializeWSAMgr() {
        ::WSACleanup();
    }
};

// Initialize Winsock
static void initializeWSA()
{
    static InitializeWSAMgr manager;
}
#endif

PlatformEasySocket::PlatformEasySocket():
    m_socket(INVALID_SOCKET_VALUE)
{
#ifdef _MSC_VER
    std::call_once(s_wsaInitFlag, initializeWSA);
#endif
}

PlatformEasySocket::PlatformEasySocket(const SocketSetupOptions& opts):
    m_socket(INVALID_SOCKET_VALUE)
{
#ifdef _MSC_VER
    std::call_once(s_wsaInitFlag, initializeWSA);
#endif
    setupSocket(opts);
}

PlatformEasySocket::~PlatformEasySocket()
{
    close();
}

bool PlatformEasySocket::setupSocket(const SocketSetupOptions& opts)
{
    if (m_opts == opts && m_socket != INVALID_SOCKET_VALUE) {
        return true;
    }

    m_opts = opts;
    if (m_socket != INVALID_SOCKET_VALUE) {
        close();
    }

    m_socket = (socket_t)socket(m_opts.af, m_opts.type, m_opts.protocol);
    if (m_socket == INVALID_SOCKET_VALUE) {
        return false;
    }

    if (m_opts.recv_timeout != 0) {
#ifdef _MSC_VER
        if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&m_opts.recv_timeout, sizeof(m_opts.recv_timeout)) == SOCKET_ERROR) {
            close();
            return false;
        }
#else
        struct timeval tv;
        tv.tv_sec = m_opts.recv_timeout / 1000;
        tv.tv_usec = (m_opts.recv_timeout % 1000) * 1000;
        if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) == SOCKET_ERROR) {
            close();
            return false;
        }
#endif

    }
    if (m_opts.send_timeout != 0) {
#ifdef _MSC_VER
        if (setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&m_opts.send_timeout, sizeof(m_opts.send_timeout)) == SOCKET_ERROR) {
            close();
            return false;
        }
#else
        struct timeval tv;
        tv.tv_sec = m_opts.recv_timeout / 1000;
        tv.tv_usec = (m_opts.recv_timeout % 1000) * 1000;
        if (setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv) == SOCKET_ERROR) {
            close();
            return false;
        }
#endif
    }

    return true;
}

SocketSetupOptions PlatformEasySocket::getCurrentSocketOptions()
{
    return m_opts;
}

bool PlatformEasySocket::connect()
{
    if (m_socket == INVALID_SOCKET_VALUE) {
        return false;
    }

    if (m_opts.af == AF_INET) {
        struct sockaddr_in serverAddr = {};
        serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, m_opts.ip.c_str(), &serverAddr.sin_addr);
        serverAddr.sin_port = htons(m_opts.port);

        if (::connect(m_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            return false;
        }
    }
    else if (m_opts.af == AF_INET6) {
        sockaddr_in6 serverAddr = {};
        serverAddr.sin6_family = AF_INET6;
        inet_pton(AF_INET6, m_opts.ip.c_str(), &serverAddr.sin6_addr);
        serverAddr.sin6_port = htons(m_opts.port);

        if (::connect(m_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            return false;
        }
    }
    else {
        return false;
    }

    return true;
}

bool PlatformEasySocket::close()
{
    if (m_socket == INVALID_SOCKET_VALUE) {
        return false;
    }
#ifdef _MSC_VER
    ::closesocket(m_socket);
#else
    ::close(m_socket);
#endif
    m_socket = INVALID_SOCKET_VALUE;
    return true;
}

bool PlatformEasySocket::shutdown()
{
    if (m_socket == INVALID_SOCKET_VALUE) {
        return false;
    }
#ifdef _MSC_VER
    ::shutdown(m_socket, SD_SEND);
#else
    ::shutdown(m_socket, SHUT_WR);
#endif
    return true;
}

std::optional<int> PlatformEasySocket::sendData(const std::vector<uint8_t>& sendData)
{
    if (m_socket == INVALID_SOCKET_VALUE) {
        return std::nullopt;
    }

    size_t totalSent = 0; 
    size_t dataSize = sendData.size();

    while (totalSent < dataSize) {
        size_t sent = ::send(m_socket,
            reinterpret_cast<const char*>(sendData.data() + totalSent),
            static_cast<int>(dataSize - totalSent),
            0);

        if (sent == SOCKET_ERROR) {
            return std::nullopt; 
        }

        totalSent += sent;
    }
    return static_cast<int>(totalSent); 
}

std::optional<std::vector<uint8_t>> PlatformEasySocket::recvData(int recvLen)
{
    std::vector<uint8_t> res(recvLen);

    int received = (int)::recv(m_socket, reinterpret_cast<char*>(res.data()), recvLen, 0);
    if (received == SOCKET_ERROR) {
        printf("ERROR: %s\n", getErrorString().c_str());
        return std::nullopt;
    }
    res.resize(received);
    return res;
}

std::optional<int> PlatformEasySocket::sendMessage(const std::string& message)
{
    if (m_socket == INVALID_SOCKET_VALUE) {
        return std::nullopt;
    }

    size_t totalSent = 0;
    size_t dataSize = message.size();

    while (totalSent < dataSize) {
        size_t sent = ::send(m_socket,
            reinterpret_cast<const char*>(message.data() + totalSent),
            static_cast<int>(dataSize - totalSent),
            0);

        if (sent == SOCKET_ERROR) {
            return std::nullopt;
        }

        totalSent += sent;
    }
    return static_cast<int>(totalSent);
}

std::optional<std::string> PlatformEasySocket::recvMessage(int recvLen)
{
    std::string res;
    res.resize(recvLen);
    int received = (int)::recv(m_socket, reinterpret_cast<char*>(res.data()), recvLen, 0);
    if (received == SOCKET_ERROR) {
        return std::nullopt;
    }
    res.resize(received);
    return res;
}

std::optional<int> PlatformEasySocket::sendRaw(const char* byte, int len)
{
    if (m_socket == INVALID_SOCKET_VALUE) {
        return std::nullopt;
    }
    int res = (int)::send(m_socket, byte, len, 0);
    if (res != SOCKET_ERROR) {
        return res;
    }
    return std::nullopt;
}

std::optional<int> PlatformEasySocket::recvRaw(char* byte, int len)
{
    int recvSize = (int)::recv(m_socket, byte, len, 0);
    if (recvSize > 0) {
        return recvSize;
    }
    return std::nullopt;
}

int PlatformEasySocket::error() const
{
#ifdef _MSC_VER
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

std::string PlatformEasySocket::getErrorString() const
{
#ifdef _MSC_VER
    char* errMsg = nullptr;
    ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr,
        error(),
        0,
        (LPSTR)&errMsg,
        0,
        nullptr);
    std::string errStr = errMsg ? errMsg : "Unknown error";
    ::LocalFree(errMsg);
    return errStr;
#else
    return std::string(strerror(errno));
#endif
}
