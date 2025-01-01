/**
*   基于可变参数模板的轻量级线程类
* 
*   Create by lihuanqian on 4/1/2024
* 
*   Copyright (c) 2024 lihuanqian, All Rights Reserved.
*/

#pragma once

#include <functional>
#include <future>
#include <stdint.h>
#include <atomic>

#ifdef _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else // _MSC_VER
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

private:
#ifdef _MSC_VER
    static unsigned __stdcall threadProc(void* p);
#else // _MSC_VER
    static void* threadProc(void* p);
#endif // UNIX 

private:
#ifdef _MSC_VER
    uintptr_t m_thread = NULL;
#else // _MSC_VER
    pthread_t m_thread = NULL;
#endif // UNIX
    std::atomic_bool m_bRunning = false;
    std::packaged_task<T()> m_task;
    std::future<T> m_future;
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
	if (m_thread != NULL) {
#ifdef _MSC_VER
        ::WaitForSingleObject((HANDLE)m_thread, INFINITE);
        ::CloseHandle((HANDLE)m_thread);
#else // _MSC_VER
        pthread_join(m_thread, NULL);
#endif // UNIX
        m_thread = NULL;
	}
}

template<typename T>
inline uint64_t CThread<T>::threadID() const
{
    uint64_t id = 0;
    if (m_thread != NULL) {
#ifdef _MSC_VER
        id = ::GetThreadId((HANDLE)m_thread);
#else // _MSC_VER
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
#ifdef _MSC_VER
unsigned __stdcall CThread<T>::threadProc(void* p)
#else // _MSC_VER
void* CThread<T>::threadProc(void* p)
#endif // UNIX
{
    CThread<T>* thr = static_cast<CThread<T>*>(p);
    thr->m_task();
    thr->m_bRunning.store(false);
    return NULL;
}

template<typename T>
template<typename Func, typename ...Args>
inline bool CThread<T>::run(Func&& func, Args&&... args)
{
    join();
    //m_task = std::packaged_task<T()>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
    m_task = std::packaged_task<T()>(
        [func = std::forward<Func>(func), ... args = std::forward<Args>(args)]() mutable {
            return func(args...);
        }
    );
    m_future = m_task.get_future();
    m_bRunning.store(true);

#ifdef _MSC_VER
    m_thread = ::_beginthreadex(NULL, 0, threadProc, (void*)this, 0, NULL);
#else // _MSC_VER
    pthread_create(&m_thread, NULL, threadProc, (void*)this);
#endif // UNIX
    if (m_thread == NULL) {
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
