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

struct SocketSetupOptions
{
	/** socket setup */
    int af = 0; 
    int type = 0; 
	int protocol = 0; 

	int send_timeout = 5000; // send timeout (ms)
	int recv_timeout = 5000; // recv timeout (ms)

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
	using socket_t = int;
    socket_t m_socket;
	SocketSetupOptions m_opts;
};

