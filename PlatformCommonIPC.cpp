#include "PlatformCommonIPC.h"
#include "PlatformCommonUtils.h"
using namespace std;
using namespace PlatformCommonUtils;

#ifdef WIN32
#include <Windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif // WIN32

class IInterProcessCommunitor
{
public:
    virtual ~IInterProcessCommunitor(){}
	virtual bool start() = 0;
	virtual bool sentData(const std::string& data) = 0;
	virtual bool receiveData(std::string& data) = 0;
	virtual void stop() = 0;
    
    virtual void setErrorCode(int error) { m_error = error; }
    virtual int getErrorCode() { return m_error; }

    virtual void setTimeout(uint32_t timeout) { m_timeout = timeout; }
    virtual uint32_t getTimeout() { return m_timeout; }
    
private:
    int m_error = 0;
    uint32_t m_timeout = 60;
};

class IPCWithNamedPipe : public IInterProcessCommunitor
{
public:
	IPCWithNamedPipe(
		const string& processPath, 
		const string& pipeName) :
		m_pipeName(pipeName), m_processPath(processPath)
	{
#ifdef WIN32
		memset(&m_pi, 0, sizeof(PROCESS_INFORMATION));
#endif
	}

	bool start() override {
#ifdef WIN32
        setErrorCode(0);
        stop();
		STARTUPINFOW si;
		memset(&si, 0, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
        auto wpath = utf8_to_wchar(m_processPath.c_str());
		if (!CreateProcessW(nullptr, wpath.get(), nullptr, nullptr, false, 0, nullptr, nullptr, &si, &m_pi)) {
            LOG_ERROR("Failed to create process with error code %d.", GetLastError());
			return false;
		}
		string realPipeName = "\\\\.\\pipe\\" + m_pipeName;
        auto wPipeName = utf8_to_wchar(realPipeName.c_str());
		DWORD code;
		while (GetExitCodeProcess(m_pi.hProcess, &code) && code == STILL_ACTIVE && m_hPipe == INVALID_HANDLE_VALUE) {
			m_hPipe = CreateFileW(wPipeName.get(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			Sleep(50);
		}
		if (m_hPipe == INVALID_HANDLE_VALUE) {
			stop();
			return false;
		}
        return true;
#endif
        return false;
	}

	bool sentData(const std::string& data) override {
#ifdef WIN32
        setErrorCode(0);
        if (m_hPipe == INVALID_HANDLE_VALUE) {
            return false;
        }

        COMMTIMEOUTS timeouts = { 0, //interval timeout. 0 = not used
                   0, // read multiplier
                   0, // read constant (milliseconds)
                   0, // Write multiplier
                   1000 * getTimeout()  // Write Constant
        };
        SetCommTimeouts(m_hPipe, &timeouts);
		DWORD dwBytesWritten;
		if (!WriteFile(m_hPipe, data.c_str(), data.size(), &dwBytesWritten, nullptr)) {
			DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT) {
                setErrorCode(IPC_E_TIMEOUT);
            }
			LPSTR messageBuffer = nullptr;
			size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, nullptr);
			std::string message(messageBuffer, size);
			LocalFree(messageBuffer);
			LOG_INFO("Exit pipe with message : %s", message.c_str());
			return false;
		}
		return true;
#endif
        return false;
	}

	bool receiveData(std::string& data) override {
#ifdef WIN32
        setErrorCode(0);
        if (m_hPipe == INVALID_HANDLE_VALUE) {
            return false;
        }

		DWORD dwBytesRead;
		char buffer[1024] = { 0 };
        COMMTIMEOUTS timeouts = { 0, //interval timeout. 0 = not used
                           0, // read multiplier
                           1000 * getTimeout(), // read constant (milliseconds)
                           0, // Write multiplier
                           0  // Write Constant
        };
        SetCommTimeouts(m_hPipe, &timeouts);
		if (!ReadFile(m_hPipe, buffer, 1024, &dwBytesRead, nullptr)) {
			DWORD error = GetLastError();
            if (error == ERROR_TIMEOUT) {
                setErrorCode(IPC_E_TIMEOUT);
            }
			LPSTR messageBuffer = nullptr;
			size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, nullptr);
			std::string message(messageBuffer, size);
			LocalFree(messageBuffer);
			LOG_INFO("Exit pipe with message : %s", message.c_str());
			return false;
		}
        data = string{buffer};
        return true;
#endif
        return false;
	}

	void stop() override {
#ifdef WIN32
		if (m_hPipe != nullptr) {
			DisconnectNamedPipe(m_hPipe);
			CloseHandle(m_hPipe);
			m_hPipe = INVALID_HANDLE_VALUE;
		}
		if (m_pi.hProcess != nullptr) {
			TerminateProcess(m_pi.hProcess, 0);
			CloseHandle(m_pi.hProcess);
			if (m_pi.hThread) {
				CloseHandle(m_pi.hThread);
			}
			memset(&m_pi, 0, sizeof(PROCESS_INFORMATION));
		}
#endif
	}

private:
	string m_processPath;
	string m_pipeName;
	string m_cachePath;

#ifdef WIN32
	PROCESS_INFORMATION m_pi;
	HANDLE m_hPipe = INVALID_HANDLE_VALUE;
#endif
};

class IPCWithFIFO : public IInterProcessCommunitor
{
public:
    IPCWithFIFO(
                const std::string& wfifo,
                const std::string& rfifo,
                const std::string processPath
   ): m_wfifo(wfifo), m_rfifo(rfifo), m_processPath(processPath), m_pid(-1), m_fdW(-1), m_fdR(-1)
   {}
    
    
    bool start() override
    {
#ifndef WIN32
        stop();
        // for unix
        setErrorCode(0);
        int res = mkfifo(m_wfifo.c_str(), 0666);
        if (res < 0) {
            LOG_ERROR("Failed to make write fifo with error %s\n", strerror(errno));
            return false;
        }
        res = mkfifo(m_rfifo.c_str(), 0666);
        if (res < 0) {
            LOG_ERROR("Failed to make read fifo with error %s\n", strerror(errno));
            return false;
        }
        m_pid = execute_process(m_processPath);
        if (m_pid < 0) {
            remove_file(m_wfifo.c_str());
            remove_file(m_rfifo.c_str());
            return false;
        }
        return true;
#endif
        return false;
    }
    
    bool sentData(const std::string& data) override {
#ifndef WIN32
        setErrorCode(0);
        int fd = open(m_wfifo.c_str(), O_WRONLY | O_NONBLOCK);
        m_fdW = fd;
        if (fd < 0) {
            LOG_ERROR("Open write pipe failed with error %s\n", strerror(errno));
            return false;
        }
        struct timeval timeout;
        fd_set set;
        FD_ZERO(&set); /* clear the set */
        FD_SET(fd, &set);
        timeout.tv_sec = getTimeout();
        timeout.tv_usec = 0;
        int rv = select(fd + 1, nullptr, &set, nullptr, &timeout);
        bool res = false;
        if(rv == -1) {
            LOG_ERROR("Select file failed");
        }
        else if(rv == 0) {
            LOG_INFO("sent timeout"); /* a timeout occured */
            setErrorCode(IPC_E_TIMEOUT);
        }
        else {
            ssize_t size = write(fd, data.c_str(), data.size());
            res = size > 0;
        }
        close(fd);
        m_fdW = -1;
        return res;
#endif
        return false;
    }
    
    bool receiveData(std::string& data) override {
#ifndef WIN32
        setErrorCode(0);
        int fd = open(m_rfifo.c_str(), O_RDONLY | O_NONBLOCK);
        m_fdR = fd;
        if (fd < 0) {
            LOG_ERROR("Open write pipe failed with error %s\n", strerror(errno));
            return false;
        }
        struct timeval timeout;
        fd_set set;
        FD_ZERO(&set); /* clear the set */
        FD_SET(fd, &set);
        timeout.tv_sec = getTimeout();
        timeout.tv_usec = 0;
        int rv = select(fd + 1, &set, nullptr, nullptr, &timeout);
        bool res = false;
        if(rv == -1) {
            LOG_ERROR("Select failed");
        }
        else if(rv == 0) {
            LOG_INFO("receive timeout"); /* a timeout occured */
            setErrorCode(IPC_E_TIMEOUT);
        }
        else {
            char buffer[1024] = { 0 };
            ssize_t count = read(fd, buffer, sizeof(buffer)); /* there was data to read */
            if (count > 0) {
                data = string(buffer);
                res = true;
            }
        }
        close(fd);
        m_fdR = -1;
        return res;
#endif
        return false;
    }

    void stop() override {
#ifndef WIN32
        if (m_pid > 0) {
            kill(m_pid, SIGKILL);
            m_pid = -1;
        }
        if (m_fdR > 0) {
            close(m_fdR);
            m_fdR = -1;
        }
        if (m_fdW > 0) {
            close(m_fdW);
            m_fdW = -1;
        }
        remove_file(m_wfifo.c_str());
        remove_file(m_rfifo.c_str());
#endif
    }
    
private:
    std::string m_wfifo;
    std::string m_rfifo;
    std::string m_processPath;
    int m_fdW;
    int m_fdR;
    int m_pid;
};

PlatformCommonIPC::PlatformCommonIPC(const std::string& processPath, const std::string& cachePath, IPCMethod method):
	m_cachePath(cachePath),
	m_processPath(processPath),
	m_ipcMethod(method)
{
}

PlatformCommonIPC::~PlatformCommonIPC()
{
	stop();
}

void PlatformCommonIPC::setPipeName(const std::string& pipeName)
{
	m_pipeName = pipeName;
}

bool PlatformCommonIPC::start()
{
	stop();
	if (m_ipcMethod == Namedpipe) {
		m_pIPCCommtor = new IPCWithNamedPipe(m_processPath, m_pipeName);
    } else if (m_ipcMethod == FIFO) {
        m_pIPCCommtor = new IPCWithFIFO(m_wfifo, m_rfifo, m_processPath);
    }
	if (m_pIPCCommtor != nullptr) {
		return m_pIPCCommtor->start();
	}
	return false;
}

bool PlatformCommonIPC::sentData(const std::string& data)
{
	return m_pIPCCommtor != nullptr ? m_pIPCCommtor->sentData(data) : false;
}

bool PlatformCommonIPC::receiveData(std::string& data)
{
	return m_pIPCCommtor != nullptr ? m_pIPCCommtor->receiveData(data) : false;
}

void PlatformCommonIPC::stop()
{
	if (m_pIPCCommtor != nullptr) {
		m_pIPCCommtor->stop();
		delete m_pIPCCommtor;
		m_pIPCCommtor = nullptr;
	}
}

int PlatformCommonIPC::getErrorCode() const
{
	return m_pIPCCommtor->getErrorCode();
}

void PlatformCommonIPC::setRWTimeout(uint32_t timeout)
{
    m_timeout = timeout;
}

void PlatformCommonIPC::setFIFOFileName(const std::string &wfifo, const std::string &rfifo) { 
    m_wfifo = wfifo;
    m_rfifo = rfifo;
}

