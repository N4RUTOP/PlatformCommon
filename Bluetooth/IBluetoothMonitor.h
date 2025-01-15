/** 蓝牙设备监听 */
#pragma once

#include <string>

enum BluetoothEventType
{
	BTH_ADD,
	BTH_REMOVE
};

struct BluetoothEvent
{
	BluetoothEventType event;
	std::string address;
	std::string name;

	bool operator==(const BluetoothEvent& d) const
	{
		return address == d.address && name == d.name;
	}
};

class IBluetoothMonitor
{
public:
	IBluetoothMonitor() {}
	virtual ~IBluetoothMonitor() {}
	
	using BluetoothEventCallback = void(*)(const BluetoothEvent*, void*);
	
	/**
	 * @brief 添加监听回调
	 */
	virtual void addBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) = 0;

	/**
	 * @brief 移除监听回调
	 */
	virtual void removeBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) = 0;
};

#ifdef _MSC_VER
class BluetoothMonitorForWin : public IBluetoothMonitor
{
public:
	void addBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) override;
	void removeBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) override;
};
#elif __APPLE__
class BluetoothMonitorForMac : public IBluetoothMonitor
{
public:
	void addBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) override;
	void removeBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) override;

};
#endif // WIN32

