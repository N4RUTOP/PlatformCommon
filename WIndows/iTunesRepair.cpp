#include "iTunesRepair.h"
#include <string>
#include <Windows.h>
#include <SetupAPI.h>
#include <ShlObj.h>
#include <Cfgmgr32.h>
#include <tchar.h>
#include <functional>
#include <thread>
#include <atomic>

#include "PlatformCommonUtils.h"
#include "CThread.hpp"

#pragma comment(lib, "SetupAPI.lib")
#pragma comment(lib, "Version.lib")

#define WIN_INF_PATH            "C:\\Windows\\INF"
#define WIN_FILEREPOSITORY_PATH "C:\\Windows\\System32\\DriverStore\\FileRepository"

#define AMDS_NAME         "Apple Mobile Device Service"
#define AMDS_PORT         "27015"
#define BONJOUR_SERVICE   "Bonjour Service"

#ifdef WIN32
#define DIR_SEP '\\'
#define DIR_SEP_S "\\"
#else
#define DIR_SEP '/'
#define DIR_SEP_S "/"
#endif

#ifdef WIN32
#define USERPREF_CONFIG_DIR "Apple" DIR_SEP_S "Lockdown"
#else
#define USERPREF_CONFIG_DIR "lockdown"
#endif

using namespace PlatformCommonUtils;
using namespace std;

using cb_handle_apple_device_t = bool(*)(HDEVINFO, PSP_DEVINFO_DATA, itunesrepair_error_t&);

static unique_ptr<CThread<void>> g_thread;

std::condition_variable cv;


struct ITUNES_RESOURCE
{
	string usbaapl_inf_path;
	string usbaapl64_inf_path;
	string appleusb_inf_path;
	string applekis_inf_path;
	string applersm_inf_path;
	string netaapl_inf_path;
	string driver_installer_path;
	string apple_device_process_path;
	string tool_7z_path;

} g_itunes_resource;

struct ITUNES_SETUP_RESOURCE
{
	string apple_mobile_device_support;
	string apple_software_update;
	string bonjour;
} g_itunes_setup_resource;

static bool g_resource_loaded = false;
static bool g_setup_resource_loaded = false;

static cb_apple_process_exception_happened g_user_handle_exception = nullptr;
static void* g_user_data = nullptr;
static std::atomic_bool g_check_process_stopped = true;
std::condition_variable g_cv;
std::mutex g_cv_m;

/**
 * @brief 服务是否存在
 */
static bool service_exisit(const string& service_name)
{
	// 打开服务控制管理器
	SC_HANDLE serviceControlManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!serviceControlManager) {
		return false;
	}

	// 打开指定的服务
	SC_HANDLE serviceHandle = OpenServiceA(serviceControlManager, service_name.c_str(), SERVICE_QUERY_STATUS);
	if (!serviceHandle) {
		CloseServiceHandle(serviceControlManager);
		return false;
	}

	CloseServiceHandle(serviceHandle);
	CloseServiceHandle(serviceControlManager);
	return true;
}

/**
 * @brief 服务是否可用
 */
static bool service_enable(const string& service_name) {
	// 打开服务控制管理器
	SC_HANDLE serviceControlManager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (!serviceControlManager) {
		return false;
	}

	// 打开指定的服务
	SC_HANDLE serviceHandle = OpenServiceA(serviceControlManager, service_name.c_str(), SERVICE_QUERY_CONFIG);
	if (!serviceHandle) {
		CloseServiceHandle(serviceControlManager);
		return false;
	}

	// 查询服务配置信息
	QUERY_SERVICE_CONFIGA* serviceConfig = nullptr;
	DWORD bytesNeeded = 0;
	BOOL res = QueryServiceConfigA(serviceHandle, nullptr, 0, &bytesNeeded);
	if (!res && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
		CloseServiceHandle(serviceHandle);
		CloseServiceHandle(serviceControlManager);
		return false;
	}

	serviceConfig = (QUERY_SERVICE_CONFIGA*)LocalAlloc(LPTR, bytesNeeded);
	if (!QueryServiceConfigA(serviceHandle, serviceConfig, bytesNeeded, &bytesNeeded)) {
		LocalFree(serviceConfig);
		CloseServiceHandle(serviceHandle);
		CloseServiceHandle(serviceControlManager);
		return false;
	}

	// 检查启动类型是否为禁用
	bool isDisabled = (serviceConfig->dwStartType == SERVICE_DISABLED);

	// 清理资源
	LocalFree(serviceConfig);
	CloseServiceHandle(serviceHandle);
	CloseServiceHandle(serviceControlManager);

	return !isDisabled;
}

static itunesrepair_error_t switch_service(const string& service_name, bool on) {
	SC_HANDLE serviceControlManager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (!serviceControlManager) {
		return ITUNESREPAIR_E_UNKNOWN_ERROR;
	}
	// 打开指定的服务
	SC_HANDLE serviceHandle = OpenServiceA(serviceControlManager, service_name.c_str(), on ? SERVICE_START : SERVICE_STOP);
	if (!serviceHandle) {
		CloseServiceHandle(serviceControlManager);
		return ITUNESREPAIR_E_NO_SERVICE_FOUND;
	}
	bool res = false;
	if (on) {
		res = StartServiceA(serviceHandle, 0, nullptr);
	}
	else {
		SERVICE_STATUS serviceStatus;
		res = ControlService(serviceHandle, SERVICE_CONTROL_STOP, &serviceStatus);
	}
	/*E = GetLastError();*/
	CloseServiceHandle(serviceHandle);
	CloseServiceHandle(serviceControlManager);
	return res ? ITUNESREPAIR_E_SUCCESS : ITUNESREPAIR_E_UNKNOWN_ERROR;
}

static itunesrepair_error_t change_service_type(const string& service_name, DWORD type) {
	// 打开服务控制管理器
	SC_HANDLE hSCManager = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (hSCManager == nullptr) {
		LOG_INFO("OpenSCManager error: %d", GetLastError());
		return ITUNESREPAIR_E_UNKNOWN_ERROR;
	}

	// 打开指定的服务
	SC_HANDLE hService = OpenServiceA(hSCManager, service_name.c_str(), SERVICE_CHANGE_CONFIG);
	if (hService == nullptr) {
		CloseServiceHandle(hSCManager);
		return ITUNESREPAIR_E_NO_SERVICE_FOUND;
	}

	if (!ChangeServiceConfigA(
		hService,                  // 服务句柄
		SERVICE_NO_CHANGE,         // 服务类型（不改变）
		type,          // 启动类型
		SERVICE_NO_CHANGE,         // 错误控制（不改变）
		NULL,                      // 二进制路径名（不改变）
		NULL,                      // 加载顺序组（不改变）
		NULL,                      // 标记ID（不改变）
		NULL,                      // 依赖项（不改变）
		NULL,                      // 服务启动帐户名（不改变）
		NULL,                      // 服务启动帐户密码（不改变）
		NULL)) {                   // 显示名称（不改变）
		CloseServiceHandle(hService);
		CloseServiceHandle(hSCManager);
		LOG_INFO("ChangeServiceConfig error: %d", GetLastError());
		return ITUNESREPAIR_E_UNKNOWN_ERROR;
	}

	// 关闭服务句柄和SCM句柄
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);

	return ITUNESREPAIR_E_SUCCESS;

}

static bool is_local_port_in_used(const string& port) {
	string cmd = "cmd.exe /C netstat -ano | findstr \"127.0.0.1:" + port + "\"" + " | findstr \"LISTENING\"";
	string res;
	execute_process(cmd, res);
	return res.find(port) != string::npos;
}

/**
 * @brief  获取设备属性
 */
static std::wstring get_device_property(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData, DWORD property) {
	DWORD requiredSize = 0;
	::SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, property, NULL, NULL, 0, &requiredSize);

	if (requiredSize == 0) {
		return L"";
	}

	std::wstring propertyValue(requiredSize / sizeof(wchar_t), L'\0');
	if (::SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, property, NULL,
		reinterpret_cast<BYTE*>(&propertyValue[0]), requiredSize, NULL)) {
		return propertyValue;
	}

	return L"";
}

/**
 * @brief 获取操作系统位数
 */
static int get_system_bits()
{
	BOOL(WINAPI * fnIsWow64Process)(HANDLE, PBOOL) = (BOOL(WINAPI*)(HANDLE, PBOOL))GetProcAddress(GetModuleHandle(_T("kernel32")), "IsWow64Process");
	// IsWow64Process  |  32 system  |  64 system  |
	// 32 process      |    FALSE    |    TRUE     |
	// 64 process      |    ERROR    |    FALSE    |
	BOOL b64System = FALSE;
	if (sizeof(void*) == 8)
	{
		b64System = TRUE;
	}
	else if (fnIsWow64Process != NULL)
	{
		fnIsWow64Process(GetCurrentProcess(), &b64System);
	}
	return b64System ? 64 : 32;
}

/**
 * @brief 枚举Apple设备
 */
static itunesrepair_error_t enum_apple_device(cb_handle_apple_device_t cb)
{
	HDEVINFO deviceInfoSet;
	SP_DEVINFO_DATA deviceInfoData;
	DWORD memberIndex = 0;
	BOOL result = FALSE;
	bool deviceFound = false;

	// 获取所有设备的信息集
	deviceInfoSet = ::SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (deviceInfoSet == INVALID_HANDLE_VALUE) {
		LOG_INFO("Error: Unable to get device information set. Error code: %d", ::GetLastError());
		return ITUNESREPAIR_E_UNKNOWN_ERROR;
	}

	deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	while (::SetupDiEnumDeviceInfo(deviceInfoSet, memberIndex, &deviceInfoData)) {
		memberIndex++;
		wchar_t instanceId[MAX_DEVICE_ID_LEN];
		if (::CM_Get_Device_IDW(deviceInfoData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
			std::wstring manufacturer = get_device_property(deviceInfoSet, deviceInfoData, SPDRP_DEVICEDESC);
			std::wstring id{ instanceId };

			// 匹配 USB\VID_05AC 设备: 05AC为APPLE设备的厂商ID
			if (id.find(L"USB\\VID_05AC") != std::wstring::npos) {
				deviceFound = true;

				itunesrepair_error_t error;
				if (!cb(deviceInfoSet, &deviceInfoData, error)) {
					::SetupDiDestroyDeviceInfoList(deviceInfoSet);
					return error;
				}
			}
		}
	}

	::SetupDiDestroyDeviceInfoList(deviceInfoSet);

	if (!deviceFound) {
		return ITUNESREPAIR_E_NO_DEVICE_FOUND;
	}

	return ITUNESREPAIR_E_SUCCESS;
}

static bool uninstall_device_driver(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData, itunesrepair_error_t& error)
{
	BOOL res = ::SetupDiCallClassInstaller(DIF_REMOVE, deviceInfoSet, deviceInfoData);
	DWORD e = GetLastError();
	if (e == ERROR_IN_WOW64) {
		return false;
	}
	return true;
}

static bool check_device_connected(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData, itunesrepair_error_t& error)
{
	error = ITUNESREPAIR_E_SUCCESS;
	return false;
}

static bool is_software_installed(const std::wstring& softwareName) {
	HKEY hKey;
	std::wstring uninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstallKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
		LOG_ERROR("Failed to open registry key.");
		return false;
	}

	DWORD index = 0;
	WCHAR name[256];
	DWORD nameSize = sizeof(name) / sizeof(name[0]);

	while (RegEnumKeyExW(hKey, index, name, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
		HKEY subKey;
		std::wstring subKeyPath = uninstallKey + L"\\" + name;

		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKeyPath.c_str(), 0, KEY_READ, &subKey) == ERROR_SUCCESS) {
			WCHAR displayName[256];
			DWORD size = sizeof(displayName);
			if (RegQueryValueExW(subKey, L"DisplayName", nullptr, nullptr, (LPBYTE)displayName, &size) == ERROR_SUCCESS) {
				if (softwareName == displayName) {
					RegCloseKey(subKey);
					RegCloseKey(hKey);
					return true;
				}
			}
			RegCloseKey(subKey);
		}

		index++;
		nameSize = sizeof(name) / sizeof(name[0]);
	}

	RegCloseKey(hKey);
	return false;
}

/**
 * @brief 找出Apple驱动对应的omeNN.inf
 */
static itunesrepair_error_t get_apple_driver_oem_names(vector<string>& res, uint32_t which_driver)
{
	list<string> match_strs;
	static vector<pair<uint32_t, function<void()>>> tasks{
	{ITUNESREPAIR_DRIVER_USBAAPL,   [&]() { match_strs.push_back("USBAAPL.cat"); }},
	{ITUNESREPAIR_DRIVER_USBAAPL64, [&]() { match_strs.push_back("USBAAPL64.cat"); }},
	{ITUNESREPAIR_DRIVER_APPLEUSB,  [&]() { match_strs.push_back("AppleUSB.cat");  }},
	{ITUNESREPAIR_DRIVER_APPLERSM,  [&]() { match_strs.push_back("AppleRSM.cat"); }},
	{ITUNESREPAIR_DRIVER_APPLEKIS,  [&]() { match_strs.push_back("AppleKIS.cat");  }},
	{ITUNESREPAIR_DRIVER_NET64,     [&]() { match_strs.push_back("netaapl64.cat");  }},
	};

	for (auto& task : tasks) {
		if (task.first & which_driver) {
			task.second();
		}
	}

	vector<string> dirs;
	vector<string> files;
	scan_directory(WIN_INF_PATH, files, dirs);
	for (string& file : files) {
		if (file.find(".inf") != string::npos && file.find("oem") != string::npos) {
			HINF hInf = ::SetupOpenInfFileA(file.c_str(), nullptr, INF_STYLE_WIN4, nullptr);
			if (hInf == INVALID_HANDLE_VALUE) {
				return ITUNESREPAIR_E_FAILED_TO_OPEN_INF;
			}
			//DWORD requiredSize = 0;
			char lineText[256];
			if (::SetupGetLineTextA(nullptr, hInf, "Version", "CatalogFile", lineText, sizeof(lineText), nullptr)) {
				string text{ lineText };
				if (!text.empty())
				{
					string found ;
					for (string& match_str : match_strs) {
						if (compare_string_insensitive(text, match_str)) {
							res.push_back(extract_filename(file));
							found = match_str;
							break;
						}
					}
					if (!found.empty()) {
						match_strs.remove(found);
					}
				}
			}
			::SetupCloseInfFile(hInf);
		}
	}
	return ITUNESREPAIR_E_SUCCESS;
}

static itunesrepair_error_t run_driver_installer(const string& opt, const string& dst)
{
	string cmd = g_itunes_resource.driver_installer_path + " " + opt + " \"" + dst + "\"";
	string res;
	int code = -1;
	if (!execute_process(cmd, res, &code)) {
		return ITUNESREPAIR_E_FAILED_TO_OPEN_PROCESS;
	}

	if (code == 0) {
		return ITUNESREPAIR_E_SUCCESS;
	}
	else {
		LOG_ERROR("%s", res.c_str());
	}
	return ITUNESREPAIR_E_UNKNOWN_ERROR;
}

static itunesrepair_error_t run_msiexec(const string& opt, const string& dst)
{
	string cmd = "msiexec " + opt + " \"" + dst + "\" " + "/quiet";
	string s;
	int code;
	if (!execute_process(cmd, s, &code)) {
		return ITUNESREPAIR_E_FAILED_TO_OPEN_PROCESS;
	}
	if (code != 0 && code != 3010) {
		LOG_INFO("msiexec error: %d", code);
		return ITUNESREPAIR_E_MSIEXEC_ERROR;
	}
	return ITUNESREPAIR_E_SUCCESS;
}
 
static char* userpref_utf16_to_utf8(wchar_t* unistr, long len, long* items_read, long* items_written)
{
	if (!unistr || (len <= 0)) return nullptr;
	char* outbuf = (char*)malloc(3 * (len + 1));
	int p = 0;
	int i = 0;

	wchar_t wc;

	while (i < len) {
		wc = unistr[i++];
		if (wc >= 0x800) {
			outbuf[p++] = (char)(0xE0 + ((wc >> 12) & 0xF));
			outbuf[p++] = (char)(0x80 + ((wc >> 6) & 0x3F));
			outbuf[p++] = (char)(0x80 + (wc & 0x3F));
		}
		else if (wc >= 0x80) {
			outbuf[p++] = (char)(0xC0 + ((wc >> 6) & 0x1F));
			outbuf[p++] = (char)(0x80 + (wc & 0x3F));
		}
		else {
			outbuf[p++] = (char)(wc & 0x7F);
		}
	}
	if (items_read) {
		*items_read = i;
	}
	if (items_written) {
		*items_written = p;
	}
	outbuf[p] = 0;

	return outbuf;
}

static char* stpcpy(char* s1, const char* s2)
{
	if (s1 == NULL || s2 == NULL)
		return NULL;

	strcpy(s1, s2);

	return s1 + strlen(s2);
}

static char* string_concat(const char* str, ...)
{
	size_t len;
	va_list args;
	char* s;
	char* result;
	char* dest;

	if (!str)
		return NULL;

	/* Compute final length */

	len = strlen(str) + 1; /* plus 1 for the null terminator */

	va_start(args, str);
	s = va_arg(args, char*);
	while (s) {
		len += strlen(s);
		s = va_arg(args, char*);
	}
	va_end(args);

	/* Concat each string */

	result = (char*)malloc(len);
	if (!result)
		return NULL; /* errno remains set */

	dest = result;

	dest = stpcpy(dest, str);

	va_start(args, str);
	s = va_arg(args, char*);
	while (s) {
		dest = stpcpy(dest, s);
		s = va_arg(args, char*);
	}
	va_end(args);

	return result;
}

static const char* userpref_get_config_dir()
{
	static char* __config_dir = nullptr;
	char* base_config_dir = nullptr;

	if (__config_dir)
		return __config_dir;

#ifdef WIN32
	wchar_t path[MAX_PATH + 1];
	HRESULT hr;
	LPITEMIDLIST pidl = nullptr;
	BOOL b = FALSE;

	hr = SHGetSpecialFolderLocation(NULL, CSIDL_COMMON_APPDATA, &pidl);
	if (hr == S_OK) {
		b = SHGetPathFromIDListW(pidl, path);
		if (b) {
			base_config_dir = userpref_utf16_to_utf8(path, wcslen(path), nullptr, nullptr);
			CoTaskMemFree(pidl);
		}
	}
#else
#ifdef __APPLE__
	base_config_dir = strdup("/var/db");
#else
	base_config_dir = strdup("/var/lib");
#endif
#endif
	__config_dir = string_concat(base_config_dir, DIR_SEP_S, USERPREF_CONFIG_DIR, NULL);

	if (__config_dir) {
		int i = strlen(__config_dir) - 1;
		while ((i > 0) && (__config_dir[i] == DIR_SEP)) {
			__config_dir[i--] = '\0';
		}
	}

	free(base_config_dir);

	LOG_INFO("initialized config_dir to %s", __config_dir);

	return __config_dir;
}

static bool get_value(const HKEY MainKey, const wstring SubKey, const wstring KeyName, wstring& Value, bool b64bit = false)
{
	HKEY hKey;
	DWORD dMask = b64bit ? KEY_READ | KEY_WOW64_64KEY : KEY_READ;
	LONG status = ::RegOpenKeyEx(MainKey, SubKey.c_str(), 0, dMask, &hKey);
	if (status == ERROR_SUCCESS)
	{
		DWORD dwType;
		BYTE szValue[MAX_PATH * 5];
		DWORD nLength = sizeof(szValue);

		status = ::RegQueryValueEx(hKey, KeyName.c_str(), NULL, &dwType, (LPBYTE)&szValue, &nLength);
		if ((status == ERROR_SUCCESS) && (dwType == REG_SZ))
		{
			Value = wstring((WCHAR*)szValue);
		}
		::RegCloseKey(hKey);
		return true;
	}

	return false;
}

static bool get_value(const HKEY MainKey, const wstring SubKey, const wstring KeyName, std::vector<wstring>& Values)
{
	HKEY hKey;
	LONG status = ::RegOpenKeyEx(MainKey, SubKey.c_str(), 0, KEY_READ, &hKey);
	if (status == ERROR_SUCCESS) {
		BYTE szValue[MAX_PATH * 2];
		DWORD nLength = sizeof(szValue);
		DWORD dwIndexs = 0;
		while (::RegEnumKeyEx(hKey, dwIndexs, (LPWSTR)&szValue, &nLength, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
			Values.push_back(wstring((WCHAR*)szValue));
			nLength = sizeof(szValue);
			++dwIndexs;
		}
		::RegCloseKey(hKey);
		return true;
	}

	return false;
}

static wstring get_itunes_path() {

	wstring strInstallPath;
	get_value(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Apple Computer, Inc.\\iTunes"), _T("InstallDir"), strInstallPath);
	if (strInstallPath.size() == 0) {
		get_value(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Apple Computer, Inc.\\iTunes"), _T("InstallDir"), strInstallPath, true);
	}

	if (!strInstallPath.empty()) {
		size_t nPos = strInstallPath.rfind(_T("\\"));
		wstring strSplitChar = (nPos == strInstallPath.size() - 1) ? _T("") : _T("\\");
		wstring strPath = strInstallPath + strSplitChar + _T("iTunes.exe");
		if (path_exisit(strPath.c_str())) {
			return strPath;
		}
	}

	wstring striTunesPath;
	get_value(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\iTunes.exe"), _T(""), striTunesPath);
	if (path_exisit(striTunesPath.c_str())) {
		return striTunesPath;
	}

	striTunesPath = _T("");
	get_value(HKEY_CURRENT_USER, _T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\iTunes.exe"), _T(""), striTunesPath);
	if (path_exisit(striTunesPath.c_str())) {
		return striTunesPath;
	}

	std::vector<wstring> strValues;
	wstring SubKey = _T("SOFTWARE\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\PackageRepository\\Packages");
	get_value(HKEY_LOCAL_MACHINE, SubKey, _T(""), strValues);
	if (!strValues.empty()) {
		std::vector<wstring>::iterator iter = strValues.begin();
		for (; iter != strValues.end(); ++iter) {
			if ((*iter).find(_T("AppleInc.iTunes")) == 0) {
				wstring strPath = _T("");
				wstring strKey = SubKey + _T("\\") + *iter;
				get_value(HKEY_LOCAL_MACHINE, strKey, _T("Path"), strPath);
				if (!strPath.empty()) {
					strPath = strPath + _T("\\iTunes.exe");
					if (path_exisit(strPath.c_str())) {
						return strPath;
					}
				}
			}
		}
	}

	TCHAR szTunes[MAX_PATH + 32] = { 0 };
	GetWindowsDirectory(szTunes, MAX_PATH);

	szTunes[3] = 0;
	_tcscat_s(szTunes, _T("Program Files (x86)\\iTunes\\iTunes.exe"));
	if (path_exisit(szTunes)) {
		return szTunes;
	}

	szTunes[3] = 0;
	_tcscat_s(szTunes, _T("Program Files\\iTunes\\iTunes.exe"));
	if (path_exisit(szTunes)) {
		return szTunes;
	}

	return _T("");

}

static bool get_file_version(const std::wstring& filePath, std::wstring& version)
{
	DWORD handle;
	DWORD size = GetFileVersionInfoSize(filePath.c_str(), &handle);

	if (size == 0)
		return false;

	std::vector<BYTE> buffer(size);

	// 获取版本信息
	if (!GetFileVersionInfo(filePath.c_str(), handle, size, buffer.data()))
		return false;

	VS_FIXEDFILEINFO* fileInfo;
	UINT fileInfoSize;

	// 获取固定的文件信息
	if (!VerQueryValue(buffer.data(), L"\\", (LPVOID*)&fileInfo, &fileInfoSize))
		return false;

	// 格式化版本号
	version = std::to_wstring(HIWORD(fileInfo->dwFileVersionMS)) + L"." +
		std::to_wstring(LOWORD(fileInfo->dwFileVersionMS)) + L"." +
		std::to_wstring(HIWORD(fileInfo->dwFileVersionLS)) + L"." +
		std::to_wstring(LOWORD(fileInfo->dwFileVersionLS));

	return true;
}

static void check_apple_mobile_process() {
	std::unique_lock<std::mutex> lock(g_cv_m);
	while (!g_check_process_stopped) {
		if (!is_local_port_in_used(AMDS_PORT)) {
			if (!execute_process(g_itunes_resource.apple_device_process_path)) {
				LOG_ERROR("Could not open apple mobile device process.");
				if (g_user_handle_exception != nullptr && !g_user_handle_exception(g_user_data)) {
					break;
				}
			}
		}
		g_cv.wait_for(lock, std::chrono::seconds(5), [] { return g_check_process_stopped.load(); });
	}
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_set_resource(const char* resource_dir_path)
{
	string path{ resource_dir_path ? resource_dir_path : "" };
	if (!path_exisit(path)) {
		LOG_ERROR("path not exist: %s", path.c_str());
		return ITUNESREPAIR_E_PATH_NOT_EXIST;
	}
	if (path[path.size() - 1] != '\\' && path[path.size() - 1] != '/') {
		path += "\\";
	}

	g_resource_loaded = false;
	g_itunes_resource.apple_device_process_path = path + "AppleMobileDeviceProcess.exe";
	g_itunes_resource.tool_7z_path = path + "utility\\7z.exe";
	g_itunes_resource.appleusb_inf_path = path + "drivier\\x64\\appleusb\\AppleUsb.inf";
	g_itunes_resource.applekis_inf_path = path + "drivier\\x64\\applekis\\AppleKIS.inf";
	g_itunes_resource.applersm_inf_path = path + "drivier\\x64\\applersm\\AppleRsm.inf";
	g_itunes_resource.netaapl_inf_path = path + "drivier\\x64\\netaapl\\netaapl64.inf";
	g_itunes_resource.usbaapl64_inf_path = path + "drivier\\x64\\usbaapl64\\usbaapl64.inf";
	g_itunes_resource.usbaapl_inf_path = path + "drivier\\x86\\usbaapl.inf";
	
	if (get_system_bits() == 64) {
		g_itunes_resource.driver_installer_path = path + "drivier\\x64\\DriverInstaller.exe";	
	}
	else {
		g_itunes_resource.driver_installer_path = path + "drivier\\x86\\DriverInstaller.exe";
	}
	
	vector<string*> paths
	{
		&g_itunes_resource.usbaapl_inf_path,
		&g_itunes_resource.usbaapl64_inf_path,
		&g_itunes_resource.apple_device_process_path,
		&g_itunes_resource.driver_installer_path,
		&g_itunes_resource.appleusb_inf_path,
		&g_itunes_resource.applekis_inf_path,
		&g_itunes_resource.applersm_inf_path,
		&g_itunes_resource.netaapl_inf_path,
		&g_itunes_resource.tool_7z_path
	};

	for (string* path : paths) {
		if (!path_exisit(*path)) {
			LOG_ERROR("path not exist: %s", (*path).c_str());
			return ITUNESREPAIR_E_RESOURCE_MISS;
		}
	}
	g_resource_loaded = true;
	g_thread = make_unique<CThread<void>>();
	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_driver(uint32_t which_driver)
{
	if (!g_resource_loaded) {
		return ITUNESREPAIR_E_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	
	vector<string> install_paths;
	static vector<pair<uint32_t, function<void()>>> tasks{
		{ITUNESREPAIR_DRIVER_USBAAPL,   [&]() { install_paths.push_back(g_itunes_resource.usbaapl_inf_path); }},
		{ITUNESREPAIR_DRIVER_USBAAPL64, [&]() { install_paths.push_back(g_itunes_resource.usbaapl64_inf_path); }},
		{ITUNESREPAIR_DRIVER_APPLEUSB,  [&]() { install_paths.push_back(g_itunes_resource.appleusb_inf_path);  }},
		{ITUNESREPAIR_DRIVER_APPLERSM,  [&]() { install_paths.push_back(g_itunes_resource.applersm_inf_path); }},
		{ITUNESREPAIR_DRIVER_APPLEKIS,  [&]() { install_paths.push_back(g_itunes_resource.applekis_inf_path);  }},
		{ITUNESREPAIR_DRIVER_NET64,     [&]() { install_paths.push_back(g_itunes_resource.netaapl_inf_path);  }},
	};

	for (auto& task : tasks) {
		if (task.first & which_driver) {
			task.second();
		}
	}

	for (auto& path : install_paths) {
		itunesrepair_error_t error = run_driver_installer("install", path);
		if (error != ITUNESREPAIR_E_SUCCESS) {
			return error;
		}
	}
	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_delete_driver(uint32_t which_driver)
{
	if (!g_resource_loaded) {
		return ITUNESREPAIR_E_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}

	vector<string> ome_names;
	// Windows会将安装的驱动重命名为oemNN.inf，删除驱动需要通过发布名来删除
	itunesrepair_error_t error = get_apple_driver_oem_names(ome_names, which_driver);
	if (error != ITUNESREPAIR_E_SUCCESS) {
		return error;
	}
	for (string& ome_name : ome_names) {
		itunesrepair_error_t error = run_driver_installer("delete", ome_name);
		if (error != ITUNESREPAIR_E_SUCCESS) {
			return error;
		}
	}
	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_driver(uint32_t which_driver)
{
	if (!g_resource_loaded) {
		return ITUNESREPAIR_E_RESOURCE_MISS;
	}
	itunesrepair_error_t error = run_driver_installer("uninstall", "");
	if (error != ITUNESREPAIR_E_SUCCESS) {
		return error;
	}
	return ITUNESREPAIR_E_SUCCESS; 
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_check_itunes_state(pitunes_state_t p, uint32_t opt)
{
	if (p == nullptr) {
		return ITUNESREPAIR_E_INVALID_PARAMETER;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}

	static vector<pair<uint32_t, function<void(pitunes_state_t p)>>> tasks
	{
		{ITUNESREPAIR_OPT_DEVICE_CONNECTED, [](pitunes_state_t p) { p->has_device_connected = (enum_apple_device(check_device_connected) == ITUNESREPAIR_E_SUCCESS); }},
		{ITUNESREPAIR_OPT_ITUNES_INSTALLED, [](pitunes_state_t p) { p->is_itunes_installed = !get_itunes_path().empty(); }},
		{ITUNESREPAIR_OPT_MOBILE_SUPPORT_INSTALLED, [](pitunes_state_t p) { p->is_mobile_support_installed = service_exisit(AMDS_NAME); }},
		{ITUNESREPAIR_OPT_BONJOUR_INSTALLED, [](pitunes_state_t p) { p->is_bonjour_installed = service_exisit(BONJOUR_SERVICE); }},
		{ITUNESREPAIR_OPT_SOFTWARE_UPDATE_INSTALLED, [](pitunes_state_t p) { p->is_software_update_installed = is_software_installed(L"Apple Software Update"); }},
		{ITUNESREPAIR_OPT_MOBILE_SERVICE_ENABLED, [](pitunes_state_t p) { p->is_mobile_service_enabled = service_enable(AMDS_NAME); }},
		{ITUNESREPAIR_OPT_USBMUXD_PORT_DETECTED, [](pitunes_state_t p) { p->is_usbmuxd_port_detected = is_local_port_in_used(AMDS_PORT); }}, // 27015 apple设备通信进程的端口号
	};

	for (auto& task : tasks) {
		if (task.first & opt) {
			task.second(p);
		}
	}

	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API const char* itunesrepair_get_itunes_version()
{
	wstring version;
	if (get_file_version(get_itunes_path(), version)) {
		static string _version = wstring_to_string(version);
		return _version.c_str();
	}
	return nullptr;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_set_itunes_setupmsi(const char* setup, bool is_64_bit)
{
	if (!g_resource_loaded) {
		return ITUNESREPAIR_E_RESOURCE_MISS;
	}

	if (!path_exisit(setup)) {
		return ITUNESREPAIR_E_PATH_NOT_EXIST;
	}
	g_setup_resource_loaded = false;

	string path{ setup };
	size_t pos = path.find_last_of("/\\");
	if (pos != std::string::npos) {
		path = path.substr(0, pos);
	}

	string cmd = g_itunes_resource.tool_7z_path + " x \"" + setup + "\" " + "-o\"" + path + "\" -y";
	string res;
	int code;
	if (!execute_process(cmd, res, &code)) {
		return ITUNESREPAIR_E_FAILED_TO_OPEN_PROCESS;
	}

	if (code != 0) {
		return ITUNESREPAIR_E_UNKNOWN_ERROR;
	}
	if (is_64_bit) {
		g_itunes_setup_resource.apple_mobile_device_support = path + "\\AppleMobileDeviceSupport64.msi";
		g_itunes_setup_resource.bonjour = path + "\\Bonjour64.msi";
	}
	else {
		g_itunes_setup_resource.apple_mobile_device_support = path + "\\AppleMobileDeviceSupport.msi";
		g_itunes_setup_resource.bonjour = path + "\\Bonjour.msi";
	}
	g_itunes_setup_resource.apple_software_update = path + "\\AppleSoftwareUpdate.msi";

	if (!path_exisit(g_itunes_setup_resource.apple_mobile_device_support)) {
		LOG_ERROR("path not exist: %s", g_itunes_setup_resource.apple_mobile_device_support.c_str());
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!path_exisit(g_itunes_setup_resource.bonjour)) {
		LOG_ERROR("path not exist: %s", g_itunes_setup_resource.bonjour.c_str());
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!path_exisit(g_itunes_setup_resource.apple_software_update)) {
		LOG_ERROR("path not exist: %s", g_itunes_setup_resource.apple_software_update.c_str());
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	g_setup_resource_loaded = true;
	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_device_support()
{
	if (!g_setup_resource_loaded) {
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return run_msiexec("/x", g_itunes_setup_resource.apple_mobile_device_support);
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_device_support()
{
	if (!g_setup_resource_loaded) {
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return run_msiexec("/i", g_itunes_setup_resource.apple_mobile_device_support);
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_bonjour_service()
{
	if (!g_setup_resource_loaded) {
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return run_msiexec("/x", g_itunes_setup_resource.bonjour);
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_bonjour_service()
{
	if (!g_setup_resource_loaded) {
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return run_msiexec("/i", g_itunes_setup_resource.bonjour);
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_update_service()
{
	if (!g_setup_resource_loaded) {
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return run_msiexec("/x", g_itunes_setup_resource.apple_software_update);
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_update_service()
{
	if (!g_setup_resource_loaded) {
		return ITUNESREPAIR_E_SETUP_RESOURCE_MISS;
	}
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return run_msiexec("/i", g_itunes_setup_resource.apple_software_update);
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_enable_device_service()
{
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return change_service_type(AMDS_NAME, SERVICE_DISABLED);
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_disable_device_service()
{
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	return change_service_type(AMDS_NAME, SERVICE_AUTO_START);
	
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_is_driver_installed(uint32_t which_driver)
{
	// 这里只检测驱动包是否存在
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	vector<string> dirs;
	vector<string> files;
	scan_directory(WIN_FILEREPOSITORY_PATH, files, dirs);

	list<string> check_list;
	static vector<pair<uint32_t, function<void()>>> tasks{
		{ITUNESREPAIR_DRIVER_USBAAPL,   [&]() { check_list.push_back("usbaapl.inf");  }},
		{ITUNESREPAIR_DRIVER_USBAAPL64, [&]() { check_list.push_back("usbaapl64.inf");  }},
		{ITUNESREPAIR_DRIVER_APPLEUSB,  [&]() { check_list.push_back("appleusb.inf");  }},
		{ITUNESREPAIR_DRIVER_NET64,     [&]() { check_list.push_back("netaapl64.inf");  }},
		{ITUNESREPAIR_DRIVER_APPLERSM,  [&]() { check_list.push_back("applersm.inf");  }},
		{ITUNESREPAIR_DRIVER_APPLEKIS,  [&]() { check_list.push_back("applekis.inf");  }},
	};

	for (auto& task : tasks) {
		if (task.first & which_driver) {
			task.second();
		}
	}

	for (string& dir : dirs) {
		string found_name;
		for (string& check_name : check_list) {
			if (dir.find(check_name) != string::npos) {
				found_name = check_name;
				break;
			}
		}
		if (!found_name.empty()) {
			check_list.remove(found_name);
			if (check_list.empty()) {
				return ITUNESREPAIR_E_SUCCESS;
			}
		}
	}

	if (check_list.empty()) {
		return ITUNESREPAIR_E_SUCCESS;
	}
	else {
		for (string& check_name : check_list) {
			LOG_INFO("%s not installed", check_name.c_str());
		}
		return ITUNESREPAIR_E_DRIVER_IS_NOT_INSTALLED;
	}
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_open_and_check_apple_device_process(cb_apple_process_exception_happened cb, void* user_data)
{
	itunesrepair_error_t error = itunesrepair_open_apple_device_process();
	if (error != ITUNESREPAIR_E_SUCCESS) {
		return error;
	}
	g_user_handle_exception = cb;
	g_user_data = user_data;
	g_check_process_stopped = false;
	return g_thread->run(check_apple_mobile_process) ? ITUNESREPAIR_E_SUCCESS : ITUNESREPAIR_E_UNKNOWN_ERROR;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_open_apple_device_process()
{
	if (!g_resource_loaded) {
		return ITUNESREPAIR_E_RESOURCE_MISS;
	}

	// 2024.11.21 跟进竞品调整逻辑，不再强制关闭apple服务
#if 0
	if (!::IsUserAnAdmin()) {
		return ITUNESREPAIR_E_PERMISSION_DENIED;
	}
	switch_service(AMDS_NAME, false);
#endif 
	itunesrepair_close_apple_device_process();
	if (!execute_process(g_itunes_resource.apple_device_process_path)) {
		LOG_ERROR("Could not open apple mobile device process.");
		return ITUNESREPAIR_E_FAILED_TO_OPEN_PROCESS;
	}
	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_close_apple_device_process()
{
	if (!g_resource_loaded) {
		return ITUNESREPAIR_E_RESOURCE_MISS;
	}
	if (g_thread->isRunning()) {
		g_check_process_stopped = true;
		g_cv.notify_all();
		g_thread->join();
	}
	while (kill_process_by_name("AppleMobileDeviceProcess.exe"));
	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_clean_lockdown_files()
{
	const char* config_dir = userpref_get_config_dir();
	if (!config_dir) {
		return ITUNESREPAIR_E_UNKNOWN_ERROR;
	}
	std::vector<std::string> files;
	std::vector<std::string> dirs;
	scan_directory(std::string(config_dir), files, dirs);
	for (const auto& file : files) {
		remove_file(file.c_str());
	}
	return ITUNESREPAIR_E_SUCCESS;
}

ITUNESREPAIR_API itunesrepair_error_t itunesrepair_set_log_callback(cb_itunesrepair_log_t cb, void* user_data)
{
	if (cb == nullptr) {
		return ITUNESREPAIR_E_INVALID_PARAMETER;
	}
	set_log_info_callback(cb, user_data, false);
	return ITUNESREPAIR_E_SUCCESS;
}