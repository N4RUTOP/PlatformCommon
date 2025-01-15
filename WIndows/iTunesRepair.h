/**
*	itunes修复
*
*	Create by lihuanqian on 10/15/2024.
*/
#pragma once

#include <stdint.h>

#define ITUNESREPAIR_API

typedef enum
{
	/** 成功 */
	ITUNESREPAIR_E_SUCCESS = 0,

	/** 缺少资源文件 */
	ITUNESREPAIR_E_RESOURCE_MISS,

	/** 缺少SETUP资源文件 */
	ITUNESREPAIR_E_SETUP_RESOURCE_MISS,

	/** 路径不存在 */
	ITUNESREPAIR_E_PATH_NOT_EXIST,

	/** 驱动未安装 */
	ITUNESREPAIR_E_DRIVER_IS_NOT_INSTALLED,

	/** 权限不足 */
	ITUNESREPAIR_E_PERMISSION_DENIED,

	/** 打开进程失败 */
	ITUNESREPAIR_E_FAILED_TO_OPEN_PROCESS,

	/** 设备未找到 */
	ITUNESREPAIR_E_NO_DEVICE_FOUND,

	/** 无效参数 */
	ITUNESREPAIR_E_INVALID_PARAMETER,

	/** 无法打开inf文件 */
	ITUNESREPAIR_E_FAILED_TO_OPEN_INF,

	/** 安装进程错误 */
	ITUNESREPAIR_E_MSIEXEC_ERROR,

	/** 服务不存在 */
	ITUNESREPAIR_E_NO_SERVICE_FOUND,

	/** 未知错误 */
	ITUNESREPAIR_E_UNKNOWN_ERROR,

} itunesrepair_error_t;

typedef void(*cb_itunesrepair_log_t)(const char* log_msg, void* user_data);
typedef bool(*cb_apple_process_exception_happened)(void* user_data);

#define ITUNESREPAIR_OPT_ALL                        UINT32_MAX
#define ITUNESREPAIR_OPT_DEVICE_CONNECTED           UINT32_C(1 << 0)  // 设备连接
#define ITUNESREPAIR_OPT_ITUNES_INSTALLED           UINT32_C(1 << 1)  // 设备连接
#define ITUNESREPAIR_OPT_MOBILE_SUPPORT_INSTALLED   UINT32_C(1 << 2)  // Apple Mobile Support
#define ITUNESREPAIR_OPT_BONJOUR_INSTALLED          UINT32_C(1 << 3)  // Bonjour
#define ITUNESREPAIR_OPT_SOFTWARE_UPDATE_INSTALLED  UINT32_C(1 << 4)  // Apple Software update
#define ITUNESREPAIR_OPT_MOBILE_SERVICE_ENABLED     UINT32_C(1 << 5)  // Apple Mobile service
#define ITUNESREPAIR_OPT_USBMUXD_PORT_DETECTED      UINT32_C(1 << 6)  // USBMUXD Port

/** 驱动安装选项 */
#define ITUNESREPAIR_DRIVER_ALL 				    UINT32_MAX
#define ITUNESREPAIR_DRIVER_USBAAPL			        UINT32_C(1 << 0)  // x86
#define ITUNESREPAIR_DRIVER_USBAAPL64			    UINT32_C(1 << 1)  // x64 新版本驱动(目前产品使用该驱动与设备通信)
#define ITUNESREPAIR_DRIVER_APPLEUSB			    UINT32_C(1 << 2)  // x64 旧版本驱动
#define ITUNESREPAIR_DRIVER_APPLERSM			    UINT32_C(1 << 3)  // x64	
#define ITUNESREPAIR_DRIVER_APPLEKIS			    UINT32_C(1 << 4)  // x64	
#define ITUNESREPAIR_DRIVER_NET64				    UINT32_C(1 << 5)  // x64	

typedef struct
{
	/** 是否有设备连接 */
	bool has_device_connected;

	/** iTunes是否安装 */
	bool is_itunes_installed;

	/** Apple Mobile Support是否安装 */
	bool is_mobile_support_installed;

	/** Bonjour是否安装 */
	bool is_bonjour_installed;

	/** Apple Software Update是否安装 */
	bool is_software_update_installed;

	/** Apple Mobile Service是否可用 */
	bool is_mobile_service_enabled;

	/** usbmuxd端口是否能探测到 */
	bool is_usbmuxd_port_detected;
} itunes_state_t, *pitunes_state_t;

/** 获取日志回调 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_set_log_callback(cb_itunesrepair_log_t cb, void* user_data);

/** 设置资源文件夹 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_set_resource(const char* resource_dir_path);

/// itunes驱动修复
/** 检测驱动是否安装 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_is_driver_installed(uint32_t which_driver /** ITUNESREPAIR_DRIVER_* */);

/** 安装驱动  */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_driver(uint32_t which_driver /** ITUNESREPAIR_DRIVER_* */);

/** 删除驱动 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_delete_driver(uint32_t which_driver /** ITUNESREPAIR_DRIVER_* */);

/** 从设备卸载驱动 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_driver(uint32_t which_driver /** ITUNESREPAIR_DRIVER_* */);

/// itunes模块修复
/** 检测itunes状态 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_check_itunes_state(pitunes_state_t itunes_state, uint32_t opt /** ITUNESREPAIR_OPT_* */);

/** 获取itunes版本 */
ITUNESREPAIR_API const char* itunesrepair_get_itunes_version();

/** 设置itunes安装包 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_set_itunes_setupmsi(const char* setup, bool is_64_bit);

/** 卸载/安装 Apple Mobile Device Support */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_device_support();
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_device_support();

/** 卸载/安装 Bonjour */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_bonjour_service();
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_bonjour_service();

/** 卸载/安装 Apple Software update */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_uninstall_update_service();
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_install_update_service();

/** 启动/禁用服务 Apple Mobile Device Service */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_enable_device_service();
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_disable_device_service();

/**
 * 打开并检测Apple服务进程
 * 
 * @接口描述：
 * 接口会先调用'itunesrepair_open_apple_device_process()'先启动一次服务进程，
 * 之后会一直检测usbmuxd端口(27015)是否启用，若usbmuxd未启用则会重新启动apple通信进程，启动失败会触发'cb_apple_process_exception_happened'回调，
 * 如果'cb_apple_process_exception_happened'回调返回false则中断检测。
 * 
 * 参数 cb：进程启动失败触发事件回调
 * 参数 user_data：用户数据
 * 
 * @注意：一定不要在'cb_apple_process_exception_happened'内调用'itunesrepair_close_apple_device_process()'，否则会出现死锁。
 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_open_and_check_apple_device_process(cb_apple_process_exception_happened cb, void* user_data);

/** 
 * 打开Apple服务进程
 * 打开服务AppleMobilePorcess进程(2024.11.21不再停止apple原本的服务)
 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_open_apple_device_process();

/** 关闭Apple服务进程 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_close_apple_device_process();

/** 清理lockdown文件 */
ITUNESREPAIR_API itunesrepair_error_t itunesrepair_clean_lockdown_files();
