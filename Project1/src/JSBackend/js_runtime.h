#pragma once
# include <litehtml/litehtml.h>

#include "js_factory.h"

class js_runtime {
public:
    explicit js_runtime(litehtml::document* doc);
    ~js_runtime();

    /* �������л����棬�ɹ����� true */
    bool switch_engine(const std::string& name);

    /* ת������ǰ���� */
    void eval(const std::string& code,
        const std::string& filename = "<inline>");

    /* ÿ֡/����ʱ���ã����� Promise��setTimeout ������ */
    void pump();

    /* �� litehtml �� document �󶨵���ǰ JS ȫ�ֶ��� */
    void bind_document(litehtml::document* doc);
    void set_logger(std::function<void(const char*)> log);
private:
    void shutdown();                 // ���ٵ�ǰ����
    litehtml::document* m_doc = nullptr;
    std::unique_ptr<js_engine> m_engine;
};
