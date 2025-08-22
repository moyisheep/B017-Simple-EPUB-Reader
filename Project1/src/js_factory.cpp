#include "js_factory.h"

struct js_factory::impl {
    std::unordered_map<std::string, js_factory::creator_t> creators;
};

js_factory& js_factory::instance() {
    static js_factory f;
    return f;
}
js_factory::js_factory() : p(std::make_unique<impl>()) {}
js_factory::~js_factory() = default;

void js_factory::add(const std::string& name, creator_t c) {
    p->creators[name] = std::move(c);
}

std::unique_ptr<js_engine> js_factory::create(const std::string& name) const {
    auto it = p->creators.find(name);
    return (it != p->creators.end()) ? it->second() : nullptr;
}