// 辅助函数：从Stash获取绑定的document

#include "duktape_engine.h"

// ========== 函数声明 =========
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



/* ---------- 构造 / 析构 ---------- */
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

/* ---------- 生命周期 ---------- */
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

/* ---------- 执行脚本 ---------- */
void duktape_engine::eval(const std::string& code,
    const std::string& filename)
{
    if (!ctx_) { OutputDebugStringA("ctx is null\n"); return; }
    duk_idx_t top = duk_get_top(ctx_);          // 记住栈顶
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
    duk_set_top(ctx_, top);                     // 强制恢复栈平衡

}

/* ---------- 事件循环 ---------- */
void duktape_engine::pump_tasks()
{
}

// 绑定文档的实现 =======================================================
void duktape_engine::bind_document(litehtml::document* doc) {
    // 创建文档指针（使用空删除器）
    auto* doc_ptr = new std::shared_ptr<litehtml::document>(
        doc, [](litehtml::document*) {}
    );

    if (!ctx_) {
        OutputDebugStringA("[DUK] bind_document: ctx_ is null\n");
        delete doc_ptr;
        return;
    }

    OutputDebugStringA("[DUK] === bind_document start ===\n");

    // 更新全局Stash
    duk_push_global_stash(ctx_);

    // 清理旧文档指针
    if (duk_get_prop_string(ctx_, -1, "__document_ptr")) {
        if (duk_is_pointer(ctx_, -1)) {
            auto old_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(
                duk_get_pointer(ctx_, -1)
                );
            delete old_ptr;
        }
        duk_pop(ctx_);  // 弹出旧指针
    }
    else {
        duk_pop(ctx_);
    }
    // 存储新指针
    duk_push_pointer(ctx_, doc_ptr);
    duk_put_prop_string(ctx_, -2, "__document_ptr");
    duk_pop(ctx_);

    // 创建文档对象
    duk_push_global_object(ctx_);
    duk_push_object(ctx_);                      // [global, document]

    // 添加基础方法
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

      //// 添加高级API
      //duk_push_c_function(ctx_, duk_document_exec_command, 3);
      //duk_put_prop_string(ctx_, -2, "execCommand");

      // 添加属性访问器
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

    // 创建元素原型
    duk_push_object(ctx_);                      // [..., ElementPrototype]
    duk_put_prop_string(ctx_, -2, "ElementPrototype"); // document.ElementPrototype = object

    // 挂到全局
    duk_put_prop_string(ctx_, -2, "document"); // global.document = document
    duk_pop(ctx_);                              // []

    OutputDebugStringA("[DUK] === bind_document end ===\n");
}


// 绑定元素到JS对象
void duk_bind_element(duk_context* ctx, litehtml::element::ptr elem) {
    if (!elem) {
        duk_push_null(ctx);
        return;
    }

    // 创建元素对象
    duk_push_object(ctx);                       // [elem]

    // 设置元素指针
    auto* elem_ptr = new std::shared_ptr<litehtml::element>(elem);
    duk_push_pointer(ctx, elem_ptr);          // [elem, ptr]
    duk_put_prop_string(ctx, -2, "__ptr");     // elem["__ptr"] = ptr

    // 添加通用方法
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

    // 添加 className 属性
    duk_push_string(ctx, "className");
    duk_push_c_function(ctx, duk_element_classname_getter, 0);
    duk_push_c_function(ctx, duk_element_classname_setter, 1);
    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER |
        DUK_DEFPROP_HAVE_SETTER |
        DUK_DEFPROP_ENUMERABLE);

    //duk_push_c_function(ctx, duk_element_click, 0);
    //duk_put_prop_string(ctx, -2, "click");

    // 设置prototype链
    duk_push_global_object(ctx);            // [elem, global]
    duk_get_prop_string(ctx, -1, "ElementPrototype"); // [elem, global, proto]
    duk_set_prototype(ctx, -3);             // elem.prototype = proto
    duk_pop(ctx);                           // [elem] 
}


// className getter 实现
static duk_ret_t duk_element_classname_getter(duk_context* ctx) {
    duk_push_this(ctx); // 获取当前对象
    duk_get_prop_string(ctx, -1, "__ptr"); // 获取元素指针
    auto* elem_ptr = static_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
    duk_pop_2(ctx); // 弹出栈顶的指针和this

    if (elem_ptr && *elem_ptr) {
        const char* className = (*elem_ptr)->get_attr("class");
        duk_push_string(ctx, className ? className : "");
        return 1;
    }

    duk_push_string(ctx, "");
    return 1;
}

// className setter 实现
static duk_ret_t duk_element_classname_setter(duk_context* ctx) {
    // 参数0是新的className值
    const char* new_class = duk_require_string(ctx, 0);

    duk_push_this(ctx); // 获取当前对象
    duk_get_prop_string(ctx, -1, "__ptr"); // 获取元素指针
    auto* elem_ptr = static_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
    duk_pop_2(ctx); // 弹出栈顶的指针和this

    if (elem_ptr && *elem_ptr) {
        (*elem_ptr)->set_attr("class", new_class);

        //// 重要：通知文档样式需要更新
        //auto doc = (*elem_ptr)->get_document();
        //if (doc) {
        //    // 标记元素需要重新计算样式
        //    (*elem_ptr)->refresh_styles();

        //    // 如果文档支持重新布局
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
// 辅助函数：绑定文档和元素 =============================================

// 从stash获取绑定文档指针
static std::shared_ptr<litehtml::document>* get_binded_document_ptr(duk_context* ctx) {
    duk_push_global_stash(ctx);                // [stash]
    duk_get_prop_string(ctx, -1, "__document_ptr"); // [stash, ptr?]
    std::shared_ptr<litehtml::document>* doc_ptr = nullptr;
    if (duk_is_pointer(ctx, -1)) {
        doc_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(duk_to_pointer(ctx, -1));
    }
    duk_pop_2(ctx);  // 弹出ptr和stash -> []
    return doc_ptr;   // 可能返回nullptr
}


// querySelector - 获取匹配的第一个元素
duk_ret_t duk_element_query_selector(duk_context* ctx) {
    /* 参数：
       [0] 选择器字符串
       this：元素对象
    */

    // 1. 获取选择器参数
    const char* selector = duk_require_string(ctx, 0);

    // 2. 获取当前元素指针
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
    duk_pop_2(ctx);; // 弹出ptr和this [空栈]

    if (!elem_ptr || !*elem_ptr) {
        duk_push_null(ctx);
        return 1;
    }

    auto element = *elem_ptr;

    // 3. 执行选择器查询
    try {
        auto result = element->select_one(selector);

        // 4. 处理查询结果
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

// querySelectorAll - 获取所有匹配的元素
duk_ret_t duk_element_query_selector_all(duk_context* ctx) {
    /* 参数：
       [0] 选择器字符串
       this：元素对象
    */

    // 1. 获取选择器参数
    const char* selector = duk_require_string(ctx, 0);

    // 2. 获取当前元素指针
    duk_push_this(ctx); // [this]
    duk_get_prop_string(ctx, -1, "__ptr"); // [this, ptr]

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx);
        duk_push_array(ctx); // 返回空数组
        return 1;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // 弹出ptr和this [空栈]

    if (!elem_ptr || !*elem_ptr) {
        duk_push_array(ctx); // 返回空数组
        return 1;
    }

    auto element = *elem_ptr;

    // 3. 执行选择器查询
    try {
        litehtml::elements_list results = element->select_all(selector);
        duk_idx_t arr_idx = duk_push_array(ctx);

        // 正确遍历 std::list 的方式
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
    /* 参数：无
       this：元素对象
    */

    // 1. 获取元素指针
    duk_push_this(ctx); // [this]
    duk_get_prop_string(ctx, -1, "__ptr"); // [this, ptr]

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx); // 清除栈
        duk_push_string(ctx, "");
        return 1;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // 弹出指针和this [空栈]

    if (!elem_ptr || !*elem_ptr) {
        duk_push_string(ctx, "");
        return 1;
    }

    auto element = *elem_ptr;

    // 2. 序列化所有子节点为HTML字符串
    std::string html;

    // 遍历所有子节点
    for (auto& child : element->children()) {
        serialize_node(child, html);
    }

    // 3. 返回HTML字符串
    duk_push_string(ctx, html.c_str());
    return 1;
}

// 节点序列化函数
void serialize_node(litehtml::element::ptr node, std::string& output) {
    if (!node) return;

    if (node->is_text()) {
        // 文本节点：HTML转义
        std::string txt;
        node->get_text(txt);
        output += escape_html(txt);
        return;
    }

    // 注释节点
    if (node->is_comment()) {
        output += "<!--";
        std::string txt;
        node->get_text(txt);
        output += txt;
        output += "-->";
        return;
    }

    // 普通元素节点
    output += "<" + std::string(node->get_tagName());

    // 序列化所有属性（适配新版litehtml接口）
    auto attrs = node->dump_get_attrs();
    for (const auto& attr_tuple : attrs) {
        output += " " + std::get<0>(attr_tuple) + "=\""
            + escape_html_attribute(std::get<1>(attr_tuple))
            + "\"";
    }

    for (int i = 0; i < static_cast<int>(node->children().size()); i++) {
        // 自闭合标签（如<img>, <br>等）
        output += " />";
        return;
    }

    // 正常闭合标签
    output += ">";

    // 序列化子节点
// 序列化子节点
    for (const auto& child : node->children()) {
        serialize_node(child, output);
    }

    // 安全获取标签名（含空指针检查）
    if (const char* tag_name = node->get_tagName()) {
        output += "</" + std::string(tag_name) + ">";
    }
    else {
        // 异常处理：输出空标签或记录错误
        output += "</>";
        // 建议添加错误日志：
        // log_error("Element has invalid tag name at %p", static_cast<void*>(node));
    }
}

// HTML转义函数
std::string escape_html(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 1.2); // 预分配空间

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

// HTML属性值转义
std::string escape_html_attribute(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 1.2); // 预分配空间

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


// 设置元素属性
duk_ret_t duk_element_set_attribute(duk_context* ctx) {
    /* 参数：
       [0] 属性名 (string)
       [1] 属性值 (string)
       this：元素对象
    */

    // 1. 验证参数
    const char* attr_name = duk_require_string(ctx, 0);
    const char* attr_value = duk_require_string(ctx, 1);

    // 2. 获取元素指针
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "__ptr");

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx); // 弹出指针和this
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid element");
        return 0;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // 弹出指针和this

    if (!elem_ptr || !*elem_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element has been destroyed");
        return 0;
    }

    // 3. 特殊属性处理 - class
    if (strcmp(attr_name, "class") == 0) {
        litehtml::element::ptr element = *elem_ptr;

        // 处理类名操作
        if (attr_value && attr_value[0] != '\0') {
            // 检查是否已有类名
            if (const char* existing = element->get_attr("class", nullptr)) {
                // 已有类名：添加新类名
                element->set_class(attr_value, true);
            }
            else {
                // 没有类名：设置新类名
                element->set_class(attr_value, false);
            }
        }
        else {
            // 删除类名：设置为空字符串
            element->set_attr("class", "");
        }
        return 0;
    }

    if (strcmp(attr_name, "style") == 0) {
        // style属性直接设置行内样式
        (*elem_ptr)->set_attr("style", attr_value);

        //// 同时触发样式重新计算
        //if (auto doc = (*elem_ptr)->get_document()) {
        //    doc->on_media_changed();
        //}
        return 0;
    }

    // 4. 设置属性
    try {
        (*elem_ptr)->set_attr(attr_name, attr_value);
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "Failed to set attribute: %s", e.what());
    }

    return 0; // 无返回值
}

// 获取元素属性
duk_ret_t duk_element_get_attribute(duk_context* ctx) {
    /* 参数：
       [0] 属性名 (string)
       this：元素对象
    */

    // 1. 获取属性名
    const char* attr_name = duk_require_string(ctx, 0);

    // 2. 获取元素指针
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "__ptr");

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx); // 弹出指针和this
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid element");
        return 0;
    }

    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // 弹出指针和this

    if (!elem_ptr || !*elem_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element has been destroyed");
        return 0;
    }

    // 3. 特殊属性处理
    if (strcmp(attr_name, "class") == 0) {
        // 使用正确的接口获取类名
        const char* cls = (*elem_ptr)->get_attr("class", nullptr);
        duk_push_string(ctx, cls ? cls : "");
        return 1;
    }

    // 4. 获取属性值
    const char* value = (*elem_ptr)->get_attr(attr_name);

    // 5. 返回结果（属性存在返回字符串，不存在返回null）
    if (value) {
        duk_push_string(ctx, value);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;
}

//// 元素绑定增强
//void bind_element(duk_context* ctx, litehtml::element::ptr elem) {
//    duk_push_object(ctx);
//
//    // 存储元素指针
//    auto* elem_ptr = new std::shared_ptr<litehtml::element>(elem);
//    duk_push_pointer(ctx, elem_ptr);
//    duk_put_prop_string(ctx, -2, "__ptr");
//
//    // 设置finalizer
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
//    // 添加属性方法
//    duk_push_c_function(ctx, duk_element_set_attribute, 2);
//    duk_put_prop_string(ctx, -2, "setAttribute");
//
//    duk_push_c_function(ctx, duk_element_get_attribute, 1);
//    duk_put_prop_string(ctx, -2, "getAttribute");
//
//    // 添加便捷属性访问器
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
//    // ... 其他属性 ...
//}

// 关键函数实现示例 ==================================================
duk_ret_t duk_element_append_child(duk_context* ctx) {
    /* 参数：
       [0] 要添加的子元素 (element 对象)
       this：父元素对象
    */

    // 1. 验证子元素参数
    if (!duk_is_object(ctx, 0) || !duk_has_prop_string(ctx, 0, "__ptr")) {
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Argument must be an element");
        return 0;
    }

    // 2. 获取父元素指针
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
    duk_pop(ctx); // 弹出父元素指针 [保留this]

    if (!parent_ptr || !*parent_ptr) {
        duk_pop(ctx); // 弹出this
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Parent element has been destroyed");
        return 0;
    }

    auto parent = *parent_ptr;

    // 3. 获取子元素指针
    duk_dup(ctx, 0); // 创建子元素的副本
    duk_get_prop_string(ctx, -1, "__ptr");

    if (!duk_is_pointer(ctx, -1)) {
        duk_pop_2(ctx);
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Invalid child element");
        return 0;
    }

    auto child_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop_2(ctx); // 弹出子元素指针和副本 [保留this]

    if (!child_ptr || !*child_ptr) {
        duk_pop(ctx); // 弹出this
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Child element has been destroyed");
        return 0;
    }

    auto child = *child_ptr;

    // 4. 检查父子关系有效性
    if (child->parent() == parent) {
        // 已经是子节点，直接返回
        duk_pop(ctx); // 弹出this
        duk_dup(ctx, 0); // 返回相同的子元素
        return 1;
    }

    // 5. 执行DOM操作
    try {
        // 如果子元素已经有父元素，先将其移除
        if (auto old_parent = child->parent()) {
            old_parent->removeChild(child);
        }

        // 添加为新的子元素
        parent->appendChild(child);

        //// 更新DOM树
        //if (auto doc = parent->get_document()) {
        //    doc->on_dom_changed();
        //}
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "DOM exception: %s", e.what());
    }

    // 6. 返回添加的子元素
    duk_pop(ctx); // 弹出this
    duk_dup(ctx, 0);
    return 1;
}


// 标题属性 getter
duk_ret_t duk_title_getter(duk_context* ctx) {
    // 参数：无 (属性访问)
    // this：文档对象

    // 1. 获取文档指针
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_push_string(ctx, "");
        return 1;
    }

    // 2. 查找文档中的第一个<title>元素
    litehtml::element::ptr title_element = find_first_element_by_tag(*doc_ptr, "title");

    // 3. 获取并返回标题文本
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

// 标题属性 setter
duk_ret_t duk_title_setter(duk_context* ctx) {
    // 参数：
    // [0] 新的标题值
    // this：文档对象

    // 1. 验证并获取新标题
    const char* new_title = duk_require_string(ctx, 0);

    // 2. 获取文档指针
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        return 0; // 无返回值
    }

    // 3. 查找现有的<title>元素
    litehtml::element::ptr title_element = find_first_element_by_tag(*doc_ptr, "title");

    // 4. 处理标题元素（创建或更新）
    if (!title_element) {
        // 创建新的<title>元素
        auto head_element = find_first_element_by_tag(*doc_ptr, "head");
        if (!head_element) {
            // 如果<head>也不存在，先创建head
            litehtml::string_map attributes;  // 创建一个空属性映射
            head_element = (*doc_ptr)->create_element("head", attributes);
            auto html_element = (*doc_ptr)->root();
            if (html_element) {
                html_element->appendChild(head_element);
            }
        }

        // 创建标题元素
        litehtml::string_map attributes;  // 创建一个空属性映射
        title_element = (*doc_ptr)->create_element("title", attributes);
        head_element->appendChild(title_element);
    }

    // 5. 更新标题内容
    set_element_text(title_element, new_title);

    return 0; // 无返回值
}


// 辅助函数：深度优先遍历查找匹配条件的第一个元素
litehtml::element::ptr find_first_element(
    const litehtml::element::ptr& start,
    const std::function<bool(litehtml::element::ptr)>& predicate
) {
    if (!start) return nullptr;

    // 检查当前元素是否匹配
    if (predicate(start)) {
        return start;
    }

    // 递归检查所有子元素
    const auto& children = start->children();
    for (const auto& child : children) {
        if (auto result = find_first_element(child, predicate)) {
            return result;
        }
    }

    return nullptr;
}

// 辅助函数：查找第一个指定标签的元素
litehtml::element::ptr find_first_element_by_tag(
    litehtml::document::ptr doc,
    const std::string& tag_name
) {
    if (!doc) return nullptr;

    // 高效版本 - 手动查找 head 或 body 等特殊元素
    if (auto root = doc->root()) {
        if (tag_name == "head") {
            // 使用选择器查找 head 元素
            return root->select_one("head");
        }
        else if (tag_name == "body") {
            // 使用选择器查找 body 元素
            return root->select_one("body");
        }
        else if (tag_name == "html") {
            // html 元素就是根元素
            return root;
        }
    }

    // 通用遍历查找
    return find_first_element(doc->root(), [&](litehtml::element::ptr el) {
        return tag_name == el->get_tagName();
        });
}

// 辅助函数：设置元素文本内容
// 辅助函数：设置元素文本内容
void set_element_text(litehtml::element::ptr element, const std::string& text) {
    if (!element) return;

    // 1. 清除现有内容 - 直接遍历并移除所有子节点
    const auto& children_list = element->children();
    for (auto it = children_list.begin(); it != children_list.end();) {
        auto child = *it;
        ++it;  // 先递增迭代器再移除元素
        element->removeChild(child);
    }

    // 2. 如果没有文本内容，直接返回
    if (text.empty()) return;

    // 3. 创建一个包含文本的新元素
    // 使用文档的append_children_from_string方法
    if (auto doc = element->get_document()) {
        // 转义特殊字符
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

        // 使用文档方法添加文本内容
        doc->append_children_from_string(*element, escaped_text.c_str());
    }
}
//// 在文档绑定中添加标题属性
//void add_title_property_to_document(duk_context* ctx) {
//    duk_get_global_string(ctx, "document");
//
//    // 添加标题属性getter/setter
//    duk_push_string(ctx, "title");
//    duk_push_c_function(ctx, duk_title_getter, 0);
//    duk_push_c_function(ctx, duk_title_setter, 1);
//    duk_def_prop(
//        ctx, -4,
//        DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_ENUMERABLE
//    );
//
//    duk_pop(ctx); // 弹出document
//}
//void enhance_document_bindings(duk_context* ctx) {
//    // 确保文档已存在
//    duk_get_global_string(ctx, "document");
//
//    // 添加文档内容类型属性
//    duk_push_string(ctx, "contentType");
//    duk_push_string(ctx, "text/html"); // 或从文档检测实际类型
//    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_ENUMERABLE);
//
//    // 添加charSet属性
//    duk_push_string(ctx, "charset");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        // 检测文档字符集
//        const char* charset = "UTF-8"; // 实际实现从文档获取
//        duk_push_string(ctx, charset);
//        return 1;
//        }, 0);
//    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
//
//    // 添加compatMode属性
//    duk_push_string(ctx, "compatMode");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        auto doc_ptr = get_binded_document_ptr(ctx);
//        if (doc_ptr && *doc_ptr) {
//            // 根据文档类型返回CSS1Compat/BackCompat
//            duk_push_string(ctx, (*doc_ptr)->is_quirks_mode() ? "BackCompat" : "CSS1Compat");
//        }
//        else {
//            duk_push_string(ctx, "CSS1Compat");
//        }
//        return 1;
//        }, 0);
//    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
//
//    duk_pop(ctx); // 弹出document
//}
duk_ret_t duk_document_element_getter(duk_context* ctx) {
    /*
    参数：无（作为访问器属性）
    this：文档对象
    */

    // 1. 获取当前文档指针
    duk_push_this(ctx); // 获取文档对象 [this]

    // 检查是否是我们绑定的文档对象
    if (!duk_has_prop_string(ctx, -1, "__ptr") ||
        !duk_has_prop_string(ctx, -1, "documentElement")) {
        duk_pop(ctx); // 弹出非文档对象
        duk_push_undefined(ctx);
        return 1;
    }

    // 2. 获取文档原生指针
    duk_get_prop_string(ctx, -1, "__ptr"); // [this, doc_ptr]
    auto doc_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop(ctx); // 弹出doc_ptr指针 [this]

    if (!doc_ptr || !*doc_ptr) {
        duk_pop(ctx); // 弹出this
        duk_push_null(ctx);
        return 1;
    }

    // 3. 获取文档根元素（documentElement）
    litehtml::element::ptr root = (*doc_ptr)->root();

    // 4. 处理结果
    if (root) {
        duk_bind_element(ctx, root); // [this, root_elem]
    }
    else {
        duk_push_null(ctx); // [this, null]
    }

    duk_replace(ctx, -2); // 用结果替换this [result]
    return 1;
}


// 扩展实现其他文档属性
void add_document_property(duk_context* ctx,
    const char* name,
    duk_c_function getter) {
    duk_push_string(ctx, name);
    duk_push_c_function(ctx, getter, 0);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_ENUMERABLE);
}



duk_ret_t duk_document_body_getter(duk_context* ctx) {
    /*
    参数：无（作为访问器属性）
    this：文档对象
    */

    // 1. 获取当前文档指针
    duk_push_this(ctx); // 获取文档对象 [this]

    //// 检查是否是我们绑定的文档对象
    //if (!duk_has_prop_string(ctx, -1, "__document_ptr") ||
    //    !duk_has_prop_string(ctx, -1, "getBody")) {
    //    duk_pop(ctx); // 弹出非文档对象
    //    duk_push_undefined(ctx);
    //    return 1;
    //}

    // 2. 获取文档原生指针
    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "__document_ptr"); // [this, doc_ptr]
    auto doc_ptr = reinterpret_cast<std::shared_ptr<litehtml::document>*>(
        duk_get_pointer(ctx, -1)
        );
    duk_pop(ctx); // 弹出doc_ptr指针 [this]

    if (!doc_ptr || !*doc_ptr) {
        duk_pop(ctx); // 弹出this
        duk_push_null(ctx);
 
        return 1;
    }

    // 3. 直接调用文档API获取body元素
    litehtml::element::ptr body = (*doc_ptr) -> root()->select_one("body");

    // 4. 处理结果
    if (body) {
        duk_bind_element(ctx, body); // [this, body_elem]
    }
    else {
        duk_push_null(ctx); // [this, null]
    }

    duk_replace(ctx, -2); // 用结果替换this [result]

    return 1;
}



//duk_ret_t duk_remove_event_listener(duk_context* ctx) {
//    /* 参数：
//       [0] 事件类型 (string)
//       [1] 事件处理函数 (function)
//       [2] 可选配置 (object/boolean)
//    */
//
//    // 1. 获取事件类型
//    const char* event_type = duk_require_string(ctx, 0);
//
//    // 2. 检查事件处理函数
//    if (!duk_is_function(ctx, 1)) {
//        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Event handler must be a function");
//        return 0;
//    }
//
//    // 3. 解析选项 (useCapture 或 options)
//    bool useCapture = false;
//    EventListenerOptions options = { 0 };
//
//    if (duk_is_object(ctx, 2) && !duk_is_boolean(ctx, 2)) {
//        // 处理 {capture, passive, once} 对象
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
//        // 简单的useCapture参数
//        useCapture = duk_to_boolean(ctx, 2);
//    }
//
//    // 4. 获取当前元素
//    duk_push_this(ctx);
//    if (!duk_is_object(ctx, -1)) {
//        duk_pop(ctx);  // 弹出this
//        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Method called on non-object");
//        return 0;
//    }
//
//    // 5. 获取元素指针和DOM对象
//    duk_get_prop_string(ctx, -1, "__ptr");
//    if (!duk_is_pointer(ctx, -1)) {
//        duk_pop_2(ctx);  // 弹出指针和this
//        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element not bound");
//        return 0;
//    }
//
//    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
//        duk_get_pointer(ctx, -1)
//        );
//    duk_pop(ctx);  // 弹出指针，保留this
//
//    if (!elem_ptr || !*elem_ptr) {
//        duk_pop(ctx);  // 弹出this
//        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Element destroyed");
//        return 0;
//    }
//
//    // 6. 获取元素的事件管理器
//    auto event_manager = get_event_manager(*elem_ptr);
//    if (!event_manager) {
//        duk_pop(ctx);  // 弹出this
//        duk_error(ctx, DUK_ERR_ERROR, "Event system not initialized");
//        return 0;
//    }
//
//    // 7. 尝试移除事件监听器
//    bool removed = event_manager->remove_event_listener(
//        event_type,
//        [ctx, elem_ptr, event_type](const EventCallback& callback) {
//            // 比较函数：检查是否是同一个处理函数
//            return are_event_handlers_equal(ctx, 1, callback.js_function);
//        },
//        useCapture,
//        options
//    );
//
//    if (!removed) {
//        // 没有找到匹配的监听器，但不是错误
//        duk_pop(ctx);  // 弹出this
//        duk_push_false(ctx);
//        return 1;
//    }
//
//    // 8. 清理JavaScript引用（如果有）
//    cleanup_event_refs(ctx, *elem_ptr, event_type, 1);
//
//    duk_pop(ctx);  // 弹出this
//    duk_push_true(ctx);
//    return 1;
//}

//// 辅助函数：比较两个事件处理函数是否相同
//bool are_event_handlers_equal(duk_context* ctx, duk_idx_t handler_idx, duk_idx_t stored_ref) {
//    if (!duk_is_function(ctx, handler_idx) || stored_ref == 0) {
//        return false;
//    }
//
//    // 获取当前函数引用
//    duk_dup(ctx, handler_idx);
//
//    // 获取存储的函数引用
//    duk_push_heapptr(ctx, reinterpret_cast<void*>(stored_ref));
//
//    // 比较两个函数
//    bool equal = duk_samevalue(ctx, -1, -2) != 0;
//
//    duk_pop_2(ctx);  // 弹出两个比较的值
//    return equal;
//}
//
//// 清理事件引用
//void cleanup_event_refs(duk_context* ctx, litehtml::element::ptr elem, const char* event_type, duk_idx_t handler_idx) {
//    // 获取元素的事件存储
//    duk_push_global_stash(ctx);
//    duk_get_prop_string(ctx, -1, "__event_refs");
//
//    if (!duk_is_object(ctx, -1)) {
//        duk_pop_2(ctx);
//        return;
//    }
//
//    // 获取元素键 (ptr地址作为key)
//    char element_key[64];
//    snprintf(element_key, sizeof(element_key), "e%p", elem.get());
//    duk_get_prop_string(ctx, -1, element_key);
//
//    if (!duk_is_object(ctx, -1)) {
//        duk_pop_3(ctx);
//        return;
//    }
//
//    // 获取事件类型存储
//    duk_get_prop_string(ctx, -1, event_type);
//
//    if (!duk_is_array(ctx, -1)) {
//        duk_pop_n(ctx, 4);  // 修正为 pop_n 并指定数量
//        return;
//    }
//    // 遍历数组并删除匹配的处理函数引用
//    duk_size_t len = duk_get_length(ctx, -1);
//    for (duk_uarridx_t i = 0; i < len; i++) {
//        duk_get_prop_index(ctx, -1, i);
//
//        if (duk_get_prop_string(ctx, -1, "handler")) {
//            duk_uarridx_t stored_ref = duk_get_heapptr(ctx, -1);
//            duk_pop(ctx);
//
//            if (are_event_handlers_equal(ctx, handler_idx, stored_ref)) {
//                // 删除引用
//                duk_del_prop_index(ctx, -2, i);
//                duk_push_heapptr(ctx, reinterpret_cast<void*>(stored_ref));
//                duk_unref(ctx, -1);
//                duk_pop(ctx);
//
//                // 调整索引
//                len--;
//                i--;
//            }
//        }
//
//        duk_pop(ctx);  // 弹出当前数组元素
//    }
//
//    // 清理堆栈
//    duk_pop_n(ctx, 4);  // 弹出event_type数组 + element对象 + refs对象 + global stash
//}
duk_ret_t duk_query_selector_all(duk_context* ctx) {
    /* 参数：
       [0] CSS选择器 (string)
    */

    // 1. 参数验证
    const char* selector = duk_require_string(ctx, 0);
    if (!selector || strlen(selector) == 0) {
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Selector cannot be empty");
        return 0;
    }

    // 2. 确定作用域（文档或元素）
    std::shared_ptr<litehtml::element> scope;
    std::shared_ptr<litehtml::document> doc;

    duk_push_this(ctx);
    if (duk_is_object(ctx, -1)) {
        // 检查this是文档还是元素
        if (duk_has_prop_string(ctx, -1, "getBody")) {
            // 文档对象
            auto doc_ptr = get_binded_document_ptr(ctx);
            if (doc_ptr && *doc_ptr) {
                doc = *doc_ptr;
                scope = (*doc_ptr)->root();
            }
        }
        else {
            // 元素对象
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
            duk_pop(ctx);  // 弹出指针
        }
    }
    duk_pop(ctx);  // 弹出this

    if (!doc) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "No valid document context");
        return 0;
    }


    // 3. 执行选择器查询
    litehtml::elements_list elements;
    try {
        // 使用元素或文档的select_all方法
        elements = scope->select_all(selector);
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_SYNTAX_ERROR, "Invalid selector: %s (%s)", selector, e.what());
        return 0;
    }


    // 4. 创建并返回节点集合
    return create_node_list(ctx, elements, doc);
}

// 创建节点集合（NodeList）
duk_ret_t create_node_list(duk_context* ctx,
    const litehtml::elements_list& elements,
    std::shared_ptr<litehtml::document> /*doc*/)
{
    /* 创建 JS 数组（类 NodeList） */
    duk_idx_t arr_idx = duk_push_array(ctx);

    /* 逐个元素包装成 JS 对象并压入数组 */
    size_t index = 0;
    for (const auto& elem : elements) {
        duk_push_object(ctx);   // [..., newObj]

        /* 保存 C++ element 指针 */
        auto* elem_ptr_ptr = new std::shared_ptr<litehtml::element>(elem);
        duk_push_pointer(ctx, elem_ptr_ptr);
        duk_put_prop_string(ctx, -2, "__ptr");

        /* 设置 finalizer，释放 shared_ptr */
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

        /* 放入数组 */
        duk_put_prop_index(ctx, arr_idx, index++);
    }

    /* length 属性 */
    duk_push_uint(ctx, static_cast<duk_uint_t>(index));
    duk_put_prop_string(ctx, arr_idx, "length");

    /* item(index) 方法 */
    duk_push_c_function(ctx,
        [](duk_context* ctx) -> duk_ret_t {
            duk_size_t idx = duk_require_uint(ctx, 0);
            duk_push_this(ctx);                       // this (NodeList)
            duk_get_prop_string(ctx, -1, "length");
            duk_size_t len = duk_get_uint(ctx, -1);
            duk_pop(ctx);                             // 弹出 length

            if (idx < len) {
                duk_get_prop_index(ctx, -1, idx);
                return 1;
            }
            duk_push_null(ctx);
            return 1;
        }, 1);
    duk_put_prop_string(ctx, arr_idx, "item");

    /* forEach(callback[, thisArg]) 方法（ES5） */
    duk_push_c_function(ctx,
        [](duk_context* ctx) -> duk_ret_t {
            duk_require_function(ctx, 0);             // callback
            duk_push_this(ctx);                     // NodeList
            duk_size_t len = 0;
            if (duk_get_prop_string(ctx, -1, "length")) {
                len = duk_get_uint(ctx, -1);
            }
            duk_pop(ctx);                             // 弹出 length

            /* 可选 thisArg */
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
                duk_get_prop_index(ctx, -3, i);     // 元素
                duk_dup(ctx, -1);                   // element
                duk_push_uint(ctx, i);              // index
                duk_dup(ctx, -5);                   // NodeList
                duk_call_method(ctx, 3);            // callback.call(...)
                duk_pop(ctx);                       // 丢弃回调返回值
            }
            return 0;
        }, 1);
    duk_put_prop_string(ctx, arr_idx, "forEach");

    /* 不再添加 Symbol.iterator，兼容纯 ES5 环境 */
    return 1;   // 返回 NodeList 对象
}

// 在元素绑定中增加querySelectorAll方法

//duk_ret_t duk_create_text_node(duk_context* ctx) {
//    /* 参数：
//       [0] 文本内容 (string)
//    */
//
//    // 1. 参数验证
//    const char* text = "";
//    if (duk_is_null_or_undefined(ctx, 0)) {
//        // null/undefined 视为空字符串
//    }
//    else {
//        text = duk_require_string(ctx, 0);
//    }
//
//    // 2. 获取文档上下文
//    auto doc_ptr = get_binded_document_ptr(ctx);
//    if (!doc_ptr || !*doc_ptr) {
//        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Document context not available");
//        return 0;
//    }
//
//    // 3. 创建文本节点
//    litehtml::element::ptr text_node;
//    try {
//        text_node = std::make_shared<litehtml::el_text>(text, *doc_ptr);
//    }
//    catch (const std::exception& e) {
//        duk_error(ctx, DUK_ERR_ERROR, "Text node creation failed: %s", e.what());
//        return 0;
//    }
//
//    // 4. 创建JS对象并绑定
//    duk_idx_t obj_idx = duk_push_object(ctx);
//
//    // 存储原生指针
//    auto* text_ptr = new std::shared_ptr<litehtml::element>(text_node);
//    duk_push_pointer(ctx, text_ptr);
//    duk_put_prop_string(ctx, obj_idx, "__ptr");
//
//    // 设置文本节点专属属性
//    duk_push_string(ctx, "nodeType");
//    duk_push_int(ctx, 3);  // Node.TEXT_NODE = 3
//    duk_def_prop(ctx, obj_idx, DUK_DEFPROP_CLEAR_WRITABLE | DUK_DEFPROP_SET_ENUMERABLE);
//
//    duk_push_string(ctx, "nodeName");
//    duk_push_string(ctx, "#text");
//    duk_def_prop(ctx, obj_idx, DUK_DEFPROP_CLEAR_WRITABLE | DUK_DEFPROP_SET_ENUMERABLE);
//
//    // 添加data属性 (getter/setter)
//    duk_push_string(ctx, "data");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        // getter
//        duk_push_this(ctx);
//        duk_get_prop_string(ctx, -1, "__ptr");
//        auto text_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
//        duk_pop_2(ctx);  // 弹出指针和this
//
//        if (text_ptr && *text_ptr) {
//            // 假设文本节点实现了 get_text 方法
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
//        duk_pop_2(ctx);  // 弹出指针和this
//
//        if (text_ptr && *text_ptr) {
//            // 假设文本节点实现了 set_text 方法
//            (*text_ptr)->set_data(new_text);
//        }
//        return 0;
//        }, 1);
//
//    duk_def_prop(ctx, obj_idx, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER | DUK_DEFPROP_SET_ENUMERABLE);
//
//    // 5. 设置原型 (Text.prototype)
//    duk_get_global_string(ctx, "TextPrototype");
//    if (duk_is_object(ctx, -1)) {
//        duk_set_prototype(ctx, obj_idx);
//    }
//    else {
//        duk_pop(ctx);  // 弹出非对象值
//        // 回退到基本对象原型
//        duk_get_global_string(ctx, "Object");
//        duk_get_prop_string(ctx, -1, "prototype");
//        duk_set_prototype(ctx, obj_idx);
//        duk_pop(ctx);  // 弹出Object
//    }
//
//    // 6. 设置 finalizer 释放内存
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
//    return 1;  // 返回文本节点对象
//}
duk_ret_t duk_create_element(duk_context* ctx) {
    /* 参数：
       [0] 标签名 (string)
    */

    // 1. 参数校验
    const char* tag_name = duk_require_string(ctx, 0); // 强制字符串类型
    if (!tag_name || strlen(tag_name) == 0) {
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "Element tag name cannot be empty");
        return 0;
    }

    // 2. 获取文档上下文
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Document context is not available");
        return 0;
    }

    // 3. 创建原生元素对象
    litehtml::element::ptr new_element;
    try {
        litehtml::string_map attributes;  // 创建一个空属性映射
        new_element = (*doc_ptr)->create_element(tag_name, attributes);
    }
    catch (const std::exception& e) {
        duk_error(ctx, DUK_ERR_ERROR, "Failed to create element: %s", e.what());
        return 0;
    }

    // 4. 绑定到JS对象
    if (new_element) {
        duk_bind_element(ctx, new_element);

        // 设置默认属性
        duk_push_string(ctx, "tagName");
        duk_push_string(ctx, tag_name);
        duk_put_prop(ctx, -3);  // elem.tagName = tag_name

        // 设置原型链
        duk_get_global_string(ctx, "ElementPrototype");
        duk_set_prototype(ctx, -2);

        return 1;  // 返回新元素
    }

    duk_push_null(ctx);
    return 1;
}

duk_ret_t duk_document_get_body(duk_context* ctx) {
    // 1. 获取文档指针
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_push_null(ctx);
        return 1;  // 而不是错误，符合DOM规范
    }

    // 2. 获取body元素
    litehtml::element::ptr body = (*doc_ptr)->root()->select_one("body"); 

    // 3. 处理结果
    if (body) {
        duk_bind_element(ctx, body);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;  // 返回body元素或null
}

duk_ret_t duk_get_elements_by_tag_name(duk_context* ctx) {
    // 获取标签名参数
    const char* tag_name = duk_require_string(ctx, 0);

    // 检查是文档调用还是元素调用
    bool is_document = false;

    // 尝试获取文档指针
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (doc_ptr && *doc_ptr) {
        is_document = true;
    }
    else {
        // 可能是元素调用的 - 从 this 获取元素指针
        duk_push_this(ctx);
        if (duk_is_object(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "__ptr");
            if (duk_is_pointer(ctx, -1)) {
                auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
                    duk_to_pointer(ctx, -1)
                    );
                if (elem_ptr && *elem_ptr) {
                    // 获取元素的文档指针
                    doc_ptr = new std::shared_ptr<litehtml::document>((*elem_ptr)->get_document());
                    is_document = false;
                }
            }
            duk_pop(ctx);  // 弹出指针
        }
        duk_pop(ctx);  // 弹出 this
    }

    if (!doc_ptr || !*doc_ptr) {
        duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "No document context available");
        return 0;
    }

    // 获取元素集合
    litehtml::elements_list elements; // 使用 elements_list 替代 elements_vector
    if (is_document) {
        // 文档调用 - 使用 CSS 选择器获取元素
        if (auto root = (*doc_ptr)->root()) {
            elements = root->select_all(tag_name);
        }
    }
    else {
        // 元素调用 - 使用 CSS 选择器获取子元素
        duk_push_this(ctx);
        duk_get_prop_string(ctx, -1, "__ptr");
        auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
            duk_to_pointer(ctx, -1)
            );
        duk_pop_2(ctx);  // 弹出指针和 this
        if (elem_ptr && *elem_ptr) {
            elements = (*elem_ptr)->select_all(tag_name);
        }
    }

    // 创建结果数组
    duk_idx_t arr_idx = duk_push_array(ctx);

    // 遍历匹配的元素，绑定到JS对象（使用迭代器）
    int index = 0;
    for (auto it = elements.begin(); it != elements.end(); ++it) {
        duk_bind_element(ctx, *it);  // 解引用迭代器获取元素指针
        duk_put_prop_index(ctx, arr_idx, index++);
    }

    return 1;  // 返回数组
}
// 创建文本节点
duk_ret_t duk_create_text_node(duk_context* ctx) {
    const char* text = duk_get_string(ctx, 0);
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !text) return 0;

    // 创建文本节点
    auto text_node = std::make_shared<litehtml::el_text>(text, *doc_ptr);

    // 绑定到JS对象
    duk_bind_element(ctx, text_node);
    return 1;
}

// querySelector实现
duk_ret_t duk_query_selector(duk_context* ctx) {
    // 1. 获取CSS选择器参数
    const char* selector = duk_get_string(ctx, 0);
    if (!selector) {
        duk_push_null(ctx);
        return 1;
    }

    // 2. 获取绑定的文档指针
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr) {
        duk_push_null(ctx);
        return 1;
    }

    // 3. 通过文档根元素执行查询
    litehtml::element::ptr element = nullptr;
    if (auto root = (*doc_ptr)->root()) {
        element = root->select_one(selector);
    }

    // 4. 绑定结果到JS对象或返回null
    if (element) {
        duk_bind_element(ctx, element);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;
}

//// 添加事件监听器
//duk_ret_t duk_add_event_listener(duk_context* ctx) {
//    const char* event_type = duk_get_string(ctx, 0);
//    if (!duk_is_function(ctx, 1)) return 0;
//
//    // 复制函数引用到heap
//    duk_dup(ctx, 1);
//    int func_ref = duk_require_heapptr(ctx, -1);
//    duk_pop(ctx);
//
//    auto doc_ptr = get_binded_document_ptr(ctx);
//    if (!doc_ptr || !event_type) return 0;
//
//    // 存储回调引用
//    (*doc_ptr)->addEventListener(event_type, [ctx, func_ref](const litehtml::Event& event) {
//        // 调用回调
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
        // 获取文档
        auto doc = elem->get_document();
        if (!doc) return;

        // 获取文档容器
        auto container = doc->container();
        if (!container) return;

        // 创建临时文档对象来解析 HTML
        auto frag_doc = litehtml::document::createFromString(html, container);
        if (!frag_doc) return;

        // 清除目标元素的子节点
        elem->clearRecursive();

        // 获取临时文档根节点的所有子节点
        const auto& children = frag_doc->root()->children();

        // 将这些子节点移动到目标元素下
        for (const auto& child : children) {
            // 直接从临时文档根节点移除
            frag_doc->root()->removeChild(child);

            // 添加到目标元素
            elem->appendChild(child);
        }

        // 不再在此处执行渲染，由外部统一控制
        // 在JS操作完成后统一执行 doc->render()

    }
    catch (const std::exception& e) {
        // 错误处理
        std::cerr << "setInnerHTML error: " << e.what() << std::endl;
    }
}
// innerHTML的setter实现
duk_ret_t duk_element_set_inner_html(duk_context* ctx) {
    // 获取参数
    const char* html = duk_require_string(ctx, 0);

    // 获取当前元素
    duk_push_this(ctx);
    auto elem = get_binded_element(ctx, -1);

    // 检查元素有效性
    if (!elem) {
        duk_pop(ctx); // 弹出 this
        return duk_error(ctx, DUK_ERR_REFERENCE_ERROR, "Invalid element");
    }

    // 设置 innerHTML
    set_element_inner_html(elem, html);

    // 清理堆栈并返回
    duk_pop(ctx); // 弹出 this
    return 0;
}

// 辅助函数：获取绑定的元素
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

//// 点击事件模拟
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

// 文档命令执行
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

//// 元素事件回调记录结构
//struct event_listener_record {
//    std::string event_type;
//    int callback_ref;  // Duktape 堆引用
//    bool use_capture;
//};
//
//// 添加事件监听器
//duk_ret_t duk_element_add_event_listener(duk_context* ctx) {
//    /* 参数：
//        [0] 事件类型 (string)
//        [1] 回调函数 (function)
//        [2] useCapture (boolean, 可选)
//    */
//
//    // 1. 参数校验
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
//    // 2. 获取当前元素对象（从 this 绑定）
//    duk_push_this(ctx);  // [event_type, callback, use_capture? this]
//    duk_get_prop_string(ctx, -1, "__ptr");  // [..., elem_ptr]
//
//    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(
//        duk_get_pointer(ctx, -1)
//        );
//    duk_pop(ctx);  // 弹出指针 -> [event_type, callback, use_capture? this]
//
//    if (!elem_ptr || !*elem_ptr) {
//        duk_pop(ctx);  // 弹出 this
//        return DUK_RET_TYPE_ERROR;
//    }
//
//    // 3. 创建事件回调记录
//    // 持久化函数引用
//    duk_dup(ctx, 1);  // [..., this, callback]
//    int callback_ref = duk_require_heapptr(ctx, -1);
//
//    // 4. 存储回调引用到元素的回调列表
//    const char* kEventListenersKey = "\xFFevent_listeners";
//    duk_get_prop_string(ctx, -2, kEventListenersKey);  // [..., this, callback, listeners?]
//
//    if (duk_is_undefined(ctx, -1)) {
//        duk_pop(ctx);  // 弹出 undefined
//        duk_push_array(ctx);  // [..., this, callback, new_listeners]
//    }
//    duk_swap_top(ctx, -2);  // [..., this, listeners, callback] -> [..., this, callback, listeners]
//
//    // 在 listeners 数组中添加记录对象
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
//    // 添加到 listeners 数组
//    duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2));  // listeners.push(record)
//
//    // 保存更新后的 listeners 数组
//    duk_dup(ctx, -1);  // [..., this, callback, listeners, listeners]
//    duk_put_prop_string(ctx, -4, kEventListenersKey);  // this[kEventListenersKey] = listeners
//
//    duk_pop_2(ctx);  // 弹出 listeners 和 callback -> [..., this]
//    duk_pop(ctx);  // 弹出 this
//
//    // 5. 向 native 元素注册事件
//    (*elem_ptr)->addEventListener(event_type, [ctx, callback_ref](const litehtml::Event& event) {
//        duk_push_heapptr(ctx, reinterpret_cast<void*>(callback_ref));  // 推送回调函数
//
//        // 创建事件对象
//        duk_idx_t event_idx = duk_push_object(ctx);
//
//        // 设置事件属性
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
//        // 调用回调函数 (event)
//        if (duk_pcall(ctx, 1) != 0) {
//            // 处理JS错误
//            const char* err = duk_safe_to_string(ctx, -1);
//            OutputDebugStringA("Event handler error: ");
//            OutputDebugStringA(err);
//            OutputDebugStringA("\n");
//            duk_pop(ctx);
//        }
//        else {
//            duk_pop(ctx);  // 忽略返回值
//        }
//        });
//
//    return 0;
//}

//// 辅助函数：获取当前元素的事件回调列表
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
//        duk_pop_2(ctx);  // 弹出key和值
//    }
//    duk_pop_2(ctx);  // 弹出枚举器和数组
//
//    return listeners;
//}
//
//// 移除事件监听器
//duk_ret_t duk_element_remove_event_listener(duk_context* ctx) {
//    /* 参数:
//        [0] 事件类型 (string)
//        [1] 回调函数 (function)
//    */
//
//    const char* event_type = duk_get_string(ctx, 0);
//    if (!duk_is_function(ctx, 1)) return 0;
//
//    duk_push_this(ctx);
//
//    // 获取当前的回调引用
//    duk_dup(ctx, 1);
//    int callback_ref = duk_require_heapptr(ctx, -1);
//    duk_pop(ctx);
//
//    // 获取元素指针
//    duk_get_prop_string(ctx, -1, "__ptr");
//    auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_get_pointer(ctx, -1));
//    duk_pop(ctx);
//
//    if (!elem_ptr || !*elem_ptr) {
//        duk_pop(ctx);  // 弹出 this
//        return 0;
//    }
//
//    // 获取事件监听器列表
//    duk_get_prop_string(ctx, -1, "\xFFevent_listeners");
//    if (!duk_is_array(ctx, -1)) {
//        duk_pop_2(ctx);
//        return 0;
//    }
//
//    // 查找并移除匹配的回调
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
//            // 从JS数组中移除
//            duk_del_prop_index(ctx, -2, i);
//
//            // 从native元素中移除事件
//            (*elem_ptr)->removeEventListener(event_type);
//            found = true;
//            break;
//        }
//        duk_pop(ctx);  // 弹出当前元素
//    }
//
//    duk_pop_2(ctx);  // 弹出数组和 this
//
//    return found ? 1 : 0;
//}
//// 元素绑定增强（添加ID属性访问）
//void duk_bind_element(duk_context* ctx, litehtml::element::ptr elem) {
//    duk_push_object(ctx);
//
//    // 存储原生指针
//    auto* elem_ptr = new std::shared_ptr<litehtml::element>(elem);
//    duk_push_pointer(ctx, elem_ptr);
//    duk_put_prop_string(ctx, -2, "__ptr");
//
//    // 添加ID属性访问器
//    duk_push_string(ctx, "id");
//    duk_push_c_function(ctx, [](duk_context* ctx) -> duk_ret_t {
//        duk_push_this(ctx);
//        duk_get_prop_string(ctx, -1, "__ptr");
//        auto elem_ptr = reinterpret_cast<std::shared_ptr<litehtml::element>*>(duk_to_pointer(ctx, -1));
//        duk_pop_2(ctx);  // 弹出指针和this
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
//        duk_pop_2(ctx);  // 弹出指针和this
//
//        if (elem_ptr && *elem_ptr) {
//            (*elem_ptr)->set_attr("id", new_id ? new_id : "");
//        }
//        return 0;
//        }, 1);  // setter
//    duk_def_prop(ctx, -4, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_HAVE_SETTER);
//
//    // ... 其他属性和方法 ...
//}


//// 然后在 .cpp 文件中实现这两个函数
//static duk_ret_t duk_element_get_attribute_helper(duk_context* ctx, const char* attr_name) {
//    duk_push_this(ctx);
//    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("__element"));
//    litehtml::element* element = static_cast<litehtml::element*>(duk_get_pointer(ctx, -1));
//    duk_pop_2(ctx); // 弹出指针和 this
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
//    duk_pop_2(ctx); // 弹出指针和 this
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
    // 1. 获取ID参数
    const char* id = duk_require_string(ctx, 0);
    if (!id || *id == '\0') {
        duk_push_null(ctx);
        return 1;
    }

    // 2. 获取绑定的文档指针
    auto doc_ptr = get_binded_document_ptr(ctx);
    if (!doc_ptr || !*doc_ptr) {
        duk_push_null(ctx);
        return 1;
    }

    // 3. 使用ID选择器查找元素
    litehtml::element::ptr elem = nullptr;
    if (auto root = (*doc_ptr)->root()) {
        // 构建ID选择器，转义特殊字符
        std::string selector = "#";
        // 添加ID转义处理
        for (const char* c = id; *c; c++) {
            if (is_id_char(*c)) {
                selector += *c;
            }
            else {
                // 简单转义
                selector += '\\';
                selector += *c;
            }
        }
        elem = root->select_one(selector.c_str());
    }

    // 4. 绑定结果到JS对象
    if (elem) {
        duk_bind_element(ctx, elem);
    }
    else {
        duk_push_null(ctx);
    }

    return 1;
}

// 辅助函数检查合法ID字符
static bool is_id_char(char c) {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' ||
        (unsigned char)c > 0x7F; // 非ASCII字符
}

// 增强版本的 get_binded_document_ptr (添加错误检查)
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

