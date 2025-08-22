// ������������Stash��ȡ�󶨵�document

#include "duktape_engine.h"

// ========== �������� =========
void duk_bind_element(duk_context* ctx, litehtml::element::ptr elem);
static duk_ret_t duk_element_classname_getter(duk_context* ctx);
static duk_ret_t duk_element_classname_setter(duk_context* ctx);
static litehtml::element::ptr find_body(const litehtml::element::ptr& root);
static void _stack_dump(duk_context* ctx, const char* tag);
static std::shared_ptr<litehtml::document>* get_binded_document_ptr(duk_context* ctx);

duk_ret_t duk_element_query_selector(duk_context* ctx);

duk_ret_t duk_element_query_selector_all(duk_context* ctx);

duk_ret_t duk_element_get_inner_html(duk_context* ctx);

void serialize_node(litehtml::element::ptr node, std::string& output);

std::string escape_html(const std::string& input);

std::string escape_html_attribute(const std::string& input);

duk_ret_t duk_element_set_attribute(duk_context* ctx);

duk_ret_t duk_element_get_attribute(duk_context* ctx);

duk_ret_t duk_element_append_child(duk_context* ctx);

duk_ret_t duk_title_getter(duk_context* ctx);

duk_ret_t duk_title_setter(duk_context* ctx);

litehtml::element::ptr find_first_element(const litehtml::element::ptr& start, const std::function<bool(litehtml::element::ptr)>& predicate);

litehtml::element::ptr find_first_element_by_tag(litehtml::document::ptr doc, const std::string& tag_name);

void set_element_text(litehtml::element::ptr element, const std::string& text);

duk_ret_t duk_document_element_getter(duk_context* ctx);

duk_ret_t duk_document_body_getter(duk_context* ctx);



duk_ret_t duk_query_selector_all(duk_context* ctx);

duk_ret_t create_node_list(duk_context* ctx, const litehtml::elements_list& elements, std::shared_ptr<litehtml::document> doc);

duk_ret_t duk_create_text_node(duk_context* ctx);

duk_ret_t duk_query_selector(duk_context* ctx);

void set_element_inner_html(const litehtml::element::ptr& elem, const char* html);

duk_ret_t duk_element_set_inner_html(duk_context* ctx);

std::shared_ptr<litehtml::element> get_binded_element(duk_context* ctx, duk_idx_t idx);

//duk_ret_t duk_element_click(duk_context* ctx);

duk_ret_t duk_get_element_by_id(duk_context* ctx);

static bool is_id_char(char c);

static std::shared_ptr<litehtml::document>* get_binded_document_ptr_safe(duk_context* ctx);

duk_ret_t duk_create_element(duk_context* ctx);

duk_ret_t duk_document_get_body(duk_context* ctx);

duk_ret_t duk_get_elements_by_tag_name(duk_context* ctx);

void add_document_property(duk_context* ctx, const char* name, duk_c_function getter);



/* ---------- ���� / ���� ---------- */
duktape_engine::duktape_engine() = default;
duktape_engine::~duktape_engine() { shutdown(); }

duk_ret_t duktape_engine::duk_console_log(duk_context* ctx)
{
    duk_push_current_function(ctx);                // -> [func]
    duk_get_prop_string(ctx, -1, "\xFF" "self");   // -> [func, this]
    auto* self = static_cast<duktape_engine*>(duk_get_pointer(ctx, -1));
    duk_pop_2(ctx);                                // -> []

    if (!self || !self->m_logger) return 0;

    std::stringstream ss;
    for (int i = 0, n = duk_get_top(ctx); i < n; ++i) {
        ss << duk_to_string(ctx, i) << (i + 1 == n ? "" : " ");
    }
    self->m_logger(ss.str().c_str());
    return 0;
}

/* ---------- �������� ---------- */
void duktape_engine::duk_fatal_handler(void* udata, const char* msg)
{
    auto* self = static_cast<duktape_engine*>(udata);
    if (self && self->m_logger) self->m_logger(msg);
}

bool duktape_engine::init()
{
    ctx_ = duk_create_heap(nullptr, nullptr, nullptr,
        this,           // userdata
        duk_fatal_handler);
    if (!ctx_) return false;

    /* console.log */
    duk_push_object(ctx_);
    duk_push_c_function(ctx_, duk_console_log, DUK_VARARGS);
    duk_push_pointer(ctx_, this);
    duk_put_prop_string(ctx_, -2, "\xFF" "self");
    duk_put_prop_string(ctx_, -2, "log");
    duk_put_global_string(ctx_, "console");

    return true;
}

void duktape_engine::shutdown() {
    if (ctx_) {
        duk_destroy_heap(ctx_);
        ctx_ = nullptr;
    }
}

/* ---------- ִ�нű� ---------- */
void duktape_engine::eval(const std::string& code,
    const std::string& filename)
{
    if (!ctx_) { OutputDebugStringA("ctx is null\n"); return; }
    duk_idx_t top = duk_get_top(ctx_);          // ��סջ��
    char buf[512];
    //snprintf(buf, sizeof(buf), "eval() size=%zu, data=|%s|\n", code.size(), code.c_str());
    //OutputDebugStringA(buf);

    duk_push_string(ctx_, code.c_str());
    if (duk_peval(ctx_) != 0) {
        snprintf(buf, sizeof(buf), "Duktape error: %s\n", duk_safe_to_string(ctx_, -1));
        OutputDebugStringA(buf);
    }
    else {
        snprintf(buf, sizeof(buf), "Duktape ok, result=%s\n", duk_safe_to_string(ctx_, -1));
        OutputDebugStringA(buf);
    }
    duk_set_top(ctx_, top);                     // ǿ�ƻָ�ջƽ��

}

/* ---------- �¼�ѭ�� ---------- */
void duktape_engine::pump_tasks()
{
}

// ���ĵ���ʵ�� =======================================================
void duktape_engine::bind_document(litehtml::document* doc) {
    // �����ĵ�ָ�루ʹ�ÿ�ɾ������
    auto* doc_ptr = new std::shared_ptr<litehtml::document>(
        doc, [](litehtml::document*) {}
    );

    if (!ctx_) {
        OutputDebugStringA("[DUK] bind_document: ctx_ is null\n");
        delete doc_ptr;
        return;
    }

    OutputDebugStringA("[DUK] === bind_document start ===\n");

    // ����ȫ��Stash
    duk_push_global_stash(ctx_);

    // ������ĵ�ָ��
    if (duk_get_prop_string(ctx_, -1, "__document_ptr")) {
        if (duk_is_pointer(ctx_, -1)) {
            auto old_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(
                duk_get_pointer(ctx_, -1)
                );
            delete old_ptr;
        }
        duk_pop(ctx_);  // ������ָ��
    }
    else {
        duk_pop(ctx_);
    }
    // �洢��ָ��
    duk_push_pointer(ctx_, doc_ptr);
    duk_put_prop_string(ctx_, -2, "__document_ptr");
    duk_pop(ctx_);

    // �����ĵ�����
    duk_push_global_object(ctx_);
    duk_push_object(ctx_);                      // [global, document]

    // ��ӻ�������
    duk_push_c_function(ctx_, duk_get_element_by_id, 1);
    duk_put_prop_string(ctx_, -2, "getElementById");

    duk_push_c_function(ctx_, duk_document_get_body, 0);
    duk_put_prop_string(ctx_, -2, "getBody");

    duk_push_c_function(ctx_, duk_create_element, 1);
    duk_put_prop_string(ctx_, -2, "createElement");

    duk_push_c_function(ctx_, duk_create_text_node, 1);
    duk_put_prop_string(ctx_, -2, "createTextNode");

    duk_push_c_function(ctx_, duk_get_elements_by_tag_name, 1);
    duk_put_prop_string(ctx_, -2, "getElementsByTagName");

    duk_push_c_function(ctx_, duk_query_selector, 1);
    duk_put_prop_string(ctx_, -2, "querySelector");

    duk_push_c_function(ctx_, duk_query_selector_all, 1);
    duk_put_prop_string(ctx_, -2, "querySelectorAll");

    /*  duk_push_c_function(ctx_, duk_add_event_listener, 3);
      duk_put_prop_string(ctx_, -2, "addEventListener");

      duk_push_c_function(ctx_, duk_remove_event_listener, 3);
      duk_put_prop_string(ctx_, -2, "removeEventListener");*/

      //// ��Ӹ߼�API
      //duk_push_c_function(ctx_, duk_document_exec_command, 3);
      //duk_put_prop_string(ctx_, -2, "execCommand");

      // ������Է�����
    duk_push_string(ctx_, "body");
    duk_push_c_function(ctx_, duk_document_body_getter, 0);
    duk_def_prop(ctx_, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);

    duk_push_string(ctx_, "documentElement");
    duk_push_c_function(ctx_, duk_document_element_getter, 0);
    duk_def_prop(ctx_, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);

    duk_push_string(ctx_, "title");
    duk_push_c_function(ctx_, duk_title_getter, 0);
    duk_push_c_function(ctx_, duk_title_setter, 1);
    duk_def_prop(ctx_, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_ENUMERABLE);

    // ����Ԫ��ԭ��
    duk_push_object(ctx_);                      // [..., ElementPrototype]
    duk_put_prop_string(ctx_, -2, "ElementPrototype"); // document.ElementPrototype = object

    // �ҵ�ȫ��
    duk_put_prop_string(ctx_, -2, "document"); // global.document = document
    duk_pop(ctx_);                              // []

    OutputDebugStringA("[DUK] === bind_document end ===\n");
}


// ��Ԫ�ص�JS����
void duk_bind_element(duk_context* ctx, litehtml::element::ptr elem) {
    if (!elem) {
        duk_push_null(ctx);
        return;
    }

    // ����Ԫ�ض���
    duk_push_object(ctx);                       // [elem]

    // ����Ԫ��ָ��
    auto* elem_ptr = new std::shared_ptr<litehtml::element>(elem);
    duk_push_pointer(ctx, elem_ptr);          // [elem, ptr]
    duk_put_prop_string(ctx, -2, "__ptr");     // elem["__ptr"] = ptr

    // ���ͨ�÷���
    //duk_push_c_function(ctx, duk_element_add_event_listener, 2);
    //duk_put_prop_string(ctx, -2, "addEventListener");

    //duk_push_c_function(ctx, duk_element_remove_event_listener, 2);
    //duk_put_prop_string(ctx, -2, "removeEventListener");

    duk_push_c_function(ctx, duk_element_set_attribute, 2);
    duk_put_prop_string(ctx, -2, "setAttribute");

    duk_push_c_function(ctx, duk_element_get_attribute, 1);
    duk_put_prop_string(ctx, -2, "getAttribute");

    duk_push_c_function(ctx, duk_element_set_inner_html, 1);
    duk_put_prop_string(ctx, -2, "setInnerHTML");

    duk_push_c_function(ctx, duk_element_get_inner_html, 0);
    duk_put_prop_string(ctx, -2, "getInnerHTML");

    duk_push_c_function(ctx, duk_element_append_child, 1);
    duk_put_prop_string(ctx, -2, "appendChild");

    duk_push_c_function(ctx, duk_element_query_selector, 1);
    duk_put_prop_string(ctx, -2, "querySelector");

    duk_push_c_function(ctx, duk_element_query_selector_all, 1);
    duk_put_prop_string(ctx, -2, "querySelectorAll");

    // ��� className ����
    duk_push_string(ctx, "className");
    duk_push_c_function(ctx, duk_element_classname_getter, 0);
    duk_push_c_function(ctx, duk_element_classname_setter, 1);
    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER |
        DUK_DEFPROP_HAVE_SETTER |
        DUK_DEFPROP_ENUMERABLE);

    //duk_push_c_function(ctx, duk_element_click, 0);
    //duk_put_prop_string(ctx, -2, "click");

    // ����prototype��
    duk_push_global_object(ctx);            // [elem, global]
    duk_get_prop_string(ctx, -1, "ElementPrototype"); // [elem, global, proto]
    duk_set_prototype(ctx, -3);             // elem.prototype = proto
    duk_pop(ctx);                           // [elem] 
}


// className getter ʵ��
static duk_ret_t duk_element_classname_getter(duk_context* ctx) {
    duk_push_this(ctx); // ��ȡ��ǰ����
    duk_get_prop_string(ctx, -1, "__ptr"); // ��ȡԪ��ָ��
    auto* elem_ptr = static_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
    duk_pop_2(ctx); // ����ջ����ָ���this

    if (elem_ptr && *elem_ptr) {
        const char* className = (*elem_ptr)->get_attr("class");
        duk_push_string(ctx, className ? className : "");
        return 1;
    }

    duk_push_string(ctx, "");
    return 1;
}

// className setter ʵ��
static duk_ret_t duk_element_classname_setter(duk_context* ctx) {
    // ����0���µ�classNameֵ
    const char* new_class = duk_require_string(ctx, 0);

    duk_push_this(ctx); // ��ȡ��ǰ����
    duk_get_prop_string(ctx, -1, "__ptr"); // ��ȡԪ��ָ��
    auto* elem_ptr = static_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
    duk_pop_2(ctx); // ����ջ����ָ���this

    if (elem_ptr && *elem_ptr) {
        (*elem_ptr)->set_attr("class", new_class);

        //// ��Ҫ��֪ͨ�ĵ���ʽ��Ҫ����
        //auto doc = (*elem_ptr)->get_document();
        //if (doc) {
        //    // ���Ԫ����Ҫ���¼�����ʽ
        //    (*elem_ptr)->refresh_styles();

        //    // ����ĵ�֧�����²���
        //    if (auto render_root = doc->root_render_item()) {
        //        render_root->apply_styles();
        //        render_root->layout(render_root->content_width());
        //    }
        //}
    }

    return 0;
}
static litehtml::element::ptr find_body(const litehtml::element::ptr& root)
{
    if (!root) return nullptr;
    if (litehtml::t_strcasecmp(root->get_tagName(), "body") == 0)
        return root;

    for (const auto& child : root->children())
    {
        auto body = find_body(child);
        if (body) return body;
    }
    return nullptr;
}

static inline void _stack_dump(duk_context* ctx, const char* tag)
{
    duk_idx_t n = duk_get_top(ctx);
    std::ostringstream oss;
    oss << "[DUK] " << tag << " | top=" << n << "\n";
    OutputDebugStringA(oss.str().c_str());
}

#define DUK_TRACE(ctx, msg)  _stack_dump(ctx, msg)
// �������������ĵ���Ԫ�� =============================================

// ��stash��ȡ���ĵ�ָ��
static std::shared_ptr<litehtml::document>* get_binded_document_ptr(duk_context* ctx) {
    duk_push_global_stash(ctx);                // [stash]
    duk_get_prop_string(ctx, -1, "__document_ptr"); // [stash, ptr?]
    std::shared_ptr<litehtml::document>* doc_ptr = nullptr;
    if (duk_is_pointer(ctx, -1)) {
        doc_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(duk_to_pointer(ctx, -1));
    }
    duk_pop_2(ctx);  // ����ptr��stash -> []
    return doc_ptr;   // ���ܷ���nullptr
}


// querySelector - ��ȡƥ��ĵ�һ��Ԫ��
duk_ret_t duk_element_query_selector(duk_context* ctx) {
    /* ������
       [0] ѡ�����ַ���
       this��Ԫ�ض���
    */

    // 1. ��ȡѡ��������
    const char* selector = duk_require_string(ctx, 0);

    // 2. ��ȡ��ǰԪ��ָ��
    duk_push_this(ctx); // [this]
    duk_get_prop_string(ctx, -1, "__ptr"); // [this, ptr]

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx);
        duk_push_null(ctx);
        return 1;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx);; // ����ptr��this [��ջ]

    if (!elem_ptr || !*elem_ptr) {
        duk_push_null(ctx);
        return 1;
    }

    auto element = *elem_ptr;

    // 3. ִ��ѡ������ѯ
    try {
        auto result = element->select_one(selector);

        // 4. �����ѯ���
        if (result) {
            duk_bind_element(ctx, result);
        }
        else {
            duk_push_null(ctx);
        }
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "Invalid selector: %s, %s", selector, e.what());
    }

    return 1;
}

// querySelectorAll - ��ȡ����ƥ���Ԫ��
duk_ret_t duk_element_query_selector_all(duk_context* ctx) {
    /* ������
       [0] ѡ�����ַ���
       this��Ԫ�ض���
    */

    // 1. ��ȡѡ��������
    const char* selector = duk_require_string(ctx, 0);

    // 2. ��ȡ��ǰԪ��ָ��
    duk_push_this(ctx); // [this]
    duk_get_prop_string(ctx, -1, "__ptr"); // [this, ptr]

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx);
        duk_push_array(ctx); // ���ؿ�����
        return 1;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // ����ptr��this [��ջ]

    if (!elem_ptr || !*elem_ptr) {
        duk_push_array(ctx); // ���ؿ�����
        return 1;
    }

    auto element = *elem_ptr;

    // 3. ִ��ѡ������ѯ
    try {
        litehtml::elements_list results = element->select_all(selector);
        duk_idx_t arr_idx = duk_push_array(ctx);

        // ��ȷ���� std::list �ķ�ʽ
        size_t index = 0;
        for (const auto& element_ptr : results) {
            duk_bind_element(ctx, element_ptr);
            duk_put_prop_index(ctx, arr_idx, index++);
        }
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "Invalid selector: %s, %s", selector, e.what());
    }

    return 1;
}


duk_ret_t duk_element_get_inner_html(duk_context* ctx) {
    /* ��������
       this��Ԫ�ض���
    */

    // 1. ��ȡԪ��ָ��
    duk_push_this(ctx); // [this]
    duk_get_prop_string(ctx, -1, "__ptr"); // [this, ptr]

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx); // ���ջ
        duk_push_string(ctx, "");
        return 1;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // ����ָ���this [��ջ]

    if (!elem_ptr || !*elem_ptr) {
        duk_push_string(ctx, "");
        return 1;
    }

    auto element = *elem_ptr;

    // 2. ���л������ӽڵ�ΪHTML�ַ���
    std::string html;

    // ���������ӽڵ�
    for (auto& child : element->children()) {
        serialize_node(child, html);
    }

    // 3. ����HTML�ַ���
    duk_push_string(ctx, html.c_str());
    return 1;
}

// �ڵ����л�����
void serialize_node(litehtml::element::ptr node, std::string& output) {
    if (!node) return;

    if (node->is_text()) {
        // �ı��ڵ㣺HTMLת��
        std::string txt;
        node->get_text(txt);
        output += escape_html(txt);
        return;
    }

    // ע�ͽڵ�
    if (node->is_comment()) {
        output += "<!--";
        std::string txt;
        node->get_text(txt);
        output += txt;
        output += "-->";
        return;
    }

    // ��ͨԪ�ؽڵ�
    output += "<" + std::string(node->get_tagName());

    // ���л��������ԣ������°�litehtml�ӿڣ�
    auto attrs = node->dump_get_attrs();
    for (const auto& attr_tuple : attrs) {
        output += " " + std::get<0>(attr_tuple) + "=\""
            + escape_html_attribute(std::get<1>(attr_tuple))
            + "\"";
    }

    for (int i = 0; i < static_cast<int>(node->children().size()); i++) {
        // �Ապϱ�ǩ����<img>, <br>�ȣ�
        output += " />";
        return;
    }

    // �����պϱ�ǩ
    output += ">";

    // ���л��ӽڵ�
// ���л��ӽڵ�
    for (const auto& child : node->children()) {
        serialize_node(child, output);
    }

    // ��ȫ��ȡ��ǩ��������ָ���飩
    if (const char* tag_name = node->get_tagName()) {
        output += "</" + std::string(tag_name) + ">";
    }
    else {
        // �쳣��������ձ�ǩ���¼����
        output += "</>";
        // ������Ӵ�����־��
        // log_error("Element has invalid tag name at %p", static_cast<void*>(node));
    }
}

// HTMLת�庯��
std::string escape_html(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 1.2); // Ԥ����ռ�

    for (char c : input) {
        switch (c) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        default: output += c;
        }
    }

    return output;
}

// HTML����ֵת��
std::string escape_html_attribute(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 1.2); // Ԥ����ռ�

    for (char c : input) {
        switch (c) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        case '\n': output += "&#10;"; break;
        case '\r': output += "&#13;"; break;
        case '\t': output += "&#9;"; break;
        default: output += c;
        }
    }

    return output;
}


// ����Ԫ������
duk_ret_t duk_element_set_attribute(duk_context* ctx) {
    /* ������
       [0] ������ (string)
       [1] ����ֵ (string)
       this��Ԫ�ض���
    */

    // 1. ��֤����
    const char* attr_name = duk_require_string(ctx, 0);
    const char* attr_value = duk_require_string(ctx, 1);

    // 2. ��ȡԪ��ָ��
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "__ptr");

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx); // ����ָ���this
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid element");
        return 0;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // ����ָ���this

    if (!elem_ptr || !*elem_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element has been destroyed");
        return 0;
    }

    // 3. �������Դ��� - class
    if (strcmp(attr_name, "class") == 0) {
        litehtml::element::ptr element = *elem_ptr;

        // ������������
        if (attr_value && attr_value[0] != '\0') {
            // ����Ƿ���������
            if (const char* existing = element->get_attr("class", nullptr)) {
                // �������������������
                element->set_class(attr_value, true);
            }
            else {
                // û������������������
                element->set_class(attr_value, false);
            }
        }
        else {
            // ɾ������������Ϊ���ַ���
            element->set_attr("class", "");
        }
        return 0;
    }

    if (strcmp(attr_name, "style") == 0) {
        // style����ֱ������������ʽ
        (*elem_ptr)->set_attr("style", attr_value);

        //// ͬʱ������ʽ���¼���
        //if (auto doc = (*elem_ptr)->get_document()) {
        //    doc->on_media_changed();
        //}
        return 0;
    }

    // 4. ��������
    try {
        (*elem_ptr)->set_attr(attr_name, attr_value);
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "Failed to set attribute: %s", e.what());
    }

    return 0; // �޷���ֵ
}

// ��ȡԪ������
duk_ret_t duk_element_get_attribute(duk_context* ctx) {
    /* ������
       [0] ������ (string)
       this��Ԫ�ض���
    */

    // 1. ��ȡ������
    const char* attr_name = duk_require_string(ctx, 0);

    // 2. ��ȡԪ��ָ��
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "__ptr");

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx); // ����ָ���this
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid element");
        return 0;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // ����ָ���this

    if (!elem_ptr || !*elem_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element has been destroyed");
        return 0;
    }

    // 3. �������Դ���
    if (strcmp(attr_name, "class") == 0) {
        // ʹ����ȷ�Ľӿڻ�ȡ����
        const char* cls = (*elem_ptr)->get_attr("class", nullptr);
        duk_push_string(ctx, cls ? cls : "");
        return 1;
    }

    // 4. ��ȡ����ֵ
    const char* value = (*elem_ptr)->get_attr(attr_name);

    // 5. ���ؽ�������Դ��ڷ����ַ����������ڷ���null��
    if (value) {
        duk_push_string(ctx, value);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;
}

//// Ԫ�ذ���ǿ
//void bind_element(duk_context* ctx, litehtml::element::ptr elem) {
//    duk_push_object(ctx);
//
//    // �洢Ԫ��ָ��
//    auto* elem_ptr = new std::shared_ptr<litehtml::element>(elem);
//    duk_push_pointer(ctx, elem_ptr);
//    duk_put_prop_string(ctx, -2, "__ptr");
//
//    // ����finalizer
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        if (duk_get_prop_string(ctx, -1, "__ptr")) {
//            auto ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
//                duk_get_pointer(ctx, -1)
//                );
//            if (ptr) delete ptr;
//            duk_del_prop_string(ctx, -2, "__ptr");
//        }
//        return 0;
//        }, 1);
//    duk_set_finalizer(ctx, -2);
//
//    // ������Է���
//    duk_push_c_function(ctx, duk_element_set_attribute, 2);
//    duk_put_prop_string(ctx, -2, "setAttribute");
//
//    duk_push_c_function(ctx, duk_element_get_attribute, 1);
//    duk_put_prop_string(ctx, -2, "getAttribute");
//
//    // ��ӱ�����Է�����
//    duk_push_string(ctx, "id");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        return duk_element_get_attribute_helper(ctx, "id");
//        }, 0);
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        duk_require_string(ctx, 0);
//        duk_element_set_attribute_helper(ctx, "id", 0);
//        return 0;
//        }, 1);
//    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);
//
//    // ... �������� ...
//}

// �ؼ�����ʵ��ʾ�� ==================================================
duk_ret_t duk_element_append_child(duk_context* ctx) {
    /* ������
       [0] Ҫ��ӵ���Ԫ�� (element ����)
       this����Ԫ�ض���
    */

    // 1. ��֤��Ԫ�ز���
    if (!duk_is_object(ctx, 0) || !duk_has_prop_string(ctx, 0, "__ptr")) {
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Argument must be an element");
        return 0;
    }

    // 2. ��ȡ��Ԫ��ָ��
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "__ptr");

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx);
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid parent element");
        return 0;
    }

    auto parent_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop(ctx); // ������Ԫ��ָ�� [����this]

    if (!parent_ptr || !*parent_ptr) {
        duk_pop(ctx); // ����this
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Parent element has been destroyed");
        return 0;
    }

    auto parent = *parent_ptr;

    // 3. ��ȡ��Ԫ��ָ��
    duk_dup(ctx, 0); // ������Ԫ�صĸ���
    duk_get_prop_string(ctx, -1, "__ptr");

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx);
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid child element");
        return 0;
    }

    auto child_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // ������Ԫ��ָ��͸��� [����this]

    if (!child_ptr || !*child_ptr) {
        duk_pop(ctx); // ����this
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Child element has been destroyed");
        return 0;
    }

    auto child = *child_ptr;

    // 4. ��鸸�ӹ�ϵ��Ч��
    if (child->parent() == parent) {
        // �Ѿ����ӽڵ㣬ֱ�ӷ���
        duk_pop(ctx); // ����this
        duk_dup(ctx, 0); // ������ͬ����Ԫ��
        return 1;
    }

    // 5. ִ��DOM����
    try {
        // �����Ԫ���Ѿ��и�Ԫ�أ��Ƚ����Ƴ�
        if (auto old_parent = child->parent()) {
            old_parent->removeChild(child);
        }

        // ���Ϊ�µ���Ԫ��
        parent->appendChild(child);

        //// ����DOM��
        //if (auto doc = parent->get_document()) {
        //    doc->on_dom_changed();
        //}
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "DOM exception: %s", e.what());
    }

    // 6. ������ӵ���Ԫ��
    duk_pop(ctx); // ����this
    duk_dup(ctx, 0);
    return 1;
}


// �������� getter
duk_ret_t duk_title_getter(duk_context* ctx) {
    // �������� (���Է���)
    // this���ĵ�����

    // 1. ��ȡ�ĵ�ָ��
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_push_string(ctx, "");
        return 1;
    }

    // 2. �����ĵ��еĵ�һ��<title>Ԫ��
    litehtml::element::ptr title_element = find_first_element_by_tag(*doc_ptr, "title");

    // 3. ��ȡ�����ر����ı�
    if (title_element) {
        litehtml::string text;
        title_element->get_text(text);
        duk_push_string(ctx, text.c_str());
    }
    else {
        duk_push_string(ctx, "");
    }

    return 1;
}

// �������� setter
duk_ret_t duk_title_setter(duk_context* ctx) {
    // ������
    // [0] �µı���ֵ
    // this���ĵ�����

    // 1. ��֤����ȡ�±���
    const char* new_title = duk_require_string(ctx, 0);

    // 2. ��ȡ�ĵ�ָ��
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        return 0; // �޷���ֵ
    }

    // 3. �������е�<title>Ԫ��
    litehtml::element::ptr title_element = find_first_element_by_tag(*doc_ptr, "title");

    // 4. �������Ԫ�أ���������£�
    if (!title_element) {
        // �����µ�<title>Ԫ��
        auto head_element = find_first_element_by_tag(*doc_ptr, "head");
        if (!head_element) {
            // ���<head>Ҳ�����ڣ��ȴ���head
            litehtml::string_map attributes;  // ����һ��������ӳ��
            head_element = (*doc_ptr)->create_element("head", attributes);
            auto html_element = (*doc_ptr)->root();
            if (html_element) {
                html_element->appendChild(head_element);
            }
        }

        // ��������Ԫ��
        litehtml::string_map attributes;  // ����һ��������ӳ��
        title_element = (*doc_ptr)->create_element("title", attributes);
        head_element->appendChild(title_element);
    }

    // 5. ���±�������
    set_element_text(title_element, new_title);

    return 0; // �޷���ֵ
}


// ����������������ȱ�������ƥ�������ĵ�һ��Ԫ��
litehtml::element::ptr find_first_element(
    const litehtml::element::ptr& start,
    const std::function<bool(litehtml::element::ptr)>& predicate
) {
    if (!start) return nullptr;

    // ��鵱ǰԪ���Ƿ�ƥ��
    if (predicate(start)) {
        return start;
    }

    // �ݹ���������Ԫ��
    const auto& children = start->children();
    for (const auto& child : children) {
        if (auto result = find_first_element(child, predicate)) {
            return result;
        }
    }

    return nullptr;
}

// �������������ҵ�һ��ָ����ǩ��Ԫ��
litehtml::element::ptr find_first_element_by_tag(
    litehtml::document::ptr doc,
    const std::string& tag_name
) {
    if (!doc) return nullptr;

    // ��Ч�汾 - �ֶ����� head �� body ������Ԫ��
    if (auto root = doc->root()) {
        if (tag_name == "head") {
            // ʹ��ѡ�������� head Ԫ��
            return root->select_one("head");
        }
        else if (tag_name == "body") {
            // ʹ��ѡ�������� body Ԫ��
            return root->select_one("body");
        }
        else if (tag_name == "html") {
            // html Ԫ�ؾ��Ǹ�Ԫ��
            return root;
        }
    }

    // ͨ�ñ�������
    return find_first_element(doc->root(), [&](litehtml::element::ptr el) {
        return tag_name == el->get_tagName();
        });
}

// ��������������Ԫ���ı�����
// ��������������Ԫ���ı�����
void set_element_text(litehtml::element::ptr element, const std::string& text) {
    if (!element) return;

    // 1. ����������� - ֱ�ӱ������Ƴ������ӽڵ�
    const auto& children_list = element->children();
    for (auto it = children_list.begin(); it != children_list.end();) {
        auto child = *it;
        ++it;  // �ȵ������������Ƴ�Ԫ��
        element->removeChild(child);
    }

    // 2. ���û���ı����ݣ�ֱ�ӷ���
    if (text.empty()) return;

    // 3. ����һ�������ı�����Ԫ��
    // ʹ���ĵ���append_children_from_string����
    if (auto doc = element->get_document()) {
        // ת�������ַ�
        std::string escaped_text;
        escaped_text.reserve(text.size());
        for (char c : text) {
            switch (c) {
            case '<': escaped_text += "&lt;"; break;
            case '>': escaped_text += "&gt;"; break;
            case '&': escaped_text += "&amp;"; break;
            case '"': escaped_text += "&quot;"; break;
            case '\'': escaped_text += "&#39;"; break;
            default: escaped_text += c; break;
            }
        }

        // ʹ���ĵ���������ı�����
        doc->append_children_from_string(*element, escaped_text.c_str());
    }
}
//// ���ĵ�������ӱ�������
//void add_title_property_to_document(duk_context* ctx) {
//    duk_get_global_string(ctx, "document");
//
//    // ��ӱ�������getter/setter
//    duk_push_string(ctx, "title");
//    duk_push_c_function(ctx, duk_title_getter, 0);
//    duk_push_c_function(ctx, duk_title_setter, 1);
//    duk_def_prop(
//        ctx, -4,
//        DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_ENUMERABLE
//    );
//
//    duk_pop(ctx); // ����document
//}
//void enhance_document_bindings(duk_context* ctx) {
//    // ȷ���ĵ��Ѵ���
//    duk_get_global_string(ctx, "document");
//
//    // ����ĵ�������������
//    duk_push_string(ctx, "contentType");
//    duk_push_string(ctx, "text/html"); // ����ĵ����ʵ������
//    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_ENUMERABLE);
//
//    // ���charSet����
//    duk_push_string(ctx, "charset");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        // ����ĵ��ַ���
//        const char* charset = "UTF-8"; // ʵ��ʵ�ִ��ĵ���ȡ
//        duk_push_string(ctx, charset);
//        return 1;
//        }, 0);
//    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
//
//    // ���compatMode����
//    duk_push_string(ctx, "compatMode");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        auto doc_ptr = get_binded_document_ptr(ctx);
//        if (doc_ptr && *doc_ptr) {
//            // �����ĵ����ͷ���CSS1Compat/BackCompat
//            duk_push_string(ctx, (*doc_ptr)->is_quirks_mode() ? "BackCompat" : "CSS1Compat");
//        }
//        else {
//            duk_push_string(ctx, "CSS1Compat");
//        }
//        return 1;
//        }, 0);
//    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
//
//    duk_pop(ctx); // ����document
//}
duk_ret_t duk_document_element_getter(duk_context* ctx) {
    /*
    �������ޣ���Ϊ���������ԣ�
    this���ĵ�����
    */

    // 1. ��ȡ��ǰ�ĵ�ָ��
    duk_push_this(ctx); // ��ȡ�ĵ����� [this]

    // ����Ƿ������ǰ󶨵��ĵ�����
    if (!duk_has_prop_string(ctx, -1, "__ptr") ||
        !duk_has_prop_string(ctx, -1, "documentElement")) {
        duk_pop(ctx); // �������ĵ�����
        duk_push_undefined(ctx);
        return 1;
    }

    // 2. ��ȡ�ĵ�ԭ��ָ��
    duk_get_prop_string(ctx, -1, "__ptr"); // [this, doc_ptr]
    auto doc_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop(ctx); // ����doc_ptrָ�� [this]

    if (!doc_ptr || !*doc_ptr) {
        duk_pop(ctx); // ����this
        duk_push_null(ctx);
        return 1;
    }

    // 3. ��ȡ�ĵ���Ԫ�أ�documentElement��
    litehtml::element::ptr root = (*doc_ptr)->root();

    // 4. ������
    if (root) {
        duk_bind_element(ctx, root); // [this, root_elem]
    }
    else {
        duk_push_null(ctx); // [this, null]
    }

    duk_replace(ctx, -2); // �ý���滻this [result]
    return 1;
}


// ��չʵ�������ĵ�����
void add_document_property(duk_context* ctx,
    const char* name,
    duk_c_function getter) {
    duk_push_string(ctx, name);
    duk_push_c_function(ctx, getter, 0);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
}



duk_ret_t duk_document_body_getter(duk_context* ctx) {
    /*
    �������ޣ���Ϊ���������ԣ�
    this���ĵ�����
    */

    // 1. ��ȡ��ǰ�ĵ�ָ��
    duk_push_this(ctx); // ��ȡ�ĵ����� [this]

    //// ����Ƿ������ǰ󶨵��ĵ�����
    //if (!duk_has_prop_string(ctx, -1, "__document_ptr") ||
    //    !duk_has_prop_string(ctx, -1, "getBody")) {
    //    duk_pop(ctx); // �������ĵ�����
    //    duk_push_undefined(ctx);
    //    return 1;
    //}

    // 2. ��ȡ�ĵ�ԭ��ָ��
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "__document_ptr"); // [this, doc_ptr]
    auto doc_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop(ctx); // ����doc_ptrָ�� [this]

    if (!doc_ptr || !*doc_ptr) {
        duk_pop(ctx); // ����this
        duk_push_null(ctx);
 
        return 1;
    }

    // 3. ֱ�ӵ����ĵ�API��ȡbodyԪ��
    litehtml::element::ptr body = (*doc_ptr) -> root()->select_one("body");

    // 4. ������
    if (body) {
        duk_bind_element(ctx, body); // [this, body_elem]
    }
    else {
        duk_push_null(ctx); // [this, null]
    }

    duk_replace(ctx, -2); // �ý���滻this [result]

    return 1;
}



//duk_ret_t duk_remove_event_listener(duk_context* ctx) {
//    /* ������
//       [0] �¼����� (string)
//       [1] �¼������� (function)
//       [2] ��ѡ���� (object/boolean)
//    */
//
//    // 1. ��ȡ�¼�����
//    const char* event_type = duk_require_string(ctx, 0);
//
//    // 2. ����¼�������
//    if (!duk_is_function(ctx, 1)) {
//        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Event handler must be a function");
//        return 0;
//    }
//
//    // 3. ����ѡ�� (useCapture �� options)
//    bool useCapture = false;
//    EventListenerOptions options = { 0 };
//
//    if (duk_is_object(ctx, 2) && !duk_is_boolean(ctx, 2)) {
//        // ���� {capture, passive, once} ����
//        if (duk_get_prop_string(ctx, 2, "capture")) {
//            useCapture = duk_to_boolean(ctx, -1);
//            duk_pop(ctx);
//        }
//        if (duk_get_prop_string(ctx, 2, "passive")) {
//            options.passive = duk_to_boolean(ctx, -1);
//            duk_pop(ctx);
//        }
//        if (duk_get_prop_string(ctx, 2, "once")) {
//            options.once = duk_to_boolean(ctx, -1);
//            duk_pop(ctx);
//        }
//    }
//    else if (duk_is_boolean(ctx, 2)) {
//        // �򵥵�useCapture����
//        useCapture = duk_to_boolean(ctx, 2);
//    }
//
//    // 4. ��ȡ��ǰԪ��
//    duk_push_this(ctx);
//    if (!duk_is_object(ctx, -1)) {
//        duk_pop(ctx);  // ����this
//        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Method called on non-object");
//        return 0;
//    }
//
//    // 5. ��ȡԪ��ָ���DOM����
//    duk_get_prop_string(ctx, -1, "__ptr");
//    if (!duk_is_pointer(ctx, -1)) {
//        duk_pop_2(ctx);  // ����ָ���this
//        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element not bound");
//        return 0;
//    }
//
//    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
//        duk_get_pointer(ctx, -1)
//        );
//    duk_pop(ctx);  // ����ָ�룬����this
//
//    if (!elem_ptr || !*elem_ptr) {
//        duk_pop(ctx);  // ����this
//        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element destroyed");
//        return 0;
//    }
//
//    // 6. ��ȡԪ�ص��¼�������
//    auto event_manager = get_event_manager(*elem_ptr);
//    if (!event_manager) {
//        duk_pop(ctx);  // ����this
//        duk_error(ctx, DUK_ERR_ERROR, "Event system not initialized");
//        return 0;
//    }
//
//    // 7. �����Ƴ��¼�������
//    bool removed = event_manager->remove_event_listener(
//        event_type,
//        [ctx, elem_ptr, event_type](const EventCallback& callback) {
//            // �ȽϺ���������Ƿ���ͬһ��������
//            return are_event_handlers_equal(ctx, 1, callback.js_function);
//        },
//        useCapture,
//        options
//    );
//
//    if (!removed) {
//        // û���ҵ�ƥ��ļ������������Ǵ���
//        duk_pop(ctx);  // ����this
//        duk_push_false(ctx);
//        return 1;
//    }
//
//    // 8. ����JavaScript���ã�����У�
//    cleanup_event_refs(ctx, *elem_ptr, event_type, 1);
//
//    duk_pop(ctx);  // ����this
//    duk_push_true(ctx);
//    return 1;
//}

//// �����������Ƚ������¼��������Ƿ���ͬ
//bool are_event_handlers_equal(duk_context* ctx, duk_idx_t handler_idx, duk_idx_t stored_ref) {
//    if (!duk_is_function(ctx, handler_idx) || stored_ref == 0) {
//        return false;
//    }
//
//    // ��ȡ��ǰ��������
//    duk_dup(ctx, handler_idx);
//
//    // ��ȡ�洢�ĺ�������
//    duk_push_heapptr(ctx, reinterpret_cast<void*>(stored_ref));
//
//    // �Ƚ���������
//    bool equal = duk_samevalue(ctx, -1, -2) != 0;
//
//    duk_pop_2(ctx);  // ���������Ƚϵ�ֵ
//    return equal;
//}
//
//// �����¼�����
//void cleanup_event_refs(duk_context* ctx, litehtml::element::ptr elem, const char* event_type, duk_idx_t handler_idx) {
//    // ��ȡԪ�ص��¼��洢
//    duk_push_global_stash(ctx);
//    duk_get_prop_string(ctx, -1, "__event_refs");
//
//    if (!duk_is_object(ctx, -1)) {
//        duk_pop_2(ctx);
//        return;
//    }
//
//    // ��ȡԪ�ؼ� (ptr��ַ��Ϊkey)
//    char element_key[64];
//    snprintf(element_key, sizeof(element_key), "e%p", elem.get());
//    duk_get_prop_string(ctx, -1, element_key);
//
//    if (!duk_is_object(ctx, -1)) {
//        duk_pop_3(ctx);
//        return;
//    }
//
//    // ��ȡ�¼����ʹ洢
//    duk_get_prop_string(ctx, -1, event_type);
//
//    if (!duk_is_array(ctx, -1)) {
//        duk_pop_n(ctx, 4);  // ����Ϊ pop_n ��ָ������
//        return;
//    }
//    // �������鲢ɾ��ƥ��Ĵ���������
//    duk_size_t len = duk_get_length(ctx, -1);
//    for (duk_uarridx_t i = 0; i < len; i++) {
//        duk_get_prop_index(ctx, -1, i);
//
//        if (duk_get_prop_string(ctx, -1, "handler")) {
//            duk_uarridx_t stored_ref = duk_get_heapptr(ctx, -1);
//            duk_pop(ctx);
//
//            if (are_event_handlers_equal(ctx, handler_idx, stored_ref)) {
//                // ɾ������
//                duk_del_prop_index(ctx, -2, i);
//                duk_push_heapptr(ctx, reinterpret_cast<void*>(stored_ref));
//                duk_unref(ctx, -1);
//                duk_pop(ctx);
//
//                // ��������
//                len--;
//                i--;
//            }
//        }
//
//        duk_pop(ctx);  // ������ǰ����Ԫ��
//    }
//
//    // �����ջ
//    duk_pop_n(ctx, 4);  // ����event_type���� + element���� + refs���� + global stash
//}
duk_ret_t duk_query_selector_all(duk_context* ctx) {
    /* ������
       [0] CSSѡ���� (string)
    */

    // 1. ������֤
    const char* selector = duk_require_string(ctx, 0);
    if (!selector || strlen(selector) == 0) {
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Selector cannot be empty");
        return 0;
    }

    // 2. ȷ���������ĵ���Ԫ�أ�
    std::shared_ptr<litehtml::element> scope;
    std::shared_ptr<litehtml::document> doc;

    duk_push_this(ctx);
    if (duk_is_object(ctx, -1)) {
        // ���this���ĵ�����Ԫ��
        if (duk_has_prop_string(ctx, -1, "getBody")) {
            // �ĵ�����
            auto doc_ptr = get_binded_document_ptr(ctx);
            if (doc_ptr && *doc_ptr) {
                doc = *doc_ptr;
                scope = (*doc_ptr)->root();
            }
        }
        else {
            // Ԫ�ض���
            duk_get_prop_string(ctx, -1, "__ptr");
            if (duk_is_pointer(ctx, -1)) {
                auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
                    duk_to_pointer(ctx, -1)
                    );
                if (elem_ptr && *elem_ptr) {
                    scope = *elem_ptr;
                    doc = scope->get_document();
                }
            }
            duk_pop(ctx);  // ����ָ��
        }
    }
    duk_pop(ctx);  // ����this

    if (!doc) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "No valid document context");
        return 0;
    }


    // 3. ִ��ѡ������ѯ
    litehtml::elements_list elements;
    try {
        // ʹ��Ԫ�ػ��ĵ���select_all����
        elements = scope->select_all(selector);
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_SYNTAX_ERROR, "Invalid selector: %s (%s)", selector, e.what());
        return 0;
    }


    // 4. ���������ؽڵ㼯��
    return create_node_list(ctx, elements, doc);
}

// �����ڵ㼯�ϣ�NodeList��
duk_ret_t create_node_list(duk_context* ctx,
    const litehtml::elements_list& elements,
    std::shared_ptr<litehtml::document> /*doc*/)
{
    /* ���� JS ���飨�� NodeList�� */
    duk_idx_t arr_idx = duk_push_array(ctx);

    /* ���Ԫ�ذ�װ�� JS ����ѹ������ */
    size_t index = 0;
    for (const auto& elem : elements) {
        duk_push_object(ctx);   // [..., newObj]

        /* ���� C++ element ָ�� */
        auto* elem_ptr_ptr = new std::shared_ptr<litehtml::element>(elem);
        duk_push_pointer(ctx, elem_ptr_ptr);
        duk_put_prop_string(ctx, -2, "__ptr");

        /* ���� finalizer���ͷ� shared_ptr */
        duk_push_c_function(ctx,
            [](duk_context* ctx) -> duk_ret_t {
                if (duk_get_prop_string(ctx, 0, "__ptr") && duk_is_pointer(ctx, -1)) {
                    auto ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
                        duk_get_pointer(ctx, -1));
                    delete ptr;
                }
                return 0;
            }, 1);
        duk_set_finalizer(ctx, -2);

        /* �������� */
        duk_put_prop_index(ctx, arr_idx, index++);
    }

    /* length ���� */
    duk_push_uint(ctx, static_cast<duk_uint_t>(index));
    duk_put_prop_string(ctx, arr_idx, "length");

    /* item(index) ���� */
    duk_push_c_function(ctx,
        [](duk_context* ctx) -> duk_ret_t {
            duk_size_t idx = duk_require_uint(ctx, 0);
            duk_push_this(ctx);                       // this (NodeList)
            duk_get_prop_string(ctx, -1, "length");
            duk_size_t len = duk_get_uint(ctx, -1);
            duk_pop(ctx);                             // ���� length

            if (idx < len) {
                duk_get_prop_index(ctx, -1, idx);
                return 1;
            }
            duk_push_null(ctx);
            return 1;
        }, 1);
    duk_put_prop_string(ctx, arr_idx, "item");

    /* forEach(callback[, thisArg]) ������ES5�� */
    duk_push_c_function(ctx,
        [](duk_context* ctx) -> duk_ret_t {
            duk_require_function(ctx, 0);             // callback
            duk_push_this(ctx);                     // NodeList
            duk_size_t len = 0;
            if (duk_get_prop_string(ctx, -1, "length")) {
                len = duk_get_uint(ctx, -1);
            }
            duk_pop(ctx);                             // ���� length

            /* ��ѡ thisArg */
            if (duk_is_undefined(ctx, 1)) {
                duk_push_undefined(ctx);
            }
            else {
                duk_dup(ctx, 1);
            }
            duk_idx_t thisArgIdx = duk_get_top_index(ctx);

            for (duk_size_t i = 0; i < len; ++i) {
                duk_dup(ctx, thisArgIdx);             // thisArg
                duk_dup(ctx, 0);                    // callback
                duk_get_prop_index(ctx, -3, i);     // Ԫ��
                duk_dup(ctx, -1);                   // element
                duk_push_uint(ctx, i);              // index
                duk_dup(ctx, -5);                   // NodeList
                duk_call_method(ctx, 3);            // callback.call(...)
                duk_pop(ctx);                       // �����ص�����ֵ
            }
            return 0;
        }, 1);
    duk_put_prop_string(ctx, arr_idx, "forEach");

    /* ������� Symbol.iterator�����ݴ� ES5 ���� */
    return 1;   // ���� NodeList ����
}

// ��Ԫ�ذ�������querySelectorAll����

//duk_ret_t duk_create_text_node(duk_context* ctx) {
//    /* ������
//       [0] �ı����� (string)
//    */
//
//    // 1. ������֤
//    const char* text = "";
//    if (duk_is_null_or_undefined(ctx, 0)) {
//        // null/undefined ��Ϊ���ַ���
//    }
//    else {
//        text = duk_require_string(ctx, 0);
//    }
//
//    // 2. ��ȡ�ĵ�������
//    auto doc_ptr = get_binded_document_ptr(ctx);
//    if (!doc_ptr || !*doc_ptr) {
//        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Document context not available");
//        return 0;
//    }
//
//    // 3. �����ı��ڵ�
//    litehtml::element::ptr text_node;
//    try {
//        text_node = std::make_shared<litehtml::el_text>(text, *doc_ptr);
//    }
//    catch (const std::exception& e) {
//        duk_error(ctx, DUK_ERR_ERROR, "Text node creation failed: %s", e.what());
//        return 0;
//    }
//
//    // 4. ����JS���󲢰�
//    duk_idx_t obj_idx = duk_push_object(ctx);
//
//    // �洢ԭ��ָ��
//    auto* text_ptr = new std::shared_ptr<litehtml::element>(text_node);
//    duk_push_pointer(ctx, text_ptr);
//    duk_put_prop_string(ctx, obj_idx, "__ptr");
//
//    // �����ı��ڵ�ר������
//    duk_push_string(ctx, "nodeType");
//    duk_push_int(ctx, 3);  // Node.TEXT_NODE = 3
//    duk_def_prop(ctx, obj_idx, DUK_DEFPROP_CLEAR_WRITABLE | DUK_DEFPROP_SET_ENUMERABLE);
//
//    duk_push_string(ctx, "nodeName");
//    duk_push_string(ctx, "#text");
//    duk_def_prop(ctx, obj_idx, DUK_DEFPROP_CLEAR_WRITABLE | DUK_DEFPROP_SET_ENUMERABLE);
//
//    // ���data���� (getter/setter)
//    duk_push_string(ctx, "data");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        // getter
//        duk_push_this(ctx);
//        duk_get_prop_string(ctx, -1, "__ptr");
//        auto text_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
//        duk_pop_2(ctx);  // ����ָ���this
//
//        if (text_ptr && *text_ptr) {
//            // �����ı��ڵ�ʵ���� get_text ����
//            std::string text;
//            (*text_ptr)->get_text(text);
//            duk_push_string(ctx, text.c_str());
//            return 1;
//        }
//        duk_push_string(ctx, "");
//        return 1;
//        }, 0);
//
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        // setter
//        const char* new_text = duk_require_string(ctx, 0);
//        duk_push_this(ctx);
//        duk_get_prop_string(ctx, -1, "__ptr");
//        auto text_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
//        duk_pop_2(ctx);  // ����ָ���this
//
//        if (text_ptr && *text_ptr) {
//            // �����ı��ڵ�ʵ���� set_text ����
//            (*text_ptr)->set_data(new_text);
//        }
//        return 0;
//        }, 1);
//
//    duk_def_prop(ctx, obj_idx, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_SET_ENUMERABLE);
//
//    // 5. ����ԭ�� (Text.prototype)
//    duk_get_global_string(ctx, "TextPrototype");
//    if (duk_is_object(ctx, -1)) {
//        duk_set_prototype(ctx, obj_idx);
//    }
//    else {
//        duk_pop(ctx);  // �����Ƕ���ֵ
//        // ���˵���������ԭ��
//        duk_get_global_string(ctx, "Object");
//        duk_get_prop_string(ctx, -1, "prototype");
//        duk_set_prototype(ctx, obj_idx);
//        duk_pop(ctx);  // ����Object
//    }
//
//    // 6. ���� finalizer �ͷ��ڴ�
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        if (duk_get_prop_string(ctx, -1, "__ptr")) {
//            auto ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
//                duk_get_pointer(ctx, -1)
//                );
//            if (ptr) {
//                delete ptr;
//            }
//            duk_del_prop_string(ctx, -2, "__ptr");
//        }
//        return 0;
//        }, 1);
//    duk_set_finalizer(ctx, obj_idx);
//
//    return 1;  // �����ı��ڵ����
//}
duk_ret_t duk_create_element(duk_context* ctx) {
    /* ������
       [0] ��ǩ�� (string)
    */

    // 1. ����У��
    const char* tag_name = duk_require_string(ctx, 0); // ǿ���ַ�������
    if (!tag_name || strlen(tag_name) == 0) {
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Element tag name cannot be empty");
        return 0;
    }

    // 2. ��ȡ�ĵ�������
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Document context is not available");
        return 0;
    }

    // 3. ����ԭ��Ԫ�ض���
    litehtml::element::ptr new_element;
    try {
        litehtml::string_map attributes;  // ����һ��������ӳ��
        new_element = (*doc_ptr)->create_element(tag_name, attributes);
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "Failed to create element: %s", e.what());
        return 0;
    }

    // 4. �󶨵�JS����
    if (new_element) {
        duk_bind_element(ctx, new_element);

        // ����Ĭ������
        duk_push_string(ctx, "tagName");
        duk_push_string(ctx, tag_name);
        duk_put_prop(ctx, -3);  // elem.tagName = tag_name

        // ����ԭ����
        duk_get_global_string(ctx, "ElementPrototype");
        duk_set_prototype(ctx, -2);

        return 1;  // ������Ԫ��
    }

    duk_push_null(ctx);
    return 1;
}

duk_ret_t duk_document_get_body(duk_context* ctx) {
    // 1. ��ȡ�ĵ�ָ��
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_push_null(ctx);
        return 1;  // �����Ǵ��󣬷���DOM�淶
    }

    // 2. ��ȡbodyԪ��
    litehtml::element::ptr body = (*doc_ptr)->root()->select_one("body"); 

    // 3. ������
    if (body) {
        duk_bind_element(ctx, body);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;  // ����bodyԪ�ػ�null
}

duk_ret_t duk_get_elements_by_tag_name(duk_context* ctx) {
    // ��ȡ��ǩ������
    const char* tag_name = duk_require_string(ctx, 0);

    // ������ĵ����û���Ԫ�ص���
    bool is_document = false;

    // ���Ի�ȡ�ĵ�ָ��
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (doc_ptr && *doc_ptr) {
        is_document = true;
    }
    else {
        // ������Ԫ�ص��õ� - �� this ��ȡԪ��ָ��
        duk_push_this(ctx);
        if (duk_is_object(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "__ptr");
            if (duk_is_pointer(ctx, -1)) {
                auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
                    duk_to_pointer(ctx, -1)
                    );
                if (elem_ptr && *elem_ptr) {
                    // ��ȡԪ�ص��ĵ�ָ��
                    doc_ptr = new std::shared_ptr<litehtml::document>((*elem_ptr)->get_document());
                    is_document = false;
                }
            }
            duk_pop(ctx);  // ����ָ��
        }
        duk_pop(ctx);  // ���� this
    }

    if (!doc_ptr || !*doc_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "No document context available");
        return 0;
    }

    // ��ȡԪ�ؼ���
    litehtml::elements_list elements; // ʹ�� elements_list ��� elements_vector
    if (is_document) {
        // �ĵ����� - ʹ�� CSS ѡ������ȡԪ��
        if (auto root = (*doc_ptr)->root()) {
            elements = root->select_all(tag_name);
        }
    }
    else {
        // Ԫ�ص��� - ʹ�� CSS ѡ������ȡ��Ԫ��
        duk_push_this(ctx);
        duk_get_prop_string(ctx, -1, "__ptr");
        auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
            duk_to_pointer(ctx, -1)
            );
        duk_pop_2(ctx);  // ����ָ��� this
        if (elem_ptr && *elem_ptr) {
            elements = (*elem_ptr)->select_all(tag_name);
        }
    }

    // �����������
    duk_idx_t arr_idx = duk_push_array(ctx);

    // ����ƥ���Ԫ�أ��󶨵�JS����ʹ�õ�������
    int index = 0;
    for (auto it = elements.begin(); it != elements.end(); ++it) {
        duk_bind_element(ctx, *it);  // �����õ�������ȡԪ��ָ��
        duk_put_prop_index(ctx, arr_idx, index++);
    }

    return 1;  // ��������
}
// �����ı��ڵ�
duk_ret_t duk_create_text_node(duk_context* ctx) {
    const char* text = duk_get_string(ctx, 0);
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !text) return 0;

    // �����ı��ڵ�
    auto text_node = std::make_shared<litehtml::el_text>(text, *doc_ptr);

    // �󶨵�JS����
    duk_bind_element(ctx, text_node);
    return 1;
}

// querySelectorʵ��
duk_ret_t duk_query_selector(duk_context* ctx) {
    // 1. ��ȡCSSѡ��������
    const char* selector = duk_get_string(ctx, 0);
    if (!selector) {
        duk_push_null(ctx);
        return 1;
    }

    // 2. ��ȡ�󶨵��ĵ�ָ��
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr) {
        duk_push_null(ctx);
        return 1;
    }

    // 3. ͨ���ĵ���Ԫ��ִ�в�ѯ
    litehtml::element::ptr element = nullptr;
    if (auto root = (*doc_ptr)->root()) {
        element = root->select_one(selector);
    }

    // 4. �󶨽����JS����򷵻�null
    if (element) {
        duk_bind_element(ctx, element);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;
}

//// ����¼�������
//duk_ret_t duk_add_event_listener(duk_context* ctx) {
//    const char* event_type = duk_get_string(ctx, 0);
//    if (!duk_is_function(ctx, 1)) return 0;
//
//    // ���ƺ������õ�heap
//    duk_dup(ctx, 1);
//    int func_ref = duk_require_heapptr(ctx, -1);
//    duk_pop(ctx);
//
//    auto doc_ptr = get_binded_document_ptr(ctx);
//    if (!doc_ptr || !event_type) return 0;
//
//    // �洢�ص�����
//    (*doc_ptr)->addEventListener(event_type, [ctx, func_ref](const litehtml::Event& event) {
//        // ���ûص�
//        duk_push_heapptr(ctx, func_ref);
//        duk_bind_element(ctx, event.target);
//        duk_call(ctx, 1);
//        duk_pop(ctx);
//        });
//
//    return 0;
//}


void set_element_inner_html(const litehtml::element::ptr& elem, const char* html) {
    if (!elem || !html || !*html) return;

    try {
        // ��ȡ�ĵ�
        auto doc = elem->get_document();
        if (!doc) return;

        // ��ȡ�ĵ�����
        auto container = doc->container();
        if (!container) return;

        // ������ʱ�ĵ����������� HTML
        auto frag_doc = litehtml::document::createFromString(html, container);
        if (!frag_doc) return;

        // ���Ŀ��Ԫ�ص��ӽڵ�
        elem->clearRecursive();

        // ��ȡ��ʱ�ĵ����ڵ�������ӽڵ�
        const auto& children = frag_doc->root()->children();

        // ����Щ�ӽڵ��ƶ���Ŀ��Ԫ����
        for (const auto& child : children) {
            // ֱ�Ӵ���ʱ�ĵ����ڵ��Ƴ�
            frag_doc->root()->removeChild(child);

            // ��ӵ�Ŀ��Ԫ��
            elem->appendChild(child);
        }

        // �����ڴ˴�ִ����Ⱦ�����ⲿͳһ����
        // ��JS������ɺ�ͳһִ�� doc->render()

    }
    catch (const std::exception& e) {
        // ������
        std::cerr << "setInnerHTML error: " << e.what() << std::endl;
    }
}
// innerHTML��setterʵ��
duk_ret_t duk_element_set_inner_html(duk_context* ctx) {
    // ��ȡ����
    const char* html = duk_require_string(ctx, 0);

    // ��ȡ��ǰԪ��
    duk_push_this(ctx);
    auto elem = get_binded_element(ctx, -1);

    // ���Ԫ����Ч��
    if (!elem) {
        duk_pop(ctx); // ���� this
        return duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Invalid element");
    }

    // ���� innerHTML
    set_element_inner_html(elem, html);

    // �����ջ������
    duk_pop(ctx); // ���� this
    return 0;
}

// ������������ȡ�󶨵�Ԫ��
std::shared_ptr<litehtml::element> get_binded_element(duk_context* ctx, duk_idx_t idx) {
    if (!duk_is_object(ctx, idx)) {
        return nullptr;
    }

    duk_get_prop_string(ctx, idx, "__ptr");
    if (!duk_is_pointer(ctx, -1)) {
        duk_pop(ctx);
        return nullptr;
    }

    auto ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop(ctx);

    return ptr ? *ptr : nullptr;
}

//// ����¼�ģ��
//duk_ret_t duk_element_click(duk_context* ctx) {
//    duk_push_this(ctx);
//    duk_get_prop_string(ctx, -1, "__ptr");
//    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
//        duk_to_pointer(ctx, -1)
//        );
//    duk_pop_2(ctx);
//
//    if (elem_ptr && *elem_ptr) {
//        litehtml::Event ev;
//        ev.type = "click";
//        ev.target = *elem_ptr;
//        (*elem_ptr)->dispatchEvent(ev);
//    }
//
//    return 0;
//}

// �ĵ�����ִ��
//duk_ret_t duk_document_exec_command(duk_context* ctx) {
//    const char* command = duk_get_string(ctx, 0);
//    duk_bool_t showUI = duk_get_boolean(ctx, 1);
//    const char* value = duk_get_string(ctx, 2);
//
//    auto doc_ptr = get_binded_document_ptr(ctx);
//    if (!doc_ptr || !command) return 0;
//
//    bool result = (*doc_ptr)->execCommand(command, showUI != 0, value ? value : "");
//    duk_push_boolean(ctx, result);
//    return 1;
//}

//// Ԫ���¼��ص���¼�ṹ
//struct event_listener_record {
//    std::string event_type;
//    int callback_ref;  // Duktape ������
//    bool use_capture;
//};
//
//// ����¼�������
//duk_ret_t duk_element_add_event_listener(duk_context* ctx) {
//    /* ������
//        [0] �¼����� (string)
//        [1] �ص����� (function)
//        [2] useCapture (boolean, ��ѡ)
//    */
//
//    // 1. ����У��
//    const char* event_type = duk_get_string(ctx, 0);
//    if (!event_type || !duk_is_function(ctx, 1)) {
//        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid arguments for addEventListener");
//        return 0;
//    }
//
//    bool use_capture = false;
//    if (duk_is_boolean(ctx, 2)) {
//        use_capture = duk_get_boolean(ctx, 2) != 0;
//    }
//
//    // 2. ��ȡ��ǰԪ�ض��󣨴� this �󶨣�
//    duk_push_this(ctx);  // [event_type, callback, use_capture? this]
//    duk_get_prop_string(ctx, -1, "__ptr");  // [..., elem_ptr]
//
//    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
//        duk_get_pointer(ctx, -1)
//        );
//    duk_pop(ctx);  // ����ָ�� -> [event_type, callback, use_capture? this]
//
//    if (!elem_ptr || !*elem_ptr) {
//        duk_pop(ctx);  // ���� this
//        return DUK_RET_TYPE_ERROR;
//    }
//
//    // 3. �����¼��ص���¼
//    // �־û���������
//    duk_dup(ctx, 1);  // [..., this, callback]
//    int callback_ref = duk_require_heapptr(ctx, -1);
//
//    // 4. �洢�ص����õ�Ԫ�صĻص��б�
//    const char* kEventListenersKey = "\xFFevent_listeners";
//    duk_get_prop_string(ctx, -2, kEventListenersKey);  // [..., this, callback, listeners?]
//
//    if (duk_is_undefined(ctx, -1)) {
//        duk_pop(ctx);  // ���� undefined
//        duk_push_array(ctx);  // [..., this, callback, new_listeners]
//    }
//    duk_swap_top(ctx, -2);  // [..., this, listeners, callback] -> [..., this, callback, listeners]
//
//    // �� listeners ��������Ӽ�¼����
//    duk_push_object(ctx);  // [..., this, callback, listeners, record]
//    duk_push_string(ctx, "type");  // [..., listeners, record, "type"]
//    duk_push_string(ctx, event_type); // [..., "type", event_type]
//    duk_put_prop(ctx, -3);  // record.type = event_type
//
//    duk_push_string(ctx, "ref");
//    duk_push_heapptr(ctx, reinterpret_cast<void*>(callback_ref));
//    duk_put_prop(ctx, -3);  // record.ref = callback_ref
//
//    duk_push_string(ctx, "capture");
//    duk_push_boolean(ctx, use_capture);
//    duk_put_prop(ctx, -3);  // record.capture = use_capture
//
//    // ��ӵ� listeners ����
//    duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2));  // listeners.push(record)
//
//    // ������º�� listeners ����
//    duk_dup(ctx, -1);  // [..., this, callback, listeners, listeners]
//    duk_put_prop_string(ctx, -4, kEventListenersKey);  // this[kEventListenersKey] = listeners
//
//    duk_pop_2(ctx);  // ���� listeners �� callback -> [..., this]
//    duk_pop(ctx);  // ���� this
//
//    // 5. �� native Ԫ��ע���¼�
//    (*elem_ptr)->addEventListener(event_type, [ctx, callback_ref](const litehtml::Event& event) {
//        duk_push_heapptr(ctx, reinterpret_cast<void*>(callback_ref));  // ���ͻص�����
//
//        // �����¼�����
//        duk_idx_t event_idx = duk_push_object(ctx);
//
//        // �����¼�����
//        duk_push_string(ctx, "type");
//        duk_push_string(ctx, event.type.c_str());
//        duk_put_prop(ctx, event_idx);
//
//        duk_push_string(ctx, "target");
//        duk_bind_element(ctx, event.target);
//        duk_put_prop(ctx, event_idx);
//
//        duk_push_string(ctx, "currentTarget");
//        duk_bind_element(ctx, event.currentTarget);
//        duk_put_prop(ctx, event_idx);
//
//        // ���ûص����� (event)
//        if (duk_pcall(ctx, 1) != 0) {
//            // ����JS����
//            const char* err = duk_safe_to_string(ctx, -1);
//            OutputDebugStringA("Event handler error: ");
//            OutputDebugStringA(err);
//            OutputDebugStringA("\n");
//            duk_pop(ctx);
//        }
//        else {
//            duk_pop(ctx);  // ���Է���ֵ
//        }
//        });
//
//    return 0;
//}

//// ������������ȡ��ǰԪ�ص��¼��ص��б�
//std::vector<event_listener_record> get_element_event_listeners(duk_context* ctx, duk_idx_t obj_idx) {
//    std::vector<event_listener_record> listeners;
//
//    duk_get_prop_string(ctx, obj_idx, "\xFFevent_listeners");
//    if (!duk_is_array(ctx, -1)) {
//        duk_pop(ctx);
//        return listeners;
//    }
//
//    duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
//    while (duk_next(ctx, -1, 1)) {
//        duk_get_prop_string(ctx, -1, "type");
//        const char* type = duk_get_string(ctx, -1);
//        duk_pop(ctx);
//
//        duk_get_prop_string(ctx, -1, "ref");
//        int ref = duk_require_heapptr(ctx, -1);
//        duk_pop(ctx);
//
//        duk_get_prop_string(ctx, -1, "capture");
//        bool capture = duk_get_boolean(ctx, -1);
//        duk_pop(ctx);
//
//        listeners.push_back({ type ? type : "", ref, capture });
//        duk_pop_2(ctx);  // ����key��ֵ
//    }
//    duk_pop_2(ctx);  // ����ö����������
//
//    return listeners;
//}
//
//// �Ƴ��¼�������
//duk_ret_t duk_element_remove_event_listener(duk_context* ctx) {
//    /* ����:
//        [0] �¼����� (string)
//        [1] �ص����� (function)
//    */
//
//    const char* event_type = duk_get_string(ctx, 0);
//    if (!duk_is_function(ctx, 1)) return 0;
//
//    duk_push_this(ctx);
//
//    // ��ȡ��ǰ�Ļص�����
//    duk_dup(ctx, 1);
//    int callback_ref = duk_require_heapptr(ctx, -1);
//    duk_pop(ctx);
//
//    // ��ȡԪ��ָ��
//    duk_get_prop_string(ctx, -1, "__ptr");
//    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
//    duk_pop(ctx);
//
//    if (!elem_ptr || !*elem_ptr) {
//        duk_pop(ctx);  // ���� this
//        return 0;
//    }
//
//    // ��ȡ�¼��������б�
//    duk_get_prop_string(ctx, -1, "\xFFevent_listeners");
//    if (!duk_is_array(ctx, -1)) {
//        duk_pop_2(ctx);
//        return 0;
//    }
//
//    // ���Ҳ��Ƴ�ƥ��Ļص�
//    bool found = false;
//    duk_size_t len = duk_get_length(ctx, -1);
//    for (duk_size_t i = 0; i < len; ++i) {
//        duk_get_prop_index(ctx, -1, i);
//        duk_get_prop_string(ctx, -1, "type");
//        const char* type = duk_get_string(ctx, -1);
//        duk_pop(ctx);
//
//        duk_get_prop_string(ctx, -1, "ref");
//        int ref = duk_require_heapptr(ctx, -1);
//        duk_pop(ctx);
//
//        if (type && strcmp(type, event_type) == 0 && ref == callback_ref) {
//            // ��JS�������Ƴ�
//            duk_del_prop_index(ctx, -2, i);
//
//            // ��nativeԪ�����Ƴ��¼�
//            (*elem_ptr)->removeEventListener(event_type);
//            found = true;
//            break;
//        }
//        duk_pop(ctx);  // ������ǰԪ��
//    }
//
//    duk_pop_2(ctx);  // ��������� this
//
//    return found ? 1 : 0;
//}
//// Ԫ�ذ���ǿ�����ID���Է��ʣ�
//void duk_bind_element(duk_context* ctx, litehtml::element::ptr elem) {
//    duk_push_object(ctx);
//
//    // �洢ԭ��ָ��
//    auto* elem_ptr = new std::shared_ptr<litehtml::element>(elem);
//    duk_push_pointer(ctx, elem_ptr);
//    duk_put_prop_string(ctx, -2, "__ptr");
//
//    // ���ID���Է�����
//    duk_push_string(ctx, "id");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        duk_push_this(ctx);
//        duk_get_prop_string(ctx, -1, "__ptr");
//        auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_to_pointer(ctx, -1));
//        duk_pop_2(ctx);  // ����ָ���this
//
//        if (elem_ptr && *elem_ptr) {
//            duk_push_string(ctx, (*elem_ptr)->get_attribute("id").c_str());
//            return 1;
//        }
//        duk_push_undefined(ctx);
//        return 1;
//        }, 0);  // getter
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        const char* new_id = duk_require_string(ctx, 0);
//        duk_push_this(ctx);
//        duk_get_prop_string(ctx, -1, "__ptr");
//        auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_to_pointer(ctx, -1));
//        duk_pop_2(ctx);  // ����ָ���this
//
//        if (elem_ptr && *elem_ptr) {
//            (*elem_ptr)->set_attr("id", new_id ? new_id : "");
//        }
//        return 0;
//        }, 1);  // setter
//    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);
//
//    // ... �������Ժͷ��� ...
//}


//// Ȼ���� .cpp �ļ���ʵ������������
//static duk_ret_t duk_element_get_attribute_helper(duk_context* ctx, const char* attr_name) {
//    duk_push_this(ctx);
//    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("__element"));
//    litehtml::element* element = static_cast<litehtml::element*>(duk_get_pointer(ctx, -1));
//    duk_pop_2(ctx); // ����ָ��� this
//
//    if (element) {
//        if (const char* value = element->get_attribute(attr_name)) {
//            duk_push_string(ctx, value);
//            return 1;
//        }
//    }
//
//    duk_push_undefined(ctx);
//    return 1;
//}
//
//static duk_ret_t duk_element_set_attribute_helper(duk_context* ctx, const char* attr_name, duk_idx_t value_idx) {
//    duk_push_this(ctx);
//    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("__element"));
//    litehtml::element* element = static_cast<litehtml::element*>(duk_get_pointer(ctx, -1));
//    duk_pop_2(ctx); // ����ָ��� this
//
//    if (!element) {
//        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid element binding");
//        return DUK_RET_ERROR;
//    }
//
//    const char* value = duk_require_string(ctx, value_idx);
//    element->set_attribute(attr_name, value ? value : "");
//    return 0;
//}
duk_ret_t duk_get_element_by_id(duk_context* ctx) {
    // 1. ��ȡID����
    const char* id = duk_require_string(ctx, 0);
    if (!id || *id == '\0') {
        duk_push_null(ctx);
        return 1;
    }

    // 2. ��ȡ�󶨵��ĵ�ָ��
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_push_null(ctx);
        return 1;
    }

    // 3. ʹ��IDѡ��������Ԫ��
    litehtml::element::ptr elem = nullptr;
    if (auto root = (*doc_ptr)->root()) {
        // ����IDѡ������ת�������ַ�
        std::string selector = "#";
        // ���IDת�崦��
        for (const char* c = id; *c; c++) {
            if (is_id_char(*c)) {
                selector += *c;
            }
            else {
                // ��ת��
                selector += '\\';
                selector += *c;
            }
        }
        elem = root->select_one(selector.c_str());
    }

    // 4. �󶨽����JS����
    if (elem) {
        duk_bind_element(ctx, elem);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;
}

// �����������Ϸ�ID�ַ�
static bool is_id_char(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' ||
        (unsigned char)c > 0x7F; // ��ASCII�ַ�
}

// ��ǿ�汾�� get_binded_document_ptr (��Ӵ�����)
static std::shared_ptr<litehtml::document>* get_binded_document_ptr_safe(duk_context* ctx) {
    std::shared_ptr<litehtml::document>* doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Document context lost");
        return nullptr;
    }
    if (!*doc_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Document has been destroyed");
        return nullptr;
    }
    return doc_ptr;
}

