#include "js_runtime.h"



js_runtime::js_runtime(litehtml::document* doc) : m_doc(doc) {}
js_runtime::~js_runtime() { shutdown(); }

bool js_runtime::switch_engine(const std::string& name)
{
    shutdown();   // 先清理旧引擎
    m_engine = js_factory::instance().create(name);
    if (!m_engine) return false;

    if (!m_engine->init()) {
        shutdown();
        return false;
    }

    /* 把当前 document 绑定到新引擎 */
    bind_document(m_doc);
    return true;
}

void js_runtime::shutdown()
{
    if (m_engine) {
        m_engine->shutdown();
        m_engine.reset();
    }
}

void js_runtime::eval(const std::string& code,
    const std::string& filename)
{
    if (m_engine) m_engine->eval(code, filename);
}

void js_runtime::pump()
{
    if (m_engine) m_engine->pump_tasks();
}

void js_runtime::bind_document(litehtml::document* doc)
{
    if (!m_engine || !doc) return;
    m_doc = doc;
    m_engine->bind_document(m_doc);
}

void js_runtime::set_logger(std::function<void(const char*)> log) {
    m_engine->set_logger(std::move(log));
}


