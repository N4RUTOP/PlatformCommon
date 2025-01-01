/**
*
*	Platform common functions (Windows，MacOs)
*	
*	Create by lihuanqian on 5/24/2024
* 
*	Copyright (c) lihuanqian All Rights Reserved. 
*
*/

#pragma once

#include <string>
#include <stdint.h>
#include <vector>
#include <memory>
#include <chrono>
#include <filesystem>

#ifdef _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <intrin.h>
#elif defined(__APPLE__)
#include <os/log.h>
#include <utility>
#include <pthread.h>
#endif 

/**
 * @brief File system list
 */
struct entry {
	char* name;
	struct entry* next;
};

namespace PlatformCommonUtils
{
	/************ Type define ************/
#ifdef _MSC_VER
	using mutex_t = LPCRITICAL_SECTION;
#else 
	using mutex_t = pthread_mutex_t;
#endif
	using log_info_callback = void(*)(const char*, void*);

	/************ File system ************/
	bool path_exisit(const std::string& path);
	bool path_exisit(const char* path);

#ifdef _MSC_VER
	bool path_exisit(const std::wstring& path);
	bool path_exisit(const wchar_t* path);
#endif

	bool remove_directory(const char* path);
	bool remove_file(const char* path);
	void scan_directory(const std::string& path, std::vector<std::string>& files, std::vector<std::string>& directories); 
	void scan_directory(const char* path, struct entry** files, struct entry** directories); // c impl, should free memory after use
	bool make_directory(const char* path, int mode);
	bool mkdir_with_parents(const char* dir, int mode);
	bool rmdir_recursive(const char* path);
	bool mkfile_with_parents(const char* file);

	void copy_file_by_path(const char* src, const char* dst);
	void copy_directory_by_path(const char* src, const char* dst);
	
	bool path_is_dir(const char* path);
	bool path_is_file(const char* path);
	size_t get_file_size(const char* path);
	
	const char* extract_filename(const char* filepath);
	std::string extract_filename(const std::string& filePath); 

	template<typename... Args>
	std::string build_path(std::string base, Args&&... args) {
#ifndef __APPLE__
		std::filesystem::path p(base);
		(p /= ... /= args);
		return p.string();
#else
		if (!base.empty() && base[base.size() - 1] == '/') {
			base.erase(base.size() - 1, 1);
		}
		std::ostringstream oss;
		oss << base;
		((oss << "/" << args), ...);
		return oss.str();
#endif
	}

	FILE* open_file(const char* path, const char* mode);
	FILE* open_file(const std::string& path, const std::string& mode);

	bool write_data_to_file(const std::string& path, const std::vector<uint8_t>& data);
	bool write_data_to_file(const char* path, const uint8_t* data, size_t len);

	std::vector<uint8_t> read_data_from_file(const std::string& path);

	/************ Mutex ************/
	void mutex_lock(mutex_t mutex);
	void mutex_unlock(mutex_t mutex);

	/************ String ************/
	std::vector<std::string> str_split(const std::string& s, char seperator);
	std::vector<std::string> str_split(const std::string& s, const std::string& separator);
	std::string wstring_to_string(const std::wstring& wstr); // wstring to string, since c++ 20
	std::shared_ptr<wchar_t> utf8_to_wchar(const char* data);
	std::shared_ptr<char> wchar_to_utf8(const wchar_t* data);
	std::shared_ptr<char> utf8_to_local_encoding(const char* utf8Str); // Just for windows
	bool compare_string_insensitive(const std::string& str1, const std::string& str2);

	/************ Process ************/
	bool execute_process(const std::string& cmd, std::string& revMsg, int* exitCode = nullptr);
	int execute_process(const std::string& cmd); // if success return process id else return -1
	bool kill_process(const std::string& proc_path); 
	bool kill_process_by_name(const std::string& proc_name); 
	void kill_process_completely(const std::string& proc_path); // kill process with while cycle until no process exisit
	void kill_process_by_name_completely(const std::string& proc_name);
	bool is_process_running(int process_id);
	bool is_process_running(const std::string& proc_path);
	bool is_process_running_by_name(const std::string& proc_name);

	/************ Log output ************/
	void set_log_info_callback(log_info_callback cb, void* user_data, bool debug);
	std::pair<log_info_callback, void*> get_log_info_cb();
	void set_log_tag_name(const std::string& name);
	std::string get_log_tag_name();

	void disable_log_for_current_thread(bool disable);
	bool is_disable_log();

	template<typename ...Args>
	void output_log_info(const char* info, Args&&... args)
	{
		if (is_disable_log()) {
			return;
		}
		char* buffer = new char[1024];
		std::pair<log_info_callback, void*> pair = get_log_info_cb();
		snprintf(buffer, 1024, info, std::forward<Args>(args)...);
		if (pair.first != nullptr) {
			pair.first(buffer, pair.second);
		}
		else {
#ifdef _MSC_VER
			OutputDebugStringA(buffer);
#else
			//os_log(OS_LOG_DEFAULT, "%{public}s", buffer);
            printf(buffer);
#endif // WIN32
		}
		delete[] buffer;
	}

	/************ Parse binary data ************/
	inline uint16_t swap_uint16(uint16_t val)
	{
#ifdef _MSC_VER
		return _byteswap_ushort(val);
#else
		return __builtin_bswap16(val);
#endif 
	}

	inline uint32_t swap_uint32(uint32_t val)
	{
#ifdef _MSC_VER
		return _byteswap_ulong(val);
#else
		return __builtin_bswap32(val);
#endif
	}

	inline uint64_t swap_uint64(uint64_t val)
	{
#ifdef _MSC_VER
		return _byteswap_uint64(val);
#else
		return __builtin_bswap64(val);
#endif 
	}

	inline uint16_t bin_to_uint16(const uint8_t* data, bool is_little_endian = true)
	{
		return is_little_endian ? *reinterpret_cast<const uint16_t*>(data) : swap_uint16(*reinterpret_cast<const uint16_t*>(data));
	}

	inline uint32_t bin_to_uint32(const uint8_t* data, bool is_little_endian = true)
	{
		return is_little_endian ? *reinterpret_cast<const uint32_t*>(data) : swap_uint32(*reinterpret_cast<const uint32_t*>(data));
	}

	inline uint64_t bin_to_uint64(const uint8_t* data, bool is_little_endian = true)
	{
		return is_little_endian ? *reinterpret_cast<const uint64_t*>(data) : swap_uint64(*reinterpret_cast<const uint64_t*>(data));
	}

	/************ Other ************/
	std::string get_current_directory_path();
	std::string get_current_time();

	void set_debug_output(bool bDebug);

	bool is_debug();

	int get_current_thread_id();

#ifdef _MSC_VER
	void usleep(uint32_t waitTime);
#endif
	void msleep(uint32_t waitTime);

	void start_clock();
	uint64_t end_clock_with_us();
	uint64_t end_clock_with_ms();
	uint64_t end_clock_with_s();
};

/**
*  @brief  Support construct std::string with null point.
*/
#define String_Constructor(psz) (std::string(psz != nullptr ? psz : ""))

#define LOG_INFO(fmt, ...) do { \
    PlatformCommonUtils::output_log_info("[%s] [%s_INFO] " fmt "\n", PlatformCommonUtils::get_current_time().c_str(), PlatformCommonUtils::get_log_tag_name().c_str(), ##__VA_ARGS__); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    PlatformCommonUtils::output_log_info("[%s] [%s_ERROR] " fmt "\n", PlatformCommonUtils::get_current_time().c_str(), PlatformCommonUtils::get_log_tag_name().c_str(), ##__VA_ARGS__); \
} while(0)

#define LOG_DEBUG(fmt, ...) do {\
	if (PlatformCommonUtils::is_debug()) {\
		PlatformCommonUtils::output_log_info("[%s] [%s_DEBUG] " fmt "\n", PlatformCommonUtils::get_current_time().c_str(), PlatformCommonUtils::get_log_tag_name().c_str(), ##__VA_ARGS__); \
	} \
} while (0)

#define TEST_TIMER_START    auto start = std::chrono::high_resolution_clock::now();

#define TEST_TIMER_US_END   auto end = std::chrono::high_resolution_clock::now(); \
						    LOG_INFO("Execution time: %lld microseconds", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());

#define TEST_TIMER_MS_END   auto end = std::chrono::high_resolution_clock::now(); \
						    LOG_INFO("Execution time: %lld milliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

#define TEST_TIMER_S_END    auto end = std::chrono::high_resolution_clock::now(); \
						    LOG_INFO("Execution time: %lld second", std::chrono::duration_cast<std::chrono::seconds>(end - start).count());