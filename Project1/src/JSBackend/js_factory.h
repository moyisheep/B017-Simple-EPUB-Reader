#pragma once

#include <functional>
#include <string>
#include <memory>
#include "js_engine.h"
#include "duktape_engine.h"

/* 显式工厂：手动注册，无宏 */
class js_factory {
public:
    js_factory();
    ~js_factory();
    using creator_t = std::function<std::unique_ptr<js_engine>()>;

    // 单例
    static js_factory& instance();

    // 手动注册
    void add(const std::string& name, creator_t c);

    // 创建
    std::unique_ptr<js_engine> create(const std::string& name) const;

private:
    struct impl;
    std::unique_ptr<impl> p;
};


static bool reg_duktape = [] {
    js_factory::instance().add("duktape", [] { return std::make_unique<duktape_engine>(); });
    return true;
    }();