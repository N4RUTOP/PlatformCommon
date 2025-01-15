#pragma once

#include <list>
#include <memory>
#include "IBluetoothCommunitor.h"
#include "IBluetoothMonitor.h"

class BluetootDevicehManager : public IBluetoothCommunitor, public IBluetoothMonitor
{
public:
	BluetootDevicehManager();

	static bool isBluetoothSupported();
	static bool isBluetoothOpened();
	
	/**
	 * @brief 添加/移除 蓝牙设备监听回调
	 */
	void addBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) override;
	void removeBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data) override;

	/**
	 * @brief 设置蓝牙地址
	 */
	void setBluetoothAddress(const std::string& addr) override;

	/**
	 * @brief  检测设备是否配对    
	 */
	BluetoothError isPair(bool& pair) override;

	/**
	 * @brief 与设备进行配对
	 */
	BluetoothError pair() override;

	/**
	 * @brief 与蓝牙设备建立连接
	 */
	BluetoothError connect() override;

	/**
     * @brief 发送数据
     */
	BluetoothError send(const std::vector<uint8_t>& data) override;

	/**
     * @brief 接收数据
     */
	BluetoothError recv(std::vector<uint8_t>& data) override;

	/**
     * @brief 断开连接
     */
	BluetoothError disconnect() override;

private:
#ifdef _MSC_VER
	std::unique_ptr<IBluetoothCommunitor> m_bthCommt = std::make_unique<BluetoothCommunitorForWin>();
	std::unique_ptr<IBluetoothMonitor> m_bthMonitor = std::make_unique<BluetoothMonitorForWin>();
#else
	std::unique_ptr<IBluetoothCommunitor> m_bthCommt = std::make_unique<BluetoothCommunitorForMac>();
	std::unique_ptr<IBluetoothMonitor> m_bthMonitor = std::make_unique<BluetoothMonitorForMac>();
#endif
};