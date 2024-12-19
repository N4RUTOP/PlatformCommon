#include "PlatformCommonMutex.h"

PlatformCommonMutex::PlatformCommonMutex()
{
#ifdef WIN32
	m_mutex = new CRITICAL_SECTION;
	InitializeCriticalSection(m_mutex);
#endif // WIN32
}

PlatformCommonMutex::~PlatformCommonMutex()
{
	mutex_unlock();
#ifdef WIN32
	DeleteCriticalSection(m_mutex);
	delete m_mutex;
#endif // WIN32
}

void PlatformCommonMutex::mutex_lock()
{
	PlatformCommonUtils::mutex_lock(m_mutex);
}

void PlatformCommonMutex::mutex_unlock()
{
	PlatformCommonUtils::mutex_unlock(m_mutex);
}
