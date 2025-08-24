#pragma once
#include "IRenderBackend.h"

// -------------- DirectWrite-D2D 后端 -----------------
class D2DBackend : public IRenderBackend {
public:
    D2DBackend(int w, int h, ComPtr<ID2D1RenderTarget> devCtx);
    //D2DBackend(const D2DBackend&) = default;   // 或自己实现深拷贝
    /* 下面 6 个函数在 .cpp 里用 IDWriteTextLayout / ID2D1SolidColorBrush 实现 */
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override;
    std::vector<std::wstring> split_font_list(const std::string& src);
    litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr h) override;
    void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) override;
    int pt_to_px(int pt) const override;
    int text_width(const char* text, litehtml::uint_ptr hFont) override;
    void load_all_fonts(void);
    void unload_fonts(void);
    void resize(int width, int height) override;
    std::optional<std::wstring> mapDynamic(const std::wstring& key);
    std::wstring resolveFace(const std::wstring& raw);
    ComPtr<ID2D1BitmapRenderTarget> m_rt;
    std::set<std::wstring> getCurrentFonts() override;
private:
    // 自动 AddRef/Release
    ComPtr<IDWriteFactory>    m_dwrite;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> m_privateFonts;  // 新增
    std::vector<std::wstring> m_tempFontFiles;
    std::wstring m_actualFamily;   // 保存命中的字体家族名m_actualFamily
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap>> m_d2dBitmapCache;
    ComPtr<ID2D1RenderTarget> m_devCtx;
    static std::wstring toLower(std::wstring s);
    static std::optional<std::wstring> mapStatic(const std::wstring& key);
    float m_baselineY = 0;

    ComPtr<IDWriteFontCollection> m_sysFontColl;
    int  m_w, m_h;
};