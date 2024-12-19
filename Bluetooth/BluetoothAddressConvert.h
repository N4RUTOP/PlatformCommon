#pragma once

#include <string>
#include <vector>

class BluetoothAddressConverter
{
public:
	 /**
	  * @brief      mac地址转rgbyte
	  * @param  mac  蓝牙mac地址
	  * @return     rgbyte
	  */
	 static std::vector<uint8_t> mac2rgBytes(const std::string& mac);
	 
	 /**
	  * @brief         rgByte转mac地址
	  * @param  rgByte  rgByte
	  * @return        mac地址
	  * @note          rgByte 6个字节，该函数不做边界检测！
	  */
	 static std::string rgByte2Mac(const uint8_t* rgByte); 

	 /**
	  * @brief     mac地址转ull
	  * @param mac  蓝牙mac地址
	  */
	 static uint64_t mac2ull(std::string mac);

};
