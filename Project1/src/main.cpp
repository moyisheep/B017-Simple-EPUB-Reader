// main.cpp  ——  优化后完整单文件
#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#define WM_LOAD_ERROR (WM_USER + 3)
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <future>
#include <unordered_map>
#include <algorithm>
#include <miniz/miniz.h>
#include <tinyxml2.h>
using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;
#include <litehtml/litehtml.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#pragma comment(lib, "freetype.lib")
#pragma comment(lib, "comctl32.lib")
#include <objidl.h>
#include <filesystem>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
using namespace Gdiplus;
#include <commctrl.h>          // TreeView
#pragma comment(lib, "comctl32.lib")
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

HWND  g_hwndTV = nullptr;    // 侧边栏 TreeView
HIMAGELIST g_hImg = nullptr;   // 图标(可选)
HWND      g_hWnd;
HWND g_hStatus = nullptr;   // 状态栏句柄
HWND g_hView = nullptr;

struct GdiplusDeleter { void operator()(Gdiplus::Image* p) const { delete p; } };
using ImagePtr = std::unique_ptr<Gdiplus::Image, GdiplusDeleter>;
// ---------- 工具 ----------
static std::string w2a(const std::wstring& s) {
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}
static std::wstring a2w(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

static std::wstring utf8_to_utf16(const std::string& src)
{
    if (src.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, nullptr, 0);
    std::wstring dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, &dst[0], len);
    // 去掉末尾的 L'\0'
    while (!dst.empty() && dst.back() == L'\0') dst.pop_back();
    return dst;
}

bool is_xhtml(const std::wstring& file_path)
{
    auto dot = file_path.rfind(L'.');
    if (dot == std::wstring::npos) return false;

    std::wstring ext = file_path.substr(dot + 1);

    // 1. 小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    // 2. 去掉控制字符
    ext.erase(std::remove_if(ext.begin(), ext.end(),
        [](wchar_t c) { return c < 32 || c > 126; }),
        ext.end());

    // 3. 比较
    return ext == L"xhtml";
}

struct FontWrapper {
    HFONT hFont;
    explicit FontWrapper(HFONT f) : hFont(f) {}
    ~FontWrapper() { if (hFont) DeleteObject(hFont); }
};

inline void SetStatus(const wchar_t* msg) {
    SendMessage(g_hStatus, SB_SETTEXT, 0, (LPARAM)msg);
}

// -------------- 新增数据结构 --------------
struct OCFItem {
    std::wstring id, href, media_type, properties;
};
struct OCFRef {
    std::wstring idref, href, linear = L"yes";
};
struct OCFNavPoint {
    std::wstring label, href;
    int order = 0;
};
struct OCFPackage {
    std::wstring rootfile;                // OPF 绝对路径
    std::wstring opf_dir;                 // 目录，带 '/'
    std::vector<OCFItem>   manifest;
    std::vector<OCFRef>    spine;
    std::vector<OCFNavPoint> toc;
    std::map<std::wstring, std::wstring> meta;
    std::wstring toc_path;
};

struct TocItem
{
    std::wstring title;
    std::wstring href;
    std::vector<TocItem> children;
};
// ---------- EPUB 零解压 ----------
struct EPUBBook {
    struct MemFile {
        std::vector<uint8_t> data;
        const char* begin() const { return reinterpret_cast<const char*>(data.data()); }
        size_t      size()  const { return data.size(); }
    };
    mz_zip_archive zip = {};
    std::map<std::wstring, MemFile> cache;

    // -------------- EPUBBook 内部新增成员 --------------
    OCFPackage ocf_pkg_;                     // 解析结果
    void parse_ocf_(void);                       // 主解析入口
    void parse_opf_(void);   // 解析 OPF
    void parse_toc_(void);                        // 解析 TOC
    void LoadToc(void);
    void OnTreeSelChanged(const wchar_t* href);
    bool load(const wchar_t* epub_path);
    MemFile read_zip(const wchar_t* file_name) const;
    std::string load_html(const std::wstring& path) const;

    static std::string resolve_path(const std::string& opf, const std::string& href) {
        size_t pos = opf.find_last_of('/');
        if (pos == std::string::npos) pos = opf.find_last_of('\\');
        std::string base = (pos == std::string::npos) ? "" : opf.substr(0, pos + 1);
        return base + href;
    }


    static std::wstring resolve_path_w(const std::wstring& base,
        const std::wstring& rel)
    {
        if (base.empty())
            return rel;

        std::wstring out = base;
        if (!out.empty() && out.back() != L'/' && out.back() != L'\\')
            out += L'/';
        out += rel;
        return out;
    }



    ~EPUBBook() { mz_zip_reader_end(&zip); }
};
EPUBBook::MemFile EPUBBook::read_zip(const wchar_t* file_name) const {
    MemFile mf;

    // 2.1 先按给定宽路径找
    std::string narrow_name = w2a(file_name);
    size_t uncomp_size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(
        const_cast<mz_zip_archive*>(&zip),
        narrow_name.c_str(),
        &uncomp_size, 0);

    // 2.2 没找到 && 是相对路径，再拼一次 opf_dir
    if (!p && file_name[0] != L'/' && !ocf_pkg_.opf_dir.empty()) {
        std::wstring abs = ocf_pkg_.opf_dir + file_name;
        std::string narrow_abs = w2a(abs);
        p = mz_zip_reader_extract_file_to_heap(
            const_cast<mz_zip_archive*>(&zip),
            narrow_abs.c_str(),
            &uncomp_size, 0);
    }

    if (p) {
        mf.data.assign(static_cast<uint8_t*>(p),
            static_cast<uint8_t*>(p) + uncomp_size);
        mz_free(p);
    }
    return mf;
}

std::string EPUBBook::load_html(const std::wstring& path) const {
    auto it = cache.find(path);
    if (it != cache.end()) return std::string(it->second.begin(), it->second.size());
    EPUBBook::MemFile mf = read_zip(path.c_str());
    if (mf.data.empty()) return {};
    return std::string(mf.begin(), mf.size());
}

bool EPUBBook::load(const wchar_t* epub_path) {
    namespace fs = std::filesystem;
    if (!fs::exists(epub_path))
        throw std::runtime_error("文件不存在");

    mz_zip_reader_end(&zip);           // 1. 先关闭旧 zip
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, w2a(epub_path).c_str(), 0))
        throw std::runtime_error("zip 打开失败：" +
            std::to_string(mz_zip_get_last_error(&zip)));

    parse_ocf_();
    parse_opf_();
    parse_toc_();

    LoadToc();
    return true;
}


// -------------- 实现（直接粘到 EPUBBook 末尾即可） --------------
void EPUBBook::parse_ocf_() {
    ocf_pkg_ = {};  // 清空
    auto container = read_zip(L"META-INF/container.xml");
    if (container.data.empty()) return;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(container.begin(), container.size()) != tinyxml2::XML_SUCCESS) return;

    auto* rootfile = doc.FirstChildElement("container")
        ? doc.FirstChildElement("container")->FirstChildElement("rootfiles")
        : nullptr;
    rootfile = rootfile ? rootfile->FirstChildElement("rootfile") : nullptr;
    if (!rootfile || !rootfile->Attribute("full-path")) return;

    ocf_pkg_.rootfile = a2w(rootfile->Attribute("full-path"));
    ocf_pkg_.opf_dir = ocf_pkg_.rootfile.substr(0, ocf_pkg_.rootfile.find_last_of(L'/') + 1);

}

void EPUBBook::parse_opf_() {
    auto opf = read_zip(ocf_pkg_.rootfile.c_str());
    std::string xml(opf.begin(), opf.begin() + opf.size());
    if (opf.data.empty()) return;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) return;

    auto* man = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("manifest")
        : nullptr;

    for (auto* it = man ? man->FirstChildElement("item") : nullptr;
        it; it = it->NextSiblingElement("item"))
    {
        OCFItem item;
        item.id = a2w(it->Attribute("id") ? it->Attribute("id") : "");
        item.href = a2w(it->Attribute("href") ? it->Attribute("href") : "");
        item.media_type = a2w(it->Attribute("media-type") ? it->Attribute("media-type") : "");
        item.properties = a2w(it->Attribute("properties") ? it->Attribute("properties") : "");


        // 只在 href 非空时拼绝对路径
        if (!item.href.empty())
            item.href = resolve_path_w(ocf_pkg_.opf_dir, item.href);

        ocf_pkg_.manifest.emplace_back(std::move(item));
    }

    // spine
    auto* spine = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("spine")
        : nullptr;
    // 先把 manifest 做成 id -> href 的映射
    std::unordered_map<std::wstring, std::wstring> id2href;
    for (const auto& m : ocf_pkg_.manifest)
        id2href[m.id] = m.href;

    // 再解析 spine
    for (auto* it = spine ? spine->FirstChildElement("itemref") : nullptr;
        it; it = it->NextSiblingElement("itemref")) {

        OCFRef ref;
        ref.idref = a2w(it->Attribute("idref") ? it->Attribute("idref") : "");
        ref.href = id2href[ref.idref];   // 直接填进去
        ref.linear = a2w(it->Attribute("linear") ? it->Attribute("linear") : "yes");
        ocf_pkg_.spine.emplace_back(std::move(ref));
    }
    // meta
    auto* meta = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("metadata")
        : nullptr;
    for (auto* it = meta ? meta->FirstChildElement() : nullptr;
        it; it = it->NextSiblingElement()) {
        ocf_pkg_.meta[a2w(it->Name())] = a2w(it->GetText() ? it->GetText() : "");
    }
}


void EPUBBook::parse_toc_() {
    std::wstring toc_path = L"";
    for (const auto& it : ocf_pkg_.manifest)
    {
        if (it.properties.find(L"nav") != std::wstring::npos || it.id.find(L"ncx") != std::wstring::npos) {
        toc_path = it.href;
        break;
        }
    }
    
    if (toc_path.empty()) return;
    ocf_pkg_.toc_path = toc_path;
    auto toc = read_zip(toc_path.c_str());
    if (toc.data.empty()) return;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(toc.begin(), toc.size()) != tinyxml2::XML_SUCCESS) return;




    bool is_nav = is_xhtml(toc_path);

    int order = 0;

    if (is_nav) {
        auto* nav = doc.FirstChildElement("html")
            ? doc.FirstChildElement("html")->FirstChildElement("body")
            : nullptr;
        nav = nav ? nav->FirstChildElement("nav") : nullptr;
        if (!nav || std::string(nav->Attribute("epub:type") ? nav->Attribute("epub:type") : "") != "toc")
            return;

        for (auto* li = nav->FirstChildElement("ol") ? nav->FirstChildElement("ol")->FirstChildElement("li") : nullptr;
            li; li = li->NextSiblingElement("li")) {
            auto* a = li->FirstChildElement("a");
            if (!a) continue;
            OCFNavPoint np;
            np.label = a2w(a->GetText() ? a->GetText() : "");
            np.href = a2w(a->Attribute("href") ? a->Attribute("href") : "");
            if (!np.href.empty())
                np.href = a2w(resolve_path(w2a(ocf_pkg_.opf_dir), w2a(np.href)));
            np.order = order++;
            ocf_pkg_.toc.emplace_back(std::move(np));
        }
    }
    else { // NCX
        auto* navMap = doc.RootElement()
            ? doc.RootElement()->FirstChildElement("navMap")
            : nullptr;
        if (!navMap) return;

        for (auto* pt = navMap->FirstChildElement("navPoint"); pt; pt = pt->NextSiblingElement("navPoint")) {
            auto* lbl = pt->FirstChildElement("navLabel");
            auto* txt = lbl ? lbl->FirstChildElement("text") : nullptr;
            auto* con = pt->FirstChildElement("content");
            OCFNavPoint np;
            np.label = a2w(txt && txt->GetText() ? txt->GetText() : "");
            np.href = a2w(con && con->Attribute("src") ? con->Attribute("src") : "");
            if (!np.href.empty())
                np.href = a2w(resolve_path(w2a(ocf_pkg_.opf_dir), w2a(np.href)));
            np.order = order++;
            ocf_pkg_.toc.emplace_back(std::move(np));
        }
    }
    
}

// ---------- LiteHtml 容器 ----------
class SimpleContainer : public litehtml::document_container {
public:
    explicit SimpleContainer(const std::wstring& root) : m_root(root) {}
    void clear_images() { m_img_cache.clear(); }
    litehtml::uint_ptr create_font(const char* faceName, int size, int weight,
        litehtml::font_style italic, unsigned int decoration,
        litehtml::font_metrics* fm) override {
        HFONT hFont = CreateFontA(
            -MulDiv(size, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
            0, 0, 0, weight, italic != litehtml::font_style_normal,
            FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, faceName);
        if (!hFont) return 0;

        // 计算字体度量
        HDC hdc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(hdc, hFont);
        TEXTMETRICA tm{};
        GetTextMetricsA(hdc, &tm);
        SelectObject(hdc, old);
        ReleaseDC(nullptr, hdc);

        fm->height = tm.tmHeight;
        fm->ascent = tm.tmAscent;
        fm->descent = tm.tmDescent;

        auto* fw = new std::shared_ptr<FontWrapper>(new FontWrapper(hFont));
        return reinterpret_cast<litehtml::uint_ptr>(fw);
    }
    void delete_font(litehtml::uint_ptr h) override {
        auto* fw = reinterpret_cast<std::shared_ptr<FontWrapper>*>(h);
        delete fw;
    }
    int text_width(const char* text, litehtml::uint_ptr hFont) override {
        if (!hFont || !text) return 0;
        auto* fw = reinterpret_cast<std::shared_ptr<FontWrapper>*>(hFont);
        HFONT hF = (*fw)->hFont;

        HDC hdc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(hdc, hF);
        SIZE sz{};
        GetTextExtentPoint32A(hdc, text, static_cast<int>(strlen(text)), &sz);
        SelectObject(hdc, old);
        ReleaseDC(nullptr, hdc);
        return sz.cx;
    }
    void draw_text(litehtml::uint_ptr hdc, const char* text,
        litehtml::uint_ptr hFont, litehtml::web_color color,
        const litehtml::position& pos) override {
        if (!hFont || !text) return;
        auto* fw = reinterpret_cast<std::shared_ptr<FontWrapper>*>(hFont);
        HFONT hF = (*fw)->hFont;

        HDC dc = reinterpret_cast<HDC>(hdc);
        HGDIOBJ old = SelectObject(dc, hF);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(color.red, color.green, color.blue));
        TextOutA(dc, pos.left(), pos.top(), text, static_cast<int>(strlen(text)));
        SelectObject(dc, old);
    }
    void load_image(const char* src, const char* baseurl, bool) override {
        if (m_img_cache.contains(src)) return;
        std::string path = w2a(m_root) + "\\" + src;
        ImagePtr img(Gdiplus::Image::FromFile(a2w(path).c_str()));
        if (img && img->GetLastStatus() == Gdiplus::Ok)
            m_img_cache[src] = std::move(img);
    }
    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override {
        if (!m_img_cache.contains(src)) { sz.width = sz.height = 0; return; }
        auto img = m_img_cache[src];
        sz.width = img->GetWidth();
        sz.height = img->GetHeight();
    }
    void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) override {}
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void set_cursor(const char*) override {}
    void transform_text(litehtml::string&, litehtml::text_transform) override {}
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override {}
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override {}
    void del_clip() override {}
    void get_client_rect(litehtml::position& client) const override {
        RECT rc{}; GetClientRect(g_hWnd, &rc);
        client = { 0, 0, rc.right, rc.bottom - 30 };
    }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
        const std::shared_ptr<litehtml::document>&) override {
        return nullptr;
    }
    void get_media_features(litehtml::media_features&) const override {}
    void get_language(litehtml::string&, litehtml::string&) const override {}
    int pt_to_px(int pt) const override {
        return MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
    }
    int get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "Times New Roman"; }
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override {}
private:
    std::wstring m_root;
    std::unordered_map<std::string, std::shared_ptr<Gdiplus::Image>> m_img_cache;
};

// ---------- 分页 ----------
class Paginator {
public:
    void load(litehtml::document* doc, int w, int h) {
        m_doc = doc; m_w = w; m_h = h;
        m_pages.clear();
        if (!doc) return;
        doc->render(w);                 // 先排版
        int total = doc->height();
        for (int y = 0; y < total; y += h)
            m_pages.push_back(y);
     }
    
    int count() const { return static_cast<int>(m_pages.size()); }
    void render(HDC hdc, int page) {
        if (page < 0 || page >= count()) return;
        SetViewportOrgEx(hdc, 0, -m_pages[page], nullptr);
        litehtml::position clip{ 0, m_pages[page], m_w, m_h };
        m_doc->draw(reinterpret_cast<litehtml::uint_ptr>(hdc),
            0, m_pages[page], &clip);
        SetViewportOrgEx(hdc, 0, 0, nullptr);
    }
    void clear() {
        m_doc = nullptr;
        m_w = m_h = 0;
        m_pages.clear();
    }
private:
    litehtml::document* m_doc = nullptr;
    int m_w = 0, m_h = 0;
    std::vector<int> m_pages;
};

// ---------- 全局 ----------
HINSTANCE g_hInst;
std::shared_ptr<SimpleContainer> g_container;
HWND      g_hTrack = nullptr;
EPUBBook  g_book;
std::shared_ptr<litehtml::document> g_doc;
Paginator g_pg;
int       g_page = 0;
std::future<void> g_parse_task;
constexpr UINT WM_EPUB_PARSED = WM_APP + 1;
static HBITMAP g_hCachedBmp = nullptr;
static int     g_cachedPage = -1;
static SIZE    g_cachedSize = {};
LRESULT CALLBACK ViewWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);

        if (g_hCachedBmp && g_cachedPage == g_page)
        {
            HDC mem = CreateCompatibleDC(hdc);
            HGDIOBJ old = SelectObject(mem, g_hCachedBmp);
            BitBlt(hdc, 0, 0, g_cachedSize.cx, g_cachedSize.cy, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
        }
        EndPaint(h, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   // 防止闪烁
    }
    return DefWindowProc(h, msg, wp, lp);
}

ATOM RegisterViewClass(HINSTANCE hInst)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = ViewWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"EPUBView";
    return RegisterClassW(&wc);
}
void UpdateCache()
{
    if (!g_doc) return;

    RECT rc;
    GetClientRect(g_hView, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    // 1) 排版
    g_doc->render(w);
    g_pg.load(g_doc.get(), w, h);

    // 2) 重建位图
    if (g_hCachedBmp) { DeleteObject(g_hCachedBmp); g_hCachedBmp = nullptr; }

    HDC hdc = GetDC(g_hView);               // ← 拿到窗口 DC
    g_hCachedBmp = CreateCompatibleBitmap(hdc, w, h);
    if (!g_hCachedBmp) { ReleaseDC(g_hView, hdc); return; }

    // 3) 画到内存 DC
    HDC mem = CreateCompatibleDC(hdc);      // ← 用同一个有效的 hdc
    HGDIOBJ old = SelectObject(mem, g_hCachedBmp);

    FillRect(mem, &rc, GetSysColorBrush(COLOR_WINDOW));
    g_pg.render(mem, g_page);

    SelectObject(mem, old);
    DeleteDC(mem);

    ReleaseDC(g_hView, hdc);                // ← 最后再释放

    g_cachedSize = { w, h };
    g_cachedPage = g_page;
}
// ---------- 窗口 ----------
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {


        DragAcceptFiles(h, TRUE);
        SendMessage(g_hWnd, WM_SIZE, 0, 0);
        return 0;
    }
    case WM_DROPFILES: {
        wchar_t file[MAX_PATH]{};
        DragQueryFileW(reinterpret_cast<HDROP>(w), 0, file, MAX_PATH);
        DragFinish(reinterpret_cast<HDROP>(w));

        // 1. 等待上一次任务结束（简单做法：阻塞等待）
        if (g_parse_task.valid()) {
            g_parse_task.wait();
        }

        // 2. 立即释放旧对象，防止野指针
        g_doc.reset();
        g_container.reset();
        g_pg.clear();              // 如果你 Paginator 有 clear() 就调

        // 3. 启动新任务
        SetStatus(L"正在加载...");
        g_parse_task = std::async(std::launch::async, [file] {
            try {
                g_book = EPUBBook{};   // 保险：用新实例
                g_book.load(file);
                PostMessage(g_hWnd, WM_EPUB_PARSED, 0, 0);
            }
            catch (const std::exception& e) {
                // 把异常文本发到主线程
                std::string what = e.what();
                auto* buf = (wchar_t*)CoTaskMemAlloc((what.size() + 1) * sizeof(wchar_t));
                MultiByteToWideChar(CP_UTF8, 0, what.c_str(), -1, buf, (int)what.size() + 1);
                PostMessage(g_hWnd, WM_LOAD_ERROR, 0, (LPARAM)buf);
            }
            catch (...) {
                PostMessage(g_hWnd, WM_LOAD_ERROR, 0,
                    (LPARAM)_wcsdup(L"未知错误"));
            }
            });
        return 0;
    }

    case WM_EPUB_PARSED: {
        std::string html = g_book.load_html(g_book.ocf_pkg_.spine[0].href);
        if (html.empty()) break;
        g_doc.reset();
        g_container.reset();
        g_container = std::make_shared<SimpleContainer>(L".");
        g_doc = litehtml::document::createFromString(html.c_str(), g_container.get());

        g_page = 0;

        // 2) 立即把第 0 页画到缓存位图
        UpdateCache();          // 复用前面给出的 UpdateCache()

        // 3) 更新滚动条
        SendMessage(g_hTrack, TBM_SETRANGE, TRUE,
            MAKELPARAM(0, g_pg.count() - 1));
        SendMessage(g_hTrack, TBM_SETPOS, TRUE, g_page);

        // 4) 触发一次轻量 WM_PAINT（只 BitBlt）
        InvalidateRect(g_hView, nullptr, FALSE);
        InvalidateRect(g_hWnd, nullptr, FALSE);
        UpdateWindow(g_hView);
        UpdateWindow(g_hWnd);
        SetStatus(L"加载完成");
        return 0;
    }
    case WM_SIZE:
    {
        SendMessage(g_hStatus, WM_SIZE, 0, 0);
        SendMessage(g_hView, WM_SIZE, 0, 0);
        RECT rcStatus, rcClient;
        GetClientRect(h, &rcClient);
        GetWindowRect(g_hStatus, &rcStatus);
        ScreenToClient(h, (POINT*)&rcStatus);
        ScreenToClient(h, (POINT*)&rcStatus + 1);
        int cyStatus = rcStatus.bottom - rcStatus.top;

        int cx = rcClient.right;
        int cy = rcClient.bottom - cyStatus;

        const int TV_W = 200;
        const int BAR_H = 30;

        MoveWindow(g_hwndTV, 0, 0, TV_W, cy, TRUE);
        MoveWindow(g_hTrack, TV_W, cy - BAR_H, cx - TV_W, BAR_H, TRUE);
        MoveWindow(g_hView, TV_W, 0, cx - TV_W, cy, TRUE);
        UpdateCache();
        UpdateWindow(g_hWnd);
        UpdateWindow(g_hView);
        return 0;
    }
    case WM_LOAD_ERROR: {
        wchar_t* msg = (wchar_t*)l;
        SetStatus(msg);
        free(msg);                // 对应 CoTaskMemAlloc / _wcsdup
        return 0;
    }
    case WM_PAINT: {
        
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(l) == g_hTrack) {
            g_page = SendMessage(g_hTrack, TBM_GETPOS, 0, 0);
            UpdateCache();          // 关键：立即排版并缓存
            InvalidateRect(g_hView, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        g_page += (delta < 0 ? 1 : -1);
        g_page = std::clamp(g_page, 0, g_pg.count() - 1);
        SendMessage(g_hTrack, TBM_SETPOS, TRUE, g_page);
        UpdateCache();              // 关键：立即排版并缓存
        InvalidateRect(g_hView, nullptr, FALSE);
        return 0;
    }


    case WM_KEYDOWN: {
        if (w == VK_RIGHT && g_page < g_pg.count() - 1) {
            ++g_page; SendMessage(g_hTrack, TBM_SETPOS, TRUE, g_page); InvalidateRect(h, nullptr, TRUE);
        }
        else if (w == VK_LEFT && g_page > 0) {
            --g_page; SendMessage(g_hTrack, TBM_SETPOS, TRUE, g_page); InvalidateRect(h, nullptr, TRUE);
        }
        return 0;
    }
    case WM_DESTROY: {
        for (HTREEITEM h = TreeView_GetRoot(g_hwndTV); h; )
        {
            TVITEMW tvi{ TVIF_PARAM };
            tvi.hItem = h;
            TreeView_GetItem(g_hwndTV, &tvi);
            free((void*)tvi.lParam);
            h = TreeView_GetNextSibling(g_hwndTV, h);
        }
        PostQuitMessage(0);
        return 0;
    }
    case WM_NOTIFY:
    {
        LPNMHDR nm = reinterpret_cast<LPNMHDR>(l);
        if (nm->hwndFrom == g_hwndTV)
        {
            // 1. 让系统完成默认选中/高亮
            LRESULT lr = DefWindowProc(h, m, w, l);

            // 2. 只在 *真正改变后* 做加载
            if (nm->code == TVN_SELCHANGED)
            {
                HTREEITEM hSel = TreeView_GetSelection(g_hwndTV);
                if (hSel)
                {
                    SetFocus(g_hwndTV);
                    wchar_t buf[128];
                    wsprintfW(buf, L"Focus=%08X  TV=%08X",
                        (DWORD)(ULONG_PTR)GetFocus(), (DWORD)(ULONG_PTR)g_hwndTV);
                        OutputDebugStringW(buf);
                    TVITEMW tvi{ TVIF_PARAM, hSel };
                    if (TreeView_GetItem(g_hwndTV, &tvi))
                    {
                        const wchar_t* href = reinterpret_cast<const wchar_t*>(tvi.lParam);
                        if (href && *href)
                            g_book.OnTreeSelChanged(href);
                    }
                }
            }
            return lr;   // 必须返回 DefWindowProc 的结果
        }
        break;
    }

    }

    return DefWindowProc(h, m, w, l);
    
}



// ---------- 入口 ----------
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int n) {
    ULONG_PTR gdiplusToken{};
    GdiplusStartupInput gdiplusStartupInput{};
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    g_hInst = h;
    InitCommonControls();
    WNDCLASSEX w{ sizeof(WNDCLASSEX) };
    w.lpfnWndProc = WndProc;
    w.hInstance = h;
    w.hCursor = LoadCursor(nullptr, IDC_ARROW);
    w.lpszClassName = L"EPUBLite";
    w.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassEx(&w);
    RegisterViewClass(g_hInst);
  
        
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    g_hWnd = CreateWindowW(L"EPUBLite", L"EPUB Lite Reader",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600,
        nullptr, nullptr, h, nullptr);
    // 放在主窗口 CreateWindow 之后
    g_hStatus = CreateWindowEx(
        0, STATUSCLASSNAME, L"就绪",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,           // 位置和大小由 WM_SIZE 调整
        g_hWnd, nullptr, g_hInst, nullptr);
    g_hTrack = CreateWindowEx(0, TRACKBAR_CLASS, nullptr,
        WS_CHILD | TBS_HORZ | TBS_BOTTOM, 0, 0, 100, 30,
        g_hWnd, nullptr, g_hInst, nullptr);
    g_hTrack = nullptr;
    g_hwndTV = CreateWindowExW(
        0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 200, 600,
        g_hWnd, (HMENU)100, g_hInst, nullptr);
    g_hView = CreateWindowExW(
        0, L"EPUBView", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 1, 1,
        g_hWnd, (HMENU)101, g_hInst, nullptr);

    ShowWindow(g_hWnd, n);
    UpdateWindow(g_hWnd);
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}

// ---------- 目录解析 ----------
void EPUBBook::LoadToc()
{
    OutputDebugStringA(("toc size = " + std::to_string(ocf_pkg_.toc.size()) + "\n").c_str());
    TreeView_DeleteAllItems(g_hwndTV);
    if (ocf_pkg_.toc.empty()) return;   // toc 是 std::vector<OCFNavPoint>

    for (const auto& np : ocf_pkg_.toc)
    {
        TVINSERTSTRUCTW tvi{};
        tvi.hParent = TVI_ROOT;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
        tvi.item.pszText = const_cast<LPWSTR>(np.label.c_str());
        tvi.item.lParam = reinterpret_cast<LPARAM>(_wcsdup(np.href.c_str()));
        TreeView_InsertItem(g_hwndTV, &tvi);
    }
}
// ---------- 点击目录跳转 ----------
void EPUBBook::OnTreeSelChanged(const wchar_t* href)
{
    if (!href || !*href) return;

    std::string html = g_book.load_html(href);
    if (html.empty()) return;
 
    g_doc.reset();
    g_container.reset();
    g_container = std::make_shared<SimpleContainer>(L".");
    g_doc = litehtml::document::createFromString(html.c_str(), g_container.get());

    g_page = 0;
    
    UpdateCache();

    SendMessage(g_hTrack, TBM_SETRANGE, TRUE,
        MAKELPARAM(0, g_pg.count() - 1));
    SendMessage(g_hTrack, TBM_SETPOS, TRUE, g_page);

    InvalidateRect(g_hView, nullptr, FALSE);
    UpdateWindow(g_hView);
    UpdateWindow(g_hWnd);
}