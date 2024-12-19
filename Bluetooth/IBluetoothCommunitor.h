#pragma once
#include <string>
#include <vector>
#include <memory>

class IBluetoothCommunitor
{
public:
	IBluetoothCommunitor() {}
	virtual ~IBluetoothCommunitor() {}

	enum BluetoothError
	{
		/** 成功 */
		BTH_E_SUCCESS = 0,

		/** 未找到设备 */
		BTH_E_NO_DEVICE_FOUND,

		/** 设备不支持 */
		BTH_E_NO_SUPPORTED,

		/** 蓝牙未启动 */
		BTH_E_BLUETOOTH_CLOSED,

		/** 设备配对窗口弹出 */
		BTH_E_DEVICE_PAIR_AUTHENTICATION_PRESENTED,

		/** 设备配认证失败 */
		BTH_E_PAIR_AUTHENTICATION_FAILED,

		/** 用户取消配对 */
		BTH_E_USER_CANCEL_PAIR,

		/** 设备未连接 */
		BTH_E_SOCKET_DEVICE_NO_CONNECT,

		/** 套接字错误 */
		BTH_E_SOCKET_ERROR,

		/** 未知错误 */
		BTH_E_UNKNOWN_ERROR
	};

	virtual void setBluetoothAddress(const std::string& addr) = 0;

	virtual BluetoothError isPair(bool& pair) = 0;

	virtual BluetoothError pair() = 0;

	virtual BluetoothError connect() = 0;

	virtual BluetoothError send(const std::vector<uint8_t>& data) = 0;

	virtual BluetoothError recv(std::vector<uint8_t>& data) = 0;

	virtual BluetoothError disconnect() = 0;
};

#ifdef WIN32
class BluetoothCommunitorForWin : public IBluetoothCommunitor
{
public:
	BluetoothCommunitorForWin();

	void setBluetoothAddress(const std::string& addr) override;

	BluetoothError isPair(bool& pair) override;

	BluetoothError pair() override;

	BluetoothError connect() override;

	BluetoothError send(const std::vector<uint8_t>& data) override;

	BluetoothError recv(std::vector<uint8_t>& data) override;

	BluetoothError disconnect() override;

private:
	std::unique_ptr<struct BthCommPrivateData> m_d = nullptr;
};
#elif __APPLE__
class BluetoothCommunitorForMac : public IBluetoothCommunitor
{
public:
	void setBluetoothAddress(const std::string& addr) override;

    BluetoothError isPair(bool& pair) override;
    
	BluetoothError pair() override;

	BluetoothError connect() override;

	BluetoothError send(const std::vector<uint8_t>& data) override;

	BluetoothError recv(std::vector<uint8_t>& data) override;

	BluetoothError disconnect() override;
};
#endif // WIN32

