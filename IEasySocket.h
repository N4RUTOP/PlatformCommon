/**
 *   Easy socket API
 *
 *   Created by lihuanqian on 12/14/2024
 *
 *   Copyright (c) lihuanqian. All rights reserved.
 */

#pragma once
#include <stdint.h>
#include <vector>
#include <optional>
#include <string>

class IEasySocket
{
public:
	virtual ~IEasySocket() {}

	virtual bool connect() = 0;
	virtual bool close() = 0;

	virtual std::optional<int> sendData(const std::vector<uint8_t>& data) = 0;
	virtual std::optional<std::vector<uint8_t>> recvData(int recvLen = 1024) = 0;
	
	virtual std::optional<int> sendMessage(const std::string& message) = 0;
	virtual std::optional<std::string> recvMessage(int recvLen = 1024) = 0;

	virtual std::optional<int> sendRaw(const char* byte, int len) = 0;
	virtual std::optional<int> recvRaw(char* byte, int len) = 0;
};