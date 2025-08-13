#include "paginator.h"
#include <windows.h>

void paginator::load_chapter(litehtml::document* doc, int w, int h) {
    m_doc = doc;
    m_w = w; m_h = h;
    m_pages.clear();
    int total_h = doc->height();
    for (int y = 0; y < total_h; y += h)
        m_pages.push_back(y);
}

void paginator::render_page(HDC hdc, int page_no) {
    if (page_no < 0 || page_no >= (int)m_pages.size()) return;
    int y = m_pages[page_no];
    SetViewportOrgEx(hdc, 0, -y, nullptr);
    litehtml::position clip(0, y, m_w, m_h);
    m_doc->draw((litehtml::uint_ptr)hdc, 0, y, &clip);
    SetViewportOrgEx(hdc, 0, 0, nullptr);
}