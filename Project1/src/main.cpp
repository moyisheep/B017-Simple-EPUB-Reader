// main.cpp  ——  优化后完整单文件
#define _WINSOCKAPI_
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#define WM_LOAD_ERROR (WM_USER + 3)
#include <windows.h>
#include <windowsx.h>   // 加这一行
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
#include <lunasvg/lunasvg.h>    
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
#pragma comment(lib, "comctl32.lib")
#include <shlwapi.h>
#include <regex>
#pragma comment(lib, "shlwapi.lib")

#include <wininet.h>
#include "resource.h"
#include <duktape.h>
#include <litehtml/el_text.h>


#include <chrono>
#include <thread>

namespace fs = std::filesystem;
HWND  g_hwndTV = nullptr;    // 侧边栏 TreeView
HIMAGELIST g_hImg = nullptr;   // 图标(可选)
HWND      g_hWnd;
HWND g_hStatus = nullptr;   // 状态栏句柄
HWND g_hView = nullptr;
enum class FontBackend { GDI = 0, DirectWrite = 1, FreeType = 2 };

static int g_scrollY = 0;   // 当前像素偏移
static int g_maxScroll = 0;   // 总高度 - 客户区高度
std::wstring g_currentHtmlDir = L"";
constexpr UINT WM_EPUB_PARSED = WM_APP + 1;
constexpr UINT WM_EPUB_UPDATE_SCROLLBAR = WM_APP + 2;
constexpr UINT WM_EPUB_CSS_RELOAD = WM_APP + 3;

struct AppSettings {
    bool disableCSS = false;   // 默认启用
    bool disableJS = false;   // 默认不禁用 JS
    bool disablePreprocessHTML = false;
    FontBackend selectedFontBackend = FontBackend::GDI;
};
AppSettings g_cfg;
enum class ImgFmt { PNG, JPEG, BMP, GIF, TIFF, SVG, UNKNOWN };
static std::unordered_map<std::wstring, HANDLE> g_customFonts;

std::string PreprocessHTML(std::string html);
void UpdateCache(void);
struct GdiplusDeleter { void operator()(Gdiplus::Image* p) const { delete p; } };
using ImagePtr = std::unique_ptr<Gdiplus::Image, GdiplusDeleter>;
static  std::string g_globalCSS = "";
static fs::file_time_type g_lastTime;

class Paginator {
public:
    void load(litehtml::document* doc, int w, int h);
    void render(HDC hdc, int scrollY);
    void clear();
private:
    litehtml::document* m_doc = nullptr;
    int m_w = 0, m_h = 0;
};

static ImgFmt detect_fmt(const uint8_t* d, size_t n, const wchar_t* ext)
{

    if (n >= 4 && memcmp(d, "\x89PNG", 4) == 0) return ImgFmt::PNG;
    if (n >= 2 && d[0] == 0xFF && d[1] == 0xD8)   return ImgFmt::JPEG;
    if (n >= 2 && d[0] == 'B' && d[1] == 'M')      return ImgFmt::BMP;
    if (n >= 6 && memcmp(d, "GIF87a", 6) == 0)    return ImgFmt::GIF;
    if (n >= 6 && memcmp(d, "GIF89a", 6) == 0)    return ImgFmt::GIF;
    if (n >= 4 && memcmp(d, "MM\x00*", 4) == 0)   return ImgFmt::TIFF;
    if (n >= 4 && memcmp(d, "II*\x00", 4) == 0)   return ImgFmt::TIFF;
    if (ext && _wcsicmp(ext, L"svg") == 0)       return ImgFmt::SVG;

    return ImgFmt::UNKNOWN;
}
struct IFontEngine {
    virtual ~IFontEngine() = default;
    virtual litehtml::uint_ptr create_font(const wchar_t* face,
        int size,
        int weight,
        bool italic,
        litehtml::font_metrics* fm) = 0;
    virtual void draw_text(litehtml::uint_ptr hdc,
        const char* text,
        litehtml::uint_ptr hFont,
        litehtml::web_color color,
        const litehtml::position& pos) = 0;
    virtual void delete_font(litehtml::uint_ptr hFont) = 0;
};
struct FontWrapper {
    HFONT hFont = nullptr;
    int height = 0, ascent = 0, descent = 0;
    FontWrapper(const wchar_t* face, int size, int weight, bool italic);
    FontWrapper(HFONT f) : hFont(f) {}
    ~FontWrapper();
};

class GdiEngine : public IFontEngine {
public:
    litehtml::uint_ptr create_font(const wchar_t* face,
        int size,
        int weight,
        bool italic,
        litehtml::font_metrics* fm) override;
    void draw_text(litehtml::uint_ptr hdc,
        const char* text,
        litehtml::uint_ptr hFont,
        litehtml::web_color color,
        const litehtml::position& pos) override;
    void delete_font(litehtml::uint_ptr hFont) override;

private:
    std::unordered_map<litehtml::uint_ptr, std::unique_ptr<FontWrapper>> m_fonts;
};



// 真正读文件
static void do_reload()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path cssPath = fs::path(exe).parent_path() / L"config" / L"global.css";

    try
    {
        auto t = fs::last_write_time(cssPath);
        if (t != g_lastTime)
        {
            std::ifstream f(cssPath, std::ios::binary);
            if (f)
            {
                std::ostringstream oss;
                oss << f.rdbuf();
                g_globalCSS = oss.str();
                g_lastTime = t;
                PostMessage(g_hView, WM_EPUB_CSS_RELOAD, 0, 0);
            }
        }
    }
    catch (...) { /* 文件不存在就忽略 */ }
}

// 后台线程：每秒检查一次
static void css_watcher_thread()
{
    do_reload();   // 启动时先读一次
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        do_reload();
    }
}

// 在 main() 里调用一次即可
inline void start_css_watcher()
{
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread(css_watcher_thread).detach();
        });
}
// ---------- 工具 ----------
static std::string w2a(const std::wstring& s)
{
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len - 1, 0);                 // 去掉末尾 '\0'
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], len, nullptr, nullptr);
    return out;
}

static std::wstring a2w(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len - 1, 0);                // 去掉末尾 '\0'
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
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


// 读取 ./config/global.css


static std::string insert_global_css(std::string html) {
    if (!g_globalCSS.empty())
    {
        // 简单暴力：直接插到 </head> 之前
        const std::string styleBlock =
            "<style>\n" + g_globalCSS + "\n</style>\n";
        size_t pos = html.find("</head>");
        if (pos != std::string::npos)
            html.insert(pos, styleBlock);
        else
            html.insert(0, "<head>" + styleBlock + "</head>");
    }
    return html;
}

void EnableClearType()
{
    BOOL ct = FALSE;
    SystemParametersInfoW(SPI_GETCLEARTYPE, 0, &ct, 0);
    if (!ct)
        SystemParametersInfoW(SPI_SETCLEARTYPE, TRUE, 0, SPIF_UPDATEINIFILE);
}
static HFONT CreateFontBetter(const wchar_t* faceW, int size, int weight,
    bool italic, HDC hdcForDpi)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(size, GetDeviceCaps(hdcForDpi, LOGPIXELSY), 72);
    lf.lfWeight = weight;
    lf.lfItalic = italic ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;          // ← 关键
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    const wchar_t* src = (faceW && *faceW) ? faceW : L"Microsoft YaHei";
    wcsncpy_s(lf.lfFaceName, src, _TRUNCATE);   // 超长自动截断

    // 让系统根据 FontLink 自动 fallback（中文、日文、符号都能匹配）
    return CreateFontIndirectW(&lf);
}
//// 创建字体时顺带把度量算好
//struct FontWrapper {
//    HFONT hFont = nullptr;
//    int   height = 0;
//    int   ascent = 0;
//    int   descent = 0;
//
//    explicit FontWrapper(const wchar_t* face, int size, int weight, bool italic)
//    {
//        HDC hdc = GetDC(nullptr);                    // 临时 HDC
//        hFont = CreateFontBetter(face, size, weight, italic, hdc);
//
//        if (hFont) {
//            HGDIOBJ old = SelectObject(hdc, hFont);
//            TEXTMETRICW tm{};
//            GetTextMetricsW(hdc, &tm);
//            height = tm.tmHeight;
//            ascent = tm.tmAscent;
//            descent = tm.tmDescent;
//            SelectObject(hdc, old);
//        }
//        ReleaseDC(nullptr, hdc);
//    }
//    ~FontWrapper() { if (hFont) DeleteObject(hFont); }
//};

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

struct TreeNode {
    const OCFNavPoint* data;
    std::vector<TreeNode> children;
};

static std::vector<TreeNode> BuildTree(const std::vector<OCFNavPoint>& flat)
{
    std::vector<TreeNode> roots;
    std::vector<TreeNode*> stack;          // 充当“当前父节点栈”
    stack.push_back(nullptr);              // 栈底：根层

    for (const auto& np : flat)
    {
        // 根据 order 决定栈深度
        while (stack.size() > static_cast<size_t>(np.order + 1))
            stack.pop_back();

        TreeNode node{ &np, {} };
        if (stack.back())
            stack.back()->children.push_back(std::move(node));
        else
            roots.push_back(std::move(node));

        stack.push_back(&roots.back());    // 新节点入栈
    }
    return roots;
}



class ZipIndexW {
public:
    ZipIndexW() = default;
    explicit ZipIndexW(mz_zip_archive& zip) { build(zip); }

    // 输入/输出均为 std::wstring
    std::wstring find(std::wstring href) const {
        // 1. 分离锚点
        size_t pos = href.find(L'#');
        std::wstring pure = pos == std::wstring::npos ? href : href.substr(0, pos);
        std::wstring anchor = pos == std::wstring::npos ? L"" : href.substr(pos);

        // 2. 查索引
        std::wstring key = normalize_key(pure);
        auto it = map_.find(key);
        std::wstring result = it == map_.end() ? std::wstring{} : it->second;

        // 3. 把锚点拼回去
        return result.empty() ? result : result + anchor;
    }

private:
    /* ---------- 大小写不敏感哈希/比较 ---------- */
    struct StringHashI {
        size_t operator()(std::wstring s) const noexcept {
            size_t h = 0;
            for (wchar_t c : s)
                h = h * 131 + towlower(static_cast<wint_t>(c));
            return h;
        }
    };
    struct StringEqualI {
        bool operator()(std::wstring a, std::wstring b) const noexcept {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                [](wchar_t c1, wchar_t c2) {
                    return towlower(static_cast<wint_t>(c1)) ==
                        towlower(static_cast<wint_t>(c2));
                });
        }
    };

    using Map = std::unordered_map<std::wstring, std::wstring,
        StringHashI, StringEqualI>;

    Map map_;



    /* ---------- 正规化路径 ---------- */
    static std::wstring url_decode(const std::wstring& in)
    {
        wchar_t out[INTERNET_MAX_URL_LENGTH];
        DWORD len = INTERNET_MAX_URL_LENGTH;
        if (SUCCEEDED(UrlCanonicalizeW(in.c_str(), out, &len, URL_UNESCAPE)))
            return std::wstring(out, len);
        return in;
    }

    static std::wstring normalize_key(std::wstring href)
    {
        // 1. 去掉 ? 和 #
        auto pure = href.substr(0, href.find_first_of(L"?#"));
        // 2. URL 解码
        pure = url_decode(pure);
        // 3. 取文件名并转小写
        auto filename = fs::path(pure).filename().wstring();
        std::transform(filename.begin(), filename.end(), filename.begin(),
            [](wchar_t c) { return towlower(static_cast<wint_t>(c)); });
        return filename;
    }

    /* ---------- 建立索引 ---------- */
    void build(mz_zip_archive& zip) {
        mz_uint idx = 0;
        mz_zip_archive_file_stat st{};
        while (mz_zip_reader_file_stat(&zip, idx++, &st)) {
            // miniz 返回 UTF-8，转成 wstring
            std::wstring wpath = a2w(st.m_filename);
            std::wstring key = normalize_key(wpath);
            map_.emplace(std::move(key), std::move(wpath));
        }
    }



};

ZipIndexW g_zipIndex;
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

    void load_all_fonts(void);
    static HTREEITEM InsertTreeNode(HWND tv, const TreeNode& node, HTREEITEM hParent)
    {
        TVINSERTSTRUCTW tvi{};
        tvi.hParent = hParent;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        tvi.item.pszText = const_cast<LPWSTR>(node.data->label.c_str());
        tvi.item.lParam = reinterpret_cast<LPARAM>(_wcsdup(node.data->href.c_str()));
        tvi.item.cChildren = node.children.empty() ? 0 : 1;
        HTREEITEM hItem = TreeView_InsertItem(tv, &tvi);

        for (const auto& child : node.children)
            InsertTreeNode(tv, child, hItem);

        return hItem;
    }
    static std::wstring extract_text(const tinyxml2::XMLElement* a)
    {
        if (!a) return L"";

        // 1. 拿到 <a> 的完整 XML 字符串
        tinyxml2::XMLPrinter printer;
        a->Accept(&printer);
        std::string xml = printer.CStr();   // "<a ...><span ...>I</span>: The Meadow</a>"

        // 2. 去掉最外层 <a ...> 和 </a>
        size_t start = xml.find('>') + 1;
        size_t end = xml.rfind('<');
        if (start == std::string::npos || end == std::string::npos || end <= start)
            return L"";

        std::string inner = xml.substr(start, end - start);   // "<span ...>I</span>: The Meadow"

        // 3. 简单剥掉所有标签（正则或手写）
        std::regex tag_re("<[^>]*>");
        std::string plain = std::regex_replace(inner, tag_re, "");

        return a2w(plain);   // "I: The Meadow"
    }
    // 递归解析 EPUB3-Nav <ol>
    static void parse_nav_list(tinyxml2::XMLElement* ol, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out)
    {
        if (!ol) return;
        for (auto* li = ol->FirstChildElement("li"); li; li = li->NextSiblingElement("li"))
        {
            auto* a = li->FirstChildElement("a");
            if (!a) continue;

            OCFNavPoint np;
            np.label = extract_text(a);
            np.href = a2w(a->Attribute("href") ? a->Attribute("href") : "");
            if (!np.href.empty())
                np.href = g_zipIndex.find(np.href);
            np.order = level;               // 层级深度
            out.emplace_back(std::move(np));

            // 递归子 <ol>
            if (auto* sub = li->FirstChildElement("ol"))
                parse_nav_list(sub, level + 1, opf_dir, out);
        }
    }

    // 递归解析 NCX <navPoint>
    static void parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
        const std::string& opf_dir,
        std::vector<OCFNavPoint>& out)
    {
        if (!navPoint) return;
        for (auto* pt = navPoint; pt; pt = pt->NextSiblingElement("navPoint"))
        {
            auto* lbl = pt->FirstChildElement("navLabel");
            auto* txt = lbl ? lbl->FirstChildElement("text") : nullptr;
            auto* con = pt->FirstChildElement("content");

            OCFNavPoint np;

            np.label = txt ? extract_text(txt) : L"";
            np.href = a2w(con && con->Attribute("src") ? con->Attribute("src") : "");
            if (!np.href.empty())
                np.href = g_zipIndex.find(np.href);
            np.order = level;               // 层级深度
            out.emplace_back(std::move(np));

            // 递归子 <navPoint>
            parse_ncx_points(pt->FirstChildElement("navPoint"), level + 1, opf_dir, out);
        }
    }



    EPUBBook() noexcept {
        load_all_fonts();
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
    namespace fs = std::filesystem;
    g_currentHtmlDir = fs::path(path).parent_path().wstring();
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
    g_zipIndex = ZipIndexW(zip);
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
            item.href = g_zipIndex.find(item.href);

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


void EPUBBook::parse_toc_()
{
    std::wstring toc_path;
    for (const auto& it : ocf_pkg_.manifest)
    {
        if (it.properties.find(L"nav") != std::wstring::npos ||
            it.id.find(L"ncx") != std::wstring::npos)
        {
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
    std::string opf_dir = w2a(ocf_pkg_.opf_dir);

    ocf_pkg_.toc.clear();

    if (is_nav)
    {
        auto* body = doc.FirstChildElement("html")
            ? doc.FirstChildElement("html")->FirstChildElement("body")
            : nullptr;
        if (!body) return;

        for (auto* nav = body->FirstChildElement("nav");
            nav;
            nav = nav->NextSiblingElement("nav"))
        {
            const char* type = nav->Attribute("epub:type");
            if (type && std::string(type) == "toc")
            {
                parse_nav_list(nav->FirstChildElement("ol"), 0, opf_dir, ocf_pkg_.toc);
                break;   // 找到就停
            }
        }
    }
    else // NCX
    {
        auto* navMap = doc.RootElement()
            ? doc.RootElement()->FirstChildElement("navMap")
            : nullptr;
        if (navMap)
            parse_ncx_points(navMap->FirstChildElement("navPoint"), 0, opf_dir, ocf_pkg_.toc);
    }
}

// ---------- LiteHtml 容器 ----------
class SimpleContainer : public litehtml::document_container {
public:
    explicit SimpleContainer(const std::wstring& root = L"")
        : m_root(root)
    {
        // 原来写在 SimpleContainer() 里的所有初始化代码搬到这里
        auto fw = std::make_unique<FontWrapper>(L"Segoe UI", 16, FW_NORMAL, false);
        m_hDefaultFont = fw->hFont;
        m_fonts[m_hDefaultFont] = std::move(fw);

        if (!g_cfg.disableJS) { enableJS(); }
        else {m_js = nullptr;}
        set_font_backend(g_cfg.selectedFontBackend);
       
    }
    void clear_images() { m_img_cache.clear(); }
    litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr h) override;
    int text_width(const char* text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;
    void load_image(const char* src, const char* /*baseurl*/, bool) override;

    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override;
    void get_client_rect(litehtml::position& client) const override;
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&, const std::shared_ptr<litehtml::document>&) override;
    void draw_background(litehtml::uint_ptr, const std::vector<litehtml::background_paint>&) override;
    int pt_to_px(int pt) const override;
    int get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "Microsoft YaHei"; }
    void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override;

    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override;
    void set_caption(const char*) override;
    void set_base_url(const char*) override;
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override;
    void on_anchor_click(const char*, const litehtml::element::ptr&) override;
    void set_cursor(const char*) override;
    void transform_text(litehtml::string&, litehtml::text_transform) override;

    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override;
    void del_clip() override;

    void get_media_features(litehtml::media_features&) const override;
    void get_language(litehtml::string&, litehtml::string&) const override;

    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override;
  
    void enableJS();
    void disableJS() { if (m_js) { duk_destroy_heap(m_js); m_js = nullptr; } }
    void run_pending_scripts();
    void bind_host_objects();   // 新增

    void set_font_backend(FontBackend b);   // 运行时切换
    void switch_backend_and_reload(FontBackend b, const std::string& html);

    ~SimpleContainer()
    {
        clear_images();   // 仅触发一次 Image 析构
        if (m_js) duk_destroy_heap(m_js);
        for (auto& kv : g_customFonts)
            RemoveFontMemResourceEx(kv.second);
        g_customFonts.clear();
    }


private:
    std::wstring m_root;
    // 1. 锚点表（id -> element）
    std::unordered_map<std::string, litehtml::element::ptr> m_anchor_map;

    // 2. 最后一次传入的 HDC，用于 set_clip / del_clip
    HDC m_last_hdc = nullptr;

    // 3. 默认字体句柄（FontWrapper 是你自己的字体包装类）
    HFONT m_hDefaultFont = nullptr;
    std::unordered_map<std::string, std::shared_ptr<Gdiplus::Image>> m_img_cache;
    std::unordered_map<HFONT, std::unique_ptr<FontWrapper>> m_fonts;
    duk_context* m_js = nullptr;   // ① Duktape 虚拟机
    struct script_info
    {
        litehtml::string src;
        litehtml::string inline_code;
    };
    std::vector<script_info> m_pending_scripts;
    std::unique_ptr<IFontEngine> m_fe;
    static std::shared_ptr<Gdiplus::Image> decode_img(const EPUBBook::MemFile& mf,
        const wchar_t* ext)
    {
        auto fmt = detect_fmt(mf.data.data(), mf.data.size(), ext);

        switch (fmt)
        {
        case ImgFmt::SVG:
        {
            auto doc = lunasvg::Document::loadFromData(
                reinterpret_cast<const char*>(mf.data.data()), mf.data.size());
            if (!doc) return nullptr;

            lunasvg::Bitmap svgBmp = doc->renderToBitmap();
            if (svgBmp.isNull()) return nullptr;

            const int w = svgBmp.width();
            const int h = svgBmp.height();
            auto* bmp = new Gdiplus::Bitmap(
                w, h, w * 4, PixelFormat32bppPARGB,
                reinterpret_cast<BYTE*>(svgBmp.data()));

            if (bmp->GetLastStatus() != Gdiplus::Ok)
            {
                delete bmp;
                return nullptr;
            }

            // Bitmap* -> Image* 隐式转换，返回 shared_ptr<Image>
            return std::shared_ptr<Gdiplus::Image>(
                bmp,
                [svgBmp = std::move(svgBmp)](Gdiplus::Image* p) { delete p; });
        }
        default:   // PNG/JPEG/BMP/GIF/TIFF/…
        {
            IStream* pStream = SHCreateMemStream(mf.data.data(),
                static_cast<UINT>(mf.data.size()));
            if (!pStream)
            {
                OutputDebugStringA("SHCreateMemStream failed\n");
                return nullptr;
            }

            std::shared_ptr<Gdiplus::Image> img(Gdiplus::Image::FromStream(pStream),
                [](Gdiplus::Image* p) {});
            pStream->Release();

            if (!img || img->GetLastStatus() != Gdiplus::Ok)
            {
                OutputDebugStringA("GDI+ decode failed\n");
                return nullptr;
            }
            return img;
        }
        }
    }
};


// ---------- 分页 ----------


void Paginator::load(litehtml::document* doc, int w, int h)
{
    m_doc = doc;
    m_w = w;
    m_h = h;
    g_maxScroll = 0;
    if (!m_doc) return;

    g_maxScroll = m_doc->height();
}
void Paginator::render(HDC hdc, int scrollY)
{
    if (scrollY < 0 || scrollY >= g_maxScroll) return;

    int old = SaveDC(hdc);

    litehtml::position clip{ 0, 0, m_w, m_h };
    m_doc->draw(reinterpret_cast<litehtml::uint_ptr>(hdc),
        0, -scrollY, &clip);

    RestoreDC(hdc, old);
}
void Paginator::clear() {
    m_doc = nullptr;
    m_w = m_h = 0;
    g_maxScroll = 0;
}

// ---------- 全局 ----------
HINSTANCE g_hInst;
std::shared_ptr<SimpleContainer> g_container;
EPUBBook  g_book;
std::shared_ptr<litehtml::document> g_doc;
Paginator g_pg;


std::future<void> g_parse_task;

static HBITMAP g_hCachedBmp = nullptr;
static int     g_cachedPage = -1;
static SIZE    g_cachedSize = {};
litehtml::element::ptr element_from_point(litehtml::element::ptr root,
    int x, int y)
{
    if (!root) return nullptr;

    // 倒序遍历（后渲染的在上面，先匹配）
    const auto& ch = root->children();
    for (auto it = ch.rbegin(); it != ch.rend(); ++it)
    {
        if (auto hit = element_from_point(*it, x, y))
            return hit;
    }

    // 检查自身
    litehtml::position pos = root->get_placement();
    if (x >= pos.left() && x < pos.right() &&
        y >= pos.top() && y < pos.bottom())
    {
        return root;
    }
    return nullptr;
}

LRESULT CALLBACK ViewWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
    {

        return 0;
    }
    case WM_EPUB_UPDATE_SCROLLBAR: {
        RECT rc;
        GetClientRect(h, &rc);
        // 垂直滚动条
        SCROLLINFO si{ sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = std::max(0, g_maxScroll);
        si.nPage = rc.bottom;               // 每次滚一页
        si.nPos = g_scrollY;
        SetScrollInfo(h, SB_VERT, &si, TRUE);
        // 水平滚动条（如果不需要可删掉）
        si.nMax = 0;
        si.nPage = rc.right;
        SetScrollInfo(h, SB_HORZ, &si, TRUE);
        // 重新排版+缓存
        UpdateCache();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }

    case WM_VSCROLL:
    {
        RECT rc;
        GetClientRect(h, &rc);
        int code = LOWORD(wp);
        int pos = HIWORD(wp);
        int delta = 0;

        switch (code)
        {
        case SB_LINEUP:   delta = -30; break;      // 3 行
        case SB_LINEDOWN: delta = 30; break;
        case SB_PAGEUP:   delta = -rc.bottom; break;
        case SB_PAGEDOWN: delta = rc.bottom; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            g_scrollY = std::clamp(pos, 0, g_maxScroll);          // 直接定位
            goto _scroll_end;
        default: return 0;
        }

        g_scrollY = std::clamp(g_scrollY + delta, 0, g_maxScroll);

    _scroll_end:
        SetScrollPos(h, SB_VERT, g_scrollY, TRUE);
        UpdateCache();
        InvalidateRect(h, nullptr, FALSE);   // 触发 WM_PAINT
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        RECT rc;
        GetClientRect(h, &rc);
        int zDelta = GET_WHEEL_DELTA_WPARAM(wp);
        g_scrollY = std::clamp<int>(g_scrollY - zDelta, 0, std::max<int>(g_maxScroll - rc.bottom, 0));
        SetScrollPos(h, SB_VERT, g_scrollY, TRUE);
        UpdateCache();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        if (g_hCachedBmp && g_cachedPage == g_scrollY)
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
        return 1;
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

// 4. UI 线程：只管发任务
void UpdateCache()
{
    if (!g_doc) return;

    RECT rc;
    GetClientRect(g_hView, &rc);
    int w = rc.right;
    int h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    // 1) 重新分页
    g_doc->render(w, litehtml::render_all);
    if (!g_cfg.disableJS) { g_container->run_pending_scripts(); }   // 现在才跑脚本
    g_pg.load(g_doc.get(), w, h);


    // 2) 重建单页位图
    if (g_hCachedBmp) DeleteObject(g_hCachedBmp);

    HDC hdc = GetDC(g_hView);
    g_hCachedBmp = CreateCompatibleBitmap(hdc, w, h);
    if (!g_hCachedBmp) { ReleaseDC(g_hView, hdc); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, g_hCachedBmp);

    RECT fillRc{ 0, 0, w, h };
    FillRect(mem, &fillRc, GetSysColorBrush(COLOR_WINDOW));

    g_pg.render(mem, g_scrollY);

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(g_hView, hdc);

    g_cachedSize = { w, h };
    g_cachedPage = g_scrollY;
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
        if (!g_cfg.disableCSS) { html = insert_global_css(html); }
        if (!g_cfg.disablePreprocessHTML) { html = PreprocessHTML(html); }
        g_doc.reset();
        g_container.reset();
        g_container = std::make_shared<SimpleContainer>(L".");
        g_doc = litehtml::document::createFromString(html.c_str(), g_container.get());

        g_scrollY = 0;

        // 2) 立即把第 0 页画到缓存位图
        UpdateCache();          // 复用前面给出的 UpdateCache()

        // 3) 更新滚动条

        // 4) 触发一次轻量 WM_PAINT（只 BitBlt）
        InvalidateRect(g_hView, nullptr, FALSE);
        InvalidateRect(g_hWnd, nullptr, FALSE);
        UpdateWindow(g_hView);
        UpdateWindow(g_hWnd);
        SetStatus(L"加载完成");
        return 0;
    }
    case WM_EPUB_CSS_RELOAD: {
        // 重新解析当前章节即可
        return 0;
    }
    case WM_SIZE:
    {
        SendMessage(g_hStatus, WM_SIZE, 0, 0);

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

        MoveWindow(g_hView, TV_W, 0, cx - TV_W, cy, TRUE);
        UpdateCache();
        SendMessage(g_hView, WM_EPUB_UPDATE_SCROLLBAR, 0, 0);

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

    case WM_MOUSEWHEEL: {

        //UpdateCache();              // 关键：立即排版并缓存
        //InvalidateRect(g_hView, nullptr, FALSE);
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

        if (g_hCachedBmp) DeleteObject(g_hCachedBmp);
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

                    TVITEMW tvi{ TVIF_PARAM, hSel };
                    if (TreeView_GetItem(g_hwndTV, &tvi))
                    {
                        const wchar_t* href = reinterpret_cast<const wchar_t*>(tvi.lParam);
                        if (href && *href)
                            g_book.OnTreeSelChanged(href);
                    }
                }
            }
            else if (nm->code == TVN_ITEMEXPANDED)
            {
                UpdateWindow(g_hWnd);
            }

            return lr;   // 必须返回 DefWindowProc 的结果
        }
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(w)) {
        case IDM_TOGGLE_CSS: {
            g_cfg.disableCSS = !g_cfg.disableCSS;
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
                MF_BYCOMMAND | (g_cfg.disableCSS ? MF_CHECKED : MF_UNCHECKED));

            break;
        }
        case IDM_TOGGLE_JS: {
            g_cfg.disableJS = !g_cfg.disableJS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
                MF_BYCOMMAND | (g_cfg.disableJS ? MF_CHECKED : MF_UNCHECKED));

            break;
        }
                          break;

        }
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
    w.lpszMenuName = nullptr;   // ← 必须为空
    RegisterClassEx(&w);
    RegisterViewClass(g_hInst);
    start_css_watcher();

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    // 在 CreateWindow 之前
    HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MENU_MAIN));
    if (!hMenu) {
        MessageBox(nullptr, L"LoadMenu 失败", L"Error", MB_ICONERROR);
        return 0;
    }



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

    g_hwndTV = CreateWindowExW(
        0, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 200, 600,
        g_hWnd, (HMENU)100, g_hInst, nullptr);
    g_hView = CreateWindowExW(
        0, L"EPUBView", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS,
        0, 0, 1, 1,
        g_hWnd, (HMENU)101, g_hInst, nullptr);

    SetMenu(g_hWnd, hMenu);            // ← 放在 CreateWindow 之后

    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
        MF_BYCOMMAND | (g_cfg.disableCSS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
        MF_BYCOMMAND | (g_cfg.disableJS ? MF_CHECKED : MF_UNCHECKED));

    
    EnableClearType();
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

    auto tree = BuildTree(ocf_pkg_.toc);
    for (const auto& root : tree)
        InsertTreeNode(g_hwndTV, root, TVI_ROOT);
}
// ---------- 点击目录跳转 ----------
void EPUBBook::OnTreeSelChanged(const wchar_t* href)
{
    if (!href || !*href) return;
 


    /* 1. 分离文件路径与锚点 */
    std::wstring whref(href);
    size_t pos = whref.find(L'#');
    std::wstring file = (pos == std::wstring::npos) ? whref : whref.substr(0, pos);
    std::string  id = (pos == std::wstring::npos) ? "" :
        litehtml::wchar_to_utf8(whref.substr(pos + 1));

    /* 2. 加载 HTML */
    std::string html = g_book.load_html(file.c_str());

    if (html.empty()) return;
    if (!g_cfg.disableCSS) { html = insert_global_css(html); }
    if (!g_cfg.disablePreprocessHTML) { html = PreprocessHTML(html); }
    g_doc.reset();
    g_container.reset();
    g_container = std::make_shared<SimpleContainer>(L".");
    // 完整兜底 UA 样式表（litehtml 专用）
 
    g_doc = litehtml::document::createFromString(html.c_str(), g_container.get());
    g_scrollY = 0;

    RECT rc;
    GetClientRect(g_hView, &rc);
    int w = rc.right;
    int h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    g_doc->render(w, litehtml::render_all);    // -1 表示“不限高度”
    if (!g_cfg.disableJS) { g_container->run_pending_scripts(); }   // 现在才跑脚本
    /* 3. 跳转到锚点 */
    if (!id.empty())
    {
        std::string cssSel = "#" + id;          // "#c10"
        litehtml::element::ptr el = g_doc->root()->select_one(cssSel.c_str());
        if (el)
        {
            litehtml::position pos = el->get_placement();
            // 同步滚动位置（示例：垂直滚动）
            g_scrollY = pos.y;

        }
    }

    UpdateCache();
    SendMessage(g_hView, WM_EPUB_UPDATE_SCROLLBAR, 0, 0);
    InvalidateRect(g_hView, nullptr, FALSE);
    UpdateWindow(g_hView);
    UpdateWindow(g_hWnd);
}

void SimpleContainer::load_image(const char* src, const char* /*baseurl*/, bool)
{
    if (m_img_cache.contains(src)) return;

    std::wstring wpath = g_zipIndex.find(a2w(src));

    EPUBBook::MemFile mf = g_book.read_zip(wpath.c_str());
    if (mf.data.empty())
    {
        OutputDebugStringW((L"EPUB not found: " + wpath + L"\n").c_str());
        return;
    }

    auto dot = wpath.find_last_of(L'.');
    std::wstring ext;
    if (dot != std::wstring::npos && dot + 1 < wpath.size()) {
        ext = wpath.substr(dot + 1);                 // 去掉“.”
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower); // 转小写
    }
    auto img = decode_img(mf, ext.empty() ? nullptr : ext.c_str());

    if (img)
    {
        m_img_cache.emplace(src, std::move(img));
    }
    else
    {
        OutputDebugStringA(("EPUB decode failed: " + std::string(src) + "\n").c_str());
    }
}
void SimpleContainer::draw_background(litehtml::uint_ptr hdc,
    const std::vector<litehtml::background_paint>& bg)
{
    HDC dc = reinterpret_cast<HDC>(hdc);
    for (const auto& b : bg)
    {
        /* 背景色 */
        if (&b == &bg.back() && b.color.alpha > 0)
        {
            HBRUSH br = CreateSolidBrush(RGB(b.color.red, b.color.green, b.color.blue));
            RECT rc{ b.border_box.x, b.border_box.y,
                     b.border_box.x + b.border_box.width,
                     b.border_box.y + b.border_box.height };
            FillRect(dc, &rc, br);
            DeleteObject(br);
        }

        if (b.image.empty()) continue;

        auto it = m_img_cache.find(b.image);
        if (it == m_img_cache.end())
        {
            OutputDebugStringA(("MISS: " + b.image + "\n").c_str());
            continue;
        }

        std::shared_ptr<Gdiplus::Image> img = it->second;   // 去掉内层作用域
        if (!img) continue;

        const int imgW = img->GetWidth();
        const int imgH = img->GetHeight();
        if (imgW <= 0 || imgH <= 0) continue;

        Gdiplus::Graphics g(dc);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        Gdiplus::Rect dstRect(b.border_box.x, b.border_box.y,
            b.border_box.width, b.border_box.height);

        const bool isImgLike = (&b == &bg.back());
        if (isImgLike)
        {
            g.DrawImage(img.get(), dstRect, 0, 0, imgW, imgH, Gdiplus::UnitPixel);
        }
        else
        {
            int srcX = static_cast<int>(b.position_x);
            int srcY = static_cast<int>(b.position_y);
            int srcW = static_cast<int>(b.image_size.width);
            int srcH = static_cast<int>(b.image_size.height);
            if (srcW <= 0 || srcH <= 0) { srcW = imgW; srcH = imgH; }
            g.DrawImage(img.get(), dstRect, srcX, srcY, srcW, srcH, Gdiplus::UnitPixel);
        }
    }
}

void SimpleContainer::delete_font(litehtml::uint_ptr hFont)
{
    //HFONT hFont = reinterpret_cast<HFONT>(h);
    //m_fonts.erase(hFont);   // unique_ptr 自动 DeleteObject
    m_fe->delete_font(hFont);
}
int SimpleContainer::text_width(const char* text, litehtml::uint_ptr hFont) 
{
    if (!hFont || !text) return 0;
    HFONT hF = reinterpret_cast<HFONT>(hFont);
    HDC hdc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(hdc, hF);

    SIZE sz{};
    std::wstring wtxt = a2w(text);
    GetTextExtentPoint32W(hdc, wtxt.c_str(), static_cast<int>(wtxt.size()), &sz);

    SelectObject(hdc, old);
    ReleaseDC(nullptr, hdc);
    return sz.cx;
}


void SimpleContainer::get_image_size(const char* src, const char* baseurl, litehtml::size& sz) {
    if (!m_img_cache.contains(src)) { sz.width = sz.height = 0; return; }
    auto img = m_img_cache[src];
    sz.width = img->GetWidth();
    sz.height = img->GetHeight();
}

void SimpleContainer::get_client_rect(litehtml::position& client) const {
    RECT rc{}; GetClientRect(g_hWnd, &rc);
    client = { 0, 0, rc.right, rc.bottom - 30 };
}

litehtml::element::ptr
SimpleContainer::create_element(const char* tag,
    const litehtml::string_map& attrs,
    const std::shared_ptr<litehtml::document>& doc)
{
    if (litehtml::t_strcasecmp(tag, "script") == 0)
    {
        script_info si;
        auto it = attrs.find("src");
        if (it != attrs.end()) si.src = it->second;

        // 内联代码暂时留空，等节点文本解析完再回填
        si.inline_code.clear();
        m_pending_scripts.emplace_back(std::move(si));
    }
    return nullptr;   // 其余元素交给 litehtml 默认流程
}

int SimpleContainer::pt_to_px(int pt) const {
    return MulDiv(pt, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72);
}

void SimpleContainer::run_pending_scripts()
{
    for (auto& script : m_pending_scripts)
    {
        // 取出 <script> 里的文本
        litehtml::string code;
     
        if (!script.src.empty())
        {
            std::wstring w_path = g_zipIndex.find(a2w(script.src));
            EPUBBook::MemFile mf = g_book.read_zip(w_path.c_str());
            code = std::string(reinterpret_cast<const char*>(mf.data.data()),
                mf.data.size());
         
        }
        else
        {
            // 2. 没有 src，就取节点内部文本
            code = script.inline_code;
        }
        if (!code.empty())
        {
            duk_push_string(m_js, code.c_str());
            if (duk_peval(m_js) != 0)   // 安全执行
            {
                OutputDebugStringA(duk_safe_to_string(m_js, -1));
            }
            duk_pop(m_js);
        }
    }
    m_pending_scripts.clear();
}
void SimpleContainer::import_css(litehtml::string& text,
    const litehtml::string& url,
    litehtml::string& baseurl)
{
    if (g_cfg.disableCSS) {
        text.clear();           // 禁用所有外部/内部 CSS
        return;
    }
    // url 可能是相对路径，baseurl 是当前 html 所在目录
    std::wstring w_path = g_zipIndex.find(a2w(url));
    EPUBBook::MemFile mf = g_book.read_zip(w_path.c_str());
    if (!mf.data.empty())
    {
        // 直接填到 text（litehtml 期望 UTF-8）
        text.assign(reinterpret_cast<const char*>(mf.data.data()),
            mf.data.size());
    }
    else
    {
        OutputDebugStringW((L"CSS not found: " + w_path + L"\n").c_str());
    }

    // baseurl 保持原样即可
}


static void AddRoundRect(Gdiplus::GraphicsPath& path,
    const Gdiplus::RectF& rc,
    const float rx[4], const float ry[4])
{
    // 左上
    path.AddArc(rc.X, rc.Y, rx[0] * 2, ry[0] * 2, 180, 90);
    // 右上
    path.AddArc(rc.X + rc.Width - rx[1] * 2, rc.Y, rx[1] * 2, ry[1] * 2, 270, 90);
    // 右下
    path.AddArc(rc.X + rc.Width - rx[2] * 2, rc.Y + rc.Height - ry[2] * 2,
        rx[2] * 2, ry[2] * 2, 0, 90);
    // 左下
    path.AddArc(rc.X, rc.Y + rc.Height - ry[3] * 2, rx[3] * 2, ry[3] * 2, 90, 90);
    path.CloseFigure();
}
void SimpleContainer::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& pos,
    bool root)
{
    HDC dc = reinterpret_cast<HDC>(hdc);

    // 把 CSS 像素转成实际像素（DPI 缩放）
    int dpi = 96;                     // 如果你前面有 dpi，直接替换
    auto px = [=](float css) -> float {
        return css * dpi / 96.0f;
        };

    // 目标矩形
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::RectF rc(
        (Gdiplus::REAL)pos.x,
        (Gdiplus::REAL)pos.y,
        (Gdiplus::REAL)pos.width,
        (Gdiplus::REAL)pos.height);

    // 构造圆角矩形路径
    Gdiplus::GraphicsPath path;
    float rx[4] = {
        px(borders.radius.top_left_x),
        px(borders.radius.top_right_x),
        px(borders.radius.bottom_right_x),
        px(borders.radius.bottom_left_x)
    };
    float ry[4] = {
        px(borders.radius.top_left_y),
        px(borders.radius.top_right_y),
        px(borders.radius.bottom_right_y),
        px(borders.radius.bottom_left_y)
    };
    AddRoundRect(path, rc, rx, ry);

    // 画四条边（颜色/宽度可能不同，这里简化成四条线）
    // 如果四条边完全一致，可以直接 FillPath + DrawPath 一次完成
    auto draw_side = [&](Gdiplus::Color c, float w,
        const Gdiplus::PointF& p1,
        const Gdiplus::PointF& p2)
        {
            if (w <= 0) return;
            Gdiplus::Pen pen(c, w);
            g.DrawLine(&pen, p1, p2);
        };

    Gdiplus::Color cTop = Gdiplus::Color(borders.top.color.alpha,
        borders.top.color.red,
        borders.top.color.green,
        borders.top.color.blue);
    Gdiplus::Color cRight = Gdiplus::Color(borders.right.color.alpha,
        borders.right.color.red,
        borders.right.color.green,
        borders.right.color.blue);
    Gdiplus::Color cBottom = Gdiplus::Color(borders.bottom.color.alpha,
        borders.bottom.color.red,
        borders.bottom.color.green,
        borders.bottom.color.blue);
    Gdiplus::Color cLeft = Gdiplus::Color(borders.left.color.alpha,
        borders.left.color.red,
        borders.left.color.green,
        borders.left.color.blue);

    float wTop = px(borders.top.width);
    float wRight = px(borders.right.width);
    float wBottom = px(borders.bottom.width);
    float wLeft = px(borders.left.width);

    // 四条直线（圆角矩形四条边）
    draw_side(cTop, wTop, { rc.X, rc.Y }, { rc.X + rc.Width, rc.Y });
    draw_side(cRight, wRight, { rc.X + rc.Width, rc.Y }, { rc.X + rc.Width, rc.Y + rc.Height });
    draw_side(cBottom, wBottom, { rc.X + rc.Width, rc.Y + rc.Height }, { rc.X, rc.Y + rc.Height });
    draw_side(cLeft, wLeft, { rc.X, rc.Y + rc.Height }, { rc.X, rc.Y });
}

// ---------- 2. 标题 ----------------------------------------------------
void SimpleContainer::set_caption(const char* cap)
{
    if (cap && g_hWnd) {
        SetWindowTextW(g_hWnd, a2w(cap).c_str());
        OutputDebugStringW((a2w(cap)+L"\n").c_str());
    }
}

// ---------- 3. base url -------------------------------------------------
void SimpleContainer::set_base_url(const char* base)
{
    return ;
}

// ---------- 4. 链接注册 --------------------------------------------------
void SimpleContainer::link(const std::shared_ptr<litehtml::document>& doc,
    const litehtml::element::ptr& el)
{
    // 简单做法：把锚点 id -> 元素 存起来，点击时滚动
    const char* id = el->get_attr("id");
    if (id && *id)
        m_anchor_map[id] = el;
}

// ---------- 5. 点击锚点 -------------------------------------------------
void SimpleContainer::on_anchor_click(const char* url,
    const litehtml::element::ptr& el)
{
    if (!url || !*url) return;

    // 内部 #id
    if (url[0] == '#')
    {
        auto it = m_anchor_map.find(url + 1);
        if (it != m_anchor_map.end())
        {
            // 这里只是示例：把元素 y 坐标发出去，真正滚动自己实现
            int y = it->second->get_placement().y;
            SendMessage(g_hWnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, y), 0);
        }
        return;
    }

    // 外部链接：交给宿主
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------- 6. 鼠标形状 -------------------------------------------------
void SimpleContainer::set_cursor(const char* cursor)
{
    LPCWSTR id = IDC_ARROW;
    if (cursor)
    {
        if (strcmp(cursor, "pointer") == 0) id = IDC_HAND;
        else if (strcmp(cursor, "text") == 0) id = IDC_IBEAM;
    }
    SetCursor(LoadCursor(nullptr, id));
}

// ---------- 7. 文本转换 ----------------------------------------------
void SimpleContainer::transform_text(litehtml::string& text,
    litehtml::text_transform tt)
{
    if (text.empty()) return;
    std::wstring w = a2w(text.c_str());
    switch (tt)
    {
    case litehtml::text_transform_capitalize:
        if (!w.empty()) w[0] = towupper(w[0]);
        for (size_t i = 1; i < w.size(); ++i)
            if (iswspace(w[i - 1])) w[i] = towupper(w[i]);
        break;
    case litehtml::text_transform_uppercase:
        CharUpperBuffW(w.data(), (DWORD)w.size());
        break;
    case litehtml::text_transform_lowercase:
        CharLowerBuffW(w.data(), (DWORD)w.size());
        break;
    default: break;
    }
    text = w2a(w);
}

// ---------- 8. 裁剪 ----------------------------------------------------
void SimpleContainer::set_clip(const litehtml::position& pos,
    const litehtml::border_radiuses& radius)
{
    // 取得当前绘制 HDC
    HDC hdc = reinterpret_cast<HDC>(m_last_hdc);
    if (!hdc) return;

    // 1. 如果四个角半径都为 0，退化为矩形
    if (radius.top_left_x == 0 && radius.top_right_x == 0 &&
        radius.bottom_left_x == 0 && radius.bottom_right_x == 0)
    {
        HRGN rgn = CreateRectRgn(pos.x, pos.y,
            pos.x + pos.width,
            pos.y + pos.height);
        SelectClipRgn(hdc, rgn);
        DeleteObject(rgn);
        return;
    }

    // 2. 否则用圆角矩形
    //    CreateRoundRectRgn 的圆角直径 = 2 * radius
    int rx = std::max({ radius.top_left_x, radius.top_right_x,
                       radius.bottom_left_x, radius.bottom_right_x });
    int ry = rx;   // 简化：保持 1:1 圆角；如需椭圆角可分别传 rx/ry
    HRGN rgn = CreateRoundRectRgn(pos.x, pos.y,
        pos.x + pos.width,
        pos.y + pos.height,
        rx * 2, ry * 2);
    SelectClipRgn(hdc, rgn);
    DeleteObject(rgn);
}
void SimpleContainer::del_clip()
{
    SelectClipRgn(reinterpret_cast<HDC>(m_last_hdc), nullptr);
}

// ---------- 9. 媒体查询 -----------------------------------------------
void SimpleContainer::get_media_features(litehtml::media_features& mf) const
{
    RECT rc; GetClientRect(g_hWnd, &rc);
    mf.width = rc.right - rc.left;
    mf.height = rc.bottom - rc.top;
    mf.device_width = GetSystemMetrics(SM_CXSCREEN);
    mf.device_height = GetSystemMetrics(SM_CYSCREEN);
    mf.color = 8;        // 24 位色
    mf.monochrome = 0;
    mf.type = litehtml::media_type_screen;
}

// ---------- 10. 语言 ---------------------------------------------------
void SimpleContainer::get_language(litehtml::string& language,
    litehtml::string& culture) const
{
    language = "en";
    culture = "US";
    // 真正 EPUB 可从 OPF <dc:language> 读
}

// ---------- 11. 列表标记 ----------------------------------------------
void SimpleContainer::draw_list_marker(litehtml::uint_ptr hdc,
    const litehtml::list_marker& marker)
{
    HDC dc = reinterpret_cast<HDC>(hdc);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));

    std::wstring txt;
    if (marker.marker_type == litehtml::list_style_type_disc)
        txt = L"•";
    else if (marker.marker_type == litehtml::list_style_type_decimal)
        txt = std::to_wstring(marker.index) + L".";

    HFONT hOld = (HFONT)SelectObject(dc, m_hDefaultFont);
    TextOutW(dc, marker.pos.x, marker.pos.y, txt.c_str(), (int)txt.size());
    SelectObject(dc, hOld);
}


// ---------- 1. console ----------
static duk_ret_t js_console_log(duk_context* ctx)
{
    const char* msg = duk_safe_to_string(ctx, 0);
    OutputDebugStringA("JS: ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
    return 0;
}

// ---------- 2. setTimeout ----------
struct TimeoutData
{
    duk_context* ctx;
    int fn_ref;
};
static void __stdcall timeout_cb(void* ud, unsigned long timer_low, unsigned long timer_high)
{
    TimeoutData* td = static_cast<TimeoutData*>(ud);
    duk_push_global_stash(td->ctx);
    duk_get_prop_index(td->ctx, -1, td->fn_ref);
    if (duk_pcall(td->ctx, 0) != 0)
        OutputDebugStringA(duk_safe_to_string(td->ctx, -1));
    duk_pop(td->ctx);               // pop result
    duk_del_prop_index(td->ctx, -1, td->fn_ref);
    duk_pop(td->ctx);               // pop stash
    delete td;
}
static duk_ret_t js_setTimeout(duk_context* ctx)
{
    if (!duk_is_function(ctx, 0)) return 0;
    int delay = duk_to_int(ctx, 1);
    duk_push_global_stash(ctx);
    int ref = duk_get_top_index(ctx);
    duk_dup(ctx, 0);
    duk_put_prop_index(ctx, -2, ref);

    TimeoutData* td = new TimeoutData{ ctx, ref };
    CreateThreadpoolTimer(
        (PTP_TIMER_CALLBACK)timeout_cb,
        td,
        nullptr);
    return 0;
}

// ---------- 3. fetch ----------
static duk_ret_t js_fetch(duk_context* ctx)
{
    const char* url = duk_require_string(ctx, 0);
    // 极简同步实现：直接读 zip
    duk_push_this(ctx);                      // 拿到 EPUBHost 对象
    duk_get_prop_string(ctx, -1, "\xff""self");
    SimpleContainer* self = static_cast<SimpleContainer*>(duk_get_pointer(ctx, -1));
    std::wstring wpath = g_zipIndex.find(a2w(url));
    EPUBBook::MemFile mf = g_book.read_zip(wpath.c_str());
    if (mf.data.empty())
    {
        duk_push_null(ctx);
        return 1;
    }
    std::string text(reinterpret_cast<const char*>(mf.data.data()), mf.data.size());
    duk_push_string(ctx, text.c_str());
    return 1;
}

// ---------- 4. document ----------
static duk_ret_t js_doc_get_by_tag(duk_context* ctx)
{
    const char* tag = duk_require_string(ctx, 0);
    // 这里只是演示：返回一个固定对象
    duk_push_object(ctx);
    duk_push_string(ctx, tag);
    duk_put_prop_string(ctx, -2, "tagName");
    return 1;
}

// ---------- 5. window ----------
static duk_ret_t js_window_scroll(duk_context* ctx)
{
    int x = duk_require_int(ctx, 0);
    int y = duk_require_int(ctx, 1);
    // 真正项目里调用 C++ 滚动接口
    OutputDebugStringA(("scrollTo(" + std::to_string(x) + "," + std::to_string(y) + ")\n").c_str());
    return 0;
}

// ---------- 绑定入口 ----------
void SimpleContainer::bind_host_objects()
{
    // 在 bind_host_objects() 里
    duk_push_object(m_js);                       // prototype
    duk_push_c_function(m_js, js_fetch, 1);
    duk_put_prop_string(m_js, -2, "fetch");
    duk_push_pointer(m_js, this);                // 把 this 存到原型
    duk_put_prop_string(m_js, -2, "\xff""self");
    duk_put_global_string(m_js, "EPUBHost");     // 全局变量 EPUBHost

    // console
    duk_push_object(m_js);
    duk_push_c_function(m_js, js_console_log, 1);
    duk_put_prop_string(m_js, -2, "log");
    duk_put_global_string(m_js, "console");

    // setTimeout
    duk_push_c_function(m_js, js_setTimeout, 2);
    duk_put_global_string(m_js, "setTimeout");

    // fetch
    duk_push_c_function(m_js, js_fetch, 1);
    duk_put_global_string(m_js, "fetch");

    // document
    duk_push_object(m_js);
    duk_push_c_function(m_js, js_doc_get_by_tag, 1);
    duk_put_prop_string(m_js, -2, "getElementsByTagName");
    duk_put_global_string(m_js, "document");

    // window
    duk_push_object(m_js);
    duk_push_c_function(m_js, js_window_scroll, 2);
    duk_put_prop_string(m_js, -2, "scrollTo");
    duk_put_global_string(m_js, "window");
}

void SimpleContainer::enableJS() {
    m_js = duk_create_heap_default();
    if (!m_js) throw std::runtime_error("Duktape init failed");
    bind_host_objects();   // 关键：注册宿主对象
    duk_push_c_function(m_js, [](duk_context* ctx)->duk_ret_t {
        const char* id = duk_require_string(ctx, 0);
        auto self = static_cast<SimpleContainer*>(duk_require_pointer(ctx, 1));
        duk_push_boolean(ctx, self->m_anchor_map.find(id) != self->m_anchor_map.end());
        return 1;
        }, 2);
    duk_put_global_string(m_js, "hasAnchor");

    duk_eval_string(m_js, "'Duktape inside SimpleContainer ready';");
    duk_pop(m_js);
}


// --------------------------------------------------
// 通用 HTML 预处理
// --------------------------------------------------
std::string PreprocessHTML(std::string html)
{
    //-------------------------------------------------
    // 1. Adobe Adept <meta name="..." value="..."/>
    //-------------------------------------------------
// 把 <title/> 或 <title /> 改成成对标签 <title></title>
    html = std::regex_replace(html,
        std::regex(R"(<title\b[^>]*?/\s*>)", std::regex::icase),
        "<title></title>");
    //-------------------------------------------------
    // 2. EPUB 3 的 <meta property="..." content="..."/>
    //     litehtml 只认识 name/content，不认识 property
    //-------------------------------------------------
    //html = std::regex_replace(html,
    //    std::regex(R"(<meta\b([^>]*)\bproperty\s*=\s*["']([^"']*)["']([^>]*)\bcontent\s*=\s*["']([^"']*)["']([^>]*)/?>)",
    //        std::regex::icase),
    //    "<meta $1name=\"$2\" content=\"$4\"$5>");

    //-------------------------------------------------
    // 3. 自闭合标签缺少空格导致解析错位
    //     例如 <br/> <hr/> <img .../> 写成 <br/ > <img.../>
    //-------------------------------------------------
    //html = std::regex_replace(html,
    //    std::regex(R"(<([a-zA-Z]+)(\s*[^>]*?)\s*/\s*>)"),
    //    "<$1$2 />");

    //-------------------------------------------------
    // 4. 删除 epub 专用命名空间属性
    //-------------------------------------------------
    //html = std::regex_replace(html,
    //    std::regex(R"(\s+xmlns(:\w+)?\s*=\s*["'][^"']*["'])"),
    //    "");
    //html = std::regex_replace(html,
    //    std::regex(R"(\s+\w+:\w+\s*=\s*["'][^"']*["'])"),
    //    "");

    //-------------------------------------------------
    // 5. 可选：压缩连续空白字符
    //-------------------------------------------------
    // html = std::regex_replace(html, std::regex(R"(\s+)"), " ");

    return html;
}



void SimpleContainer::set_font_backend(FontBackend b) {
    if (g_doc) g_doc.reset();   // 释放所有字体
    m_fe.reset();
    // 先尝试切换
    switch (b) {
    case FontBackend::GDI: {
        m_fe = std::make_unique<GdiEngine>();
        return;
    }
        // 以后扩展：
        // case FontBackend::DirectWrite:
        //     m_fe = std::make_unique<DWriteEngine>();
        //     return;
        // case FontBackend::FreeType:
        //     m_fe = std::make_unique<FtEngine>();
        //     return;
    default:
        break;
    }

    // 找不到实现 → 强制 GDI
    m_fe = std::make_unique<GdiEngine>();
}

litehtml::uint_ptr SimpleContainer::create_font(const char* faceName,
    int size,
    int weight,
    litehtml::font_style italic,
    unsigned int,
    litehtml::font_metrics* fm) {
    return m_fe->create_font(a2w(faceName).c_str(), size, weight,
        italic != litehtml::font_style_normal, fm);
}

void SimpleContainer::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos) {
    m_fe->draw_text(hdc, text, hFont, color, pos);
}

FontWrapper::FontWrapper(const wchar_t* face, int size, int weight, bool italic) {
    hFont = CreateFontW(-size, 0, 0, 0, weight,
        italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, face);

    if (hFont) {
        HDC dc = GetDC(nullptr);
        HFONT old = (HFONT)SelectObject(dc, hFont);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        height = tm.tmHeight;
        ascent = tm.tmAscent;
        descent = tm.tmDescent;
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }
}
FontWrapper::~FontWrapper() { if (hFont) DeleteObject(hFont); }


litehtml::uint_ptr GdiEngine::create_font(const wchar_t* face,
    int size,
    int weight,
    bool italic,
    litehtml::font_metrics* fm)
{
    // 1. 先尝试用 EPUB 自带字体名（已注册到系统）
    HFONT hFont = CreateFontW(-size, 0, 0, 0, weight,
        italic ? TRUE : FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, face);

    // 2. 如果系统没有该字体，再回退到系统字体
    if (!hFont)
    {
        hFont = CreateFontW(-size, 0, 0, 0, weight,
            italic ? TRUE : FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI"); // 回退字体
    }

    if (!hFont) return 0;

    auto fw = std::make_unique<FontWrapper>(hFont); // 下面改构造
    if (fm)
    {
        HDC dc = GetDC(nullptr);
        HFONT old = (HFONT)SelectObject(dc, hFont);
        //SetBkMode(dc, TRANSPARENT);          // 避免背景色影响
        //SetTextCharacterExtra(dc, 0);        // 关闭字符间距微调
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        fm->height = tm.tmHeight;
        fm->ascent = tm.tmAscent;
        fm->descent = tm.tmDescent;
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
    }

    litehtml::uint_ptr h = reinterpret_cast<litehtml::uint_ptr>(hFont);
    m_fonts[h] = std::move(fw);
    return h;
}

void GdiEngine::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos) {
    if (!hFont || !text) return;
    HDC dc = reinterpret_cast<HDC>(hdc);
    HFONT hF = reinterpret_cast<HFONT>(hFont);
    HGDIOBJ old = SelectObject(dc, hF);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(color.red, color.green, color.blue));
    std::wstring wtxt = a2w(text);
    TextOutW(dc, pos.left(), pos.top(), wtxt.c_str(), (int)wtxt.size());
    SelectObject(dc, old);
}

void GdiEngine::delete_font(litehtml::uint_ptr hFont) {
    m_fonts.erase(hFont);
}

void EPUBBook::load_all_fonts()
{

    for (const auto& item : g_book.ocf_pkg_.manifest)
    {
        const std::wstring& mime = item.media_type;
        if (mime != L"application/x-font-ttf" &&
            mime != L"application/font-sfnt" &&
            mime != L"font/otf" &&
            mime != L"font/ttf" &&
            mime != L"font/woff" &&
            mime != L"font/woff2")
        {
            continue;
        }

        // 用 zip 索引把 href 转成 zip 内路径
        std::wstring wpath = g_zipIndex.find(item.href);   
        EPUBBook::MemFile mf = g_book.read_zip(wpath.c_str());

        if (mf.data.empty())
        {
            OutputDebugStringW((L"[Font] 字体文件为空: " + wpath + L"\n").c_str());
            continue;
        }

        DWORD nFonts = 0;
        HANDLE hFont = AddFontMemResourceEx(
            (void*)mf.data.data(),
            (DWORD)mf.data.size(),
            nullptr,
            &nFonts);
        if (!hFont)
        {
            OutputDebugStringW((L"[Font] 添加失败: " + wpath + L"\n").c_str());
            continue;
        }

        OutputDebugStringW((L"[Font] 添加成功: " + wpath + L"\n").c_str());

        g_customFonts[wpath] = hFont;
    }
}