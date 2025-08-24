#pragma once
#include <functional>
#include <string>
#include <litehtml/litehtml.h>

class js_engine {
public:
    virtual ~js_engine() = default;

    /* 生命周期 */
    virtual bool init() = 0;
    virtual void shutdown() = 0;

    /* 与 litehtml 交互 */
    virtual void bind_document(litehtml::document*) = 0;
    virtual void eval(const std::string& code,
        const std::string& filename = "<inline>") = 0;

    /* 事件循环钩子 */
    virtual void pump_tasks() = 0;

    /* 调试/日志 */
    virtual void set_logger(std::function<void(const char*)> log) {
        m_logger = std::move(log);
    }
protected:
    std::function<void(const char*)> m_logger;
};


class js_engine;
namespace litehtml { class document; }
