#include "PlatformCommonUtils.h"
#include <dirent.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <array>

#ifdef WIN32
#include <Windows.h>
#include <Psapi.h>
#include <shlwapi.h>
#include <TlHelp32.h>
#pragma comment(lib, "Shlwapi.lib")
#pragma warning(disable : 4996)
#else
#include <mach-o/dyld.h>
#include <iconv.h>
#include <spawn.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <libproc.h>
#endif

using namespace std;
namespace fs = std::filesystem;

static bool s_is_debug = false;

static std::string s_log_tag_name = "DEV";

static std::pair<PlatformCommonUtils::log_info_callback, void*> s_log_cb{ nullptr, nullptr };

static char* dirname(char* path)
{
	static char buffer[260];
	size_t len;
	if (path == nullptr) {
		strcpy(buffer, ".");
		return buffer;
	}
	len = strlen(path);
	assert(len < sizeof(buffer));
	if (len != 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
		--len;
	}
	while (len != 0 && path[len - 1] != '/' && path[len - 1] != '\\') {
		--len;
	}
	if (len == 0) {
		strcpy(buffer, ".");
	}
	else if (len == 1) {
		if (path[0] == '/' || path[0] == '\\') {
			strcpy(buffer, "/");
		}
		else {
			strcpy(buffer, ".");
		}
	}
	else {
		memcpy(buffer, path, len - 1);
		buffer[len] = '\0';
	}
	return buffer;
}

#ifdef WIN32
static int win32err_to_errno(int err_value)
{
	switch (err_value) {
	case ERROR_FILE_NOT_FOUND:
		return ENOENT;
	case ERROR_ALREADY_EXISTS:
		return EEXIST;
	default:
		return EFAULT;
	}
}
#endif

static char* string_build_path(const char* elem, ...)
{
	if (!elem)
		return nullptr;

	va_list args;
	size_t len = strlen(elem) + 1;
	va_start(args, elem);
	char* arg = va_arg(args, char*);
	while (arg) {
		len += strlen(arg) + 1;
		arg = va_arg(args, char*);
	}
	va_end(args);

	char* out = (char*)malloc(len);
	if (!out) {
		return nullptr;  // 分配失败
	}
	strcpy(out, elem);

	va_start(args, elem);
	arg = va_arg(args, char*);

	while (arg) {
#ifdef WIN32
		strcat(out, "\\");
#else
		strcat(out, "/");
#endif
		strcat(out, arg);

		arg = va_arg(args, char*);
	}
	va_end(args);
	return out;
}

// Macos: pid, path, args...
// Windows: PPROCESSENTRY32, args...
template<typename Func, typename... Args>
static bool traverse_process(Func&& cb, Args&&... args)
{
#ifdef WIN32
	if (cb == nullptr) return false;
	STARTUPINFO st;
	PROCESS_INFORMATION pi;
	PROCESSENTRY32W ps;
	HANDLE hSnapshot = INVALID_HANDLE_VALUE;
	memset(&st, 0, sizeof(STARTUPINFO));
	st.cb = sizeof(STARTUPINFO);
	memset(&ps, 0, sizeof(PROCESSENTRY32));
	ps.dwSize = sizeof(PROCESSENTRY32);
	memset(&pi, 0, sizeof(PROCESS_INFORMATION));

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) return false;
	if (!Process32FirstW(hSnapshot, &ps)) return false;
	do {
		if (cb(&ps, std::forward<Args>(args)...)) {
			break;
		}
	} while (Process32NextW(hSnapshot, &ps));
	CloseHandle(hSnapshot);
	return true;
#else
	pid_t pid;
	int buffer_size = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
	pid_t* pids = (pid_t*)malloc(buffer_size);
	proc_listpids(PROC_ALL_PIDS, 0, pids, buffer_size);
	int n = buffer_size / sizeof(pid_t);
	for (int i = 0; i < n; i++) {
		pid = pids[i];
		if (pid == 0) continue;
		char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
		if (proc_pidpath(pid, pathbuf, sizeof(pathbuf)) > 0) {
			if (cb(pid, pathbuf, std::forward<Args>(args)...)) {
				break;
			}
		}
	}
	free(pids);
	return true;
#endif
}


std::shared_ptr<wchar_t> PlatformCommonUtils::utf8_to_wchar(const char* data)
{
	std::shared_ptr<wchar_t> retData = nullptr;
#ifdef WIN32
	int len = MultiByteToWideChar(CP_UTF8, 0, data, -1, nullptr, 0);
	if (len <= 0) return retData;
	retData = std::shared_ptr<wchar_t>(new wchar_t[len], [](wchar_t* p) { delete[] p; });
	int res = MultiByteToWideChar(CP_UTF8, 0, data, -1, retData.get(), len);
	if (res <= 0) {
		return nullptr;
	}
#else
/*
	char* buffer = (char*)data;
	int bufsize = strlen(buffer) + 1;

	iconv_t ic = iconv_open("UTF-32LE", "UTF-8");
	if (ic != (iconv_t)-1)
	{
		char* tmp = new char[bufsize * 4];
		memset(tmp, 0, bufsize * 4);
		char* src = (char*)buffer;
		char* dst = (char*)tmp;
		size_t srclen = bufsize - 1;
		size_t dstlen = bufsize * 4;
		iconv(ic, &src, &srclen, &dst, &dstlen);
		if (srclen == 0) // all ok
		{
			retData = std::shared_ptr<wchar_t>(reinterpret_cast<wchar_t*>(tmp), [](wchar_t* p) { delete[] p; });
		}
		else
		{
			delete[] tmp;
		}
		iconv_close(ic);
	}
 */
#endif
	return retData;
}

std::shared_ptr<char> PlatformCommonUtils::wchar_to_utf8(const wchar_t* data)
{
	std::shared_ptr<char> retData = nullptr;
#ifdef WIN32
	int len = WideCharToMultiByte(CP_UTF8, 0, data, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) return retData;
	retData = std::shared_ptr<char>(new char[len], [](char* p) { delete[] p; });
	int res = WideCharToMultiByte(CP_UTF8, 0, data, -1, retData.get(), len, nullptr, nullptr);
	if (res <= 0) {
		return nullptr;
	}
#else
/*
	wchar_t* buffer = (wchar_t*)data;
	int bufsize = wcslen(buffer) + 1;

	iconv_t ic = iconv_open("UTF-8", "UTF-32LE");
	if (ic != (iconv_t)-1)
	{
		char* tmp = new char[bufsize * 3];
		memset(tmp, 0, bufsize * 3);
		char* src = (char*)buffer;
		char* dst = (char*)tmp;
		size_t srclen = bufsize * 4 - 4;
		size_t dstlen = bufsize * 3;
		iconv(ic, &src, &srclen, &dst, &dstlen);
		if (srclen == 0) // all ok
		{
			retData = std::shared_ptr<char>(reinterpret_cast<char*>(tmp), [](char* p) { delete[] p; });
		}
		else
		{
			delete[] tmp;
		}
		iconv_close(ic);
	}
 */
#endif
	return retData;
}

std::shared_ptr<char> PlatformCommonUtils::utf8_to_local_encoding(const char* utf8Str)
{
#ifdef WIN32
	int wideCharSize = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, nullptr, 0);
	if (wideCharSize == 0)
	{
		return nullptr;  // Handle error
	}

	wchar_t* wideStr = new wchar_t[wideCharSize];
	MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, wideCharSize);

	// Step 2: Convert Unicode (wchar_t) to local encoding (ANSI)
	int ansiSize = WideCharToMultiByte(CP_ACP, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
	if (ansiSize == 0)
	{
		delete[] wideStr;
		return nullptr;  // Handle error
	}

	//char* ansiStr = new char[ansiSize];
	std::shared_ptr<char> ansiStr(new char[ansiSize], [](char* p) { delete[] p; });
	WideCharToMultiByte(CP_ACP, 0, wideStr, -1, ansiStr.get(), ansiSize, nullptr, nullptr);
	// Clean up
	delete[] wideStr;

	return ansiStr;
#else
	return nullptr;
#endif
}

bool PlatformCommonUtils::compare_string_insensitive(const std::string& str1, const std::string& str2)
{
	if (str1.length() != str2.length())
		return false;

	for (size_t i = 0; i < str1.length(); ++i) {
		if (tolower(str1[i]) != tolower(str2[i]))
			return false;
	}
	return true;
}

bool PlatformCommonUtils::path_is_dir(const char* path)
{
#ifdef WIN32
	auto wPath = utf8_to_wchar(path);
	struct _stat buffer;
	bool exists = (_wstat(wPath.get(), &buffer) == 0);
	if (exists) {
		return S_ISDIR(buffer.st_mode);
	}
#else
	struct stat buffer;
	bool exists = (stat(path, &buffer) == 0);
	if (exists) {
		return S_ISDIR(buffer.st_mode);
	}
#endif
	return false;
}

bool PlatformCommonUtils::path_is_file(const char* path)
{
#ifdef WIN32
	auto wPath = utf8_to_wchar(path);
	struct _stat buffer;
	bool exists = (_wstat(wPath.get(), &buffer) == 0);
	if (exists) {
		return S_ISREG(buffer.st_mode);
	}
	else {
		LOG_INFO("Path is no exist %s", path);
	}
	
#else
	struct stat buffer;
	bool exists = (stat(path, &buffer) == 0);
	if (exists) {
		return S_ISREG(buffer.st_mode);
	}
	else {
		LOG_INFO("Path is no exist %s", path);
	}
#endif
	return false;
}

size_t PlatformCommonUtils::get_file_size(const char* path)
{
#ifdef WIN32
	auto wPath = utf8_to_wchar(path);
	struct _stat buffer;
	bool exists = (_wstat(wPath.get(), &buffer) == 0);
	if (exists) {
		return buffer.st_size;
	}
#else
	struct stat buffer;
	bool exists = (stat(path, &buffer) == 0);
	if (exists) {
		return buffer.st_size;
	}
#endif // WIN32
	return 0;
}

bool PlatformCommonUtils::path_exisit(const char* path)
{
#ifdef WIN32
	auto wPath = utf8_to_wchar(path);
	struct _stat buffer;
	return (_wstat(wPath.get(), &buffer) == 0);
#else
	struct stat buffer;
	return (stat(path, &buffer) == 0);
#endif 
}

bool PlatformCommonUtils::path_exisit(const std::string& path)
{
	return path_exisit(path.c_str());
}

#ifdef WIN32
bool PlatformCommonUtils::path_exisit(const std::wstring& path) {
	return path_exisit(path.c_str());
}

bool PlatformCommonUtils::path_exisit(const wchar_t* path) {
	struct _stat buffer;
	return (_wstat(path, &buffer) == 0);
}
#endif // WIN32

bool PlatformCommonUtils::make_directory(const char* path, int mode)
{
	if (path_exisit(path)) {
		return true;
	}

#ifdef WIN32
	// CreateDirectory returns a non-zero value on success,
	// and zero on failure
	// https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectorya
	auto _path = utf8_to_wchar(path);
	BOOL res = CreateDirectoryW(_path.get(), nullptr);
	return res;
#else
	// mkdir returns zero on success, and an error code on failure
	// https://linux.die.net/man/2/mkdir
    int r = mkdir(path, mode);
	return r == 0;
#endif
}

bool PlatformCommonUtils::remove_directory(const char* path)
{
	if (!path_exisit(path)) {
		return true;
	}

	int e = 0;
#ifdef WIN32
	auto _path = utf8_to_wchar(path);
	if (!RemoveDirectoryW(_path.get())) {
		e = win32err_to_errno(GetLastError());
	} 
#else
	if (remove(path) < 0) {
		e = errno;
	}
#endif
	if (e != 0) {
		LOG_INFO("REMOVE FAILED %s", path);
	}
	return e == 0;
}

bool PlatformCommonUtils::remove_file(const char* path)
{
	if (!path_exisit(path)) {
		return true;
	}

	bool res = false;
#ifdef WIN32
	auto _path = utf8_to_wchar(path);
	res = std::filesystem::remove(_path.get());
#else
    res = remove(path) == 0;
#endif // WIN32
	return res;
}

void PlatformCommonUtils::scan_directory(const std::string& path, std::vector<std::string>& files, std::vector<std::string>& directories)
{
#if 0 
	// impl scan_directory with c++ 17
	// macos10.15 no support this... shit!
	try {
		for (const auto& entry : fs::directory_iterator(path)) {
			if (entry.path().filename() == "." || entry.path().filename() == "..") {
				continue;
			}
			std::string fpath = entry.path().string();
			if (fs::exists(fpath)) {
				if (fs::is_directory(fpath)) {
					directories.push_back(fpath);
					scan_directory(fpath, files, directories);
				}
				else {
					files.push_back(fpath);
				}
			}
		}
	}
	catch (const fs::filesystem_error& e) {
		LOG_ERROR("%s", e.what());
	}
#else
    struct entry* _files = nullptr;
    struct entry* _dirs = nullptr;
    scan_directory(path.c_str(), &_files, &_dirs);
    struct entry* ent = _files;
    while (ent != nullptr) {
        struct entry* del = ent;
        if (ent->name != nullptr) {
            files.push_back(std::string(ent->name));
            free(ent->name);
        }
        ent = ent->next;
        free(del);
    }
    
    ent = _dirs;
    while (ent != nullptr) {
        struct entry* del = ent;
        if (ent->name != nullptr) {
            directories.push_back(std::string(ent->name));
            free(ent->name);
        }
        ent = ent->next;
        free(del);
    }
#endif
}

void PlatformCommonUtils::scan_directory(const char* path, entry** files, entry** directories)
{
	DIR* cur_dir = opendir(path);
	if (cur_dir) {
		struct dirent* ep;
		while ((ep = readdir(cur_dir))) {
			if ((strcmp(ep->d_name, ".") == 0) || (strcmp(ep->d_name, "..") == 0)) {
				continue;
			}
			char* fpath = string_build_path(path, ep->d_name, nullptr);
			if (fpath) {
				if (!path_exisit(fpath)) return;
				if (path_is_dir(fpath)) {
					struct entry* ent = (entry*)malloc(sizeof(struct entry));
					if (!ent) return;
					ent->name = fpath;
					ent->next = *directories;
					*directories = ent;
					scan_directory(fpath, files, directories);
					fpath = nullptr;
				}
				else {
					struct entry* ent = (entry*)malloc(sizeof(struct entry));
					if (!ent) return;
					ent->name = fpath;
					ent->next = *files;
					*files = ent;
					fpath = nullptr;
				}
			}
		}
		closedir(cur_dir);
	}
}

bool PlatformCommonUtils::rmdir_recursive(const char* path)
{
	if (!path_exisit(path)) {
		return true;
	}
#ifdef WIN32
    uintmax_t remove_cnt = 0;
	auto _path = utf8_to_wchar(path);
	remove_cnt = fs::remove_all(_path.get());
    return remove_cnt > 0;
#else
	bool res = true;
	struct entry* files = nullptr;
	struct entry* directories = nullptr;
	struct entry* ent;

	ent = (entry*)malloc(sizeof(struct entry));
	ent->name = strdup(path);
	ent->next = nullptr;
	directories = ent;

	scan_directory(path, &files, &directories);

	ent = files;
	while (ent) {
		struct entry* del = ent;
		res &= remove_file(ent->name);
		free(ent->name);
		ent = ent->next;
		free(del);
	}
	ent = directories;
	while (ent) {
		struct entry* del = ent;
		res &= remove_directory(ent->name);
		free(ent->name);
		ent = ent->next;
		free(del);
	}
	return res;
#endif // WIN32
}

bool PlatformCommonUtils::mkfile_with_parents(const char* file)
{
	if (file == nullptr) {
		return false;
	}
	if (path_exisit(file)) {
		return true;
	}

	std::string filePath(file);
	size_t pos = filePath.find_last_of("\\/");
	if (pos == std::string::npos) {
		return false;
	}
	std::string dirPath = filePath.substr(0, pos);
	if (!path_exisit(dirPath)) {
		if (!mkdir_with_parents(dirPath.c_str(), 0755)) {
			return false;
		}
	}
	FILE* pFile = open_file(file, "wb");
	if (pFile) {
		fclose(pFile);
		return true;
	}
	return false;
}

void PlatformCommonUtils::copy_file_by_path(const char* src, const char* dst)
{
	FILE* from, * to;
	char buf[BUFSIZ];
	size_t length;

	/* open source file */
	from = open_file(src, "rb");
	if (from == nullptr) {
		LOG_ERROR("Cannot open source path '%s'", src);
		return;
	}
	
	/* open destination file */
	to = open_file(dst, "wb");
	if (to == nullptr) {
		LOG_ERROR("Cannot open destination path '%s'", dst);
		fclose(from);
		return;
	}

	/* copy the file */
	while ((length = fread(buf, 1, BUFSIZ, from)) != 0) {
		fwrite(buf, 1, length, to);
	}

	if (fclose(from) == EOF) {
		LOG_ERROR("closing source file.");
	}

	if (fclose(to) == EOF) {
		LOG_ERROR("closing destination file.");
	}
}

void PlatformCommonUtils::copy_directory_by_path(const char* src, const char* dst)
{
	if (!src || !dst) {
		return;
	}

	/* if src does not exist */
	if (!path_is_dir(src)) {
		LOG_ERROR("Source directory does not exist '%s': %s (%d)", src, strerror(errno), errno);
		return;
	}

	/* if dst directory does not exist */
	if (!path_is_dir(dst)) {
		/* create it */
		if (!mkdir_with_parents(dst, 0755)) {
			LOG_ERROR("Unable to create destination directory '%s': %s (%d)", dst, strerror(errno), errno);
			return;
		}
	}

	/* loop over src directory contents */
	DIR* cur_dir = opendir(src);
	if (cur_dir) {
		struct dirent* ep;
		while ((ep = readdir(cur_dir))) {
			if ((strcmp(ep->d_name, ".") == 0) || (strcmp(ep->d_name, "..") == 0)) {
				continue;
			}
			char* srcpath = string_build_path(src, ep->d_name, nullptr);
			char* dstpath = string_build_path(dst, ep->d_name, nullptr);
			if (srcpath && dstpath) {
				/* copy file */
				copy_file_by_path(srcpath, dstpath);
			}

			if (srcpath)
				free(srcpath);
			if (dstpath)
				free(dstpath);
		}
		closedir(cur_dir);
	}
}

bool PlatformCommonUtils::mkdir_with_parents(const char* dir, int mode)
{
	if (!dir) return false;
	if (make_directory(dir, mode)) {
		return true;
	}
	else {
#if WIN32
		int lastError = GetLastError();

		if (lastError == ERROR_ALREADY_EXISTS || lastError == ERROR_SUCCESS) return 0;
#else
		if (errno == EEXIST) return 0;
#endif
	}
	bool res;
#ifdef WIN32
	char* parent = _strdup(dir);
#else
	char* parent = strdup(dir);
#endif
	char* parentdir = dirname(parent);
	if (parentdir) {
		res = mkdir_with_parents(parentdir, mode);
	}
	else {
		res = false;
	}
	free(parent);
	if (res) {
		mkdir_with_parents(dir, mode);
	}
	return res;
}

const char* PlatformCommonUtils::extract_filename(const char* filepath)
{
	if (filepath == nullptr) {
		return "";
	}

	const char* lastSlash = std::strrchr(filepath, '/');
	const char* lastBackslash = std::strrchr(filepath, '\\');

	const char* lastSeparator = (lastSlash > lastBackslash) ? lastSlash : lastBackslash;

	if (lastSeparator != nullptr) {
		return lastSeparator + 1;
	}
	else {
		return filepath;
	}
}

std::string PlatformCommonUtils::extract_filename(const std::string& filePath)
{
	return std::string{ extract_filename(filePath.c_str()) };
	//return fs::path(filePath).filename().string();
}

FILE* PlatformCommonUtils::open_file(const char* path, const char* mode)
{
#ifdef WIN32
	auto _path = utf8_to_wchar(path);
	auto _mode = utf8_to_wchar(mode);
	return _wfopen(_path.get(), _mode.get());
#else
	return fopen(path, mode);
#endif // WIN32
}

void PlatformCommonUtils::mutex_lock(mutex_t mutex)
{
#ifdef WIN32
	EnterCriticalSection(mutex);
#else 
	pthread_mutex_lock(&mutex);
#endif
}

void PlatformCommonUtils::mutex_unlock(mutex_t mutex)
{
#ifdef WIN32
	LeaveCriticalSection(mutex);
#else 
	pthread_mutex_unlock(&mutex);
#endif
}

std::vector<std::string> PlatformCommonUtils::str_split(const std::string& s, char seperator)
{
	std::vector<std::string> output;
	std::string::size_type prev_pos = 0, pos = 0;
	while ((pos = s.find(seperator, pos)) != std::string::npos)
	{
		std::string substring(s.substr(prev_pos, pos - prev_pos));
		output.push_back(substring);
		prev_pos = ++pos;
	}
	auto last_word_length = s.length() - prev_pos;
	if (last_word_length > 0) {
		output.push_back(s.substr(prev_pos, pos - prev_pos)); // Last word
	}
	return output;
}

std::vector<std::string> PlatformCommonUtils::str_split(const std::string& s, const std::string& separator) {
	std::vector<std::string> output;
	std::string::size_type prev_pos = 0, pos = 0;

	while ((pos = s.find(separator, prev_pos)) != std::string::npos) {
		std::string substring(s.substr(prev_pos, pos - prev_pos));
		output.push_back(substring);
		prev_pos = pos + separator.length();
	}
	auto last_word_length = s.length() - prev_pos;
	if (last_word_length > 0) {
		output.push_back(s.substr(prev_pos, pos - prev_pos)); // Last word
	}
	return output;
}

std::string PlatformCommonUtils::wstring_to_string(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();

	std::mbstate_t state = std::mbstate_t();
	const wchar_t* src = wstr.data();
	std::size_t len = 1 + std::wcsrtombs(nullptr, &src, 0, &state);

	if (len == static_cast<std::size_t>(-1)) {
		throw std::runtime_error("Conversion failed");
	}

	std::vector<char> buffer(len);
	std::wcsrtombs(buffer.data(), &src, buffer.size(), &state);

	return std::string(buffer.data());
}

void PlatformCommonUtils::set_log_info_callback(log_info_callback cb, void* user_data, bool debug)
{
	s_log_cb.first = cb;
	s_log_cb.second = user_data;
	set_debug_output(debug);
}

std::pair<PlatformCommonUtils::log_info_callback, void*> PlatformCommonUtils::get_log_info_cb()
{
	return s_log_cb;
}

void PlatformCommonUtils::set_log_tag_name(const std::string& name)
{
	s_log_tag_name = name;
}

std::string PlatformCommonUtils::get_log_tag_name()
{
	return s_log_tag_name;
}

void PlatformCommonUtils::set_debug_output(bool bDebug)
{
	s_is_debug = bDebug;
}

bool PlatformCommonUtils::is_debug()
{
#ifdef _DEBUG
	return true;
#else
	return s_is_debug;
#endif // _DEBUG
}

std::string PlatformCommonUtils::get_current_time()
{
	char time_buffer[20] = { 0 };
	auto now = std::chrono::system_clock::now();
	std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
	return std::string(time_buffer);
}

int PlatformCommonUtils::get_current_thread_id()
{
#ifdef WIN32
	return GetCurrentThreadId();
#else
	return mach_thread_self();
#endif // WIN32
}

bool PlatformCommonUtils::execute_process(const std::string& cmd, std::string& revMsg, int* exitCode)
{
#ifdef WIN32
	//LOG_INFO_D("excute cmd: %s", cmd.c_str());
	SECURITY_ATTRIBUTES saAttr;
	ZeroMemory(&saAttr, sizeof(SECURITY_ATTRIBUTES));
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = true;
	saAttr.lpSecurityDescriptor = nullptr;

	HANDLE hChildStd_OUT_Rd = nullptr;
	HANDLE hChildStd_OUT_Wr = nullptr;

	PROCESS_INFORMATION piProcInfo;
	STARTUPINFOW siStartInfo;
	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFOW));

	bool bSuccess = false;

	if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) {
		LOG_ERROR("CreatePipe failed with error: %d", GetLastError());
		return false;
	}

	if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
		LOG_ERROR("SetHandleInformation failed with error: %d", GetLastError());
		return false;
	}

	siStartInfo.cb = sizeof(STARTUPINFOW);
	siStartInfo.hStdError = hChildStd_OUT_Wr;
	siStartInfo.hStdOutput = hChildStd_OUT_Wr;
	siStartInfo.dwFlags |= STARTF_USESHOWWINDOW;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
	siStartInfo.wShowWindow = SW_HIDE;

	auto _cmd = utf8_to_wchar(cmd.c_str());
	bSuccess = CreateProcessW(
		nullptr,
		_cmd.get(),
		nullptr,
		nullptr,
		TRUE,
		0,
		nullptr,
		nullptr,
		&siStartInfo,
		&piProcInfo
	);

	if (!bSuccess) {
		LOG_ERROR("CreateProcess failed with error: %d", GetLastError());
		return false;
	}
	else {
		CloseHandle(piProcInfo.hThread);
		CloseHandle(hChildStd_OUT_Wr);
	}

	DWORD dwRead;
	char* chBuf = new char[4096];
	revMsg = "";
	bSuccess = false;
	while (1) {
		bSuccess = ReadFile(hChildStd_OUT_Rd, chBuf, 4096, &dwRead, nullptr);
		if (!bSuccess || dwRead == 0) {
			DWORD err = GetLastError();
			if (err != 109) {
				LOG_INFO("ReadFile quit code %d", err);
			}
			break;
		}
		std::string chunk(chBuf, dwRead);
		revMsg += chunk;
	}
	delete[] chBuf;
	//LOG_INFO_D("----->MSG SIZE: %d", revMsg.size());
	WaitForSingleObject(piProcInfo.hProcess, INFINITE);
	if (exitCode != nullptr) {
		GetExitCodeProcess(piProcInfo.hProcess, (LPDWORD)exitCode);
	}
	CloseHandle(piProcInfo.hProcess);
	CloseHandle(hChildStd_OUT_Rd);
	return true;
#else
    std::array<char, 1024> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
		LOG_ERROR("popen() failed!");
        return false;
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    revMsg = result;
	if (exitCode != nullptr) {
		int _exitCode = pclose(pipe.get());
		*exitCode = WEXITSTATUS(_exitCode);
	}
    return true;
#endif // WIN32
}

int PlatformCommonUtils::execute_process(const std::string& cmd)
{
	int procID = -1;
#ifdef WIN32
	//LOG_INFO_D("excute cmd: %s", cmd.c_str());
	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = true;
	saAttr.lpSecurityDescriptor = nullptr;

	PROCESS_INFORMATION piProcInfo;
	STARTUPINFOW siStartInfo;
	bool bSuccess = false;

	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdError = nullptr;
	siStartInfo.hStdOutput = nullptr;
	siStartInfo.dwFlags |= STARTF_USESHOWWINDOW;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
	siStartInfo.wShowWindow = SW_HIDE;

	auto _cmd = utf8_to_wchar(cmd.c_str());
	bSuccess = CreateProcessW(
		nullptr,
		_cmd.get(),
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		nullptr,
		&siStartInfo,
		&piProcInfo
	);

	if (!bSuccess) {
		LOG_ERROR("CreateProcess failed with error: %d", GetLastError());
	}
	else {
		procID = piProcInfo.dwProcessId;
		CloseHandle(piProcInfo.hProcess);
		CloseHandle(piProcInfo.hThread);
	}

#else
    char **environ = nullptr;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
    
    pid_t pid;
    std::vector<char*> args;
    size_t first_space = cmd.find(' ');
    std::string program_path;
    std::string arguments;
    if (cmd[0] == '"') {
        size_t closing_quote = cmd.find('"', 1);
        if (closing_quote != std::string::npos) {
            program_path = cmd.substr(1, closing_quote - 1);
            arguments = cmd.substr(closing_quote + 2); // 提取后面的参数
        }
    } else {
        program_path = cmd.substr(0, first_space);
        arguments = cmd.substr(first_space + 1);// 提取后面的参数
    }
    args.push_back(program_path.data());
    std::vector<std::string> _cmd = str_split(arguments, ' ');
    for (auto& arg : _cmd) {
        if (arg != " ") {
            args.push_back(arg.data());
        }
    }
    args.push_back(nullptr);

    int status = posix_spawn(&pid, args[0], nullptr, &attr, args.data(), environ);
    if (status == 0) {
        procID = pid;
    } else {
		LOG_ERROR("posix_spawn failed with error: %d", status);
    }

    posix_spawnattr_destroy(&attr);
#endif 
	return procID;
}

bool PlatformCommonUtils::kill_process(const std::string& proc_path)
{
#if WIN32
	auto _proc_path = utf8_to_wchar(proc_path.c_str());
	wchar_t* file_name = PathFindFileNameW(_proc_path.get());
	bool res = false;
	static auto kill_func = [](PPROCESSENTRY32W ps, bool& res, wchar_t* file_name, std::shared_ptr<wchar_t> _proc_path) -> bool
	{
		if (0 == wcscmp(ps->szExeFile, file_name)) {
			HANDLE killHandle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION |   // Required by Alpha
				PROCESS_CREATE_THREAD |  // For CreateRemoteThread
				PROCESS_VM_OPERATION |   // For VirtualAllocEx/VirtualFreeEx
				PROCESS_VM_WRITE,        // For WriteProcessMemory);
				FALSE, ps->th32ProcessID);
			if (INVALID_HANDLE_VALUE != killHandle) {
				wchar_t path[MAX_PATH] = { 0 };
				GetModuleFileNameExW(killHandle, nullptr, path, MAX_PATH);
				fs::path _path1 = path;
				fs::path _path2 = _proc_path.get();
				if (fs::canonical(_path1) == fs::canonical(_path2)) { // format path
					res = TerminateProcess(killHandle, 0);
					LOG_INFO("kill process: %ls", path);
				}
			}
			return true;
		}
		return false;
	};
	if (!traverse_process(kill_func, res, file_name, _proc_path)) {
		return false;
	}
	return res;
#else
	bool res = false;
	auto kill_func = [](pid_t pid, const char* pathbuf, const std::string& proc_path, bool& bRes) -> bool
	{
		if (strcmp(pathbuf, proc_path.c_str()) == 0) {
			int res = kill(pid, SIGKILL);
			if (res == 0) {
				LOG_INFO("kill process: %s", pathbuf);
				bRes = true;
			}
			else {
				LOG_ERROR("ERROR KILL: %d", errno);
			}
			return true;
		}
		return false;
	};

	if (!traverse_process(kill_func, proc_path, res)) {
		return false;
	}
	return res;

#endif
}

bool PlatformCommonUtils::kill_process_by_name(const std::string& proc_name)
{
#if WIN32
	auto _proc_name = utf8_to_wchar(proc_name.c_str());
	bool res = false;
	static auto kill_func = [](PPROCESSENTRY32W ps, bool& res, std::shared_ptr<wchar_t> _proc_name) -> bool
	{
		if (0 == wcscmp(ps->szExeFile, _proc_name.get())) {
			HANDLE killHandle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION |   // Required by Alpha
				PROCESS_CREATE_THREAD |  // For CreateRemoteThread
				PROCESS_VM_OPERATION |   // For VirtualAllocEx/VirtualFreeEx
				PROCESS_VM_WRITE,        // For WriteProcessMemory);
				FALSE, ps->th32ProcessID);
			if (INVALID_HANDLE_VALUE != killHandle) {
				res = TerminateProcess(killHandle, 0);
				return true;
			}
		}
		return false;
	};
	if (!traverse_process(kill_func, res, _proc_name)) {
		return false;
	}
	return res;
#else
	bool res = false;
	auto kill_func = [](pid_t pid, const char* pathbuf, const std::string& proc_name, bool& bRes) -> bool
	{
		const char* file_name = extract_filename(pathbuf);
		if (strcmp(file_name, proc_name.c_str()) == 0) {
			int res = kill(pid, SIGKILL);
			if (res == 0) {
				LOG_INFO("kill process: %s", pathbuf);
				bRes = true;
			}
			else {
				LOG_ERROR("ERROR KILL: %d", errno);
			}
			return true;
		}
		return false;
	};

	if (!traverse_process(kill_func, proc_name, res)) {
		return false;
	}
	return res;
#endif
}

void PlatformCommonUtils::kill_process_completely(const std::string& proc_path)
{
	while (kill_process(proc_path));
}

void PlatformCommonUtils::kill_process_by_name_completely(const std::string& proc_name)
{
	while (kill_process_by_name(proc_name));
}

std::string PlatformCommonUtils::get_current_directory_path()
{
#ifdef WIN32
	char szCurPath[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, szCurPath);
	return string{ szCurPath };
#else
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    //std::vector<char> buffer(size);
    char buffer[size];
    if (_NSGetExecutablePath(buffer, &size) != 0) {
		LOG_ERROR("getting executable path");
        return std::string();
    }
    return std::string{buffer};
#endif
}

bool PlatformCommonUtils::is_process_running(int process_id)
{
#ifdef WIN32
	HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);
	if (hProc != nullptr) {
		CloseHandle(hProc);
		return true;
	}
#else
    if (kill(process_id, 0) == 0) {
        return true; // Process exists
    } else {
        if (errno == ESRCH) {
            return false; // No such process
        } else if (errno == EPERM) {
            return true; // Process exists, but no permission to signal it
        }
    }
#endif
	return false;
}

bool PlatformCommonUtils::is_process_running(const std::string& proc_path)
{
#if WIN32
	auto _proc_path = utf8_to_wchar(proc_path.c_str());
	wchar_t* file_name = PathFindFileNameW(_proc_path.get());
	bool res = false;
	static auto check_proc = [](PPROCESSENTRY32W ps, bool& res, wchar_t* file_name, std::shared_ptr<wchar_t> _proc_path) -> bool
	{
		if (0 == wcscmp(ps->szExeFile, file_name)) {
			HANDLE killHandle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION |   // Required by Alpha
				PROCESS_CREATE_THREAD |  // For CreateRemoteThread
				PROCESS_VM_OPERATION |   // For VirtualAllocEx/VirtualFreeEx
				PROCESS_VM_WRITE,        // For WriteProcessMemory);
				FALSE, ps->th32ProcessID);
			if (INVALID_HANDLE_VALUE != killHandle) {
				wchar_t path[MAX_PATH] = { 0 };
				GetModuleFileNameExW(killHandle, nullptr, path, MAX_PATH);
				fs::path _path1 = path;
				fs::path _path2 = _proc_path.get();
				if (fs::canonical(_path1) == fs::canonical(_path2)) { // format path
					res = true;
				}
			}
			return true;
		}
		return false;
	};
	if (!traverse_process(check_proc, res, file_name, _proc_path)) {
		return false;
	}
	return res;
#else
	bool res = false;
	auto check_proc = [](pid_t pid, const char* pathbuf, const std::string& proc_path, bool& bRes) -> bool
	{
		if (strcmp(pathbuf, proc_path.c_str()) == 0) {
			bRes = true;
			return true;
		}
		return false;
	};

	if (!traverse_process(check_proc, proc_path, res)) {
		return false;
	}
	return res;

#endif

}

bool PlatformCommonUtils::is_process_running_by_name(const std::string& proc_name)
{
#if WIN32
	auto _proc_name = utf8_to_wchar(proc_name.c_str());
	bool res = false;
	static auto check_proc = [](PPROCESSENTRY32W ps, bool& res, std::shared_ptr<wchar_t> _proc_name) -> bool
	{
		if (0 == wcscmp(ps->szExeFile, _proc_name.get())) {
			res = true;
			return true;
		}
		return false;
	};
	if (!traverse_process(check_proc, res, _proc_name)) {
		return false;
	}
	return res;
#else
	bool res = false;
	auto check_proc = [](pid_t pid, const char* pathbuf, const std::string& proc_name, bool& bRes) -> bool
	{
		const char* file_name = extract_filename(pathbuf);
		if (strcmp(file_name, proc_name.c_str()) == 0) {
			bRes = true;
			return true;
		}
		return false;
	};

	if (!traverse_process(check_proc, proc_name, res)) {
		return false;
	}
	return res;
#endif
}

#ifdef WIN32
void PlatformCommonUtils::usleep(uint32_t waitTime)
{
	LARGE_INTEGER perfCnt, start, now;
	QueryPerformanceFrequency(&perfCnt);
	QueryPerformanceCounter(&start);

	do {
		QueryPerformanceCounter((LARGE_INTEGER*)&now);
	} while ((now.QuadPart - start.QuadPart) / (float)(perfCnt.QuadPart) * 1000 * 1000 < waitTime);
}
#endif

void PlatformCommonUtils::msleep(uint32_t waitTime)
{
#ifdef WIN32
	Sleep(waitTime);
#else
	usleep(waitTime * 1000);
#endif // WIN32
}
