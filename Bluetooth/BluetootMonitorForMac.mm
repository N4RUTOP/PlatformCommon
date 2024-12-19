#include "IBluetoothMonitor.h"
#include <mutex>
#include <atomic>
#include <list>
#include <unordered_set>
#import  <IOBluetooth/IOBluetooth.h>

#include "CThread.hpp"

using namespace std;

using bth_evt_cb = IBluetoothMonitor::BluetoothEventCallback;
using bth_cb_unit = pair<bth_evt_cb, void*>;

static list<bth_cb_unit> g_evt_cbs;
static mutex g_mutex;
static list<BluetoothEvent> g_bth_devices;
static std::atomic_bool g_check_stopped = true;

static CThread<void> g_thread;

static void loopCheckBluetoothDevice()
{
    while (!g_check_stopped)
    {
        @autoreleasepool {
            // 获取已配对的设备列表
            NSArray<IOBluetoothDevice *> *pairedDevices = [IOBluetoothDevice pairedDevices];
            // 遍历已配对的设备，检查哪些设备已连接
            list<BluetoothEvent> dev_list;
            for (IOBluetoothDevice *device in pairedDevices) {
                if ([device isConnected]) {
                    BluetoothEvent evt;
                    evt.name = [[device name] UTF8String];
                    std::string addr = [[device addressString] UTF8String];
                    for (int i = 3; i < addr.size(); i += 3) {
                        addr[i] = ':';
                    }
                    evt.address = addr;
                    dev_list.push_back(evt);
                }
            }
        }
        usleep(1000 * 500);
    }

}

static unordered_set<string> toSetFromList(const list<BluetoothEvent>& devices)
{
    unordered_set<std::string> res;
    for (auto& device : devices) {
        res.insert(device.address);
    }
    return res;
}

static void compareDeviceList(list<BluetoothEvent>& devices)
{
    auto ret = devices.size() <=> g_bth_devices.size();
    std::list<BluetoothEvent> changeDevices;
    if (ret == std::strong_ordering::less) {
        /** 有设备断开 */
        //LOG_DEBUG("Has bluetooth device disconnect");
        auto set = toSetFromList(devices);
        for (auto& dev : g_bth_devices) {
            if (set.find(dev.address) == set.end()) {
                dev.event = BTH_REMOVE;
                g_mutex.lock();
                for (auto& cb : g_evt_cbs)
                {
                    if (cb.first != nullptr) {
                        cb.first(&dev, cb.second);
                    }
                }
                g_mutex.unlock();
                changeDevices.push_back(dev);
            }
        }
        for (auto& dev : changeDevices) {
            g_bth_devices.remove(dev);
        }
    }
    else if (ret == std::strong_ordering::greater) {
        /** 有设备连接 */
       // LOG_DEBUG("Has bluetooth device connect");
        auto set = toSetFromList(g_bth_devices);;
        for (auto& dev : devices) {
            if (set.find(dev.address) == set.end()) {
                //m_cb(ADRDEVICE_ADD, m_pUserData, snum.c_str());
                dev.event = BTH_ADD;
                g_mutex.lock();
                for (auto& cb : g_evt_cbs)
                {
                    if (cb.first != nullptr) {
                        cb.first(&dev, cb.second);
                    }
                }
                g_mutex.unlock();
                changeDevices.push_back(dev);
            }
        }
        for (auto& dev : changeDevices) {
            g_bth_devices.push_back(dev);
        }
    }
}


void BluetoothMonitorForMac::addBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data)
{
    g_mutex.lock();
    g_evt_cbs.push_back(bth_cb_unit(cb, user_data));
    g_mutex.unlock();
    if (!g_thread.isRunning()) {
        g_check_stopped = false;
        g_thread.run(loopCheckBluetoothDevice);
    }
}

void BluetoothMonitorForMac::removeBluetoothEventSubscriber(BluetoothEventCallback cb, void* user_data)
{
    g_mutex.lock();
    g_evt_cbs.remove(bth_cb_unit(cb, user_data));
    if (g_evt_cbs.empty()) {
        g_mutex.unlock();
        g_check_stopped = true;
        g_thread.join();
        return;
    }
    g_mutex.unlock();
}
