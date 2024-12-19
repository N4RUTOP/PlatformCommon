#pragma once

#include "PlatformCommonUtils.h"
class PlatformCommonMutex
{
public:
	PlatformCommonMutex();
	~PlatformCommonMutex();

	void mutex_lock();
	void mutex_unlock();
	
private:
#ifdef WIN32
	PlatformCommonUtils::mutex_t m_mutex = nullptr;
#else
    PlatformCommonUtils::mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
};
