#pragma once
#include <litehtml/litehtml.h>
#include <set>

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    /* 必须实现的 7 个纯虚函数 */
    virtual void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) = 0;
    virtual void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) = 0;
    virtual litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) = 0;
    virtual void delete_font(litehtml::uint_ptr h) = 0;
    virtual void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) = 0;
    virtual int pt_to_px(int pt) const = 0;
    virtual int text_width(const char* text, litehtml::uint_ptr hFont) = 0;
    virtual void load_all_fonts(void) = 0;
    virtual void resize(int width, int height) = 0;
    virtual std::set<std::wstring> getCurrentFonts() = 0;
};