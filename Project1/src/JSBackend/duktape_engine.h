#pragma once

#include <functional>
#include <duktape.h>
#include <litehtml/litehtml.h>
#include <litehtml/el_text.h>
#include <litehtml/document.h>
#include <litehtml/element.h>
#include <sstream>
#include <windows.h>
#include <iostream>
#include "js_engine.h"

class duktape_engine : public js_engine {

public:
    duktape_engine();
    ~duktape_engine() override;

    /* js_engine �ӿ� */
    bool init() override;
    void shutdown() override;
    void bind_document(litehtml::document*) override;
    void eval(const std::string& code,
        const std::string& filename = "<inline>") override;
    void pump_tasks() override;

private:
    /* �ڲ����ߺ��� */
    static duk_ret_t duk_console_log(duk_context*);
    static void duk_fatal_handler(void* udata, const char* msg);

    std::shared_ptr<litehtml::document> m_doc;   // ��ָ��

    duk_context* ctx_ = nullptr;

};






