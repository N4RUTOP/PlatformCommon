/**
 *   PlatformEasySocket for Windows/MacOS
 *
 *   Created by lihuanqian on 12/14/2024
 * 
 *   Copyright (c) lihuanqian. All rights reserved.
 */
#pragma once

#include "IEasySocket.h"
#include <string>

#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif // WIN32

struct SocketSetupOptions
{
	/** socket setup */
    int af = AF_INET; // default AF_INET
    int type = SOCK_STREAM; // default SOCK_STREAM
    int protocol =
#ifdef WIN32
    // default IPPROTO_TCP
	IPPROTO_TCP;
#else
    // The protocol specifies a particular protocol to be used with the
    // socket.  Normally only a single protocol exists to support a
    // particular socket type within a given protocol family, in which
    // case protocol can be specified as 0.
    0;
#endif

	int send_timeout = 5000; // send timeout (ms)
	int recv_timeout = 5000; // send timeout (ms)

	int port = 0; // port
	std::string ip; // ip

	SocketSetupOptions() {}
	SocketSetupOptions(const SocketSetupOptions& opts) = default;
	bool operator==(const SocketSetupOptions& opts) const = default;
	bool operator!=(const SocketSetupOptions& opts) const = default;
};

class PlatformEasySocket : public IEasySocket
{
public:
	explicit PlatformEasySocket();
	explicit PlatformEasySocket(const SocketSetupOptions& opts);
	~PlatformEasySocket();

	bool setupSocket(const SocketSetupOptions& opts);
	SocketSetupOptions getCurrentSocketOptions();

	bool connect() override;
	bool close() override;
	bool shutdown();

	std::optional<int> sendData(const std::vector<uint8_t>& sendData) override;
	std::optional<std::vector<uint8_t>> recvData(int recvLen = 1024) override;

	std::optional<int> sendMessage(const std::string& message) override;
	std::optional<std::string> recvMessage(int recvLen = 1024) override;

	std::optional<int> sendRaw(const char* byte, int len) override;
	std::optional<int> recvRaw(char* byte, int len) override;

	int error() const;
	std::string getErrorString() const;

private:
#ifdef WIN32
using socket_t = SOCKET;
#else
using socket_t = int;
#endif // WIN32
    
    socket_t m_socket;
	SocketSetupOptions m_opts;

};

