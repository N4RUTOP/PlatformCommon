/**
 *   Plist with smart ptr
 *
 *   Created by lihuanqian on 12/14/2024
 *
 *   Copyright (c) lihuanqian. All rights reserved.
 */
#pragma once
#include "plist/plist.h"
#include <memory>

static inline std::shared_ptr<plist_t> make_shared_ptr_for_plist(plist_t plist)
{
    return std::shared_ptr<plist_t>(new plist_t(plist), [](plist_t* p) {
        plist_free(*p);
        delete p;
        });
}

static inline std::shared_ptr<char*> make_shared_ptr_for_plist_data(char* data)
{
    return std::shared_ptr<char*>(new char*(data), [](char** p) {
        plist_mem_free(*p);
        delete p;
        });
}

template <typename CreateFunc, typename... Args>
static inline std::shared_ptr<plist_t> create_plist_ex(CreateFunc&& func, Args&&... args)
{
    return make_shared_ptr_for_plist(func(std::forward<Args>(args)...));
}

static inline std::shared_ptr<char*> plist_get_string_val_ex(plist_t plist)
{
    char* str = nullptr;
    plist_get_string_val(plist, &str);
    return str ? make_shared_ptr_for_plist_data(str) : nullptr;
}
