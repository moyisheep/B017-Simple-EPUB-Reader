#pragma once
#include <litehtml.h>
#include "epub_loader.h"
#include <windows.h>

class epub_container : public litehtml::document_container {
public:
    explicit epub_container(const std::wstring& unzip_dir) : m_root(unzip_dir) {}
    litehtml::uint_ptr create_font(const litehtml::tchar_t* face, int size, int weight,
        litehtml::font_style italic, unsigned int decoration,
        litehtml::font_metrics* fm) override;
    int text_width(const litehtml::tchar_t* text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t* text,
        litehtml::uint_ptr hFont, litehtml::web_color color,
        const litehtml::position& pos) override;
    litehtml::uint_ptr create_image(const litehtml::tchar_t* src,
        const litehtml::tchar_t* baseurl) override;
    void get_client_rect(litehtml::position& client) const override;
private:
    std::wstring m_root;
    int m_w = 0, m_h = 0;
};