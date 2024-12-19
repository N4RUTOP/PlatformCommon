/**
*   基于可变参数模板的线程类
* 
*   Create by lihuanqian on 4/1/2024
*   Copyright (c) 2024 lihuanqian, All Rights Reserved.
*/

#pragma once

#include <functional>
#include <future>
#include <stdint.h>
#include <atomic>

#ifdef WIN32
#include <Windows.h>
#else // WIN32
#include <pthread.h>
#endif // UNIX

template <typename T>
class CThread
{
public:
	CThread();
	~CThread();

    /**
     * @brief          执行线程函数
     * @param func     要执行的函数地址
     * @param args  执行函数的参数
     * @return         是否启动成功
     */
    template<typename Func, typename... Args>
    bool run(Func&& func, Args&&... args);

    /**
     * @brief 获取执行函数的返回值
     */
    T getReturnValue();

	/**
	 * @brief 阻塞等待线程完成
	 */
	void join();

    /**
     * @brief  获取线程ID
     * @return 线程id
     */
    uint64_t threadID() const;

    /**
     * @brief  线程是否运行中
     * @return true：运行中
     */
    bool isRunning() const;

    /**
     * @brief 重置线程 
     * @warning  若线程运行中会强制结束 (危险！看情况使用)
     */
    void reset();

private:
    void forceTerminate();
#ifdef WIN32
    static unsigned __stdcall threadProc(void* p);
#else // WIN32
    static void* threadProc(void* p);
#endif // UNIX 

private:
#ifdef WIN32
    HANDLE 
#else // WIN32
    pthread_t 
#endif // UNIX
    m_thread = nullptr;

    std::atomic_bool m_bRunning = false;

    std::future<T> m_future;

    std::unique_ptr<std::packaged_task<T()>> m_pTask = nullptr;
};

template<typename T>
CThread<T>::CThread()
{
}

template<typename T>
CThread<T>::~CThread()
{
    join();
}

template<typename T>
inline void CThread<T>::join()
{
	if (m_thread != nullptr) {
#ifdef WIN32
        ::WaitForSingleObject(m_thread, INFINITE);
        ::CloseHandle(m_thread);
#else // WIN32
        pthread_join(m_thread, nullptr);
#endif // UNIX
        m_thread = nullptr;
	}
}

template<typename T>
inline uint64_t CThread<T>::threadID() const
{
    uint64_t id = 0;
    if (m_thread != nullptr) {
#ifdef WIN32
        id = ::GetThreadId(m_thread);
#else // WIN32
        id = pthread_getunique_np(m_thread);
#endif // UNIX
    }
    return id;
}

template<typename T>
inline bool CThread<T>::isRunning() const
{
    return m_bRunning.load();
}

template<typename T>
inline void CThread<T>::reset()
{
    if (m_thread == nullptr) return;
    forceTerminate();
#ifdef WIN32
    ::CloseHandle(m_thread);
#endif
    m_bRunning.store(false);
    m_thread = nullptr;
}

template<typename T>
inline void CThread<T>::forceTerminate()
{
    if (m_thread != nullptr) {
#ifdef WIN32
        DWORD code;
        if (::GetExitCodeThread(m_thread, &code) && (code == STILL_ACTIVE))
            ::TerminateThread(m_thread, code);
#else // WIN32
        pthread_cancel(m_thread);
#endif // UNIX
    }
}

template<typename T>
#ifdef WIN32
unsigned __stdcall CThread<T>::threadProc(void* p)
#else // WIN32
void* CThread<T>::threadProc(void* p)
#endif // UNIX
{
    CThread<T>* thr = static_cast<CThread<T>*>(p);
    (*(thr->m_pTask))();
    thr->m_pTask.reset();
    thr->m_bRunning.store(false);
    return NULL;
}

template<typename T>
template<typename Func, typename ...Args>
inline bool CThread<T>::run(Func&& func, Args&&... args)
{
    join();

    m_pTask = std::make_unique<std::packaged_task<T()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
    m_future = m_pTask->get_future();
    m_bRunning.store(true);

    /** invoke thread */
#ifdef WIN32
    m_thread = reinterpret_cast<HANDLE>(
        ::_beginthreadex(nullptr, 0, threadProc, static_cast<void*>(this), 0, nullptr));
#else // WIN32
    pthread_create(&m_thread, nullptr, threadProc, static_cast<void*>(this));
#endif // UNIX

    if (m_thread == nullptr) {
        m_pTask.reset();
        m_bRunning.store(false);
        return false;
    }
    return true;
}

template<typename T>
inline T CThread<T>::getReturnValue()
{
    m_future.wait();
    return m_future.get();
}
