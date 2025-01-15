#include "IBluetoothMonitor.h"

#include <Windows.h>
#include <bluetoothapis.h>
#pragma comment(lib, "bthprops.lib")

#include <mutex>
#include <unordered_set>
#include "PlatformCommonUtils.h"
#include "CThread.hpp"

using namespace std;
using namespace PlatformCommonUtils;

using bth_evt_cb = IBluetoothMonitor::BluetoothEventCallback;
using bth_cb_unit = pair<bth_evt_cb, void*>;

static list<bth_cb_unit> g_evt_cbs;
mutex g_mutex;
list<BluetoothEvent> g_bth_devices;
std::atomic_bool g_check_stopped = true;

static CThread<void> g_thread;

#ifdef _MSC_VER
static string getMAC(BLUETOOTH_ADDRESS Daddress)
{
	ostringstream oss;
	oss << hex << setfill('0') << uppercase;
	for (int i = 5; i >= 0; --i) {
		oss << setw(2) << static_cast<int>(Daddress.rgBytes[i]);
		if (i > 0) {
			oss << ":";
		}
	}
	return oss.str();
}
#endif

static unordered_set<std::string> toSetFromList(const list<BluetoothEvent>& devices)
{
	unordered_set<std::string> res;
	for (auto& device : devices) {
		res.insert(device.address);
	}
	return res;
}

static void compareDeviceList(list<BluetoothEvent>& devices)
{
	auto ret = devices.size() <=> g_bth_devices.size();
	std::list<BluetoothEvent> changeDevices;
	if (ret == std::strong_ordering::less) {
		/** 有设备断开 */
		LOG_DEBUG("Has bluetooth device disconnect");
		auto set = toSetFromList(devices);
		for (auto& dev : g_bth_devices) {
			if (set.find(dev.address) == set.end()) {
				dev.event = BTH_REMOVE;
				g_mutex.lock();
				for (auto& cb : g_evt_cbs)
				{
					if (cb.first != nullptr) {
						cb.first(&dev, cb.second);
					}
				}
				g_mutex.unlock();
				changeDevices.push_back(dev);
			}
		}
		for (auto& dev : changeDevices) {
			g_bth_devices.remove(dev);
		}
	}
	else if (ret == std::strong_ordering::greater) {
		/** 有设备连接 */
		LOG_DEBUG("Has bluetooth device connect");
		auto set = toSetFromList(g_bth_devices);;
		for (auto& dev : devices) {
			if (set.find(dev.address) == set.end()) {
				//m_cb(ADRDEVICE_ADD, m_pUserData, snum.c_str());
				dev.event = BTH_ADD;
				g_mutex.lock();
				for (auto& cb : g_evt_cbs)
				{
					if (cb.first != nullptr) {
						cb.first(&dev, cb.second);
					}
				}
				g_mutex.unlock();
				changeDevices.push_back(dev);
			}
		}
		for (auto& dev : changeDevices) {
			g_bth_devices.push_back(dev);
		}
	}
}

static void loopCheckBluetoothDevice()
{
	while (!g_check_stopped)
	{
		HBLUETOOTH_RADIO_FIND hbf = nullptr;
		HANDLE hbr = nullptr;
		HBLUETOOTH_DEVICE_FIND hbdf = nullptr;
		BLUETOOTH_FIND_RADIO_PARAMS btfrp = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
		BLUETOOTH_RADIO_INFO bri = { sizeof(BLUETOOTH_RADIO_INFO) };
		BLUETOOTH_DEVICE_SEARCH_PARAMS btsp = { sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS) };
		BLUETOOTH_DEVICE_INFO btdi = { sizeof(BLUETOOTH_DEVICE_INFO) };
		hbf = BluetoothFindFirstRadio(&btfrp, &hbr);
		bool brfind = hbf != nullptr;

		while (brfind)
		{
			if (BluetoothGetRadioInfo(hbr, &bri) == ERROR_SUCCESS)
			{
				btsp.hRadio = hbr;
				btsp.fReturnAuthenticated = FALSE; // 是否搜索已配对的设备
				btsp.fReturnConnected = TRUE; // 是否搜索已连接的设备
				btsp.fReturnRemembered = FALSE; // 是否搜索已记忆的设备
				btsp.fReturnUnknown = FALSE; // 是否搜索未知设备
				btsp.fIssueInquiry = FALSE; // 是否重新搜索，True的时候会执行新的搜索，时间较长，FALSE的时候会直接返回上次的搜索结果。
				btsp.cTimeoutMultiplier = 1;
				hbdf = BluetoothFindFirstDevice(&btsp, &btdi);
				bool bfind = (hbdf != nullptr);
				list<BluetoothEvent> dev_list;
				while (bfind)
				{
					if (btdi.fAuthenticated) {
						dev_list.push_back(BluetoothEvent{ BTH_ADD, getMAC(btdi.Address), string(wchar_to_utf8(btdi.szName).get()) });
					}
					bfind = BluetoothFindNextDevice(hbdf, &btdi);
				}
				compareDeviceList(dev_list);
				if (hbdf) BluetoothFindDeviceClose(hbdf);
			}
			CloseHandle(hbr);
			brfind = BluetoothFindNextRadio(hbf, &hbr); // 查找下一个蓝牙发射器，如果有...
		}

		msleep(1000);
	}

}

void BluetoothMonitorForWin::addBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data)
{
	g_mutex.lock();
	g_evt_cbs.push_back(bth_cb_unit(cb, user_data));
	g_mutex.unlock();
	if (!g_thread.isRunning()) {
		g_check_stopped = false;
		g_thread.run(loopCheckBluetoothDevice);
	}
}

void BluetoothMonitorForWin::removeBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data)
{
	g_mutex.lock();
	g_evt_cbs.remove(bth_cb_unit(cb, user_data));
	if (g_evt_cbs.empty()) {
		g_mutex.unlock();
		g_check_stopped = true;
		g_thread.join();
		return;
	}
	g_mutex.unlock();
}