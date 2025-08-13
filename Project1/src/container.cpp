#include "container.h"
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

litehtml::uint_ptr epub_container::create_font(const litehtml::tchar_t* face, int size,
    int weight, litehtml::font_style italic,
    unsigned int decoration,
    litehtml::font_metrics* fm) {
    // 极简：直接用 GDI
    int nHeight = -MulDiv(size, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
    DWORD dw = FW_NORMAL;
    if (weight >= 600) dw = FW_BOLD;
    HFONT hFont = CreateFont(nHeight, 0, 0, 0, dw,
        italic == litehtml::fontStyleItalic,
        FALSE, FALSE, DEFAULT_CHARSET,
        0, 0, 0, 0, face);
    TEXTMETRIC tm; HDC hdc = GetDC(nullptr);
    SelectObject(hdc, hFont);
    GetTextMetrics(hdc, &tm);
    fm->height = tm.tmHeight;
    fm->ascent = tm.tmAscent;
    fm->descent = tm.tmDescent;
    ReleaseDC(nullptr, hdc);
    return (litehtml::uint_ptr)hFont;
}

int epub_container::text_width(const litehtml::tchar_t* text,
    litehtml::uint_ptr hFont) {
    SIZE sz; HDC hdc = GetDC(nullptr);
    SelectObject(hdc, (HFONT)hFont);
    GetTextExtentPoint32(hdc, text, lstrlen(text), &sz);
    ReleaseDC(nullptr, hdc);
    return sz.cx;
}

void epub_container::draw_text(litehtml::uint_ptr hdc,
    const litehtml::tchar_t* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos) {
    HDC h = (HDC)hdc;
    SetBkMode(h, TRANSPARENT);
    SetTextColor(h, RGB(color.red, color.green, color.blue));
    SelectObject(h, (HFONT)hFont);
    TextOut(h, pos.left(), pos.top(), text, lstrlen(text));
}

litehtml::uint_ptr epub_container::create_image(const litehtml::tchar_t* src,
    const litehtml::tchar_t* baseurl) {
    std::wstring path = m_root + L"\\" + src;
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::new Gdiplus::Bitmap(path.c_str());
    return (litehtml::uint_ptr)bmp;
}

void epub_container::get_client_rect(litehtml::position& client) const {
    client = { 0, 0, m_w, m_h };
}