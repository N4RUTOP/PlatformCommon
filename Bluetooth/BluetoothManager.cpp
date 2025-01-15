#include "BluetoothManager.h"

#ifdef _MSC_VER
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>  
#include <bluetoothapis.h>
#pragma comment(lib, "SetupAPI.lib")
#pragma comment(lib, "Bthprops.lib")

#define CONFIGFLAG_DISABLED 0x00000001

#endif // WIN32



using BluetoothError = IBluetoothCommunitor::BluetoothError;

BluetootDevicehManager::BluetootDevicehManager()
{

}

bool BluetootDevicehManager::isBluetoothSupported()
{
#ifdef _MSC_VER
	HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_BLUETOOTH, nullptr, nullptr, DIGCF_PRESENT);
	if (hDevInfo == INVALID_HANDLE_VALUE) {
		return false;
	}

	SP_DEVINFO_DATA devInfoData = {};
	devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	bool isAvailable = SetupDiEnumDeviceInfo(hDevInfo, 0, &devInfoData);
	SetupDiDestroyDeviceInfoList(hDevInfo);
	return isAvailable;
#endif // WIN32

	return false;
}

bool BluetootDevicehManager::isBluetoothOpened()
{
#ifdef _MSC_VER
	HBLUETOOTH_RADIO_FIND hFind = nullptr;
	BLUETOOTH_FIND_RADIO_PARAMS findParams = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
	HANDLE hRadio = nullptr;

	// 尝试查找蓝牙设备
	hFind = BluetoothFindFirstRadio(&findParams, &hRadio);
	if (hFind) {
		// 找到蓝牙设备
		CloseHandle(hRadio);
		BluetoothFindRadioClose(hFind);
		return true;
	}
	return false;
#endif

	return false;
}

static BluetoothError checkBluetoothAvailable() 
{
	if (!BluetootDevicehManager::isBluetoothSupported()) {
		return BluetoothError::BTH_E_NO_SUPPORTED;
	}

	if (!BluetootDevicehManager::isBluetoothOpened()) {
		return BluetoothError::BTH_E_BLUETOOTH_CLOSED;
	}

	return BluetoothError::BTH_E_SUCCESS;
}

void BluetootDevicehManager::addBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data)
{
	m_bthMonitor->addBluetoothEventSubscriber(cb, user_data);
}

void BluetootDevicehManager::removeBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data)
{
	m_bthMonitor->removeBluetoothEventSubscriber(cb, user_data);
}

void BluetootDevicehManager::setBluetoothAddress(const std::string& addr)
{
	m_bthCommt->setBluetoothAddress(addr);
}

BluetoothError BluetootDevicehManager::isPair(bool& pair)
{
	auto res = checkBluetoothAvailable();
	if (res != BTH_E_SUCCESS) return res;
	return m_bthCommt->isPair(pair);
}

BluetoothError BluetootDevicehManager::pair()
{
	auto res = checkBluetoothAvailable();
	if (res != BTH_E_SUCCESS) return res;
	return m_bthCommt->pair();
}

BluetoothError BluetootDevicehManager::connect()
{
	return m_bthCommt->connect();
}

BluetoothError BluetootDevicehManager::send(const std::vector<uint8_t>& data)
{
	return m_bthCommt->send(data);
}

BluetoothError BluetootDevicehManager::recv(std::vector<uint8_t>& data)
{
	return m_bthCommt->recv(data);
}

BluetoothError BluetootDevicehManager::disconnect()
{
	return m_bthCommt->disconnect();
}
