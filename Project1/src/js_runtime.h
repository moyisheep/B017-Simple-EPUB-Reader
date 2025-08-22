#pragma once
# include <litehtml/litehtml.h>

#include "js_factory.h"

class js_runtime {
public:
    explicit js_runtime(litehtml::document* doc);
    ~js_runtime();

    /* 运行期切换引擎，成功返回 true */
    bool switch_engine(const std::string& name);

    /* 转发到当前引擎 */
    void eval(const std::string& code,
        const std::string& filename = "<inline>");

    /* 每帧/空闲时调用，驱动 Promise、setTimeout 等任务 */
    void pump();

    /* 把 litehtml 的 document 绑定到当前 JS 全局对象 */
    void bind_document(litehtml::document* doc);
    void set_logger(std::function<void(const char*)> log);
private:
    void shutdown();                 // 销毁当前引擎
    litehtml::document* m_doc = nullptr;
    std::unique_ptr<js_engine> m_engine;
};
