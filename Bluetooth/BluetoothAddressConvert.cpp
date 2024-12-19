#include "BluetoothAddressConvert.h"
#include <sstream>
#include <iomanip>

std::vector<uint8_t> BluetoothAddressConverter::mac2rgBytes(const std::string& mac)
{
	std::vector<uint8_t> res(6, 0);
	unsigned int bytes[6] = { 0 };
	std::stringstream ss(mac);
	for (int i = 0; i < 6; ++i) {
		std::string byteStr;
		std::getline(ss, byteStr, ':');
		std::istringstream(byteStr) >> std::hex >> bytes[i];
	}

	for (int i = 0; i < 6; ++i) {
		res[5 - i] = static_cast<uint8_t>(bytes[i]); 
	}

	return res;
}

std::string BluetoothAddressConverter::rgByte2Mac(const uint8_t* rgByte)
{
	std::ostringstream oss;
	oss << std::hex << std::setfill('0') << std::uppercase;
	for (int i = 5; i >= 0; --i) {
		oss << std::setw(2) << static_cast<int>(rgByte[i]);
		if (i > 0) {
			oss << ":";
		}
	}
	return oss.str();
}

uint64_t BluetoothAddressConverter::mac2ull(std::string mac)
{
	uint64_t result = 0;
	unsigned int byteValue = 0;

	std::stringstream ss(mac);
	for (int i = 5; i >= 0; --i) { // 倒序解析并存储
		std::string byteStr;
		std::getline(ss, byteStr, ':'); // 按冒号分割
		std::istringstream(byteStr) >> std::hex >> byteValue;

		// 按小端序，将解析的值放到正确位置
		result |= static_cast<uint64_t>(byteValue) << (i * 8);
	}

	return result;
}
