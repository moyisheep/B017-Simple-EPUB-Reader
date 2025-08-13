#pragma once
#include <litehtml.h>
#include <vector>

class paginator {
public:
    void load_chapter(litehtml::document* doc, int w, int h);
    int page_count() const { return (int)m_pages.size(); }
    void render_page(HDC hdc, int page_no);
private:
    std::vector<int> m_pages;
    litehtml::document* m_doc = nullptr;
    int m_w = 0, m_h = 0;
};