#include "IBluetoothCommunitor.h"

#include <Windows.h>
#include <bluetoothapis.h>
#include <winsock.h>
#include <ws2bth.h>
#pragma comment(lib, "wsock32.lib")
#pragma comment(lib, "bthprops.lib")
#pragma comment(lib, "ws2_32.lib")

#include "PlatformCommonUtils.h"
#include "CThread.hpp"
#include "BluetoothAddressConvert.h"

using namespace PlatformCommonUtils;
using namespace std;

using BluetoothError = IBluetoothCommunitor::BluetoothError;

#define PAIR_STATE_NONE                    -1
#define PAIR_STATE_WAIT_TARGET_DEVICE      -2

#pragma warning(disable : 4995)

struct BthCommPrivateData {
	SOCKET socket = INVALID_SOCKET;
	std::string macAddress;
	CThread<void> thread;
	std::atomic_int current_pair_state;
	BLUETOOTH_DEVICE_INFO current_device;
};

static string getMAC(BLUETOOTH_ADDRESS Daddress)
{
	return BluetoothAddressConverter::rgByte2Mac(Daddress.rgBytes);
}

template <typename Func, typename... Args>
static void scanDevice(BLUETOOTH_DEVICE_SEARCH_PARAMS& btsp, Func&& func, Args&&... args)
{
	HBLUETOOTH_RADIO_FIND hbf = NULL;
	HANDLE hbr = NULL;
	HBLUETOOTH_DEVICE_FIND hbdf = NULL;
	BLUETOOTH_FIND_RADIO_PARAMS btfrp = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) }; 
	BLUETOOTH_RADIO_INFO bri = { sizeof(BLUETOOTH_RADIO_INFO) }; 
	BLUETOOTH_DEVICE_INFO btdi = { sizeof(BLUETOOTH_DEVICE_INFO) };  
	hbf = BluetoothFindFirstRadio(&btfrp, &hbr); 

	bool brfind = hbf != NULL;
	while (brfind)
	{
		if (BluetoothGetRadioInfo(hbr, &bri) == ERROR_SUCCESS)
		{
			btsp.hRadio = hbr; 
			hbdf = BluetoothFindFirstDevice(&btsp, &btdi);
			bool bfind = hbdf != NULL;
			int i = 0;

			while (bfind)
			{
				if (func(btdi, std::forward<Args>(args)...)) {
					BluetoothFindDeviceClose(hbdf);
					CloseHandle(hbr);
					return;
				}
				bfind = BluetoothFindNextDevice(hbdf, &btdi);
			}
			if (hbdf)
				BluetoothFindDeviceClose(hbdf);
		}
		CloseHandle(hbr);
		brfind = BluetoothFindNextRadio(hbf, &hbr);
	}
}

static DWORD CALLBACK BluetoothAuthCallback(
	LPVOID pvParam,
	PBLUETOOTH_AUTHENTICATION_CALLBACK_PARAMS pAuthCallbackParams
)
{
	BthCommPrivateData* d = (BthCommPrivateData*)pvParam;

	// 自动确认配对请求
	BLUETOOTH_AUTHENTICATE_RESPONSE authResponse;
	ZeroMemory(&authResponse, sizeof(authResponse));
	authResponse.authMethod = pAuthCallbackParams->authenticationMethod;
	authResponse.bthAddressRemote = pAuthCallbackParams->deviceInfo.Address;
	authResponse.negativeResponse = FALSE;

	switch (pAuthCallbackParams->authenticationMethod)
	{
	case BLUETOOTH_AUTHENTICATION_METHOD_LEGACY:
		// 对于传统配对，设置 PIN 码
		ZeroMemory(authResponse.pinInfo.pin, sizeof(authResponse.pinInfo.pin));
		printf("BLUETOOTH_AUTHENTICATION_METHOD_LEGACY\n");
		authResponse.pinInfo.pinLength = 4;
		break;

	case BLUETOOTH_AUTHENTICATION_METHOD_OOB:
		break;

	case BLUETOOTH_AUTHENTICATION_METHOD_NUMERIC_COMPARISON:
		d->current_pair_state = PAIR_STATE_WAIT_TARGET_DEVICE;
		// 对于数值比较，自动确认
		break;
	case BLUETOOTH_AUTHENTICATION_METHOD_PASSKEY_NOTIFICATION:
		// 对于 Passkey Notification，不需要处理
		break;
	case BLUETOOTH_AUTHENTICATION_METHOD_PASSKEY:
		// 对于 Passkey 输入，提供 passkey
		authResponse.passkeyInfo.passkey = 123456; // 示例 passkey
		break;
	default:
		break;
	}

	DWORD res = BluetoothSendAuthenticationResponseEx(NULL, &authResponse);
	if (res != ERROR_SUCCESS) {
		LOG_INFO("BluetoothSendAuthenticationResponseEx ERROR %d", res);
	}
	return ERROR_SUCCESS;
}

static void _pairDevice(BthCommPrivateData* data)
{
	BLUETOOTH_DEVICE_INFO dev = data->current_device;
	if (!dev.fAuthenticated) {
		HBLUETOOTH_AUTHENTICATION_REGISTRATION hRegHandle;
		DWORD result = BluetoothRegisterForAuthenticationEx(
			&dev,
			&hRegHandle,
			(PFN_AUTHENTICATION_CALLBACK_EX)&BluetoothAuthCallback,
			data
		);
		if (result != ERROR_SUCCESS) {
			data->current_pair_state = result;
			return;
		}
		wstring passkey = L"0000";
		result = BluetoothAuthenticateDevice(nullptr, nullptr, &dev, (PWSTR)passkey.c_str(), passkey.size());
		if (result == ERROR_SUCCESS) {
			 BluetoothUpdateDeviceRecord(&dev);
		}
		data->current_pair_state = result;
	}
	else {
		data->current_pair_state = ERROR_SUCCESS;
	}
}

static BluetoothError mapBluetoothError(int nerror) 
{
	BluetoothError error = IBluetoothCommunitor::BTH_E_PAIR_AUTHENTICATION_FAILED;
	switch (nerror)
	{
	case PAIR_STATE_WAIT_TARGET_DEVICE:
		error = IBluetoothCommunitor::BTH_E_DEVICE_PAIR_AUTHENTICATION_PRESENTED;
		break;

	case ERROR_SUCCESS:
		error = IBluetoothCommunitor::BTH_E_SUCCESS;
		break;

	case ERROR_CANCELLED:
		error = IBluetoothCommunitor::BTH_E_USER_CANCEL_PAIR;
		LOG_ERROR("[Bluetooth]: User cancelled");
		break;

	case WAIT_TIMEOUT:
		error = IBluetoothCommunitor::BTH_E_NO_DEVICE_FOUND;
		break;

	case ERROR_NOT_SUPPORTED:
		error = IBluetoothCommunitor::BTH_E_NO_SUPPORTED;
		LOG_ERROR("[Bluetooth]: Not support");
		break;

	case ERROR_INVALID_PARAMETER:
		LOG_ERROR("[Bluetooth]: Invalid parameter");
		break;

	case ERROR_NO_MORE_ITEMS:
		LOG_ERROR("[Bluetooth]: No more items");
		break;

	case ERROR_GEN_FAILURE:
		LOG_ERROR("[Bluetooth]: Gen Failed");
		break;

	case ERROR_BUSY:
		LOG_ERROR("[Bluetooth]: Busy");
		break;

	case ERROR_TIMEOUT:
		LOG_ERROR("[Bluetooth]: Timeout");
		break;

	case ERROR_DEVICE_NOT_CONNECTED:
		LOG_ERROR("[Bluetooth]: Not connected");
		break;

	case ERROR_DEVICE_NOT_AVAILABLE:
		LOG_ERROR("[Bluetooth]: Device not avaliable");
		break;

	default:
		LOG_ERROR("[Bluetooth]: Other error %d", nerror);
		break;
	}
	return error;
}

BluetoothCommunitorForWin::BluetoothCommunitorForWin() 
{
	m_d = make_unique<BthCommPrivateData>();
	ZeroMemory(&m_d->current_device, sizeof(BLUETOOTH_DEVICE_INFO));
	m_d->current_device.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
}

void BluetoothCommunitorForWin::setBluetoothAddress(const std::string& addr)
{
	if (compare_string_insensitive(addr, m_d->macAddress)) {
		return;
	}
	ZeroMemory(&m_d->current_device, sizeof(BLUETOOTH_DEVICE_INFO));
	m_d->current_device.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
	m_d->current_device.Address.ullLong = BluetoothAddressConverter::mac2ull(addr);
	m_d->macAddress = addr;
}

BluetoothError BluetoothCommunitorForWin::isPair(bool& pair)
{
	BLUETOOTH_DEVICE_SEARCH_PARAMS btsp = { sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS) };
	btsp.fReturnAuthenticated = TRUE;
	btsp.fReturnConnected = FALSE;
	btsp.fReturnRemembered = FALSE;
	btsp.fReturnUnknown = FALSE;
	btsp.fIssueInquiry = FALSE;
	btsp.cTimeoutMultiplier = 0;
	auto checkPaired = 
		[](BLUETOOTH_DEVICE_INFO& info, BLUETOOTH_DEVICE_INFO& curdev) -> bool {
			if (info.Address.ullLong == curdev.Address.ullLong) {
				curdev = info;
				return true;
			}
			return false;
		};

	scanDevice(btsp, checkPaired, m_d->current_device);
	pair = m_d->current_device.fAuthenticated;
	return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForWin::pair()
{
	if (m_d->current_device.Address.ullLong == 0) {
		return BTH_E_NO_DEVICE_FOUND;
	}
	
	BluetoothError res;
	bool paired = false;
	res = isPair(paired);
	if (res != BTH_E_SUCCESS) {
		return res;
	}
	else if (paired) {
		return BTH_E_SUCCESS;
	}

	if (m_d->thread.isRunning()) {
		while (m_d->current_pair_state == PAIR_STATE_NONE) { usleep(100); }
		return mapBluetoothError(m_d->current_pair_state);
	}
	// 在线程内执行配对，实现异步
	m_d->current_pair_state = PAIR_STATE_NONE;
	if (!m_d->thread.run(_pairDevice, m_d.get())) {
		return BTH_E_UNKNOWN_ERROR;
	}
	while (m_d->current_pair_state == PAIR_STATE_NONE) { usleep(100); }
	return mapBluetoothError(m_d->current_pair_state);
}
	
BluetoothError BluetoothCommunitorForWin::connect()
{
	if (m_d->current_device.Address.ullLong == 0) {
		return BTH_E_NO_DEVICE_FOUND;
	}

	disconnect();
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		return BTH_E_SOCKET_ERROR;
	}
	m_d->socket = socket(AF_INET, SOCK_STREAM, BTHPROTO_RFCOMM);
	if (m_d->socket == INVALID_SOCKET) {
		WSACleanup();
		return BTH_E_SOCKET_ERROR;
	}

	SOCKADDR_BTH serverAddress;
	ZeroMemory(&serverAddress, sizeof(serverAddress));
	serverAddress.addressFamily = AF_BTH;
	BTH_ADDR deviceAddress = m_d->current_device.Address.ullLong;
	serverAddress.btAddr = deviceAddress;
	serverAddress.port = 0;
	serverAddress.serviceClassId = SerialPortServiceClass_UUID;

	int timeout = 3000; // 3s
	setsockopt(m_d->socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
	setsockopt(m_d->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	int err = ::connect(m_d->socket, (SOCKADDR*)&serverAddress, sizeof(serverAddress));
	if (0 == err) {
		return BTH_E_SUCCESS;
	}
	LOG_ERROR("Socket connect failed %d", err);
	disconnect();
	return BTH_E_SOCKET_ERROR;
}

BluetoothError BluetoothCommunitorForWin::send(const std::vector<uint8_t>& data)
{
	if (m_d->socket == INVALID_SOCKET) {
		return BTH_E_SOCKET_DEVICE_NO_CONNECT;
	}
	size_t sendSize = 0;
	const char* _data = (const char*)data.data();
	while (sendSize < data.size())
	{
		int res = ::send(m_d->socket, _data + sendSize, data.size() - sendSize, 0);
		if (res == SOCKET_ERROR)
		{
			LOG_ERROR("Socket send failed %d", res);
			return BTH_E_SOCKET_ERROR;
		}
		sendSize += res;
	}
	return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForWin::recv(std::vector<uint8_t>& data)
{
	if (m_d->socket == INVALID_SOCKET) {
		return BTH_E_SOCKET_DEVICE_NO_CONNECT;
	}
	constexpr size_t recvBuffSize = 1024 * 4;
	data.resize(recvBuffSize);  
	int res = ::recv(m_d->socket, reinterpret_cast<char*>(data.data()), recvBuffSize, 0);
	if (res == SOCKET_ERROR) {
		LOG_ERROR("Socket send failed %d", res);
		return BTH_E_SOCKET_ERROR;
	}
	data.resize(res);
	return BTH_E_SUCCESS;
}

BluetoothError BluetoothCommunitorForWin::disconnect()
{
	if (m_d->socket != INVALID_SOCKET)
	{
		closesocket(m_d->socket);
		WSACleanup();
		BluetoothUpdateDeviceRecord(&m_d->current_device);
		m_d->socket = INVALID_SOCKET;
	}
	return BTH_E_SUCCESS;
}