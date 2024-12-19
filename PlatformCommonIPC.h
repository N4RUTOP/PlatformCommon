#pragma once
#include <string>

#define IPC_E_TIMEOUT  1

class PlatformCommonIPC
{
public:
	enum IPCMethod
	{
		Namedpipe,
        FIFO
	};

	PlatformCommonIPC(
		const std::string& processPath,
		const std::string& cachePath = "",
		IPCMethod method = Namedpipe
	);
	~PlatformCommonIPC();

	/** For name pipe */
	void setPipeName(const std::string& pipeName);
    /** For FIFO */
    void setFIFOFileName(const std::string& wfifo, const std::string& rfifo);

	bool start();
	bool sentData(const std::string& data);
	bool receiveData(std::string& data);
	void stop();

	int getErrorCode() const;

	void setRWTimeout(uint32_t timeout /** sec */);

private:
	std::string m_processPath;
	std::string m_pipeName;
	std::string m_cachePath;
    std::string m_wfifo;
    std::string m_rfifo;
	IPCMethod m_ipcMethod;

	uint32_t m_timeout = 30;

	int m_errorCode = 0;
	class IInterProcessCommunitor* m_pIPCCommtor = nullptr;
};
