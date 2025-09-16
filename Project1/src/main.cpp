#include "main.h"

HWND  g_hwndToc = nullptr;    // 侧边栏 TreeView
HIMAGELIST g_hImg = nullptr;   // 图标(可选)
HWND      g_hWnd = nullptr;
HWND g_hStatus = nullptr;   // 状态栏句柄
HWND g_hView = nullptr;
HWND g_hTooltip = nullptr;
HWND g_hImageview = nullptr;
HWND g_hViewScroll = nullptr;
HWND g_hHomepage = nullptr;
// ---------- 全局 ----------
HINSTANCE g_hInst;
std::shared_ptr<SimpleContainer> g_cMain;
std::shared_ptr<SimpleContainer> g_cTooltip;
std::shared_ptr<SimpleContainer> g_cImage;
std::shared_ptr<SimpleContainer> g_cHome;

std::shared_ptr<EPUBBook>  g_book;

Gdiplus::Image* g_pSplashImg = nullptr;
std::future<void> g_parse_task;
enum class StatusBar { INFO = 0, FONT = 1 };
std::unique_ptr<VirtualDoc> g_vd;
static float g_scrollY = 0.0f;   // 当前像素偏移
static float g_offsetY = 0.0f;
static int g_maxScroll = 0;   // 总高度 - 客户区高度
static float g_line_height = 1.0f;
std::wstring g_currentHtmlDir = L"";
std::wstring g_currentHtmlPath = L"";
constexpr UINT WM_EPUB_PARSED = WM_APP + 1;
constexpr UINT WM_EPUB_UPDATE_SCROLLBAR = WM_APP + 2;

constexpr UINT WM_EPUB_CACHE_UPDATED = WM_APP + 4;
constexpr UINT WM_EPUB_ANCHOR = WM_APP + 5;
constexpr UINT WM_EPUB_TOOLTIP = WM_APP + 6;
constexpr UINT WM_EPUB_NAVIGATE = WM_APP + 7;
constexpr UINT TB_SETBUTTONTEXT(WM_USER + 8);
constexpr UINT WM_LOAD_ERROR(WM_USER + 9);
constexpr UINT WM_USER_SCROLL(WM_USER + 10);
constexpr UINT SBM_SETSPINECOUNT(WM_USER + 11);
constexpr UINT SBM_SETPOSITION(WM_USER + 12);
constexpr UINT WM_EPUB_OPEN(WM_USER + 13);


constexpr UINT STATUSBAR_INFO = 0;
constexpr UINT STATUSBAR_CURRENT_SPINE = 1;
constexpr UINT STATUSBAR_TOTAL_SPINE = 2;
constexpr UINT STATUSBAR_CURRENT_OFFSET = 3;
constexpr UINT STATUSBAR_TOTAL_TIME = 4;
constexpr UINT STATUSBAR_LINK_INFO = 5;
constexpr UINT STATUSBAR_FONT_SIZE = 6;
constexpr UINT STATUSBAR_LINE_HEIGHT = 7;
constexpr UINT STATUSBAR_DOC_WIDTH = 8;
constexpr UINT STATUSBAR_DOC_ZOOM = 9;
// 可随时改


AppStates g_states;
AppSettings g_cfg;
std::wstring g_last_html_path;
enum class ImgFmt { PNG, JPEG, BMP, GIF, TIFF, SVG, UNKNOWN };
static std::vector<std::wstring> g_tempFontFiles;
void PreprocessHTML(std::string& html);
void UpdateCache(void);
static  std::string g_globalCSS = "";
static fs::file_time_type g_lastTime;
std::set<std::wstring> g_activeFonts;

std::unique_ptr<AppBootstrap> g_bootstrap;
std::unique_ptr<ReadingRecorder> g_recorder;

static MMRESULT g_tickTimer = 0;   // 0 表示当前没有定时器
static MMRESULT g_flushTimer = 0;
static MMRESULT g_tooltipTimer = 0;
static MMRESULT g_updateTimer = 0;

int g_center_offset = 0;


static HWND  g_hwndSplit = nullptr;   // 分隔条
static int   g_splitX = 200;       // 当前 TOC 宽度（初始值）
static bool  g_dragging = false;     // 是否正在拖动
static bool  g_imageview_dragging = false;
static POINT g_imageview_drag_pos{ 0,0 };
static bool g_mouse_tracked = false;


static int g_imageviewRenderW = 0;
// 全局
std::unique_ptr<TocPanel> g_toc;
std::unique_ptr<ScrollBarEx> g_scrollbar;
// 1. 在全局或合适位置声明
    // 整篇文档的所有行


static bool isWin10OrLater()
{
    static bool once = [] {
        OSVERSIONINFOEXW os{ sizeof(os), 10, 0, 0, 0, {0}, 0, 0, 0, 0, 0 };
        DWORDLONG mask = 0;
        VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
        return ::VerifyVersionInfoW(&os, VER_MAJORVERSION, mask) != FALSE;
        }();
    return once;
}
// 工具：把路径拷到堆，返回指针
inline wchar_t* DupPath(const wchar_t* src)
{
    size_t len = wcslen(src) + 1;
    wchar_t* buf = (wchar_t*)CoTaskMemAlloc(len * sizeof(wchar_t));
    wcscpy_s(buf, len, src);
    return buf;
}

// HTML 转义辅助函数
inline bool save_rgba_as_bmp(const std::wstring& path,
    const uint8_t* rgba,
    int width,
    int height)
{
    if (!rgba || width <= 0 || height <= 0) return false;

    const int rowBytes = width * 4;
    const int imageSize = rowBytes * height;
    const int fileSize = sizeof(BmpHeader) + sizeof(BmpInfo) + imageSize;

    BmpHeader hdr;
    hdr.bfSize = fileSize;

    BmpInfo info;
    info.biWidth = width;
    info.biHeight = -height;  // 负值 ⇒ 顶-下像素顺序（与 RGBA 顺序一致）

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    ofs.write(reinterpret_cast<const char*>(&info), sizeof(info));
    ofs.write(reinterpret_cast<const char*>(rgba), imageSize);
    return !!ofs;
}
void DumpHex(const wchar_t* tag, const std::wstring& s)
{
    std::wostringstream oss;
    oss << tag << L"(" << s.size() << L"): ";
    for (wchar_t ch : s)
        oss << std::hex << std::setw(4) << std::setfill(L'0') << static_cast<unsigned>(ch) << L" ";
    oss << L"\n";
    OutputDebugStringW(oss.str().c_str());
}

// HTML 转义辅助函数
std::string _escape_html(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
        case '&':  oss << "&amp;";  break;
        case '<':  oss << "&lt;";   break;
        case '>':  oss << "&gt;";   break;
        case '"':  oss << "&quot;"; break;
        case '\'': oss << "&apos;"; break;
        default:   oss << c;       break;
        }
    }
    return oss.str();
}

// 自闭合标签集合
static const std::set<std::string> void_tags = {
    "area", "base", "br", "col", "embed", "hr", "img",
    "input", "keygen", "link", "meta", "param", "source",
    "track", "wbr"
};

// 转换为小写函数
std::string to_lower(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return lower_str;
}

std::string generate_html(litehtml::element::ptr elem) {
    if (!elem) return "";

    std::ostringstream oss;

    // 处理文本节点
    if (elem->is_text()) {
        std::string text;
        elem->get_text(text);
        if (!text.empty()) { // 只有当有文本内容时才输出
            oss << _escape_html(text);
        }
    }
    // 处理普通元素
    else {
        const char* tag_name = elem->get_tagName();
        if (!tag_name) return "";

        // 输出开始标签
        oss << "<" << tag_name;

        //// 使用 dump_get_attrs 获取所有属性
        //auto attrs = elem->dump_get_attrs();
        //for (const auto& attr : attrs) {
        //    oss << " " << std::get<0>(attr) << "=\""
        //        << _escape_html(std::get<1>(attr)) << "\"";
        //}
        // 修改后的属性处理
        std::vector<const char*> standardAttrs = {
            "id", "class", "name", "value", "type", "src", "href"
        };

        for (const char* name : standardAttrs) {
            const char* value = elem->get_attr(name);
            if (value && value[0] != '\0') {
                oss << " " << name << "=\"" << _escape_html(value) << "\"";
            }
        }

        // 处理自闭合标签
        std::string tag_str = to_lower(tag_name);
        bool is_void = (void_tags.find(tag_str) != void_tags.end());

        if (is_void) {
            oss << "/>";
        }
        else {
            oss << ">";

            // 递归处理子元素
            const auto& children = elem->children();
            for (const auto& child : children) {
                oss << generate_html(child);
            }

            // 输出闭合标签
            oss << "</" << tag_name << ">";
        }
    }

    return oss.str();
}
static void gumbo_serialize(const GumboNode* node, std::string& out)
{
    if (node->type == GUMBO_NODE_TEXT)
    {
        out.append(node->v.text.text);
        return;
    }
    if (node->type != GUMBO_NODE_ELEMENT) return;

    const GumboElement& elem = node->v.element;
    out.push_back('<');
    out.append(gumbo_normalized_tagname(elem.tag));

    // 属性
    for (unsigned int i = 0; i < elem.attributes.length; ++i)
    {
        auto* attr = static_cast<GumboAttribute*>(elem.attributes.data[i]);
        out.append(" ").append(attr->name).append("=\"")
            .append(attr->value).append("\"");
    }

    if (elem.tag == GUMBO_TAG_IMG || elem.tag == GUMBO_TAG_BR)
    {
        // 自闭合
        out.append(" />");
    }
    else
    {
        out.push_back('>');
        for (unsigned int i = 0; i < elem.children.length; ++i)
            gumbo_serialize(static_cast<GumboNode*>(elem.children.data[i]), out);
        out.append("</").append(gumbo_normalized_tagname(elem.tag)).push_back('>');
    }
}
// 生成临时目录，返回路径（带反斜杠）
static std::wstring make_temp_dir()
{
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + g_cfg.temp_dir + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
// 完整的文档导出函数
std::string get_document_html(litehtml::document::ptr doc) {
    if (!doc) return "";

    // 添加文档类型声明
    std::ostringstream oss;
    oss << "<!DOCTYPE html>";

    // 从根元素开始生成
    if (doc->root()) {
        oss << generate_html(doc->root());
    }

    return oss.str();
}

void save_document_html(litehtml::document::ptr doc) {
    std::string modified_html = get_document_html(doc);

    // 输出到文件
    std::ofstream out("output.html");
    if (out.is_open()) {
        out << modified_html;
        out.close();
        std::cout << "HTML 导出成功" << std::endl;
    }
    else {
        std::cerr << "无法创建输出文件" << std::endl;
    }
}
static bool ends_with(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() &&
        str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool is_image_url(const char* url)
{
    if (!url) return false;
    std::string u = url;
    return ends_with(u, ".jpg") ||
        ends_with(u, ".png") ||
        ends_with(u, ".jpeg") ||
        ends_with(u, ".gif");
}
class InMemoryFontFileLoader
    : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDWriteInMemoryFontFileLoader>
{
public:
    // IDWriteFontFileLoader
    IFACEMETHODIMP CreateStreamFromKey(
        const void* /*key*/, UINT32 /*size*/, IDWriteFontFileStream** /*stream*/) override
    {
        return E_NOTIMPL;   // 用不到
    }

    // IDWriteInMemoryFontFileLoader
    IFACEMETHODIMP CreateInMemoryFontFileReference(
        IDWriteFactory* factory,
        const void* data,
        UINT32 size,
        IUnknown* owner,
        IDWriteFontFile** fontFile) override
    {
        return factory->CreateCustomFontFileReference(data, size, this, fontFile);
    }

    STDMETHODIMP_(UINT32) GetFileCount() override
    {
        // 简单计数即可；可按需要维护实际数量
        return 1;
    }
};
class MemoryFontLoader : public IDWriteFontCollectionLoader,
    public IDWriteFontFileEnumerator
{
public:
    // 静态工厂：一次性把若干内存字体打包成私有集合
    static HRESULT CreateCollection(
        IDWriteFactory* dwrite,
        const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
        IDWriteFontCollection** out);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override { return 1; }
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }

    // IDWriteFontCollectionLoader
    IFACEMETHODIMP CreateEnumeratorFromKey(
        IDWriteFactory* factory,
        const void* collectionKey, UINT32 collectionKeySize,
        IDWriteFontFileEnumerator** enumerator) override;

    // IDWriteFontFileEnumerator
    IFACEMETHODIMP MoveNext(BOOL* hasCurrentFile) override;
    IFACEMETHODIMP GetCurrentFontFile(IDWriteFontFile** fontFile) override;

private:
    // 私有构造，只能由 CreateCollection 调用
    MemoryFontLoader(IDWriteFactory* f,
        const std::vector<std::vector<uint8_t>>& blobs)
        : factory_(f), blobs_(blobs), idx_(0) {
    }

    // 内部辅助：把一段内存封装成 IDWriteFontFile
    static HRESULT CreateInMemoryFontFile(IDWriteFactory* factory,
        const void* data,
        UINT32 size,
        IDWriteFontFile** out);

    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
    std::vector<std::vector<uint8_t>> blobs_;
    size_t idx_;
    Microsoft::WRL::ComPtr<IDWriteFontFile> current_;
};
class TempFileEnumerator
    : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDWriteFontFileEnumerator>
{
public:
    TempFileEnumerator() = default;
    TempFileEnumerator(IDWriteFactory* factory,
        const std::vector<std::wstring>& paths)
        : m_factory(factory), m_paths(paths) {
    }
    HRESULT RuntimeClassInitialize(IDWriteFactory* f,
        const std::vector<std::wstring>& paths)
    {
        m_factory = f;
        m_paths = paths;
        return S_OK;
    }
    IFACEMETHODIMP MoveNext(BOOL* hasCurrentFile) override
    {
        *hasCurrentFile = (m_idx < m_paths.size());
        if (*hasCurrentFile) ++m_idx;
        return S_OK;
    }

    IFACEMETHODIMP GetCurrentFontFile(IDWriteFontFile** fontFile) override
    {
        if (m_idx == 0 || m_idx > m_paths.size()) return E_FAIL;
        return m_factory->CreateFontFileReference(m_paths[m_idx - 1].c_str(), nullptr, fontFile);
    }

private:
    Microsoft::WRL::ComPtr<IDWriteFactory> m_factory;
    std::vector<std::wstring> m_paths;
    size_t m_idx = 0;
};
class TempFileFontLoader
    : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IDWriteFontCollectionLoader>
{
public:
    TempFileFontLoader() = default;
    explicit TempFileFontLoader(IDWriteFactory* f) : m_factory(f) {}
    static HRESULT CreateCollection(
        IDWriteFactory* dwrite,
        const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
        IDWriteFontCollection** out,
        std::vector<std::wstring>& tempPaths)
    {
        if (!dwrite || !out) return E_INVALIDARG;

        // 注册（只一次）
        static bool reg = false;
        static Microsoft::WRL::ComPtr<TempFileFontLoader> g_loader;
        if (!reg)
        {
            Microsoft::WRL::MakeAndInitialize<TempFileFontLoader>(&g_loader, dwrite);
            dwrite->RegisterFontCollectionLoader(g_loader.Get());
            reg = true;
        }
        // 写临时文件
        std::vector<std::wstring> paths;
        for (const auto& [name, blob] : fonts)
        {
            wchar_t tmpPath[MAX_PATH]{};
            GetTempPathW(MAX_PATH, tmpPath);
            wchar_t tmpFile[MAX_PATH]{};
            PathCombineW(tmpFile, tmpPath, PathFindFileNameW(name.c_str()));
            HANDLE h = CreateFileW(tmpFile, GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            DWORD written = 0;
            WriteFile(h, blob.data(), (DWORD)blob.size(), &written, nullptr);
            CloseHandle(h);
            paths.push_back(tmpFile);
        }

        // 把路径数组整体作为 key
        // key = 连续 wchar_t 字符串数组，每个以 '\0' 结尾
        std::vector<wchar_t> key;
        for (const auto& p : paths)
        {
            key.insert(key.end(), p.begin(), p.end());
            key.push_back(L'\0');
        }
        key.push_back(L'\0');   // 双零结束

        HRESULT hr = dwrite->CreateCustomFontCollection(
            g_loader.Get(),
            key.data(),
            static_cast<UINT32>(key.size() * sizeof(wchar_t)),
            out);

        if (SUCCEEDED(hr))
            tempPaths.swap(paths);
        return hr;
    }

    // IDWriteFontCollectionLoader
    IFACEMETHODIMP CreateEnumeratorFromKey(
        IDWriteFactory* factory,
        void const* collectionKey,
        UINT32 collectionKeySize,
        IDWriteFontFileEnumerator** fontFileEnumerator) override
    {
        *fontFileEnumerator = nullptr;

        // 把 key 还原成路径列表
        std::vector<std::wstring> paths;
        const wchar_t* p = static_cast<const wchar_t*>(collectionKey);
        const wchar_t* end = p + collectionKeySize / sizeof(wchar_t);
        while (*p && p < end)
        {

            paths.emplace_back(p);
            p += wcslen(p) + 1;
        }

        return Microsoft::WRL::MakeAndInitialize<TempFileEnumerator>(
            fontFileEnumerator,
            factory,
            paths);
    }

    // 用 RuntimeClassInitialize 接收额外参数
    HRESULT RuntimeClassInitialize(IDWriteFactory* f)
    {
        m_factory = f;
        return S_OK;
    }
private:

    Microsoft::WRL::ComPtr<IDWriteFactory> m_factory;
};


HRESULT CreateCompatibleFontCollection(
    IDWriteFactory* dwrite,
    const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
    IDWriteFontCollection** out,
    std::vector<std::wstring>& tempFilesToDelete)
{
    if (isWin10OrLater())
    {
        // Win10+ 走内存
        return MemoryFontLoader::CreateCollection(dwrite, fonts, out);
    }
    else
    {
        // Win7/8 回落到临时文件
        return TempFileFontLoader::CreateCollection(dwrite, fonts, out, tempFilesToDelete);
    }
}




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


void SavePixelsToPNG(const BYTE* pixels, UINT w, UINT h, UINT stride, const wchar_t* file)
{
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));

    ComPtr<IWICBitmap> wicBmp;
    wic->CreateBitmapFromMemory(
        w, h,
        GUID_WICPixelFormat32bppPBGRA,
        stride,
        stride * h,
        const_cast<BYTE*>(pixels),
        &wicBmp);

    ComPtr<IWICBitmapEncoder> enc;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);

    ComPtr<IWICStream> stream;
    wic->CreateStream(&stream);
    stream->InitializeFromFilename(file, GENERIC_WRITE);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&frame, &props);
    frame->Initialize(props.Get());
    frame->SetSize(w, h);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&format);
    frame->WriteSource(wicBmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();
}

void PrintFontFamilies(ComPtr<IDWriteFontCollection> fontCollection)
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UINT32 familyCount = fontCollection->GetFontFamilyCount();
    char buf[512];

    for (UINT32 i = 0; i < familyCount; ++i)
    {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        fontCollection->GetFontFamily(i, &family);

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        family->GetFamilyNames(&names);

        UINT32 idx = 0;
        UINT32 len = 0;
        BOOL exists = FALSE;
        names->FindLocaleName(L"en-us", &idx, &exists);
        if (!exists) idx = 0;

        names->GetStringLength(idx, &len);
        std::wstring wname(len + 1, 0);
        names->GetString(idx, wname.data(), len + 1);

        // 转成 UTF-8
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> conv;
        std::string name = conv.to_bytes(wname.c_str());
        DumpHex(L"[actual]   ", wname);
        snprintf(buf, sizeof(buf), "[Font] %s\n", name.c_str());
        OutputDebugStringA(buf);
    }

    ::CoUninitialize();
}
void PrintSystemFontFamilies()
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 1. 拿 IDWriteFactory
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf()));

    // 2. 拿系统字体集合
    Microsoft::WRL::ComPtr<IDWriteFontCollection> sysFonts;
    factory->GetSystemFontCollection(&sysFonts);

    UINT32 familyCount = sysFonts->GetFontFamilyCount();
    char buf[512];

    for (UINT32 i = 0; i < familyCount; ++i)
    {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        sysFonts->GetFontFamily(i, &family);

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        family->GetFamilyNames(&names);

        UINT32 idx = 0;
        UINT32 len = 0;
        BOOL exists = FALSE;
        names->FindLocaleName(L"en-us", &idx, &exists);
        if (!exists) idx = 0;

        names->GetStringLength(idx, &len);
        std::wstring wname(len + 1, 0);
        names->GetString(idx, wname.data(), len + 1);

        // 转成 UTF-8
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> conv;
        std::string name = conv.to_bytes(wname.c_str());

        snprintf(buf, sizeof(buf), "[SystemFont] %s\n", name.c_str());
        OutputDebugStringA(buf);
    }

    ::CoUninitialize();
}
// 把 ID2D1Bitmap 保存为 PNG，返回 true 表示成功
bool DumpBitmap(ID2D1Bitmap* bmp, const wchar_t* file)
{
    if (!bmp || !file) return false;

    // 1. 尺寸
    D2D1_SIZE_U sz = bmp->GetPixelSize();
    if (sz.width == 0 || sz.height == 0) return false;

    // 2. 创建 WIC 工厂
    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return false;

    // 3. 创建 WIC 位图（32bpp PBGRA）
    ComPtr<IWICBitmap> wicBmp;
    hr = wic->CreateBitmap(
        sz.width, sz.height,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad,
        &wicBmp);
    if (FAILED(hr)) return false;

    // 4. 创建临时 D2D WIC RenderTarget，把 bmp 画进去
    ComPtr<ID2D1Factory> d2dFactory;
    bmp->GetFactory(&d2dFactory);

    ComPtr<ID2D1RenderTarget> rt;
    hr = d2dFactory->CreateWicBitmapRenderTarget(
        wicBmp.Get(),
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED)),
        &rt);
    if (FAILED(hr)) return false;



    // 5. 编码 PNG
    ComPtr<IWICStream> stream;
    hr = wic->CreateStream(&stream);
    if (FAILED(hr)) return false;

    hr = stream->InitializeFromFilename(file, GENERIC_WRITE);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) return false;

    hr = frame->Initialize(props.Get());
    if (FAILED(hr)) return false;

    hr = frame->SetSize(sz.width, sz.height);
    if (FAILED(hr)) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return false;

    hr = frame->WriteSource(wicBmp.Get(), nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    return SUCCEEDED(hr);
}

void DumpAllFontNames()
{
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf{ 0 };
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &lf,
        [](const LOGFONTW* lpelfe, const TEXTMETRICW*, DWORD, LPARAM) -> int {
            OutputDebugStringW((L"[Enum] " + std::wstring(lpelfe->lfFaceName) + L"\n").c_str());
            return 1;
        }, 0, 0);
    ReleaseDC(nullptr, hdc);
}

// 真正读文件




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

static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string base64_encode(const std::vector<uint8_t>& in) {
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(B64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(B64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static std::vector<unsigned char> base64_decode(const std::string& in)
{
    static const int tbl[256] = {
        /* 略：把 base64 字符映射到 0-63，非法字符为 -1 */
    };
    std::vector<unsigned char> out;
    /* 标准 Base64 解码实现，略 */
    return out;
}
static inline std::string trim_any(const std::string& s,
    const char* ws = " \t\"'")
{
    if (s.empty()) return s;
    size_t first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
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

static fs::path exe_dir()
{
    wchar_t buf[1024]{};
    GetModuleFileNameW(nullptr, buf, 1024);
    return fs::path(buf).parent_path();

}

// 工具：把文件内容读成字符串
static std::string read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

// 工具：把 css 内容塞进 <style> 并插入 <head>
static std::string embed_css(const std::string& html,
    const std::string& css)
{
    if (css.empty()) return html;
    std::string tag = "<style>\n" + css + "\n</style>\n";
    size_t pos = html.find("</head>");
    if (pos != std::string::npos)
        return html.substr(0, pos) + tag + html.substr(pos);
    return tag + html;   // 没有 </head> 时直接前置
}

// 1. 仅内嵌 global.css
std::string inject_global_css(const std::string& html)
{
    fs::path file = exe_dir() / "config" / "global.css";
    return embed_css(html, read_file(file));
}


static void insert_global_css(std::string& html) {
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
}
static void inject_css(std::string& html)
{
    std::ostringstream style;
    style << "<style>\n"
        << ":root{font-size:" << g_cfg.font_size << "px;}\n"
        << ":root,body,p,li,div,h1,h2,h3,h4,h5,h6,span, ul{line-height:" << g_cfg.line_height << ";}\n"
        << "</style>\n";

    const std::string& block = style.str();
    size_t pos = html.find("</head>");
    if (pos != std::string::npos)
        html.insert(pos, block);
    else
        html.insert(0, "<head>" + block + "</head>");

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

// 3. 写文字
// int -> wstring，保存不同片段
static std::unordered_map<int, std::wstring> g_statusBuf;

inline void SetStatus(int pane, const wchar_t* msg)
{
    if (!g_hStatus || !msg) return;

    // 1. 更新/插入片段
    if (msg && *msg)
        g_statusBuf[pane] = msg;
    else
        g_statusBuf.erase(pane);   // 空串时移除

    // 按 key 升序
    std::vector<std::pair<int, std::wstring>> vec(g_statusBuf.begin(),
        g_statusBuf.end());
    std::sort(vec.begin(), vec.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });


    // 2. 拼成一条字符串
    std::wstring text;
    for (const auto& kv : vec)
        text += kv.second + L"    ";

    // 3. 一次性写到状态栏第 0 栏
    SendMessageW(g_hStatus, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

MemFile EPUBBook::read_zip(const wchar_t* file_name) const {
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
    g_currentHtmlPath = path;
    auto it = cache.find(path);
    if (it != cache.end()) return std::string(it->second.begin(), it->second.size());
    MemFile mf = read_zip(path.c_str());
    if (mf.data.empty()) return {};
    return std::string(mf.begin(), mf.size());
}

bool EPUBBook::load(const wchar_t* epub_path) {
    namespace fs = std::filesystem;
    if (!fs::exists(epub_path))
        throw std::runtime_error("文件不存在");
    m_file_path = epub_path;
    mz_zip_reader_end(&zip);           // 1. 先关闭旧 zip
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, w2a(epub_path).c_str(), 0))
        throw std::runtime_error("zip 打开失败：" +
            std::to_string(mz_zip_get_last_error(&zip)));
    m_zipIndex = ZipIndexW(zip);
    parse_ocf_();
    parse_opf_();
    parse_toc_();
    load_all_fonts();
    //DumpAllFontNames();
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
            item.href = m_zipIndex.find(item.href);

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


static ImageFrame decode_img(const MemFile& mf, const wchar_t* ext)
{
    ImageFrame frame;
    auto fmt = detect_fmt(mf.data.data(), mf.data.size(), ext);

    switch (fmt)
    {
    case ImgFmt::SVG:
    {
        auto doc = lunasvg::Document::loadFromData(
            reinterpret_cast<const char*>(mf.data.data()), mf.data.size());
        if (!doc) return {};

        lunasvg::Bitmap svgBmp = doc->renderToBitmap();
        if (svgBmp.isNull()) return {};

        frame.width = svgBmp.width();
        frame.height = svgBmp.height();
        frame.stride = frame.width * 4;
        frame.rgba.assign(
            reinterpret_cast<const uint8_t*>(svgBmp.data()),
            reinterpret_cast<const uint8_t*>(svgBmp.data()) + frame.stride * frame.height);
        break;
    }

    default:   // PNG/JPEG/BMP/GIF/TIFF/…
    {
        IStream* pStream = SHCreateMemStream(mf.data.data(),
            static_cast<UINT>(mf.data.size()));
        if (!pStream) return {};
        Gdiplus::Bitmap bmp(pStream, FALSE);
        pStream->Release();
        if (bmp.GetLastStatus() != Gdiplus::Ok) return {};

        frame.width = bmp.GetWidth();
        frame.height = bmp.GetHeight();
        frame.stride = frame.width * 4;
        frame.rgba.resize(frame.stride * frame.height);

        Gdiplus::BitmapData data{};
        Gdiplus::Rect rc(0, 0, frame.width, frame.height);
        if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead,
            PixelFormat32bppPARGB, &data) == Gdiplus::Ok)
        {
            for (uint32_t y = 0; y < frame.height; ++y)
                memcpy(frame.rgba.data() + y * frame.stride,
                    reinterpret_cast<uint8_t*>(data.Scan0) + y * data.Stride,
                    frame.stride);
            bmp.UnlockBits(&data);
        }
        break;
    }
    }
    return frame;
}










void convert_coordinate(POINT& pt)
{
    if(g_cMain)
    {
        pt.x = pt.x/ g_cMain->m_zoom_factor - g_center_offset;
        pt.y = pt.y/g_cMain->m_zoom_factor + g_offsetY ;
    }

}



void CALLBACK Tick(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
    // 直接在工作线程/回调里刷新
    if (g_recorder && !g_vd->m_blocks.empty()) { g_recorder->updateRecord(); }
   // OutputDebugStringA("定时器触发\n");
    g_tickTimer = 0;
}
LRESULT CALLBACK ViewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        if ( g_cMain)
        {
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);   // ← 这才是客户区
            g_cMain->resize(rcClient.right, rcClient.bottom);
            UpdateCache();
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (!g_cMain || !g_cMain->m_doc) { break; }

        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_cMain->on_lbutton_down(pt.x/g_cMain->m_zoom_factor, pt.y/g_cMain->m_zoom_factor);
        convert_coordinate(pt);
        litehtml::position::vector redraw_boxes;
        g_cMain->m_doc->on_lbutton_down(pt.x, pt.y, 0, 0, redraw_boxes);
        if (!redraw_boxes.empty()) {
            for (auto r : redraw_boxes)
            {
                RECT rc{ r.left(), r.top(), r.right(), r.bottom() };
                UpdateCache();
                InvalidateRect(hwnd, &rc, true);

            }
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }

        if (g_cMain && g_cMain->m_doc)
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            g_cMain->on_lbutton_up();
            convert_coordinate(pt);

            litehtml::position::vector redraw_boxes;
            g_cMain->m_doc->on_lbutton_up(pt.x, pt.y, 0, 0, redraw_boxes);
            if (!redraw_boxes.empty()) {
                for (auto r : redraw_boxes)
                {
                    RECT rc{ r.left(), r.top(), r.right(), r.bottom() };
                    UpdateCache();
                    InvalidateRect(hwnd, &rc, true);

                }
            }
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
        if (g_cMain)
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            g_cMain->on_lbutton_dblclk(pt.x / g_cMain->m_zoom_factor, pt.y / g_cMain->m_zoom_factor);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }
        if (g_cMain && g_cMain->m_doc)
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            g_cMain->on_mouse_move(pt.x / g_cMain->m_zoom_factor, pt.y / g_cMain->m_zoom_factor);
            convert_coordinate(pt);



            litehtml::position::vector redraw_boxes;
            g_cMain->m_doc->on_mouse_over(pt.x, pt.y, 0, 0, redraw_boxes);
            if (!redraw_boxes.empty()) {
                for (auto r : redraw_boxes)
                {
                    RECT rc{ r.left(), r.top(), r.right(), r.bottom() };
                    UpdateCache();
                    InvalidateRect(hwnd, &rc, true);

                }
            }
        }
  
        return 0;
    }

    case WM_USER_SCROLL:
    {
        int offset = (int)wp;
        int spine = (int)lp;
        //scroll_to(offset);   // 你自己的函数
        return 0;
    }
    case WM_EPUB_ANCHOR:
    {
        // 更新阅读记录
        if (!g_tickTimer) 
        { 
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);  
        }
 
        if (!g_cMain->m_doc) { return 0; }
        wchar_t* sel = reinterpret_cast<wchar_t*>(wp);
        if (sel) {
            std::string cssSel = "[id=\"" + w2a(sel) + "\"]";
            if (auto el = g_cMain->m_doc->root()->select_one(cssSel.c_str())) {
                g_offsetY = el->get_placement().y;
            }
            free(sel);          // 对应 _wcsdup
        }
        UpdateCache();
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(g_hView);

        return 0;
    }
    case WM_EPUB_NAVIGATE:
    {
        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }

        wchar_t* url = reinterpret_cast<wchar_t*>(wp);
        g_vd->OnTreeSelChanged(url);  // 现在安全地在主线程执行
        free(url);


        return 0;
    }

    case WM_MOUSELEAVE:
    {

        litehtml::position::vector redraw_box;
        g_cMain->m_doc->on_mouse_leave(redraw_box);

        if (!redraw_box.empty())
        {
            for (auto box : redraw_box)
            {
                RECT r(box.left(), box.top(), box.right(), box.bottom());
                InvalidateRect(hwnd, &r, false);
            }
            UpdateCache();
        }

        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (g_cMain) { g_cMain->clear_selection(); }
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);   // ±120
            float factor = (delta > 0) ? 1.1f : 0.9f;     // 放大 / 缩小系数

            // 2. 更新全局缩放
            g_cMain->m_zoom_factor = std::clamp(g_cMain->m_zoom_factor * factor, 0.25f, 5.0f);
            UpdateCache();
            // 3. 重绘
            InvalidateRect(hwnd, NULL, FALSE);
        
            return 0;   // 已处理，不再传递
        }
 
        RECT rc;
        GetClientRect(hwnd, &rc);
        int zDelta = GET_WHEEL_DELTA_WPARAM(wp);
        float factor = std::abs(zDelta / 120);
        float step = std::max(10.0f, g_line_height*g_cfg.line_height*factor);
        if (zDelta >= 0) { g_scrollY -= step; }
        else { g_scrollY += step; }

        // 更新阅读记录
        if (!g_tickTimer)
        {
            g_tickTimer = timeSetEvent(g_cfg.record_update_interval_ms, 0, Tick, 0, TIME_ONESHOT);
        }
        litehtml::position::vector redraw_box;
        if (g_cMain && g_cMain->m_doc)
        {
            g_cMain->m_doc->on_mouse_leave(redraw_box);

            if (!redraw_box.empty())
            {
                for (auto box : redraw_box)
                {
                    RECT r(box.left(), box.top(), box.right(), box.bottom());
                    InvalidateRect(hwnd, &r, false);
                }

            }
        }
        UpdateCache();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT:

        if (g_cMain  && g_cMain->m_doc && g_states.isUpdate.exchange(false))
        {

            RECT rc;
            GetClientRect(g_hView, &rc);
            float x = g_center_offset;
            float y = -g_offsetY;
            float w = g_cfg.document_width;
            float h = rc.bottom - rc.top;
            //g_vd->draw(x, y, w, h, g_offsetY);
            litehtml::position clip(x, 0, w, h/g_cMain->m_zoom_factor);
            g_cMain->present(x, y, &clip);

        }
   
        return 0;

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}



void CALLBACK OnUpdateTimer(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
    //OutputDebugStringA("OnUpdateTimer\n");
    g_updateTimer = 0;
}


void UpdateCache()
{
    if (!g_cMain || !g_vd || !g_book) return;

    if (g_updateTimer)
    { 
        g_states.isUpdate.store(true);
        return;
    }
    g_updateTimer = timeSetEvent(g_cfg.update_interval_ms, 0, OnUpdateTimer, 0, TIME_ONESHOT);
    RECT rc;
    GetClientRect(g_hView, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    w /= g_cMain->m_zoom_factor;
    h /= g_cMain->m_zoom_factor;
    //request_doc_async(h, g_scrollY, g_offsetY);
    g_vd->get_doc(h, g_scrollY, g_offsetY);
 

    g_states.isUpdate.store(true);

    g_center_offset = (w - g_cfg.document_width ) * 0.5;
}

inline void DumpBookRecord()
{
    if (!g_recorder || g_recorder->m_book_record.id < 0) { return; }
    auto& r = g_recorder->m_book_record;
    std::wostringstream oss;            // ← 宽字符流
    oss << std::boolalpha;
    oss << L"===== BookRecord Dump =====\n";
    oss << L"id                 = " << r.id << L'\n';
    oss << L"path               = " << a2w(r.path) << L'\n';   // path 已经是 std::wstring 就 OK
    oss << L"title              = " << a2w(r.title) << L'\n';
    oss << L"author             = " << a2w(r.author) << L'\n';
    oss << L"openCount          = " << r.openCount << L'\n';
    oss << L"totalWords         = " << r.totalWords << L'\n';
    oss << L"lastSpineId        = " << r.lastSpineId << L'\n';
    oss << L"lastOffset         = " << r.lastOffset << L'\n';
    oss << L"fontSize           = " << r.fontSize << L'\n';
    oss << L"lineHeightMul      = " << std::fixed << std::setprecision(2) << r.lineHeightMul << L'\n';
    oss << L"docWidth           = " << r.docWidth << L'\n';
    oss << L"totalTime          = " << r.totalTime << L" (s)\n";
    oss << L"lastOpenTimestamp  = " << r.lastOpenTimestamp << L" (us)\n";
    oss << L"enableCSS          = " << r.enableCSS << L'\n';
    oss << L"enableJS           = " << r.enableJS << L'\n';
    oss << L"enableGlobalCSS    = " << r.enableGlobalCSS << L'\n';
    oss << L"enablePreHTML      = " << r.enablePreHTML << L'\n';
    oss << L"displayTOC         = " << r.displayTOC << L'\n';
    oss << L"displayStatus      = " << r.displayStatus << L'\n';
    oss << L"displayMenu        = " << r.displayMenu << L'\n';
    oss << L"displayScroll      = " << r.displayScroll << L'\n';
    oss << L"============================\n";

    OutputDebugStringW(oss.str().c_str());   // ← 宽字符版本
    OutputDebugStringW((L"\nTotal Read Time (s): " + std::to_wstring(g_recorder->getTotalTime()) + L"\n").c_str());
}
std::wstring OpenEpubWithDialog(HWND hwnd)
{
    wchar_t szFile[MAX_PATH] = { 0 };

    OPENFILENAME ofn = { 0 };               // 全部清零
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;                // 最好给 owner
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;

    // 过滤器：注意双 null 结尾
    const wchar_t* filter = L"EPUB 电子书\0*.epub\0所有文件\0*.*\0";
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;

    ofn.lpstrTitle = L"打开电子书";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn))
    {
        PostMessage(hwnd, WM_EPUB_OPEN, 0, (LPARAM)DupPath(szFile));
        //OutputDebugStringW(szFile);
    }
    return L"";
}
//void OpenEpubWithDialog(HWND hwnd)
//{
    //wchar_t dir[MAX_PATH]{};
    //BROWSEINFOW bi{ hwnd, nullptr, dir,
    //                L"请选择包含 EPUB 的文件夹",
    //                BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE };
    //DWORD pid;
    //GetWindowThreadProcessId(hwnd, &pid);
    //AllowSetForegroundWindow(pid);   // 允许本进程抢占前台
    //SetForegroundWindow(hwnd);       // 先把主窗口抢回前台
    //PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    //if (pidl)
    //{
    //    if (SHGetPathFromIDListW(pidl, dir))
    //    {
    //        // 枚举第一个 epub
    //        std::wstring firstEpub;
    //        for (const auto& e : std::filesystem::directory_iterator(dir))
    //            if (_wcsicmp(e.path().extension().c_str(), L".epub") == 0)
    //            {
    //                firstEpub = e.path();
    //                break;
    //            }
    //        if (!firstEpub.empty())
    //            PostMessageW(hwnd, WM_EPUB_OPEN, 0,
    //                (LPARAM)DupPath(firstEpub.c_str()));
    //        else
    //            OutputDebugStringW(L"目录里没有 epub 文件\n");
    //    }
    //    CoTaskMemFree(pidl);
    //}
//}
LRESULT CALLBACK ImageviewProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_DESTROY:
        return 0;
    case WM_PAINT:
    {
        if (!IsWindowVisible(g_hImageview)) { return 0; }
        if (!g_cImage)
        {
            OutputDebugStringA("[ImageviewProc] self or doc null\n");
            break;
        }

        if (g_states.isImageviewUpdate.exchange(false))
        {
            RECT rc;
            GetClientRect(g_hImageview, &rc);
            litehtml::position clip(0, 0, rc.right - rc.left, rc.bottom - rc.top);
            g_cImage->present(0, 0, &clip);


        }
        return 0;
    }

    case WM_MOUSEWHEEL: {



        int delta = GET_WHEEL_DELTA_WPARAM(w);
        float factor = (delta > 0) ? 1.1f : 0.9f; /* 1. 鼠标指针在屏幕上的位置 */
        POINT pt;
        GetCursorPos(&pt); /* 2. 当前窗口矩形（屏幕坐标） */
        RECT wr; GetWindowRect(hwnd, &wr); /* 3. 获取屏幕工作区大小（不含任务栏） */

        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfo(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
        int scrW = mi.rcWork.right - mi.rcWork.left;
        int scrH = mi.rcWork.bottom - mi.rcWork.top;
        /* 3.5 提前判断：若窗口外框已顶满屏幕，放大就忽略 */
        DWORD style = GetWindowLong(hwnd, GWL_STYLE);
        DWORD exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        UINT dpi = GetDpiForWindow(hwnd);
        RECT rNow{ 0, 0, g_imageviewRenderW, 1 }; // 高度先随便填，只要算宽度 
        AdjustWindowRectExForDpi(&rNow, style, FALSE, exStyle, dpi);
        int winW_now = rNow.right - rNow.left;
        g_cImage->m_doc->render(g_imageviewRenderW);
        int docH_now = g_cImage->m_doc->height(); RECT rH{ 0, 0, 1, docH_now };
        AdjustWindowRectExForDpi(&rH, style, FALSE, exStyle, dpi);
        int winH_now = rH.bottom - rH.top; // 放大且已顶满 → 直接 return 
        if (factor > 1.0f && (winW_now >= scrW || winH_now >= scrH)) { return 0; } /* 4. 计算新的渲染尺寸，并立即限制在屏幕内 */
        int renderW = std::max(32, static_cast<int>(g_imageviewRenderW * factor + 0.5f));
        int renderH = 0; // 先限制宽度 
        renderW = std::min(renderW, scrW); // 重新渲染得到高度 
        g_cImage->m_doc->render(renderW);
        renderH = g_cImage->m_doc->height(); // 再限制高度
        renderH = std::min(renderH, scrH);
        renderW = std::max(renderW, 32); // 防止极端情况宽度过小 
        /* 5. 计算窗口外框尺寸（含标题栏/边框） */
        RECT r{ 0, 0, renderW, renderH };
        AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
        int winW = r.right - r.left; int winH = r.bottom - r.top;
        /* 6. 以鼠标位置为缩放原点，计算新左上角 */
        int newX = pt.x - (pt.x - wr.left) * winW / (wr.right - wr.left);
        int newY = pt.y - (pt.y - wr.top) * winH / (wr.bottom - wr.top);
        /* 7. 最终再保证左上角也在屏幕内（简单 clamp） */
        newX = std::max((long)newX, mi.rcWork.left);
        newY = std::max((long)newY, mi.rcWork.top);
        newX = std::min((long)newX, mi.rcWork.right - winW);
        newY = std::min((long)newY, mi.rcWork.bottom - winH);
        /* 8. 更新窗口 & 画布 */
        g_cImage->resize(renderW, renderH);
        SetWindowPos(hwnd, HWND_TOPMOST, newX, newY, winW, winH, SWP_NOACTIVATE | SWP_NOZORDER);

        g_imageviewRenderW = renderW;
        g_states.isImageviewUpdate.store(true);
        InvalidateRect(hwnd, nullptr, false);

        return 0;
    }
                      /* 2. 左键拖动 */
    case WM_LBUTTONDOWN:
        g_imageview_dragging = true;
        SetCapture(hwnd);                     // 锁定鼠标
        g_imageview_drag_pos = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        return 0;

    case WM_LBUTTONUP:
        if (g_imageview_dragging)
        {
            g_imageview_dragging = false;
            ReleaseCapture();
        }

        return 0;

    case WM_MOUSEMOVE:
        if (g_imageview_dragging)
        {
            int dx = GET_X_LPARAM(l) - g_imageview_drag_pos.x;
            int dy = GET_Y_LPARAM(l) - g_imageview_drag_pos.y;

            RECT wr;
            GetWindowRect(hwnd, &wr);
            SetWindowPos(hwnd, nullptr,
                wr.left + dx, wr.top + dy,
                0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            g_states.isImageviewUpdate.store(true);
        }
        return 0;
    case WM_RBUTTONUP:
    {
        g_book->hide_imageview();
        return 0;
    }
    case WM_ERASEBKGND: {
        return 1;
    }
    }
    return DefWindowProc(hwnd, m, w, l);
}
LRESULT CALLBACK TooltipProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {

    case WM_PAINT:
    {
        if (!IsWindowVisible(g_hTooltip)) { return 0; }
        if (!g_cTooltip)
        {
            OutputDebugStringA("[TooltipProc] self or doc null\n");
            break;
        }

        if (g_states.isTooltipUpdate.exchange(false))
        {
            RECT rc;
            GetClientRect(g_hTooltip, &rc);
            litehtml::position clip(0, 0, rc.right - rc.left, rc.bottom - rc.top);
            g_cTooltip->present(0, 0, &clip);


        }
        return 0;
    }
    case WM_DESTROY:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, m, w, l);
}
LRESULT CALLBACK HomepageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        if (g_cHome && g_cHome->m_doc)
        {
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            float width = rcClient.right - rcClient.left;
            float height = rcClient.bottom - rcClient.top;
            g_cHome->m_doc->render(width);
            g_cHome->resize(width, height);
        }
    }
    case WM_PAINT:
    {
        if (!IsWindowVisible(g_hHomepage)) { return 0; }
        if (!g_cHome)
        {
            OutputDebugStringA("[TooltipProc] self or doc null\n");
            break;
        }
  
        if (!g_states.isLoaded && g_cHome->m_doc)
        {

            RECT rc;
            GetClientRect(hwnd, &rc);
            float width = rc.right - rc.left;
            float height = rc.bottom - rc.top;
            litehtml::position clip(0.0f, 0.0f, width, height);


            g_cHome->present(0, 0, &clip);

        }

        return 0;
    }
    case WM_DESTROY:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
const wchar_t TOOLTIP_CLASS[] = L"TooltipClass";

void register_tooltip_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = TooltipProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = TOOLTIP_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
}

const wchar_t VIEW_CLASS[] = L"ViewClass";
void register_view_class()
{
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;   // 关键
    wc.lpfnWndProc = ViewWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = VIEW_CLASS;
    RegisterClassW(&wc);
}


const wchar_t IMAGEVIEW_CLASS[] = L"Imageview";

void register_imageview_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = ImageviewProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = IMAGEVIEW_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
}

const wchar_t SCROLLBAR_CLASS[] = L"ScrollBarEx";

void register_scrollbar_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = ScrollBarEx::WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = SCROLLBAR_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
}


const wchar_t TOC_CLASS[] = L"TocPanelClass";
void register_toc_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = TocPanel::WndProc;          // 你的新 WndProc
    wc.hInstance = g_hInst;
    wc.lpszClassName = TOC_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;             // 自绘
    wc.cbWndExtra = sizeof(LONG_PTR);   // ← 必须有
    RegisterClassExW(&wc);
}

const wchar_t HOMEPAGE_CLASS[] = L"HomepageClass";
void register_homepage_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = HomepageProc;          // 你的新 WndProc
    wc.hInstance = g_hInst;
    wc.lpszClassName = HOMEPAGE_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;             // 自绘
    wc.cbWndExtra = sizeof(LONG_PTR);   // ← 必须有
    RegisterClassExW(&wc);
}


// ---------- 窗口 ----------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {

        register_view_class();

        register_tooltip_class();
        register_imageview_class();
        register_scrollbar_class();
        register_toc_class();
        register_homepage_class();
        // 放在主窗口 CreateWindow 之后
        g_hStatus = CreateWindowEx(
            0, STATUSCLASSNAME, L"就绪",
            WS_CHILD | SBARS_SIZEGRIP,
            0, 0, 0, 0,           // 位置和大小由 WM_SIZE 调整
            hwnd, nullptr, g_hInst, nullptr);

        // 2. 创建
        g_hwndToc = CreateWindowExW(
            WS_EX_COMPOSITED,          // 双缓冲
            TOC_CLASS,                 // 用注册的类名
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL,
            0, 0, 200, 600,
            hwnd, (HMENU)100, g_hInst, nullptr);

        g_hwndSplit = CreateWindowExW(
            0, WC_STATICW, nullptr,
            WS_CHILD | SS_ETCHEDVERT,
            200, 0, 2, 600,          // 2 px 宽
            hwnd, (HMENU)101, g_hInst, nullptr);

        g_hView = CreateWindowExW(
            0, VIEW_CLASS, nullptr,
            WS_CHILD | WS_CLIPSIBLINGS,
            0, 0, 1, 1,
            hwnd, (HMENU)101, g_hInst, nullptr);

        g_hTooltip = CreateWindowExW(
            WS_EX_COMPOSITED,
            TOOLTIP_CLASS, nullptr,
            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_BORDER,
            0, 0, 300, 200,
            hwnd, nullptr, g_hInst, nullptr);


        g_hImageview = CreateWindowExW(
            WS_EX_COMPOSITED,
            IMAGEVIEW_CLASS, nullptr,
            WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_BORDER,
            0, 0, 300, 200,
            hwnd, nullptr, g_hInst, nullptr);


        g_hViewScroll = CreateWindowExW(0, SCROLLBAR_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, nullptr,
            g_hInst, nullptr);

        g_hHomepage = CreateWindowExW(0, HOMEPAGE_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, nullptr,
            g_hInst, nullptr);

        g_bootstrap = std::make_unique<AppBootstrap>();
  
        // =====初始化隐藏=====


        DragAcceptFiles(hwnd, TRUE);

        SendMessage(g_hWnd, WM_SIZE, 0, 0);

        return 0;
    }
    case WM_DROPFILES:
    {
        wchar_t file[MAX_PATH]{};
        DragQueryFileW((HDROP)wp, 0, file, MAX_PATH);
        DragFinish((HDROP)wp);
        PostMessage(hwnd, WM_EPUB_OPEN, 0, (LPARAM)DupPath(file));
        return 0;
    }
 
    case WM_COPYDATA:
    {
        PCOPYDATASTRUCT p = (PCOPYDATASTRUCT)lp;
        if (p->dwData == WM_EPUB_OPEN && p->lpData)
        {
            PostMessage(hwnd, WM_EPUB_OPEN, 0,
                (LPARAM)DupPath((const wchar_t*)p->lpData));
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
        OpenEpubWithDialog(hwnd);
        return 0;
    }
    // ---------- WM_EPUB_OPEN ----------
    case WM_EPUB_OPEN:
    {
        const wchar_t* file = (const wchar_t*)lp;
        const wchar_t* ext = wcsrchr(file, L'.');
        if (!ext || _wcsicmp(ext, L".epub") != 0)
        {
            SetStatus(STATUSBAR_INFO, L"不是有效的 epub 文件");
            CoTaskMemFree((void*)file);   // 释放堆拷贝
            return 0;
        }

        // 如果上一次任务还在跑，直接忽略（或加入队列）
        if (g_parse_task.valid() &&
            g_parse_task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            SetStatus(STATUSBAR_INFO, L"正在加载其他文件，请稍候…");
            CoTaskMemFree((void*)file);
            return 0;
        }

        // 清理旧数据

        g_cMain->clear();
        g_cImage->clear();
        g_cTooltip->clear();
        g_book->clear();
        g_toc->clear();
        g_vd->clear();
        InvalidateRect(g_hView, nullptr, TRUE);
        InvalidateRect(g_hwndToc, nullptr, TRUE);

        // 启动新任务
        SetStatus(STATUSBAR_INFO, L"正在加载...");
        g_parse_task = std::async(std::launch::async, [file] {
            try
            {
                g_book->load(file);
                PostMessage(g_hWnd, WM_EPUB_PARSED, 0, 0);
            }
            catch (const std::exception& e)
            {
                auto* buf = DupPath(a2w(e.what()).c_str());
                PostMessage(g_hWnd, WM_LOAD_ERROR, 0, (LPARAM)buf);
            }
            catch (...)
            {
                PostMessage(g_hWnd, WM_LOAD_ERROR, 0,
                    (LPARAM)DupPath(L"未知错误"));
            }
            CoTaskMemFree((void*)file);   // 任务结束释放路径
            });
        return 0;
    }
    case WM_PAINT:
    {
        break;
    }
    case WM_EPUB_PARSED: {

        if (!g_states.isLoaded) {
            g_states.isLoaded = true;
        }
        g_vd->clear();
        g_recorder->flush();
        g_recorder->openBook(w2a(g_book->m_file_path));
   
        DumpBookRecord();

        // 更新设置
        auto& record = g_recorder->m_book_record;
        auto spine_id = record.lastSpineId;
        g_offsetY = record.lastOffset ;
        g_cfg.font_size = record.fontSize > 0 ? record.fontSize:g_cfg.default_font_size;
        g_cfg.line_height = record.lineHeightMul > 0 ? record.lineHeightMul : g_cfg.default_line_height;
        g_cfg.document_width = record.docWidth > 0 ? record.docWidth : g_cfg.default_document_width;
        int spine_size = g_book->ocf_pkg_.spine.size();
        SendMessage(g_hViewScroll, SBM_SETSPINECOUNT, spine_size, 0);
        SetStatus(STATUSBAR_TOTAL_SPINE, (L"总数：" + std::to_wstring(spine_size)).c_str());
   
 
        g_last_html_path = g_book->ocf_pkg_.spine[spine_id].href;
      
        g_vd->load_book();
        g_vd->load_html(g_last_html_path);

        UpdateCache();          // 复用前面给出的 UpdateCache()

        // 4) 触发一次轻量 WM_PAINT（只 BitBlt）
        PostMessage(hwnd, WM_SIZE, 0, 0);
        //PostMessage(g_hView, WM_SIZE, 0, 0);
        //InvalidateRect(g_hView, nullptr, true);
 
        //UpdateWindow(g_hView);

        SetStatus(STATUSBAR_INFO, L"加载完成");
        return 0;
    }


    case WM_SIZE:
    {
      
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        const int cx = rcClient.right;
        int cyClient = rcClient.bottom;
        if (g_states.isLoaded)
        {
            MoveWindow(g_hHomepage, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hTooltip, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hImageview, 0, 0, 0, 0, FALSE);
            ShowWindow(g_hHomepage, SW_HIDE);
            ShowWindow(g_hImageview, SW_HIDE);
            ShowWindow(g_hTooltip, SW_HIDE);
 
            /* 1. 工具栏高度 */
            int cyTB = 0;

            /* 2. 状态栏高度 */
            int cySB = 0;
            if (g_cfg.displayStatusBar && g_hStatus)
            {
                ShowWindow(g_hStatus, SW_SHOW);
                SendMessage(g_hStatus, WM_SIZE, 0, 0);
                RECT rcSB{};
                GetWindowRect(g_hStatus, &rcSB);
                cySB = rcSB.bottom - rcSB.top;
            }
            else if (g_hStatus)
            {
                ShowWindow(g_hStatus, SW_HIDE);
            }

            /* 3. 剩余可用高度 */
            const int cy = cyClient - cyTB - cySB;

            /* 4. 目录宽度 & 竖线 */
            const int tocW = g_cfg.displayTOC ? g_splitX : 0;
            ShowWindow(g_hwndToc, g_cfg.displayTOC ? SW_SHOW : SW_HIDE);
            ShowWindow(g_hwndSplit, g_cfg.displayTOC ? SW_SHOW : SW_HIDE);

            /* 5. 滚动条宽度 */
            const int sbW = g_cfg.displayScrollBar ? 20 : 0;
            ShowWindow(g_hViewScroll, g_cfg.displayScrollBar ? SW_SHOW : SW_HIDE);
            ShowWindow(g_hView,  SW_SHOW );
            /* 6. 摆放子窗口（Y 起点统一为 cyTB） */
            MoveWindow(g_hwndToc, 0, cyTB, tocW, cy, TRUE);
            MoveWindow(g_hwndSplit, tocW, cyTB, 2, cy, TRUE);
            MoveWindow(g_hView, tocW + 2, cyTB,
                cx - tocW - 2 - sbW, cy, TRUE);      // 正文让出滚动条
            MoveWindow(g_hViewScroll,
                cx - sbW, cyTB, sbW, cy, TRUE);      // 滚动条贴最右
            //UpdateCache();
        }
        else
        {
            MoveWindow(g_hStatus, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hwndSplit, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hwndToc, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hViewScroll, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hView, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hTooltip, 0, 0, 0, 0, FALSE);
            MoveWindow(g_hImageview, 0, 0, 0, 0, FALSE);
            ShowWindow(g_hStatus, SW_HIDE);
            ShowWindow(g_hwndSplit, SW_HIDE);
            ShowWindow(g_hwndToc, SW_HIDE);
            ShowWindow(g_hViewScroll, SW_HIDE);
            ShowWindow(g_hView, SW_HIDE);
            ShowWindow(g_hImageview, SW_HIDE);
            ShowWindow(g_hTooltip, SW_HIDE);



            ShowWindow(g_hHomepage, SW_SHOW);
            MoveWindow(g_hHomepage, 0, 0, cx, cyClient, TRUE);

        }
        //InvalidateRect(hwnd, nullptr, TRUE);
        //UpdateWindow(hwnd);
        return 0;
    }
    case WM_LOAD_ERROR:
    {
        wchar_t* msg = (wchar_t*)lp;
        SetStatus(STATUSBAR_INFO, msg);
        OutputDebugStringW(msg);
        OutputDebugStringW(L"\n");
        CoTaskMemFree(msg);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (LOWORD(lp) >= g_splitX && LOWORD(lp) <= g_splitX + 2)
        {
            SetCapture(hwnd); g_dragging = true;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (g_dragging)
        {
            int x = LOWORD(lp);
            if (x < 50) x = 50; if (x > 400) x = 400;
            g_splitX = x;
            PostMessage(hwnd, WM_SIZE, 0, 0);
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        if (g_dragging) { ReleaseCapture(); g_dragging = false; }
        break;
    }
    case WM_SETCURSOR:
    {
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            if (pt.x >= g_splitX && pt.x <= g_splitX + 2)
            {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE;
            }
        }
        if (HWND(wp) == g_hView)   // 确保是客户区
        {
            if (g_cMain)
            {
                SetCursor(LoadCursor(nullptr, g_cMain->m_currentCursor));
                return TRUE;                  // 已设置光标
            }
        }
        break;
    }
    case WM_EPUB_CACHE_UPDATED:
    {
        if (g_vd->m_isReloading.exchange(false)) { g_offsetY = g_vd->m_percent * g_vd->m_height; }

        float delta = static_cast<float>(lp);
        g_offsetY += delta;
        g_cMain->m_doc = std::move(g_vd->m_doc);
        if (g_vd->m_isAnchor.exchange(false))
        {
            std::string cssSel = "[id=\"" + g_vd->m_anchor_id + "\"]";
            if (auto el = g_cMain->m_doc->root()->select_one(cssSel.c_str())) {
                g_offsetY = el->get_placement().y;
            }
        }
        UpdateCache();
        InvalidateRect(g_hView, nullptr, false);
        return 0;
    }
    case WM_DESTROY: {
        g_recorder->flush();
        if (g_tooltipTimer)
        {
            timeKillEvent(g_tooltipTimer);
            g_tooltipTimer = 0;
        }
        PostQuitMessage(0);
        return 0;
    }


    case WM_COMMAND: {
        switch (LOWORD(wp))
        {
        case IDM_TOGGLE_CSS:
            g_cfg.enableCSS = !g_cfg.enableCSS;
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
                MF_BYCOMMAND | (g_cfg.enableCSS ? MF_CHECKED : MF_UNCHECKED));
            if (g_vd) { g_vd->reload(); }
            break;

        case IDM_TOGGLE_JS:
            g_cfg.enableJS = !g_cfg.enableJS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
                MF_BYCOMMAND | (g_cfg.enableJS ? MF_CHECKED : MF_UNCHECKED));
            if (g_vd) { g_vd->reload(); }
            break;

        case IDM_TOGGLE_GLOBAL_CSS:
            g_cfg.enableGlobalCSS = !g_cfg.enableGlobalCSS;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_GLOBAL_CSS,
                MF_BYCOMMAND | (g_cfg.enableGlobalCSS ? MF_CHECKED : MF_UNCHECKED));
            if (g_vd) { g_vd->reload(); }
            break;



        case IDM_TOGGLE_HOVER_PREVIEW:
            g_cfg.enableHoverPreview = !g_cfg.enableHoverPreview;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_HOVER_PREVIEW,
                MF_BYCOMMAND | (g_cfg.enableHoverPreview ? MF_CHECKED : MF_UNCHECKED));
            break;

        case IDM_TOGGLE_TOC_WINDOW:

            g_cfg.displayTOC = !g_cfg.displayTOC;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_TOC_WINDOW,
                MF_BYCOMMAND | (g_cfg.displayTOC ? MF_CHECKED : MF_UNCHECKED));
            if (g_cfg.displayTOC) { ShowWindow(g_hwndToc, SW_SHOW); }
            else { ShowWindow(g_hwndToc, SW_HIDE); }
            // 让主窗口重新布局
            PostMessage(g_hWnd, WM_SIZE, 0, 0);
            break;

        case IDM_TOGGLE_STATUS_WINDOW:

            g_cfg.displayStatusBar = !g_cfg.displayStatusBar;          // 切换状态
            CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_STATUS_WINDOW,
                MF_BYCOMMAND | (g_cfg.displayStatusBar ? MF_CHECKED : MF_UNCHECKED));
            if (g_cfg.displayStatusBar) { ShowWindow(g_hStatus, SW_SHOW); }
            else { ShowWindow(g_hStatus, SW_HIDE); }
            // 让主窗口重新布局
            PostMessage(g_hWnd, WM_SIZE, 0, 0);
            break;

        case ID_EPUB_RELOAD:

            if (g_vd && g_states.isLoaded) { g_vd->reload(); }
            break;

        case ID_FONT_BIGGER:        // Ctrl + '+'
            g_cfg.font_size = std::min(g_cfg.font_size + 1.0f, 72.0f);   // 上限 72
            if (g_vd) { g_vd->reload(); }   // 重新加载并排版
            break;

        case ID_FONT_SMALLER:       // Ctrl + '-'
            g_cfg.font_size = std::max(g_cfg.font_size - 1.0f, 8.0f);    // 下限 8
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_FONT_RESET:         // Ctrl + '0'
            g_cfg.font_size = g_cfg.default_font_size;   // 默认字号
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_LINE_HEIGHT_UP:     // Ctrl + Shift + '+'
            g_cfg.line_height = std::min(g_cfg.line_height + 0.1f, 3.0f);
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_LINE_HEIGHT_DOWN:   // Ctrl + Shift + '-'
            g_cfg.line_height = std::max(g_cfg.line_height - 0.1f, 1.0f);
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_LINE_HEIGHT_RESET:  // Ctrl + Shift + '0'
            g_cfg.line_height = g_cfg.default_line_height;  // 默认行高
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_WIDTH_BIGGER:       // Alt + '→'
            g_cfg.document_width = std::min(g_cfg.document_width + 50.0f, 2000.0f);
            if (g_vd) { g_vd->reload(); }   // 仅重新排版即可
            break;

        case ID_WIDTH_SMALLER:      // Alt + '←'
            g_cfg.document_width = std::max(g_cfg.document_width - 50.0f, 300.0f);
            if (g_vd) { g_vd->reload(); }
            break;

        case ID_WIDTH_RESET:        // Alt + '0'
            g_cfg.document_width = g_cfg.default_document_width;   // 默认宽度
            if (g_vd) { g_vd->reload(); }
            break;
        case ID_EDIT_COPY:
            g_cMain->copy_to_clipboard();
            break;
        case ID_FILE_OPEN:
        {
            OpenEpubWithDialog(hwnd);   // 就是之前那段 IFileOpenDialog 代码
            break;
        }
        case ID_FILE_EXIT:
            PostQuitMessage(0);
            break;
        

        }
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);

}

bool SaveHDCAsBmp(HDC hdc, int width, int height, const wchar_t* name)
{
    // 1. 创建兼容的内存 DC 和位图
    HDC     memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height;   // 正数 = 底-上 DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;       // 24-bit RGB
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp) { DeleteDC(memDC); return false; }

    // 2. 把 hdc 内容拷到 DIB
    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);
    BitBlt(memDC, 0, 0, width, height, hdc, 0, 0, SRCCOPY);

    // 3. 构造 BITMAPFILEHEADER + DIB 数据
    BITMAPFILEHEADER bfh = { 0 };
    bfh.bfType = 0x4D42; // 'BM'
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    DWORD dibSize = ((width * 3 + 3) & ~3) * height; // 每行 4 字节对齐
    bfh.bfSize = bfh.bfOffBits + dibSize;

    // 4. 写文件
    HANDLE hFile = CreateFileW(name,
        GENERIC_WRITE,
        0, nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        return false;
    }

    DWORD written = 0;
    WriteFile(hFile, &bfh, sizeof(bfh), &written, nullptr);
    WriteFile(hFile, &bi.bmiHeader, sizeof(bi.bmiHeader), &written, nullptr);
    WriteFile(hFile, bits, dibSize, &written, nullptr);
    CloseHandle(hFile);

    // 5. 清理
    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    return true;
}
const wchar_t MAIN_CLASS[] = L"SimpleEPUBReader";
void register_main_class()
{
    WNDCLASSEX w{ sizeof(WNDCLASSEX) };
    w.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;   // 关键
    w.lpfnWndProc = WndProc;
    w.hInstance = g_hInst;
    w.hCursor = LoadCursor(nullptr, IDC_ARROW);
    w.lpszClassName = MAIN_CLASS;
    w.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    w.lpszMenuName = nullptr;   // ← 必须为空
    RegisterClassEx(&w);
}

// ---------- 入口 ----------
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int n)
{

    // 在 WinMain 最开头
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // ---------- 1. 解析命令行 ----------
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    wchar_t* firstFile = nullptr;
    if (argc > 1)
        firstFile = DupPath(argv[1]);   // 堆拷贝
    LocalFree(argv);

    // ---------- 2. 单例检测 ----------
    CreateMutex(nullptr, TRUE, L"EPUBLite_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (firstFile)
        {
            HWND hPrev = FindWindow(MAIN_CLASS, nullptr);
            if (hPrev)
            {
                COPYDATASTRUCT cds{};
                cds.dwData = WM_EPUB_OPEN;
                cds.cbData = (DWORD)(wcslen(firstFile) + 1) * sizeof(wchar_t);
                cds.lpData = firstFile;          // 指向堆
                SendMessage(hPrev, WM_COPYDATA, 0, (LPARAM)&cds);
                // SendMessage 同步返回后即可释放
            }
        }
        CoTaskMemFree(firstFile);   // 无论发没发成功都要释放
        return 0;
    }
 
    ULONG_PTR gdiplusToken{};
    GdiplusStartupInput gdiplusStartupInput{};
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
    g_hInst = h;
    InitCommonControls();
    register_main_class();

    // 在 CreateWindow 之前
    HMENU hMenu = LoadMenu(g_hInst, MAKEINTRESOURCE(IDR_MENU_MAIN));

    if (!hMenu) {
        OutputDebugStringW(L"LoadMenu 失败\n");
        MessageBox(nullptr, L"LoadMenu 失败", L"Error", MB_ICONERROR);
        return 0;
    }

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();


    g_hWnd = CreateWindowW(MAIN_CLASS, a2w(g_cfg.appName).c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 800, 600,
        nullptr, nullptr, h, nullptr);
 



    // 1. 在全局或合适位置声明
    AccelManager gAccel(g_hWnd);

    // 2. 在 WinMain 里，窗口创建完成后立刻注册
    // 书签栏
    gAccel.set(IDM_TOGGLE_TOC_WINDOW, FVIRTKEY, VK_TAB);   // 无修饰符的 Tab
    
    // 刷新
    gAccel.set(ID_EPUB_RELOAD, FVIRTKEY, VK_F5);    // 新增 F5

    // 字体
    gAccel.add(ID_FONT_BIGGER, FCONTROL | FVIRTKEY, VK_OEM_PLUS);
    gAccel.add(ID_FONT_BIGGER, FCONTROL | FVIRTKEY, VK_ADD);
    gAccel.add(ID_FONT_SMALLER, FCONTROL | FVIRTKEY, VK_OEM_MINUS);
    gAccel.add(ID_FONT_SMALLER, FCONTROL | FVIRTKEY, VK_SUBTRACT);
    gAccel.add(ID_FONT_RESET, FCONTROL | FVIRTKEY, VK_BACK);


    // 行高
    gAccel.add(ID_LINE_HEIGHT_UP, FCONTROL | FSHIFT | FVIRTKEY, VK_OEM_PLUS);
    gAccel.add(ID_LINE_HEIGHT_UP, FCONTROL | FSHIFT | FVIRTKEY, VK_ADD);
    gAccel.add(ID_LINE_HEIGHT_DOWN, FCONTROL | FSHIFT | FVIRTKEY, VK_OEM_MINUS);
    gAccel.add(ID_LINE_HEIGHT_DOWN, FCONTROL | FSHIFT | FVIRTKEY, VK_SUBTRACT);
    gAccel.add(ID_LINE_HEIGHT_RESET, FCONTROL | FSHIFT | FVIRTKEY, VK_BACK);


    // 文档宽度
    gAccel.add(ID_WIDTH_BIGGER, FALT | FVIRTKEY, VK_OEM_PLUS);
    gAccel.add(ID_WIDTH_BIGGER, FALT | FVIRTKEY, VK_ADD);
    gAccel.add(ID_WIDTH_SMALLER, FALT | FVIRTKEY, VK_SUBTRACT);
    gAccel.add(ID_WIDTH_SMALLER, FALT | FVIRTKEY, VK_OEM_MINUS);
    gAccel.add(ID_WIDTH_RESET, FALT | FVIRTKEY, VK_BACK);
  
    // 新增：复制文本（Ctrl + C）
    gAccel.add(ID_EDIT_COPY, FCONTROL | FVIRTKEY, 'C');


    // 加载图标（可缩放，支持 32/48/256 像素）
    fs::path icoPath = exe_dir() / "res" / "app.ico";
    HICON hIcon = (HICON)LoadImageW(nullptr, icoPath.c_str(),
        IMAGE_ICON,
        0, 0,               // 0,0 = 使用图标内最佳尺寸
        LR_LOADFROMFILE | LR_DEFAULTSIZE);

    if (hIcon)
    {
        // 设置窗口图标
        SendMessageW(g_hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(g_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }




    SetMenu(g_hWnd, hMenu);            // ← 放在 CreateWindow 之后


    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_CSS,
        MF_BYCOMMAND | (g_cfg.enableCSS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_JS,
        MF_BYCOMMAND | (g_cfg.enableJS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_GLOBAL_CSS,
        MF_BYCOMMAND | (g_cfg.enableGlobalCSS ? MF_CHECKED : MF_UNCHECKED));

    CheckMenuItem(GetMenu(g_hWnd), IDM_TOGGLE_HOVER_PREVIEW,
        MF_BYCOMMAND | (g_cfg.enableHoverPreview ? MF_CHECKED : MF_UNCHECKED));

 
    
       
    EnableMenuItem(hMenu, IDM_TOGGLE_JS, MF_BYCOMMAND | MF_GRAYED);

    EnableMenuItem(hMenu, IDM_TOGGLE_MENUBAR_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TOGGLE_SCROLLBAR_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    //EnableMenuItem(hMenu, IDM_TOGGLE_STATUS_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    //EnableMenuItem(hMenu, IDM_TOGGLE_TOC_WINDOW, MF_BYCOMMAND | MF_GRAYED);
    EnableClearType();
  

    // ====================
    ShowWindow(g_hWnd, n);
    UpdateWindow(g_hWnd);

    // ---------- 4. 首次启动时如有文件立即加载 ----------
    if (firstFile && fs::exists(firstFile))
        PostMessage(g_hWnd, WM_EPUB_OPEN, 0, (LPARAM)firstFile);
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!gAccel.translate(&msg)) {   // ← 先给 AccelManager
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    ::CoUninitialize();
    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}

// ---------- 目录解析 ----------



// ---------- 点击目录跳转 ----------
void VirtualDoc::OnTreeSelChanged(std::wstring href)
{
    if (href.empty()) return;


    /* 1. 分离文件路径与锚点 */

    size_t pos = href.find(L'#');
    std::wstring file_path = (pos == std::wstring::npos) ? href : href.substr(0, pos);
    int spine_id = get_id_by_href(file_path);
    m_anchor_id = (pos == std::wstring::npos) ? "" :
        w2a(href.substr(pos + 1));

    if (!exists(spine_id))
    {
        clear();
        load_html(file_path);

        m_isAnchor.store(m_anchor_id.empty()? false: true);

    }
    else
    {
        if (!m_anchor_id.empty())
        {
            std::wstring cssSel = a2w(m_anchor_id);   // 转成宽字符
            // WM_APP + 3 约定为“跳转到锚点选择器”
            PostMessageW(g_hView, WM_EPUB_ANCHOR,
                reinterpret_cast<WPARAM>(_wcsdup(cssSel.c_str())), 0);
        }
    }



/* 3. 跳转到锚点 */
  
    //InvalidateRect(g_hView, nullptr, true);
    //UpdateWindow(g_hWnd);
}

// SimpleContainer.cpp

void SimpleContainer::load_image(const char* src, const char* /*baseurl*/, bool)
{
    if (m_img_cache.contains(src)) return;


    /* ---------- 1. 绝对路径优先 ---------- */
    if (fs::path(src).is_absolute())
    {
        std::error_code ec;
        size_t sz = fs::file_size(src, ec);
        if (ec) return;                       // 文件不存在
        std::ifstream ifs(src, std::ios::binary);
        if (!ifs) return;

        std::vector<uint8_t> buf(sz);
        ifs.read(reinterpret_cast<char*>(buf.data()), sz);

        MemFile mf{ std::move(buf) };
        fs::path p(src);
        std::wstring ext = p.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

        ImageFrame frame = decode_img(mf, ext.empty() ? nullptr : ext.c_str());
        if (!frame.rgba.empty())
            m_img_cache.emplace(src, std::move(frame));
        return;
    }

    /* 2. 普通文件路径分支 -------------------------------------------------- */
    std::wstring wpath = g_book->m_zipIndex.find(a2w(src));
    MemFile mf = g_book->read_zip(wpath.c_str());
    if (mf.data.empty())
    {
        OutputDebugStringW((L"EPUB not found: " + wpath + L"\n").c_str());
        return;
    }

    auto dot = wpath.find_last_of(L'.');
    std::wstring ext;
    if (dot != std::wstring::npos && dot + 1 < wpath.size())
    {
        ext = wpath.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    }

    ImageFrame frame = decode_img(mf, ext.empty() ? nullptr : ext.c_str());
    if (!frame.rgba.empty())
    {
        m_img_cache.emplace(src, std::move(frame));
    }
    else
    {
        OutputDebugStringA(("EPUB decode failed: " + std::string(src) + "\n").c_str());
    }
}





void SimpleContainer::get_image_size(const char* src, const char* baseurl, litehtml::size& sz) {
    if (!m_img_cache.contains(src)) { sz.width = sz.height = 0; return; }
    auto img = m_img_cache[src];
    sz.width = img.width;
    sz.height = img.height;

}

// get_client_rect -> get_viewport
void SimpleContainer::get_viewport(litehtml::position& client) const
{
    // 1. 取客户区物理像素
    RECT rc{};
    GetClientRect(g_hView, &rc);

    // 2. 计算滚动条宽度（垂直滚动条存在时）
    int scrollW = 0;
    if (GetWindowLongPtr(g_hView, GWL_STYLE) & WS_VSCROLL)
        scrollW = GetSystemMetrics(SM_CXVSCROLL);

    // 3. DPI 缩放因子
    HDC hdc = GetDC(g_hView);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(g_hView, hdc);

    float scaleX = 96.0f / dpiX;
    float scaleY = 96.0f / dpiY;

    // 4. 逻辑像素
    int width = static_cast<int>((rc.right - scrollW) * scaleX);
    int height = static_cast<int>((rc.bottom) * scaleY);

    client = litehtml::position(0, 0, width, height);
}


litehtml::element::ptr
SimpleContainer::create_element(const char* tag,
    const litehtml::string_map& attrs,
    const std::shared_ptr<litehtml::document>& doc)
{

    if (litehtml::t_strcasecmp(tag, "script") != 0)
        return nullptr;   // 让 litehtml 自己建别的节点
    if (!g_cfg.enableJS) { return nullptr; }

    /* 1. 建节点（litehtml 会把内联文本自动收进来） */
    auto el = std::make_shared<litehtml::html_tag>(doc);
    el->set_tagName(tag);

    /* 2. 记录到待执行列表 */
    AppBootstrap::script_info si;
    si.el = el;
    g_bootstrap->m_pending_scripts.emplace_back(std::move(si));
    return el;
    // TODO: 处理 <math> 标签
    // TODO: 处理 <script> 标签
    // TODO: 处理 <svg> 标签
}



void SimpleContainer::import_css(litehtml::string& text,
    const litehtml::string& url,
    litehtml::string& baseurl)
{

    if (!g_cfg.enableCSS) {
        //text.clear();           // 禁用所有外部/内部 CSS
        return;
    }
    // url 可能是相对路径，baseurl 是当前 html 所在目录
    std::wstring w_path = g_book->m_zipIndex.find(a2w(url));
    MemFile mf = g_book->read_zip(w_path.c_str());
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





// ---------- 2. 标题 ----------------------------------------------------
void SimpleContainer::set_caption(const char* cap)
{
    if (cap && g_hWnd) {
        std::string title;
        auto t = g_book->get_title();
        if (!t.empty()) { title += t + " - "; }
        auto a = g_book->get_author();
        if (!a.empty()) { title += a + " - "; }
        title += g_cfg.appName;
        SetWindowTextW(g_hWnd, a2w(title).c_str());
        //OutputDebugStringW((a2w(cap)+L"\n").c_str());
    }
}

// ---------- 3. base url -------------------------------------------------
void SimpleContainer::set_base_url(const char* base)
{
    return;
}

// ---------- 4. 链接注册 --------------------------------------------------
void SimpleContainer::link(const std::shared_ptr<litehtml::document>& doc,
    const litehtml::element::ptr& el)
{
    OutputDebugStringA(el->get_tagName());
    OutputDebugStringA("\n");
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

    std::string_view sv{ url };
    if (sv.starts_with('#'))
    { 
        /* 锚点 */ 
        std::wstring cssSel = a2w(url + 1);   // 去掉开头的 '#'  

        PostMessageW(g_hView, WM_EPUB_ANCHOR,
            reinterpret_cast<WPARAM>(_wcsdup(cssSel.c_str())), 0);
    }
    else if (sv.starts_with("http") || sv.starts_with("mailto:")) { /* 外部 */ }
    else 
    { 
        /* 章节跳转 */
        std::wstring href = g_book->m_zipIndex.find(a2w(url));
        wchar_t* url_copy = _wcsdup(href.c_str());
        PostMessageW(g_hView, WM_EPUB_NAVIGATE,
            reinterpret_cast<WPARAM>(url_copy), 0);
    }

}

bool SimpleContainer::on_element_click(const litehtml::element::ptr& el)
{
    OutputDebugStringA(el->get_tagName());
    OutputDebugStringA("\n");
    el->set_pseudo_class(litehtml::_hover_, true);
    if (std::strcmp(el->get_tagName(), "img") == 0 && g_cfg.enableImagePreview && !g_book->find_link_in_chain(el))
    {   

        g_book->show_imageview(el);
    }
    return true;
}
void SimpleContainer::on_mouse_event(const litehtml::element::ptr& el,
    litehtml::mouse_event event)
{
    if (!g_cfg.enableHoverPreview) return;

    if (event == litehtml::mouse_event::mouse_event_enter)
    {
        
        if (!el) return;
        auto link = g_book->find_link_in_chain(el);

        std::string html;
        if (!link) { return ; }
        const char* href_raw = link->get_attr("href");
        if (!href_raw) { return; }
        std::string id = g_book->extract_anchor(href_raw);
        if (id.empty()) {  return; }
        html = g_book->html_of_anchor_paragraph(g_cMain->m_doc.get(), id);
  
         //如果已存在，先杀掉
        if (g_tooltipTimer)
        {
            timeKillEvent(g_tooltipTimer);
            g_tooltipTimer = 0;
        }

        // 用 new 把 element 指针传进去
        struct Payload { std::string html; };
        auto* p = new Payload{ std::move(html)};

        g_tooltipTimer = timeSetEvent(
            g_cfg.tooltip_delay_ms,          // 延迟
            1,                         // 分辨率 1ms
            [](UINT, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR)
            {
                auto* t = reinterpret_cast<Payload*>(dwUser);
                g_book->show_tooltip(std::move(t->html));
                delete t;
            },
            reinterpret_cast<DWORD_PTR>(p),
            TIME_ONESHOT);             // 一次性
    }
    else
    {
        if (g_tooltipTimer)
        {
            timeKillEvent(g_tooltipTimer);
            g_tooltipTimer = 0;
        }
        g_book->hide_tooltip();
    }
}




// 内部工具：UTF-8 → UTF-16
static std::vector<UChar> utf8_to_utf16(const char* src)
{
    std::vector<UChar> buf;
    if (!src || !*src) return buf;
    int32_t len8 = static_cast<int32_t>(std::strlen(src));
    UErrorCode status = U_ZERO_ERROR;
    int32_t len16 = 0;
    u_strFromUTF8(nullptr, 0, &len16, src, len8, &status);
    buf.resize(len16 + 1);
    status = U_ZERO_ERROR;
    u_strFromUTF8(buf.data(), len16 + 1, nullptr, src, len8, &status);
    if (U_FAILURE(status)) buf.clear();
    else buf.resize(len16);
    return buf;
}

// 内部工具：UTF-16 → 堆上 UTF-8（以 '\0' 结尾，可直接传给 on_word/on_space）
static char* utf16_to_heap_utf8(const UChar* src, int32_t len)
{
    if (!src || len <= 0) return nullptr;
    UErrorCode status = U_ZERO_ERROR;
    int32_t len8 = 0;
    u_strToUTF8(nullptr, 0, &len8, src, len, &status);
    char* out = new char[len8 + 1];
    status = U_ZERO_ERROR;
    u_strToUTF8(out, len8 + 1, nullptr, src, len, &status);
    if (U_FAILURE(status)) { delete[] out; return nullptr; }
    return out;
}


void SimpleContainer::split_text(
    const char* text,
    const std::function<void(const char*)>& on_word,
    const std::function<void(const char*)>& on_space)
{
    if (!text || !*text) return;

    /* 1. UTF-8 → UTF-16 */
    auto u16 = utf8_to_utf16(text);
    if (u16.empty()) return;

    /* 2. 创建 ICU 单词边界迭代器（系统默认 locale，支持多语言） */
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator* brk = ubrk_open(
        UBRK_WORD, nullptr,
        u16.data(), static_cast<int32_t>(u16.size()),
        &status);
    if (U_FAILURE(status)) return;

    /* 3. 遍历所有边界区间 */
    std::vector<std::u16string> tokens;
    int32_t prev = ubrk_first(brk);
    for (int32_t curr = ubrk_next(brk);
        curr != UBRK_DONE;
        prev = curr, curr = ubrk_next(brk))
    {
        /* 3.1 判断区间是否全为空格 */
        bool all_space = true;
        UChar32 cp;
        int32_t idx = prev;
        while (idx < curr) {
            U16_NEXT(u16.data(), idx, curr, cp);
            if (!u_isspace(cp)) { all_space = false; break; }
        }

        /* 3.2 转回 UTF-8 并回调 */
        char* out = utf16_to_heap_utf8(u16.data() + prev, curr - prev);
        if (!out) continue;

        if (all_space && on_space)      on_space(out);
        else if (!all_space && on_word) on_word(out);

        delete[] out;   // 回调后立即释放
    }

    ubrk_close(brk);
}

//void SimpleContainer::split_text(const char* text,
//    const std::function<void(const char*)>& on_word,
//    const std::function<void(const char*)>& on_space)
//{
//    if (!text || !*text) return;
//
//    // UTF-8 → ICU UnicodeString
//    icu::UnicodeString ustr = icu::UnicodeString::fromUTF8(text);
//
//    // 创建行断行迭代器（UAX #14）
//    UErrorCode status = U_ZERO_ERROR;
//    std::unique_ptr<icu::BreakIterator> brk(
//        icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), status));
//    if (U_FAILURE(status)) return;
//
//    brk->setText(ustr);
//
//    int32_t prev = brk->first();
//    for (int32_t curr = brk->next(); curr != icu::BreakIterator::DONE;
//        prev = curr, curr = brk->next())
//    {
//        icu::UnicodeString seg(ustr, prev, curr - prev);
//
//        // 判断这一段是不是纯空格
//        bool all_space = true;
//        for (int32_t i = 0; i < seg.length(); ++i) {
//            if (!u_isspace(seg.char32At(i))) { all_space = false; break; }
//        }
//
//        std::string out;
//        seg.toUTF8String(out);
//
//        if (all_space) {
//            if (on_space) on_space(out.c_str());
//        }
//        else {
//            if (on_word) on_word(out.c_str());
//        }
//    }
//}
//✅ 使用
// ---------- 6. 鼠标形状 -------------------------------------------------
// 预置常用系统光标
static const std::unordered_map<std::string, LPCWSTR> kSysCursors = {
    {"default",  IDC_ARROW},
    {"pointer",  IDC_HAND},
    {"text",     IDC_IBEAM},
    {"wait",     IDC_WAIT},
    {"crosshair",IDC_CROSS},
    {"move",     IDC_SIZEALL},
    {"e-resize", IDC_SIZEWE},
    {"n-resize", IDC_SIZENS},
    {"w-resize", IDC_SIZEWE},
    {"s-resize", IDC_SIZENS},
    {"ne-resize",IDC_SIZENESW},
    {"nw-resize",IDC_SIZENWSE},
    {"se-resize",IDC_SIZENWSE},
    {"sw-resize",IDC_SIZENESW},
};
void SimpleContainer::set_cursor(const char* cursor)
{
    m_currentCursor = IDC_ARROW;           // 默认箭头

    if (!cursor) return;

    // 1. 系统内置光标
    auto it = kSysCursors.find(cursor);
    if (it != kSysCursors.end())
    {
        m_currentCursor = it->second;
        return;
    }

 

    // 3. 兜底：箭头
    m_currentCursor = IDC_ARROW;
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



// ---------- 9. 媒体查询 -----------------------------------------------
void SimpleContainer::get_media_features(litehtml::media_features& mf) const
{
    // 1. 窗口客户区（物理像素）
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    mf.width = MulDiv(rc.right - rc.left, GetDpiForWindow(m_hwnd), 96);
    mf.height = MulDiv(rc.bottom - rc.top, GetDpiForWindow(m_hwnd), 96);

    // 2. 屏幕物理分辨率
    const UINT dpiX = GetDpiForWindow(m_hwnd);   // 也可用 GetDpiForSystem
    mf.resolution = dpiX;
    mf.device_width = MulDiv(GetSystemMetricsForDpi(SM_CXSCREEN, dpiX), dpiX, 96);
    mf.device_height = MulDiv(GetSystemMetricsForDpi(SM_CYSCREEN, dpiX), dpiX, 96);

    // 3. 颜色深度（24 位）
    HDC hdc = GetDC(nullptr);
    mf.color = GetDeviceCaps(hdc, BITSPIXEL);   // 通常 24 或 32
    mf.monochrome = 0;

    ReleaseDC(nullptr, hdc);
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


void SimpleContainer::init_dpi() {
    if (HDC hdc = GetDC(nullptr)) {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        m_px_per_pt = static_cast<float>(dpi) / 72.0f;
        ReleaseDC(nullptr, hdc);
    }
}
litehtml::pixel_t SimpleContainer::pt_to_px(float pt) const {
    // 乘法 + 位移，比 MulDiv 更快
    return static_cast<litehtml::pixel_t>(
        pt * m_px_per_pt + 0.5f);
}


void preprocess_js(std::string& html)
{
    if (!g_cfg.enableJS) {
        // 1) 删除 <script ...>...</script>
        static const std::regex reScriptPair(
            R"(<\s*script\b[^>]*>.*?</script>)",
            std::regex::icase | std::regex::optimize | std::regex::nosubs);

        // 2) 删除自闭合 <script ... />
        static const std::regex reScriptSelf(
            R"(<\s*script\b[^>]*\/>)",
            std::regex::icase | std::regex::optimize | std::regex::nosubs);

        html = std::regex_replace(html, reScriptPair, "");
        html = std::regex_replace(html, reScriptSelf, "");
    }
    else
    {
        std::regex  scRe(R"(<script\b([^>]*)\bsrc\s*=\s*["']([^"']*)["']([^>]*)/\s*>)",
            std::regex::icase);
        std::string out;
        out.reserve(html.size());

        std::sregex_iterator it(html.begin(), html.end(), scRe);
        std::sregex_iterator end;
        size_t last = 0;

        for (; it != end; ++it)
        {
            const std::smatch& m = *it;

            // 2.1 读文件
            std::string src = m[2].str();
            std::wstring w_path = g_book->m_zipIndex.find(a2w(src));
            MemFile mf = g_book->read_zip(w_path.c_str());
            std::string code;
            if (!mf.data.empty())
                code.assign(reinterpret_cast<const char*>(mf.data.data()),
                    mf.data.size());

            // 2.2 去掉 src 属性
            std::string attrs = m[1].str() + m[3].str();
            attrs = std::regex_replace(attrs,
                std::regex(R"(\s*\bsrc\s*=\s*["'][^"']*["'])", std::regex::icase), "");

            // 2.3 拼成对标签
            out.append(html, last, m.position() - last);
            out += "<script" + attrs + ">" + code + "</script>";
            last = m.position() + m.length();
        }
        out.append(html, last, std::string::npos);
        html.swap(out);
    }
}

namespace fs = std::filesystem;

inline std::string blade16(std::string_view data)
{
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data.data(), data.size());

    std::array<uint8_t, 16> out;
    blake3_hasher_finalize(&hasher, out.data(), out.size());

    char hex[33];
    for (size_t i = 0; i < out.size(); ++i)
        std::sprintf(hex + i * 2, "%02x", out[i]);
    return std::string(hex, 32);   // 32 个十六进制字符
}

static std::wstring blake3_hex(const std::vector<uint8_t>& data)
{
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data.data(), data.size());

    std::array<uint8_t, BLAKE3_OUT_LEN> hash;          // 32 字节
    blake3_hasher_finalize(&hasher, hash.data(), hash.size());

    std::wostringstream oss;
    for (uint8_t b : hash)
        oss << std::hex << std::setw(2) << std::setfill(L'0') << (b & 0xFF);
    return oss.str();                                  // 64 个十六进制字符
}
static void save_image(const ImageFrame& img, const std::filesystem::path& bmpPath)
{
        const int w = img.width;
        const int h = img.height;
        const int rowBytes = w * 4;
        const int imgSize = rowBytes * h;
        const int fileSize = 54 + imgSize;   // 54 = 14 + 40

        std::ofstream ofs(bmpPath, std::ios::binary);
        if (ofs)
        {
            // BITMAPFILEHEADER (14 bytes)
            uint16_t bfType = 0x4D42;        // 'BM'
            uint32_t bfSize = fileSize;
            uint32_t bfOffBits = 54;
            ofs.write(reinterpret_cast<const char*>(&bfType), 2);
            ofs.write(reinterpret_cast<const char*>(&bfSize), 4);
            ofs.seekp(4, std::ios::cur);     // skip reserved
            ofs.write(reinterpret_cast<const char*>(&bfOffBits), 4);

            // BITMAPINFOHEADER (40 bytes)
            uint32_t biSize = 40;
            int32_t  biWidth = w;
            int32_t  biHeight = -h;          // top-down
            uint16_t biPlanes = 1;
            uint16_t biBitCount = 32;
            uint32_t biCompression = 0;
            uint32_t biSizeImage = imgSize;
            ofs.write(reinterpret_cast<const char*>(&biSize), 4);
            ofs.write(reinterpret_cast<const char*>(&biWidth), 4);
            ofs.write(reinterpret_cast<const char*>(&biHeight), 4);
            ofs.write(reinterpret_cast<const char*>(&biPlanes), 2);
            ofs.write(reinterpret_cast<const char*>(&biBitCount), 2);
            ofs.write(reinterpret_cast<const char*>(&biCompression), 4);
            ofs.seekp(20, std::ios::cur);    // skip rest (zeros)

            // pixel data
            ofs.write(reinterpret_cast<const char*>(img.rgba.data()), imgSize);
        }
}


std::string inline_and_rasterize_svgs_gumbo(std::string_view html,
    const fs::path& tempDir)
{
    fs::create_directories(tempDir);

    // 1. 用 Gumbo 解析
    GumboOutput* output = gumbo_parse(html.data());
    std::string newHtml;
    newHtml.reserve(html.size() * 2);

    // 2. 递归遍历 DOM
    std::function<void(GumboNode*, std::ostringstream&)> walk;
    walk = [&](GumboNode* node, std::ostringstream& out)
        {
            if (node->type == GUMBO_NODE_TEXT)
            {
                out << node->v.text.text;
                return;
            }
            if (node->type != GUMBO_NODE_ELEMENT) return;

            GumboElement& elem = node->v.element;
            const std::string tag = gumbo_normalized_tagname(elem.tag);

            if (tag == "svg")
            {
                // 2.1 重新序列化 <svg>...</svg>

                GumboNode fakeNode{};
                fakeNode.type = GUMBO_NODE_ELEMENT;
                fakeNode.v.element = elem;          // 拷贝 GumboElement
                std::string svgBlock;
                gumbo_serialize(&fakeNode, svgBlock);   // 现在完全匹配函数签名


                std::string hash = blade16(svgBlock);
                fs::path bmpPath = tempDir / (hash + ".bmp");

                if (!fs::exists(bmpPath))
                {
                    // 2.2 扫描 <image> 并解压
                    std::string patchedSvg = svgBlock;
                    std::regex imgRe("<(image|img)\\b[^>]*\\b(href|xlink:href)\\s*=\\s*\"([^\"]+)\"",
                        std::regex::icase);
                        std::sregex_iterator it(patchedSvg.begin(), patchedSvg.end(), imgRe), end;
                        for (; it != end; ++it)
                        {
                            std::string imgRel = (*it)[3].str();
                            std::wstring wRel = a2w(imgRel);

                            fs::path diskPath = tempDir / fs::path(imgRel).relative_path();
                            fs::create_directories(diskPath.parent_path());

                            std::wstring path = g_book->m_zipIndex.find(wRel);
                            MemFile mf = g_book->read_zip(path.c_str());
                            if (!mf.data.empty())
                            {
                                std::ofstream ofs(diskPath, std::ios::binary);
                                ofs.write(reinterpret_cast<const char*>(mf.data.data()),
                                    mf.data.size());
                            }
                            // 替换为绝对路径
                            patchedSvg = std::regex_replace(
                                patchedSvg,
                                std::regex(R"(()" + std::regex_replace(imgRel, std::regex(R"([.^$*+?()\[\]{}|\\])"), R"(\$&)") + R"())"),
                                diskPath.generic_string());
                        }

                        // 2.3 渲染
                        MemFile mfSvg;
                        mfSvg.data.assign(patchedSvg.begin(), patchedSvg.end());
                        ImageFrame frame = decode_img(mfSvg, L"svg");
                        if (!frame.rgba.empty())
                        {
                            save_image(frame, bmpPath);

                        }
                }

                // 2.4 拼 <img>
                int w = 0, h = 0;
                //for (unsigned i = 0; i < elem.attributes.length; ++i)
                //{
                //    GumboAttribute* a = static_cast<GumboAttribute*>(elem.attributes.data[i]);
                //    if (strcmp(a->name, "width") == 0)  w = std::atoi(a->value);
                //    if (strcmp(a->name, "height") == 0) h = std::atoi(a->value);
                //}
                out << R"(<img src=")" << bmpPath.generic_string() << R"(")";
                out << " width=\"" << "100%" << "\"";
                out << " height=\"" << "100%" << "\"";
                out << " />";
                return;
            }

            // 普通节点
            out << "<" << tag;
            for (unsigned i = 0; i < elem.attributes.length; ++i)
            {
                GumboAttribute* a = static_cast<GumboAttribute*>(elem.attributes.data[i]);
                out << " " << a->name << "=\"" << a->value << "\"";
            }
            out << ">";
            for (unsigned i = 0; i < elem.children.length; ++i)
                walk(static_cast<GumboNode*>(elem.children.data[i]), out);
            out << "</" << tag << ">";
        };

    std::ostringstream oss;
    walk(output->root, oss);
    newHtml = oss.str();
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    return newHtml;
}

// ------------------------------------------------
// 主流程：SVG 内 <image> 零拷贝替换
// ------------------------------------------------

void replace_svg_with_img(std::string& html,
    const fs::path& tempDir)
{
    fs::create_directories(tempDir);

    /* ---------- 1. 解析 ---------- */
    GumboOutput* output = gumbo_parse(html.c_str());

    /* ---------- 2. 收集 <svg> 节点 ---------- */
    struct SvgNode {
        size_t start;   // <svg ...> 的起始偏移
        size_t end;     // </svg> 的结束偏移
    };
    std::vector<SvgNode> svgs;

    std::function<void(GumboNode*)> walk = [&](GumboNode* node)
        {
            if (node->type != GUMBO_NODE_ELEMENT) return;
            GumboElement& el = node->v.element;
            if (el.tag == GUMBO_TAG_SVG)
            {
                size_t start = el.start_pos.offset;
                size_t end = el.end_pos.offset + el.original_end_tag.length;
                svgs.push_back({ start, end });
            }
            for (unsigned i = 0; i < el.children.length; ++i)
                walk(static_cast<GumboNode*>(el.children.data[i]));
        };
    walk(output->root);

    /* ---------- 3. 从后往前替换 ---------- */
  
    for (auto it = svgs.rbegin(); it != svgs.rend(); ++it)
    {
        std::string svgBlock = html.substr(it->start, it->end - it->start);
        std::string hash = blade16(svgBlock);
        if (!g_cMain) continue;
        if (!g_cMain->isImageCached(hash))
        {

            // ------------------------------------------------
            std::regex imgRe("<(image|img)\\b[^>]*\\b(href|xlink:href)\\s*=\\s*\"([^\"]+)\"",
                std::regex::icase);
            std::string patchedSvg = svgBlock;
            std::smatch m;
            std::string::const_iterator search(patchedSvg.cbegin());

            while (std::regex_search(search, patchedSvg.cend(), m, imgRe))
            {
                std::string imgRel = m[3].str();          // zip 内路径
                std::wstring wRel = a2w(imgRel);

                MemFile mf = g_book->read_zip(g_book->m_zipIndex.find(wRel).c_str());
                if (!mf.data.empty())
                {
                    // 1. 根据扩展名决定 MIME
                    fs::path p(imgRel);
                    std::string mime = "image/png";
                    if (p.extension() == ".jpg" || p.extension() == ".jpeg")
                        mime = "image/jpeg";

                    // 2. 编码 base64
                    std::string b64 = base64_encode(mf.data);

                    // 3. 生成 data URI
                    std::string dataUri = "data:" + mime + ";base64," + b64;

                    // 4. 替换 href
                    patchedSvg.replace(m.position(3), m.length(3), dataUri);
                    search = patchedSvg.cbegin() + m.position() + dataUri.size();
                }
                else
                {
                    // 读不到就保持原路径
                    search = m[0].second;
                }
            }

            g_cMain->addImageCache(hash, patchedSvg);

        }
        std::ostringstream imgTag;
        imgTag << R"(<img src=")" << hash << R"(")";
        imgTag << " width=\"" << "100%" << "\"";
        imgTag << " height=\"" << "100%" << "\"";
        imgTag << " />";

        html.replace(it->start, it->end - it->start, imgTag.str());
    }

    gumbo_destroy_output(&kGumboDefaultOptions, output);
}
// 替代 “duk_push_string_file”

//static std::string read_file(const char* path) {
//    FILE* fp = fopen(path, "rb");
//    if (!fp) return {};
//    fseek(fp, 0, SEEK_END);
//    size_t sz = ftell(fp);
//    fseek(fp, 0, SEEK_SET);
//    std::string buf(sz, '\0');
//    fread(buf.data(), 1, sz, fp);
//    fclose(fp);
//    return buf;
//}
//
//std::string tex_to_html(const std::string& tex, bool displayMode = false)
//{
//    JSRuntime* rt = JS_NewRuntime();
//    if (!rt) return {};
//    JSContext* ctx = JS_NewContext(rt);
//    if (!ctx) { JS_FreeRuntime(rt); return {}; }
//
//
//    /* 1. 注入 KaTeX 单文件 */
//    fs::path katex_path = exe_dir() / "config" / "katex" / "katex.min.js";
//    std::string katex_js = read_file(katex_path);
//    if (katex_js.empty()) { JS_FreeContext(ctx); JS_FreeRuntime(rt); return {}; }
//    JS_Eval(ctx, katex_js.c_str(), katex_js.size(), "<katex>", JS_EVAL_TYPE_GLOBAL);
//
//    /* 2. stub：把 TeX 转 HTML */
//
//    char stub[512];
//    snprintf(stub, sizeof(stub),
//        "katex.renderToString(tex, {"
//        "  displayMode: %s,"
//        "  throwOnError: false,"
//        "  output: 'html'"
//        "})",
//        displayMode ? "true" : "false");
//    /* 3. 把公式字符串放进全局变量 `tex` */
//    JSValue global = JS_GetGlobalObject(ctx);
//    JS_SetPropertyStr(ctx, global, "tex", JS_NewString(ctx, tex.c_str()));
//    JS_FreeValue(ctx, global);
//
//    /* 4. 执行 stub 并取结果 */
//    JSValue ret = JS_Eval(ctx, stub, strlen(stub), "<stub>", JS_EVAL_TYPE_GLOBAL);
//    std::string html;
//    if (JS_IsException(ret)) {
//        JSValue ex = JS_GetException(ctx);
//        const char* err = JS_ToCString(ctx, ex);
//        fprintf(stderr, "KaTeX error: %s\n", err ? err : "unknown");
//        JS_FreeCString(ctx, err);
//        JS_FreeValue(ctx, ex);
//    }
//    else {
//        const char* str = JS_ToCString(ctx, ret);
//        if (str) { html = str; JS_FreeCString(ctx, str); }
//    }
//
//    JS_FreeValue(ctx, ret);
//    JS_FreeContext(ctx);
//    JS_FreeRuntime(rt);
//    return html;
//}

/* ---------- 3. 主函数 ---------- */
//std::string replace_math_with_katex(const std::string& html)
//{
//    GumboOutput* output = gumbo_parse(html.c_str());
//
//    /* 收集所有 <math> 节点 */
//    struct MathNode {
//        GumboElement* el;
//        size_t start;
//        size_t end;
//    };
//    std::vector<MathNode> mathNodes;
//    std::function<void(GumboNode*)> walk = [&](GumboNode* node) {
//        if (node->type == GUMBO_NODE_ELEMENT) {
//            GumboElement& el = node->v.element;
//            if (el.tag == GUMBO_TAG_MATH) {
//                mathNodes.push_back({ &el,
//                                      el.start_pos.offset,
//                                      el.end_pos.offset + el.original_end_tag.length});
//            }
//            for (unsigned i = 0; i < el.children.length; ++i)
//                walk(static_cast<GumboNode*>(el.children.data[i]));
//        }
//        };
//    walk(output->root);
//
//    /* 从后往前替换，避免字节偏移失效 */
//    std::string patched = html;
//    for (auto it = mathNodes.rbegin(); it != mathNodes.rend(); ++it) {
//        /* 1. 取 MathML 原文 */
//        std::string mathml = patched.substr(it->start, it->end - it->start);
//
//        std::string tex = mathml2tex::convert(mathml);
//        
//        /* 3. LaTeX → KaTeX HTML */
//        std::string katexHtml = tex_to_html(tex, /*display=*/false);
//        OutputDebugStringA(katexHtml.c_str());
//        OutputDebugStringA("\n");
//        if (katexHtml.empty()) continue;
//
//        /* 4. 直接替换原 <math> 标签 */
//        patched.replace(it->start, it->end - it->start, katexHtml);
//    }
//
//    gumbo_destroy_output(&kGumboDefaultOptions, output);
//    return patched;
//}
//




// --------------------------------------------------
// 通用 HTML 预处理
// --------------------------------------------------
void replace_math_with_svg(std::string& html)
{
    GumboOutput* output = gumbo_parse(html.c_str());

    /* 收集所有 <math> 节点 */
    struct MathNode {
        GumboElement* el;
        size_t start;
        size_t end;
    };
    std::vector<MathNode> mathNodes;
    std::function<void(GumboNode*)> walk = [&](GumboNode* node) {
        if (node->type == GUMBO_NODE_ELEMENT) {
            GumboElement& el = node->v.element;
            if (el.tag == GUMBO_TAG_MATH) {
                mathNodes.push_back({ &el,
                                      el.start_pos.offset,
                                      el.end_pos.offset + el.original_end_tag.length});
            }
            for (unsigned i = 0; i < el.children.length; ++i)
                walk(static_cast<GumboNode*>(el.children.data[i]));
        }
        };
    walk(output->root);

    /* 从后往前替换，避免字节偏移失效 */
    //std::string patched = html;
    for (auto it = mathNodes.rbegin(); it != mathNodes.rend(); ++it) {
        /* 1. 取 MathML 原文 */
        std::string mathml = html.substr(it->start, it->end - it->start);

        std::string hash = blade16(mathml);
        if (!g_cMain)continue;
        if(!g_cMain->isImageCached(hash))
        {
            /* 2. LaTeX → KaTeX → SVG（你原来的逻辑） */
            MathML2SVG& m2s = MathML2SVG::instance();
            std::string svg = m2s.convert(mathml);
            if (svg.empty()) continue;
            g_cMain->addImageCache(hash, svg);

        }
        std::string imgTag;
        imgTag =  R"(<img class="math-png" src=")" + hash
            + R"(" alt="math" />)";

        /* 7. 替换原 <math> 标签 */
        html.replace(it->start, it->end - it->start, imgTag);
    }

    gumbo_destroy_output(&kGumboDefaultOptions, output);

}
//

struct HtmlFeatureFlags {
    bool has_svg = false;
    bool has_math = false;
    bool has_script = false;
    bool all() const { return has_svg && has_math && has_script; }
};

inline HtmlFeatureFlags detect_html_features(const std::string& html) noexcept
{
    HtmlFeatureFlags f;
    const char* s = html.data();
    const char* end = s + html.size();

    while (s < end - 6)   // 最短 "<svg" 4 字节，留余量
    {
        if (*s == '<')
        {
            ++s;
            // 跳过空白： <  svg  或 <  script
            while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
                ++s;

            if (s + 3 <= end) {
                char c0 = static_cast<char>(std::tolower(*s));
                char c1 = static_cast<char>(std::tolower(*(s + 1)));
                char c2 = static_cast<char>(std::tolower(*(s + 2)));

                if (c0 == 's' && c1 == 'v' && c2 == 'g') { f.has_svg = true; }
                else if (c0 == 'm' && c1 == 'a' && c2 == 't') { f.has_math = true; }
                else if (c0 == 's' && c1 == 'c' && c2 == 'r') { f.has_script = true; }

                if (f.all()) break;   // 提前终止
            }
        }
        ++s;
    }
    return f;
}
void PreprocessHTML(std::string& html)
{
    auto flags = detect_html_features(html);
    if(flags.has_math) replace_math_with_svg(html); 

    html = std::regex_replace(
        html,
        std::regex(R"(<([a-zA-Z][a-zA-Z0-9]*)\b([^>]*?)/\s*>)", std::regex::icase),
        "<$1$2></$1>");

    if (flags.has_script)preprocess_js(html);

  
     if (flags.has_svg) 
     {
         std::wstring dir = make_temp_dir();
         replace_svg_with_img(html, dir);
     } 
     if (g_cfg.enableGlobalCSS) { inject_global_css(html); }
     inject_css(html);

}





void SimpleContainer::BeginDraw()
{

    m_rt->BeginDraw();
    m_rt->Clear(D2D1::ColorF(D2D1::ColorF::White));

    // 1. 保存原始矩阵
    m_rt->GetTransform(&m_oldMatrix);

    // 3. 以鼠标位置为中心整体缩放
    m_rt->SetTransform(
        D2D1::Matrix3x2F::Scale(
            m_zoom_factor,
            m_zoom_factor,
            D2D1::Point2F(static_cast<float>(0),
                static_cast<float>(0))));
}
void SimpleContainer::EndDraw()
{
    // 恢复原始矩阵
    m_rt->SetTransform(m_oldMatrix);

    m_rt->EndDraw();
}

// ---------- 辅助：UTF-8 ↔ UTF-16 ----------



// ---------- 实现 ----------
ComPtr<ID2D1SolidColorBrush> SimpleContainer::getBrush(litehtml::uint_ptr hdc, const litehtml::web_color& c)
{
    uint32_t key = (c.alpha << 24) | (c.red << 16) | (c.green << 8) | c.blue;
    auto it = m_brushPool.find(key);
    if (it != m_brushPool.end()) return it->second;
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(c.red / 255.f, c.green / 255.f, c.blue / 255.f, c.alpha / 255.f),
        &brush);
    m_brushPool[key] = brush;
    return brush;
}

ComPtr<IDWriteTextLayout> SimpleContainer::getLayout(const std::wstring& txt,
    const FontPair* fp,
    float maxW)
{
    LayoutKey k{ txt, fp->descr.hash(), maxW };
    auto it = m_layoutCache.find(k);
    if (it != m_layoutCache.end()) return it->second;

    ComPtr<IDWriteTextLayout> layout;
    m_dwrite->CreateTextLayout(txt.c_str(), (UINT32)txt.size(),
        fp->format.Get(), maxW, 512.f, &layout);
    if (!layout) return nullptr;

    // 换行/截断
    bool nowrap = false;
    //layout->SetWordWrapping(nowrap ? DWRITE_WORD_WRAPPING_NO_WRAP
    //    : DWRITE_WORD_WRAPPING_WRAP);
    if (nowrap) {
        DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        layout->SetTrimming(&trim, nullptr);
    }
    m_layoutCache[k] = layout;
    return layout;
}

void SimpleContainer::record_char_boxes(ID2D1RenderTarget* rt,
    IDWriteTextLayout* layout,
    const std::wstring& wtxt,
    const litehtml::position& pos)
{
    LineBoxes line;
    float originX = static_cast<float>(pos.x);
    float originY = static_cast<float>(pos.y);

    for (size_t i = 0; i < wtxt.size(); ++i)
    {
        DWRITE_HIT_TEST_METRICS htm;
        float left, top;
        layout->HitTestTextPosition(i, FALSE, &left, &top, &htm);

        CharBox cb;
        cb.ch = wtxt[i];
        cb.rect = D2D1::RectF(
            originX + left,
            originY + top,
            originX + left + htm.width,
            originY + top + htm.height);
        cb.offset = m_plainText.size() + i;
        line.push_back(cb);
    }
    m_lines.emplace_back(std::move(line));

    // 同时累积纯文本
    m_plainText += wtxt;
}


void SimpleContainer::draw_text(litehtml::uint_ptr hdc,
    const char* text,
    litehtml::uint_ptr hFont,
    litehtml::web_color color,
    const litehtml::position& pos)
{
    if (!text || !*text || !hFont) return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    FontPair* fp = reinterpret_cast<FontPair*>(hFont);
    if (!fp) return;

    // 1. 画刷
    auto brush = getBrush(hdc, color);
    if (!brush) return;

    // 2. 文本
    std::wstring wtxt = a2w(text);
    if (wtxt.empty()) return;

    float maxW = 8192.0f;
    auto layout = getLayout(wtxt, fp, maxW);
    if (!layout) return;
    record_char_boxes(rt, layout.Get(), wtxt, pos);
    // 3. 绘制文本
    rt->DrawTextLayout(D2D1::Point2F(static_cast<float>(pos.x),
        static_cast<float>(pos.y)),
        layout.Get(), brush.Get());

    // 4. 绘制装饰线（下划线 / 删除线 / 上划线）
    draw_decoration(hdc, fp, color, pos, layout.Get());
}
void SimpleContainer::draw_decoration(litehtml::uint_ptr hdc, const FontPair* fp,
    litehtml::web_color color,
    const litehtml::position& pos,
    IDWriteTextLayout* layout)
{
    if (fp->descr.decoration_line == litehtml::text_decoration_line_none)
        return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    /* 1. 文本整体尺寸 */
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    if (tm.width <= 0) return;

    /* 2. 取第一行的 baseline */
    std::vector<DWRITE_LINE_METRICS> lineMetrics;
    UINT32 lineCount = 0;
    layout->GetLineMetrics(nullptr, 0, &lineCount);
    if (lineCount == 0) return;
    lineMetrics.resize(lineCount);
    layout->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount);

    const float baseline = lineMetrics[0].baseline;
    const float yBase = static_cast<float>(pos.y) + baseline;

    /* 3. 画刷 */
    auto brush = getBrush(hdc, fp->descr.decoration_color.is_current_color
        ? color
        : fp->descr.decoration_color);
    if (!brush) return;

    /* 4. 线粗：先用 1 px，后续可按 decoration_thickness 计算 */
    const float thick = fp->descr.decoration_thickness.val();

    /* 5. 绘制三种装饰线 */
    const float x0 = static_cast<float>(pos.x);
    const float x1 = x0 + tm.width;

    /* 下划线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_underline)
    {
        const float y = yBase + 1.0f;   // 可根据字体度量再微调
        rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }

    /* 删除线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_line_through)
    {
        const float y = yBase - lineMetrics[0].height * 0.35f;
        rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }

    /* 上划线 */
    if (fp->descr.decoration_line & litehtml::text_decoration_line_overline)
    {
        const float y = yBase - lineMetrics[0].height;
        rt->DrawLine({ x0, y }, { x1, y }, brush.Get(), thick);
    }
}
// ----------------------------------------------------------
// 工具：根据 box 类型返回实际矩形
// ----------------------------------------------------------
static litehtml::position clip_box(const litehtml::background_layer& layer,
    litehtml::background_box box_type)
{
    switch (box_type)
    {
    case litehtml::background_box_content:
    case litehtml::background_box_padding:
        // 你的版本没有 content_box / padding_box，统一回退到 border_box
        return layer.border_box;
    default:
        return layer.border_box;
    }
}

// ----------------------------------------------------------
// 工具：加载位图（WIC -> D2D）
// ----------------------------------------------------------


// ----------------------------------------------------------
// 主函数：draw_image
// ----------------------------------------------------------
void SimpleContainer::draw_image(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const std::string& url,
    const std::string& base_url)
{
    if (url.empty()) return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);

    /* ---------- 1. 取缓存位图 ---------- */
    auto it = m_img_cache.find(url);
    if (it == m_img_cache.end()) return;
    const ImageFrame& frame = it->second;
    if (frame.rgba.empty()) return;

    ComPtr<ID2D1Bitmap> bmp;

    D2D1_BITMAP_PROPERTIES bp =
        D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED));
    rt->CreateBitmap(
        D2D1::SizeU(frame.width, frame.height),
        frame.rgba.data(),
        frame.stride,
        bp,
        &bmp);

    if (!bmp) return;

    /* ---------- 2. 计算目标矩形 ---------- */
    D2D1_RECT_F dst = D2D1::RectF(
        float(layer.border_box.left()),
        float(layer.border_box.top()),
        float(layer.border_box.right()),
        float(layer.border_box.bottom()));

    /* ---------- 3. 计算绘制区域（cover / contain / stretch） ---------- */
    float imgW = float(bmp->GetPixelSize().width);
    float imgH = float(bmp->GetPixelSize().height);
    if (imgW == 0 || imgH == 0) return;

    float dstW = dst.right - dst.left;
    float dstH = dst.bottom - dst.top;

    // 这里只演示 cover（填满 + 居中）
    float scale = std::max(dstW / imgW, dstH / imgH);
    float bgW = imgW * scale;
    float bgH = imgH * scale;
    float bgX = dst.left + (dstW - bgW) * 0.5f;
    float bgY = dst.top + (dstH - bgH) * 0.5f;

    D2D1_RECT_F drawRect = { bgX, bgY, bgX + bgW, bgY + bgH };

    /* ---------- 4. 绘制 ---------- */
    rt->DrawBitmap(bmp.Get(), drawRect, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        D2D1::RectF(0, 0, imgW, imgH));

}

inline bool SimpleContainer::is_all_zero(const litehtml::border_radiuses& r)
{
    return r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;
}
void SimpleContainer::draw_solid_fill(litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::web_color& color)
{
    // 1. 取出 D2D 渲染目标
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 2. 创建/复用纯色画刷
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(color.red / 255.0f,
            color.green / 255.0f,
            color.blue / 255.0f,
            color.alpha / 255.0f),
        &brush);
    if (!brush) return;

    // 3. 计算要填充的矩形（border_box）
    D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));


    // 4. 若存在圆角，用圆角矩形；否则直接矩形
    if (is_all_zero(layer.border_radius))
    {
        rt->FillRectangle(rc, brush.Get());
    }
    else
    {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            rc,
            static_cast<float>(layer.border_radius.top_left_x),
            static_cast<float>(layer.border_radius.top_left_y));
        rt->FillRoundedRectangle(rr, brush.Get());
    }
}


void SimpleContainer::draw_linear_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::linear_gradient& g)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 把 color_points 转成 D2D 色标
    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(g.color_points.size());
    for (const auto& cp : g.color_points)
    {
        stops.push_back(D2D1::GradientStop(
            static_cast<float>(cp.offset),
            D2D1::ColorF(
                cp.color.red / 255.0f,
                cp.color.green / 255.0f,
                cp.color.blue / 255.0f,
                cp.color.alpha / 255.0f)));
    }

    // 2. 创建 stop collection
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopColl;
    if (FAILED(rt->CreateGradientStopCollection(
        stops.data(),
        static_cast<UINT>(stops.size()),
        D2D1_GAMMA_2_2,
        D2D1_EXTEND_MODE_CLAMP,
        &stopColl)))
        return;

    // 3. 创建线性渐变画刷
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brush;
    if (FAILED(rt->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(g.start.x),
                static_cast<float>(g.start.y)),
            D2D1::Point2F(static_cast<float>(g.end.x),
                static_cast<float>(g.end.y))),
        stopColl.Get(),
        &brush)))
        return;

    // 4. 计算要填充的矩形
    const D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));

    // 5. 圆角判断
    auto& r = layer.border_radius;
    bool no_radius = r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;

    if (no_radius)
        rt->FillRectangle(rc, brush.Get());
    else
    {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            rc,
            static_cast<float>(r.top_left_x),
            static_cast<float>(r.top_left_y));
        rt->FillRoundedRectangle(rr, brush.Get());
    }
}


void SimpleContainer::draw_radial_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::radial_gradient& g)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 构造 D2D 色标
    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(g.color_points.size());
    for (const auto& cp : g.color_points)
    {
        stops.push_back(D2D1::GradientStop(
            static_cast<float>(cp.offset),
            D2D1::ColorF(
                cp.color.red / 255.0f,
                cp.color.green / 255.0f,
                cp.color.blue / 255.0f,
                cp.color.alpha / 255.0f)));
    }

    // 2. 创建 stop collection
    ComPtr<ID2D1GradientStopCollection> stopColl;
    if (FAILED(rt->CreateGradientStopCollection(
        stops.data(),
        static_cast<UINT>(stops.size()),
        D2D1_GAMMA_2_2,
        D2D1_EXTEND_MODE_CLAMP,
        &stopColl)))
        return;

    // 3. 创建径向渐变画刷
    ComPtr<ID2D1RadialGradientBrush> brush;
    if (FAILED(rt->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(static_cast<float>(g.position.x),
                static_cast<float>(g.position.y)), // 圆心
            D2D1::Point2F(0.0f, 0.0f),                  // 偏移（0,0）即可
            static_cast<float>(g.radius.x),                 // rx
            static_cast<float>(g.radius.y)),                // ry（保持圆形）
        stopColl.Get(),
        &brush)))
        return;

    // 4. 计算填充区域
    const D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));

    // 5. 圆角判断
    auto& r = layer.border_radius;
    bool no_radius = r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;

    if (no_radius)
        rt->FillRectangle(rc, brush.Get());
    else
    {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            rc,
            static_cast<float>(r.top_left_x),
            static_cast<float>(r.top_left_y));
        rt->FillRoundedRectangle(rr, brush.Get());
    }
}


// 角度归一化到
static inline float normalize_angle(float a)
{
    a = fmodf(a, 2.0f * float(M_PI));
    return a < 0 ? a + 2.0f * float(M_PI) : a;
}

// 在色标数组中按角度(0..1) 线性插值颜色
static litehtml::web_color sample_color(float t,
    const std::vector<litehtml::background_layer::color_point>& stops)
{
    if (stops.empty()) return {};
    if (stops.size() == 1) return stops.front().color;

    // 保证色标有序
    auto cmp = [](const litehtml::background_layer::color_point& a,
        const litehtml::background_layer::color_point& b)
        { return a.offset < b.offset; };
    if (!std::is_sorted(stops.begin(), stops.end(), cmp))
    {
        std::vector<litehtml::background_layer::color_point> tmp = stops;
        std::sort(tmp.begin(), tmp.end(), cmp);
        return sample_color(t, tmp);
    }

    // 找到区间
    auto it = std::lower_bound(stops.begin(), stops.end(), t,
        [](const litehtml::background_layer::color_point& s, float v)
        { return s.offset < v; });

    if (it == stops.end()) return stops.back().color;
    if (it == stops.begin()) return stops.front().color;

    const auto& prev = *(it - 1);
    const auto& next = *it;
    float factor = (t - prev.offset) / (next.offset - prev.offset);
    factor = std::clamp(factor, 0.0f, 1.0f);

    litehtml::web_color c;
    c.red = static_cast<BYTE>(prev.color.red + (next.color.red - prev.color.red) * factor);
    c.green = static_cast<BYTE>(prev.color.green + (next.color.green - prev.color.green) * factor);
    c.blue = static_cast<BYTE>(prev.color.blue + (next.color.blue - prev.color.blue) * factor);
    c.alpha = static_cast<BYTE>(prev.color.alpha + (next.color.alpha - prev.color.alpha) * factor);
    return c;
}

void SimpleContainer::draw_conic_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::background_layer::conic_gradient& g)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 计算填充矩形
    const D2D1_RECT_F rc = D2D1::RectF(
        static_cast<float>(layer.border_box.left()),
        static_cast<float>(layer.border_box.top()),
        static_cast<float>(layer.border_box.right()),
        static_cast<float>(layer.border_box.bottom()));

    const float w = rc.right - rc.left;
    const float h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    // 2. 生成位图大小（固定 512×512，可改）
    const UINT bmpSize = 512;
    const UINT stride = bmpSize * 4;
    std::vector<BYTE> pixels(bmpSize * bmpSize * 4, 0); // BGRA

    // 3. 逐像素填色
    for (UINT y = 0; y < bmpSize; ++y)
    {
        for (UINT x = 0; x < bmpSize; ++x)
        {
            // 归一化到 [-1,1]
            float nx = (x / float(bmpSize - 1)) * 2.0f - 1.0f;
            float ny = (y / float(bmpSize - 1)) * 2.0f - 1.0f;

            float angle = atan2f(ny, nx);          // -π..π
            angle += float(M_PI);                  // 0..2π
            angle = normalize_angle(angle + g.angle); // 支持全局旋转
            float t = angle / (2.0f * float(M_PI));   // 0..1

            litehtml::web_color c = sample_color(t, g.color_points);

            UINT idx = (y * bmpSize + x) * 4;
            pixels[idx + 0] = c.blue;
            pixels[idx + 1] = c.green;
            pixels[idx + 2] = c.red;
            pixels[idx + 3] = c.alpha;
        }
    }

    // 4. 创建 D2D 位图
    ComPtr<ID2D1Bitmap> bmp;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(rt->CreateBitmap(
        D2D1::SizeU(bmpSize, bmpSize),
        pixels.data(),
        stride,
        props,
        &bmp)))
        return;

    // 5. 圆角判断
    auto& r = layer.border_radius;
    bool no_radius = r.top_left_x == 0 && r.top_left_y == 0 &&
        r.top_right_x == 0 && r.top_right_y == 0 &&
        r.bottom_right_x == 0 && r.bottom_right_y == 0 &&
        r.bottom_left_x == 0 && r.bottom_left_y == 0;

    // 6. 绘制
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (no_radius)
    {
        rt->DrawBitmap(bmp.Get(), rc);
    }
    else
    {
        // 用圆角矩形裁剪
        ComPtr<ID2D1Layer> layerPtr;
        rt->CreateLayer(nullptr, &layerPtr);
        rt->PushLayer(
            D2D1::LayerParameters(
                rc,
                nullptr,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(),
                1.0f,
                nullptr,
                D2D1_LAYER_OPTIONS_NONE),
            layerPtr.Get());

        rt->DrawBitmap(bmp.Get(), rc);

        rt->PopLayer();
    }
}

void SimpleContainer::draw_list_marker(
    litehtml::uint_ptr hdc,
    const litehtml::list_marker& marker)
{
    ID2D1RenderTarget* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    if (!rt) return;

    // 1. 基础信息
    const float x = static_cast<float>(marker.pos.x);
    const float y = static_cast<float>(marker.pos.y);
    const float sz = static_cast<float>(marker.pos.width);
    const D2D1_COLOR_F color = D2D1::ColorF(
        marker.color.red / 255.0f,
        marker.color.green / 255.0f,
        marker.color.blue / 255.0f,
        marker.color.alpha / 255.0f);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(color, &brush)))
        return;

    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    switch (marker.marker_type)
    {
    case litehtml::list_style_type_disc:
    {
        rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(x + sz * 0.5f, y + sz * 0.5f), sz * 0.5f, sz * 0.5f),
            brush.Get());
    }
    break;

    case litehtml::list_style_type_circle:
    {
        rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(x + sz * 0.5f, y + sz * 0.5f), sz * 0.5f, sz * 0.5f),
            brush.Get(),
            sz * 0.1f); // 线宽
    }
    break;

    case litehtml::list_style_type_square:
    {
        const D2D1_RECT_F rc = D2D1::RectF(x, y, x + sz, y + sz);
        rt->FillRectangle(rc, brush.Get());
    }
    break;

    default:
        // 其他类型（decimal、lower-alpha 等）由文本层绘制，这里忽略
        break;
    }
}


namespace {

    struct SideInfo {
        float          width = 0;
        litehtml::web_color color{};
        litehtml::border_style style = litehtml::border_style_solid;
    };

    // 根据 style 计算明暗色
    D2D1_COLOR_F AdjustColor(const litehtml::web_color& c, float factor)
    {
        return D2D1::ColorF(
            std::clamp(c.red * factor / 255.0f, 0.0f, 1.0f),
            std::clamp(c.green * factor / 255.0f, 0.0f, 1.0f),
            std::clamp(c.blue * factor / 255.0f, 0.0f, 1.0f),
            c.alpha / 255.0f);
    }

} // namespace

void SimpleContainer::draw_borders(litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& draw_pos,
    bool root)
{
    if (!hdc) return;
    auto* rt = reinterpret_cast<ID2D1RenderTarget*>(hdc);
    // 1. 收集四边
    std::array<SideInfo, 4> sides = {
        SideInfo{ (float)borders.top.width,    borders.top.color,    borders.top.style },
        SideInfo{ (float)borders.right.width,  borders.right.color,  borders.right.style },
        SideInfo{ (float)borders.bottom.width, borders.bottom.color, borders.bottom.style },
        SideInfo{ (float)borders.left.width,   borders.left.color,   borders.left.style }
    };

    if (std::all_of(sides.begin(), sides.end(),
        [](const SideInfo& s) { return s.width <= 0; }))
        return;

    // 2. 建立工厂
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);

    // 3. 构造外轮廓
    auto build_rounded_rect = [&](float l, float t, float r, float b,
        const litehtml::border_radiuses& rad,
        ComPtr<ID2D1PathGeometry>& out) -> bool
        {
            ComPtr<ID2D1PathGeometry> geo;
            if (FAILED(factory->CreatePathGeometry(&geo))) return false;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(geo->Open(&sink))) return false;

            float rtl = (float)rad.top_left_x, rtr = (float)rad.top_right_x;
            float rbr = (float)rad.bottom_right_x, rbl = (float)rad.bottom_left_x;

            sink->BeginFigure(D2D1::Point2F(l + rtl, t), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(r - rtr, t));
            if (rbr > 0) sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(r - rbr, b),
                D2D1::SizeF(rbr, rbr),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_SMALL));

            if (rbl > 0) sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(l, b - rbl),
                D2D1::SizeF(rbl, rbl),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_SMALL));

            if (rtl > 0) sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(l + rtl, t),
                D2D1::SizeF(rtl, rtl),
                0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE,
                D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            out = std::move(geo);
            return true;
        };

    float l = (float)draw_pos.left();
    float t = (float)draw_pos.top();
    float r = (float)draw_pos.right();
    float b = (float)draw_pos.bottom();

    ComPtr<ID2D1PathGeometry> outer, inner;
    if (!build_rounded_rect(l, t, r, b, borders.radius, outer)) return;

    // 4. 内轮廓（统一用最小边宽）
    float minW = std::min({ sides[0].width, sides[1].width, sides[2].width, sides[3].width });
    litehtml::border_radiuses innerRad = borders.radius;
    innerRad.top_left_x = std::max(innerRad.top_left_x - minW, 0.0f);
    innerRad.top_right_x = std::max(innerRad.top_right_x - minW, 0.0f);
    innerRad.bottom_right_x = std::max(innerRad.bottom_right_x - minW, 0.0f);
    innerRad.bottom_left_x = std::max(innerRad.bottom_left_x - minW, 0.0f);

    if (!build_rounded_rect(l + sides[3].width, t + sides[0].width,
        r - sides[1].width, b - sides[2].width,
        innerRad, inner)) return;

    // 5. 创建边框几何 = outer - inner
    ComPtr<ID2D1PathGeometry> borderGeo;
    factory->CreatePathGeometry(&borderGeo);
    ComPtr<ID2D1GeometrySink> sink;
    borderGeo->Open(&sink);
    outer->CombineWithGeometry(inner.Get(), D2D1_COMBINE_MODE_EXCLUDE,
        nullptr, sink.Get());
    sink->Close();

    // 6. 画四边（按顺序 top/right/bottom/left）
    const std::array<const char*, 4> sideNames = { "top","right","bottom","left" };
    const std::array<float, 4> offsets = { 0, 0, 0, 0 }; // 预留
    (void)offsets;

    // 6-a 纯色简单实现：先整体填充背景色，再描边
    // 这里为了演示，只画四条独立路径，实际可优化
    for (int idx = 0; idx < 4; ++idx)
    {
        const SideInfo& side = sides[idx];
        if (side.width <= 0) continue;

        ComPtr<ID2D1SolidColorBrush> brush;
        D2D1_COLOR_F clr;
        switch (side.style)
        {
        case litehtml::border_style_groove:
            clr = AdjustColor(side.color, 0.75f); break;
        case litehtml::border_style_ridge:
            clr = AdjustColor(side.color, 1.25f); break;
        case litehtml::border_style_inset:
            clr = AdjustColor(side.color, 0.60f); break;
        case litehtml::border_style_outset:
            clr = AdjustColor(side.color, 1.40f); break;
        default:
            clr = D2D1::ColorF(side.color.red / 255.0f,
                side.color.green / 255.0f,
                side.color.blue / 255.0f,
                side.color.alpha / 255.0f);
        }
        rt->CreateSolidColorBrush(clr, &brush);

        // 为每条边单独构造路径（略繁琐，但保证独立颜色）
        ComPtr<ID2D1PathGeometry> sidePath;
        factory->CreatePathGeometry(&sidePath);
        ComPtr<ID2D1GeometrySink> sideSink;
        sidePath->Open(&sideSink);

        switch (idx)
        {
        case 0: // top
            sideSink->BeginFigure(D2D1::Point2F(l + borders.radius.top_left_x, t), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(r - borders.radius.top_right_x, t));
            break;
        case 1: // right
            sideSink->BeginFigure(D2D1::Point2F(r, t + borders.radius.top_right_x), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(r, b - borders.radius.bottom_right_x));
            break;
        case 2: // bottom
            sideSink->BeginFigure(D2D1::Point2F(r - borders.radius.bottom_right_x, b), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(l + borders.radius.bottom_left_x, b));
            break;
        case 3: // left
            sideSink->BeginFigure(D2D1::Point2F(l, b - borders.radius.bottom_left_x), D2D1_FIGURE_BEGIN_HOLLOW);
            sideSink->AddLine(D2D1::Point2F(l, t + borders.radius.top_left_x));
            break;
        }
        sideSink->EndFigure(D2D1_FIGURE_END_OPEN);
        sideSink->Close();

        // 描边
        rt->DrawGeometry(sidePath.Get(), brush.Get(), side.width);
    }
}
// 工具：转小写
std::wstring  SimpleContainer::toLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}



std::vector<std::wstring>
SimpleContainer::split_font_list(const std::string& src) {
    std::vector<std::wstring> out;
    std::string token;
    for (size_t i = 0, n = src.size(); i < n; ++i)
    {
        if (src[i] == ',')
        {
            token = trim_any(token);
            if (!token.empty()) {
                std::wstring face = a2w(token);
                out.emplace_back(face);
                token.clear();
            }
        }
        else
        {
            token += src[i];
        }
    }
    token = trim_any(token);
    if (!token.empty())
    {
        std::wstring face = a2w(token);
        out.emplace_back(face);
    }
    return out;
};


litehtml::uint_ptr SimpleContainer::create_font(const litehtml::font_description& descr,
    const litehtml::document* doc,
    litehtml::font_metrics* fm)
{

    if (!m_dwrite || !fm) return 0;

    /*----------------------------------------------------------
      1. 把 font-family 字符串拆成单个字体名
    ----------------------------------------------------------*/

    std::vector<std::wstring> faces = split_font_list(
        descr.family.empty() ? g_cfg.default_font_name : descr.family);

    // 默认字体兜底
    faces.push_back(a2w(g_cfg.default_font_name));

    FontCachePair fcp;
    for (auto f : faces)
    {
        fcp = m_fontCache.get(f, descr, m_sysFontColl.Get());
        fcp.fmt;
        if (fcp.fmt) break;
        else
        {
            std::wstring txt = L"[DWrite] 加载字体失败： " + f + L"\n";
            OutputDebugStringW(txt.c_str());
        }
    }


    if (!fcp.fmt) {
        OutputDebugStringW(L"[DWrite] 加载默认字体失败\n");
        return 0;
    }

    *fm = fcp.fm;
    g_line_height = fm->height;
    FontPair* fp = new FontPair{ fcp.fmt, descr };

    return reinterpret_cast<litehtml::uint_ptr>(fp);
}

void SimpleContainer::delete_font(litehtml::uint_ptr h)
{
    if (!h) return;
    //auto* fp = reinterpret_cast<FontPair*>(h);
    //delete fp;              // 4. 真正释放
}

litehtml::pixel_t SimpleContainer::text_width(const char* text,
    litehtml::uint_ptr hFont)
{
    if (!text || !*text || !hFont) return 0;

    FontPair* fp = reinterpret_cast<FontPair*>(hFont);
    if (!fp || !fp->format) return 0;

    std::wstring wtxt = a2w(text);
    if (wtxt.empty()) return 0;

    // 1. 创建 TextLayout
 
    float maxW = 8192.0f;
    auto layout = getLayout(wtxt, fp, maxW);
    if (!layout) { return 0; }
    // 3. 取逻辑宽度（已含空白、连字、kerning）
    DWRITE_TEXT_METRICS tm{};
    HRESULT hr = layout->GetMetrics(&tm);
    if (FAILED(hr)) { return 0; }

    // 4. DPI → 物理像素（Win7 也支持）
    float dpi = 96.0f;               // 若你有当前 DPI，替换之
    float physical = tm.widthIncludingTrailingWhitespace * dpi / 96.0f;

    return physical;
}


void SimpleContainer::build_rounded_rect_path(
    ComPtr<ID2D1GeometrySink>& sink,
    const litehtml::position& pos,
    const litehtml::border_radiuses& bdr)
{
    float l = float(pos.left()), t = float(pos.top());
    float r = float(pos.right()), b = float(pos.bottom());

    float rtl = float(bdr.top_left_x);
    float rtr = float(bdr.top_right_x);
    float rbr = float(bdr.bottom_right_x);
    float rbl = float(bdr.bottom_left_x);

    sink->BeginFigure(D2D1::Point2F(l + rtl, t), D2D1_FIGURE_BEGIN_FILLED);

    // top edge
    sink->AddLine(D2D1::Point2F(r - rtr, t));
    if (rtr > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(r, t + rtr), D2D1::SizeF(rtr, rtr),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    // right edge
    sink->AddLine(D2D1::Point2F(r, b - rbr));
    if (rbr > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(r - rbr, b), D2D1::SizeF(rbr, rbr),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    // bottom edge
    sink->AddLine(D2D1::Point2F(l + rbl, b));
    if (rbl > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(l, b - rbl), D2D1::SizeF(rbl, rbl),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    // left edge
    sink->AddLine(D2D1::Point2F(l, t + rtl));
    if (rtl > 0) sink->AddArc(D2D1::ArcSegment(
        D2D1::Point2F(l + rtl, t), D2D1::SizeF(rtl, rtl),
        0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

void SimpleContainer::set_clip(const litehtml::position& pos,
    const litehtml::border_radiuses& bdr)
{
    if (!m_rt) return;

    // 无圆角 → 矩形裁剪
    if (is_all_zero(bdr))
    {
        m_rt->PushAxisAlignedClip(
            D2D1::RectF(float(pos.left()), float(pos.top()),
                float(pos.right()), float(pos.bottom())),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_clipStack.emplace_back(nullptr);          // 标记为矩形
        return;
    }

    // 有圆角 → 用 PathGeometry + Layer
    ComPtr<ID2D1Factory> factory;
    m_rt->GetFactory(&factory);

    ComPtr<ID2D1PathGeometry> path;
    factory->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    build_rounded_rect_path(sink, pos, bdr);        // 见下
    sink->Close();

    ComPtr<ID2D1Layer> layer;
    if (SUCCEEDED(m_rt->CreateLayer(nullptr, &layer)))
    {
        m_rt->PushLayer(
            D2D1::LayerParameters(D2D1::InfiniteRect(), path.Get()),
            layer.Get());
        m_clipStack.emplace_back(std::move(layer));
    }
}

void SimpleContainer::del_clip()
{
    if (m_clipStack.empty()) return;
    if (m_clipStack.back())
        m_rt->PopLayer();           // 圆角
    else
        m_rt->PopAxisAlignedClip(); // 矩形
    m_clipStack.pop_back();
}








void EPUBBook::load_all_fonts() {

        //auto fonts = collect_epub_fonts();
        FontKey key{ L"serif", 400, false, 0 };
        m_fontBin[key] = { g_cfg.default_serif };
        key = { L"sans-serif", 400, false, 0 };
        m_fontBin[key] = { g_cfg.default_sans_serif };
        key = { L"monospace", 400, false, 0 };
        m_fontBin[key] = { g_cfg.default_monospace };
        build_epub_font_index();

 
}









//------------------------------------------
// 公共辅助：从 EPUB 提取字体 blob
//------------------------------------------

static std::wstring make_safe_filename(std::wstring_view src)
{
    // 1. 去掉路径，只保留纯文件名
    size_t last = src.find_last_of(L"/\\");
    std::wstring name = (last == std::wstring::npos)
        ? std::wstring{ src }
    : std::wstring{ src.substr(last + 1) };

    // 2. 去掉前后空格/句点
    const std::wregex trim_re(L"^[ \\.]+|[ \\.]+$");
    name = std::regex_replace(name, trim_re, L"");

    // 3. 替换非法字符为单个下划线
    const std::wregex illegal_re(L"[<>:\"/\\\\|?*\\x00-\\x1F]+");
    name = std::regex_replace(name, illegal_re, L"_");

    // 4. 处理 Windows 保留设备名
    const std::wregex reserved_re(
        L"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$",
        std::regex_constants::icase);
    if (std::regex_match(name, reserved_re))
        name = L"_" + name;

    // 5. 空文件名兜底
    if (name.empty())
        name = L"file";

    // 6. 拼上序号
    return  name;
}


namespace fs = std::filesystem;

// 大小写不敏感的 set / map 比较器
struct CaseInsensitiveLess
{
    bool operator()(const std::wstring& a, const std::wstring& b) const
    {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

inline int hex_to_int(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return 0;   // 非法字符按 0 处理
}
// 简单 URL decode（仅处理 %20 等）
std::wstring url_decode(const std::wstring& in)
{
    std::wstring out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == L'%' && i + 2 < in.size())
        {
            int hi = hex_to_int(in[i + 1]);
            int lo = hex_to_int(in[i + 2]);
            out.push_back(static_cast<wchar_t>((hi << 4) | lo));
            i += 2;
        }
        else
            out.push_back(in[i]);
    }
    return out;
}





void EPUBBook::build_epub_font_index()
{

    // 1. 创建临时目录
    static std::wstring tempDir = make_temp_dir();

    // 2. 正则
    const std::wregex rx_face(LR"(@font-face\s*\{([^}]*)\})", std::regex::icase);
    const std::wregex rx_fam(LR"(font-family\s*:\s*['"]?([^;'"}]+)['"]?)", std::regex::icase);
    const std::wregex rx_url(LR"(url\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
    const std::wregex rx_loc(LR"(local\s*\(\s*['"]?([^)'"]+)['"]?\s*\))", std::regex::icase);
    const std::wregex rx_w(LR"(font-weight\s*:\s*(\d+|bold))", std::regex::icase);
    const std::wregex rx_i(LR"(font-style\s*:\s*(italic|oblique))", std::regex::icase);

    // 3. 遍历所有 CSS
    for (const auto& item : ocf_pkg_.manifest)
    {
        if (item.media_type != L"text/css") continue;

        std::wstring cssPath = ocf_pkg_.opf_dir + item.href;
        MemFile cssFile = read_zip(m_zipIndex.find(cssPath).c_str());
        if (cssFile.data.empty()) continue;
       
        std::wstring css = a2w({ (char*)cssFile.data.data(), cssFile.data.size() });

        for (std::wsregex_iterator it(css.begin(), css.end(), rx_face), end; it != end; ++it)
        {
            std::wstring block = it->str();
            std::wsmatch m;

            std::wstring family;
            std::vector<std::wstring> paths;   // 可能多个 src
            int weight = 400;
            bool italic = false;

            // family
            if (std::regex_search(block, m, rx_fam)) family = m[1];

            // weight / style
            if (std::regex_search(block, m, rx_w))
                weight = (m[1] == L"bold" || m[1] == L"700") ? 700 : std::stoi(m[1]);
            if (std::regex_search(block, m, rx_i)) italic = true;

            // 解析 src 中所有 url(...) 和 local(...)
            for (std::wsregex_iterator srcIt(block.begin(), block.end(), rx_url), srcEnd; srcIt != srcEnd; ++srcIt)
            {
                std::wstring url = (*srcIt)[1];

                // 跳过网络字体
                if (url.starts_with(L"http://") || url.starts_with(L"https://"))
                    continue;

                // 去掉 query/fragment
                if (auto pos = url.find(L'?'); pos != std::wstring::npos) url.erase(pos);
                if (auto pos = url.find(L'#'); pos != std::wstring::npos) url.erase(pos);

                // 保留扩展名
                std::wstring ext = L".ttf";
                if (auto dot = url.rfind(L'.'); dot != std::wstring::npos)
                {
                    ext = url.substr(dot);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    static const std::unordered_set<std::wstring> ok{ L".ttf", L".otf", L".woff", L".woff2", L".ttc" };
                    if (!ok.contains(ext)) ext = L".ttf";
                }

                // 解压
                std::wstring fontPath = ocf_pkg_.opf_dir + url;
                MemFile fontFile = read_zip(m_zipIndex.find(fontPath).c_str());
                if (fontFile.data.empty()) continue;

                std::wstring hashHex = blake3_hex(fontFile.data);   // 32 字节 → 64 字符
                std::wstring tempFont = tempDir + hashHex + ext;    // 例如：a1b2c3...ff.woff2
                // 2. 如果文件已存在，直接记录路径，不再写盘
                if (GetFileAttributesW(tempFont.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    paths.push_back(tempFont);   // 已缓存
                    continue;
                }
          
                HANDLE h = CreateFileW(tempFont.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                DWORD written = 0;
                WriteFile(h, fontFile.data.data(), (DWORD)fontFile.data.size(), &written, nullptr);
                CloseHandle(h);

                paths.push_back(tempFont);
            }

            // local(...)
            for (std::wsregex_iterator locIt(block.begin(), block.end(), rx_loc), locEnd; locEnd != locIt; ++locIt)
            {
                paths.push_back((*locIt)[1]);
            }

            if (family.empty() || paths.empty()) continue;

            FontKey key{ family, weight, italic, 0 };
            m_fontBin[key] = std::move(paths);
        }
    }
}




std::vector<std::pair<std::wstring, std::vector<uint8_t>>>
EPUBBook::collect_epub_fonts()
{
    std::vector<std::pair<std::wstring, std::vector<uint8_t>>> fonts;
    const std::set<std::wstring, CaseInsensitiveLess> font_exts = {
        L".ttf", L".otf", L".woff", L".woff2"
    };
    std::unordered_set<std::wstring> used_names;

    for (const auto& item : g_book->ocf_pkg_.manifest)
    {
        std::wstring href = item.href;
        std::wstring ext;               // 最终扩展名，带前导点
        size_t lastDot = href.find_last_of(L'.');
        if (lastDot != std::wstring::npos && lastDot + 1 < href.size())
        {
            ext = href.substr(lastDot);   // 包含点
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        }
        else
        {
            continue;   // 没有扩展名，跳过
        }
        if (!font_exts.count(ext)) continue;

        std::wstring href_key = url_decode(item.href);
        std::wstring wpath = g_book->m_zipIndex.find(href_key);
        if (wpath.empty()) continue;

        // 生成唯一文件名（不再用 index 参数）
        std::wstring base = href;
        std::wstring safe = make_safe_filename(base);   // 现在只接受一个参数
        std::wstring unique = safe;
        int counter = 1;
        while (used_names.count(unique))
        {
            fs::path tmp(unique);
            unique = tmp.stem().wstring() + L"_" + std::to_wstring(counter++) + tmp.extension().wstring();
        }
        used_names.insert(unique);

        try
        {
            MemFile mf = g_book->read_zip(wpath.c_str());
            if (!mf.data.empty())
                fonts.emplace_back(std::move(unique), std::move(mf.data));
        }
        catch (...) { /* log & skip */ }
    }
    return fonts;
}




// -------------------------------------------------
// 静态工厂
// -------------------------------------------------
HRESULT MemoryFontLoader::CreateCollection(
    IDWriteFactory* dwrite,
    const std::vector<std::pair<std::wstring, std::vector<uint8_t>>>& fonts,
    IDWriteFontCollection** out)
{
    if (!dwrite || !out) return E_INVALIDARG;

    // 1. 注册 loader（只注册一次）
    static bool registered = false;
    if (!registered)
    {
        Microsoft::WRL::ComPtr<MemoryFontLoader> stub(new MemoryFontLoader(nullptr, {}));
        HRESULT hr = dwrite->RegisterFontCollectionLoader(stub.Get());
        if (FAILED(hr)) return hr;
        registered = true;
    }

    // 2. 把 vector<blob> 打包成一块连续内存
    std::vector<std::vector<uint8_t>> blobs;
    blobs.reserve(fonts.size());
    for (const auto& [name, data] : fonts)
        blobs.emplace_back(data);

    // 3. 创建自定义集合
    return dwrite->CreateCustomFontCollection(
        static_cast<IDWriteFontCollectionLoader*>(nullptr),   // 用 key 区分
        blobs.data(),
        static_cast<UINT32>(blobs.size() * sizeof(blobs[0])),
        out);
}

// -------------------------------------------------
// IUnknown
// -------------------------------------------------
HRESULT MemoryFontLoader::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IDWriteFontCollectionLoader))
    {
        *ppv = static_cast<IDWriteFontCollectionLoader*>(this);
    }
    else if (riid == __uuidof(IDWriteFontFileEnumerator))
    {
        *ppv = static_cast<IDWriteFontFileEnumerator*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    return S_OK;
}

// -------------------------------------------------
// IDWriteFontCollectionLoader
// -------------------------------------------------
HRESULT MemoryFontLoader::CreateEnumeratorFromKey(
    IDWriteFactory* factory,
    const void* collectionKey, UINT32 collectionKeySize,
    IDWriteFontFileEnumerator** enumerator)
{
    if (!factory || !enumerator) return E_INVALIDARG;

    // collectionKey 指向 vector<vector<uint8_t>>
    const auto* blobs = reinterpret_cast<const std::vector<uint8_t>*>(collectionKey);
    size_t count = collectionKeySize / sizeof(std::vector<uint8_t>);
    if (!blobs || count == 0) return E_INVALIDARG;

    Microsoft::WRL::ComPtr<MemoryFontLoader> loader(
        new MemoryFontLoader(factory, std::vector<std::vector<uint8_t>>(blobs, blobs + count)));
    *enumerator = loader.Detach();
    return S_OK;
}

// -------------------------------------------------
// IDWriteFontFileEnumerator
// -------------------------------------------------
HRESULT MemoryFontLoader::MoveNext(BOOL* hasCurrentFile)
{
    if (!hasCurrentFile) return E_INVALIDARG;
    *hasCurrentFile = FALSE;

    if (idx_ < blobs_.size())
    {
        HRESULT hr = CreateInMemoryFontFile(factory_.Get(),
            blobs_[idx_].data(),
            static_cast<UINT32>(blobs_[idx_].size()),
            &current_);
        *hasCurrentFile = SUCCEEDED(hr);
        ++idx_;
        return hr;
    }
    return S_OK;
}

HRESULT MemoryFontLoader::GetCurrentFontFile(IDWriteFontFile** fontFile)
{
    if (!fontFile) return E_INVALIDARG;
    *fontFile = current_.Get();
    if (*fontFile) (*fontFile)->AddRef();
    return S_OK;
}




HRESULT MemoryFontLoader::CreateInMemoryFontFile(
    IDWriteFactory* factory,
    const void* data,
    UINT32 size,
    IDWriteFontFile** out)
{
    using Microsoft::WRL::MakeAndInitialize;

    Microsoft::WRL::ComPtr<InMemoryFontFileLoader> loader;
    HRESULT hr = MakeAndInitialize<InMemoryFontFileLoader>(&loader);
    if (FAILED(hr)) return hr;

    hr = factory->RegisterFontFileLoader(loader.Get());
    if (FAILED(hr) && hr != DWRITE_E_ALREADYREGISTERED) return hr;

    return loader->CreateInMemoryFontFileReference(
        factory,
        data,
        size,
        nullptr,
        out);
}



// 注册 Cambria Math（常规字重，非粗非斜）
bool lunasvgRegisterCambriaMath()
{

    wchar_t fontDir[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, SHGFP_TYPE_CURRENT, fontDir)))
        return false;

    wchar_t fullPath[MAX_PATH]{};
    PathCombineW(fullPath, fontDir, L"cambria.ttc");   // Cambria Math 在 .ttc 里

    char utf8Path[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_UTF8, 0, fullPath, -1, utf8Path, sizeof(utf8Path), nullptr, nullptr);

    // 把 Cambria Math 的 Regular face 注册为 "Cambria Math"
    return lunasvg_add_font_face_from_file("Cambria Math", false, false, utf8Path);

}

void BuildSplashWithText()
{
    // 1. 读源图
    fs::path szPath = exe_dir() / "res" / "splash.png";
    std::unique_ptr<Gdiplus::Image> src(
        Gdiplus::Image::FromFile(szPath.generic_wstring().c_str(), FALSE));
    if (!src || src->GetLastStatus() != Gdiplus::Ok)
        return;                     // 文件缺失直接退出函数

    int srcW = src->GetWidth();
    int srcH = src->GetHeight();

    // 2. 新建 32bpp 位图，用于离线绘制
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(srcW, srcH, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(bmp);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    // 3. 先画原图
    g.DrawImage(src.get(), 0, 0, srcW, srcH);

    // 4. 生成阅读时长文本
    if (g_recorder)
    {
        int64_t sec = g_recorder->getTotalTime();
        int64_t days = sec / 86400;
        int64_t hours = (sec % 86400) / 3600;
        int64_t minutes = (sec % 3600) / 60;
        int64_t seconds = sec % 60;

        std::wstring timeStr;
        if (days)    timeStr += std::to_wstring(days) + L"天 ";
        if (hours || days)   timeStr += std::to_wstring(hours) + L"时";
        if (minutes || hours || days) timeStr += std::to_wstring(minutes) + L"分";
        timeStr += std::to_wstring(seconds) + L"秒";

        std::wstring label = L"已阅读时长：";
        std::wstring full = label + timeStr;

        // 5. 字体 & 画刷
        Gdiplus::FontFamily fontFamily(L"SimSun");
        Gdiplus::Font labelFont(&fontFamily, 36, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::Font timeFont(&fontFamily, 36, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush grayBrush(Gdiplus::Color(0, 0, 0));

        // 6. 测量 & 居中偏下
        Gdiplus::RectF fullBound;
        g.MeasureString(full.c_str(), -1, &labelFont, Gdiplus::PointF(0, 0), &fullBound);
        float textX = (srcW - fullBound.Width) / 2.0f;
        float textY = srcH * 0.85f - fullBound.Height;

        // 7. 绘制
        Gdiplus::PointF origin(textX, textY);
        g.DrawString(label.c_str(), -1, &labelFont, origin, &grayBrush);

        Gdiplus::RectF labelBound;
        g.MeasureString(label.c_str(), -1, &labelFont, origin, &labelBound);
        Gdiplus::PointF timeOrigin(textX + labelBound.Width, textY);
        g.DrawString(timeStr.c_str(), -1, &timeFont, timeOrigin, &grayBrush);
    }

    // 8. 替换全局指针
    delete g_pSplashImg;   // 释放旧图（如果有）
    g_pSplashImg = bmp;    // 以后直接用它
}

AppBootstrap::AppBootstrap() {
    //make_tooltip_backend();
    if (lunasvgRegisterCambriaMath()) { OutputDebugStringW(L"[lunasvg] 注册字体成功： Cambria Math\n"); }
    if (g_cfg.enableJS) { enableJS(); }

    if (!g_book){ g_book = std::make_unique<EPUBBook>(); }

    if (!g_vd){g_vd = std::make_unique<VirtualDoc>();}

    if(!g_recorder){ g_recorder = std::make_unique<ReadingRecorder>(); }

    if (!g_toc) 
    { 
        g_toc = std::make_unique<TocPanel>(); 
        g_toc->GetWindow(g_hwndToc);
        // 绑定目录点击 -> 章节跳转
        g_toc->SetOnNavigate([](const std::wstring& href) {
            g_vd->OnTreeSelChanged(href.c_str());
            });
    }
    if (!g_cMain) { g_cMain = std::make_unique<SimpleContainer>(10, 10, g_hView); }

    if(!g_cTooltip){ g_cTooltip = std::make_unique<SimpleContainer>(10, 10, g_hTooltip); }

    if(!g_cImage){ g_cImage = std::make_unique<SimpleContainer>(10, 10, g_hImageview); }

    if(!g_scrollbar) 
    {
        g_scrollbar = std::make_unique<ScrollBarEx>();
        g_scrollbar->GetWindow(g_hViewScroll);
    }

    if(!g_cHome)
    {
        g_cHome = std::make_unique<SimpleContainer>(10, 10, g_hHomepage);
        fs::path html_path = exe_dir() / "res" / "homepage.html";
        auto html = read_file(html_path);
        if (html.empty()) { OutputDebugStringA("[AppBootstrap] html is null!"); return; }
        g_cHome->m_doc = litehtml::document::createFromString({ html.c_str(), litehtml::encoding::utf_8 }, g_cHome.get());
        if (!g_cHome->m_doc) { OutputDebugStringA("[AppBootstrap] g_cHome->m_doc is null!"); return; }
    }

    //BuildSplashWithText();
}

AppBootstrap::~AppBootstrap() {

}



litehtml::uint_ptr SimpleContainer::getContext() { return reinterpret_cast<litehtml::uint_ptr>(m_rt.Get()); }



void SimpleContainer::resize(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    m_w = w;
    m_h = h;

    if (m_rt) {
        D2D1_SIZE_U size{ static_cast<UINT32>(w), static_cast<UINT32>(h) };
        if (SUCCEEDED(m_rt->Resize(size))) return;   // DPI 不变时直接 Resize
        m_rt.Reset();                               // 失败就重建
    }

    /* 重新创建 HwndRenderTarget */
    RECT rc; GetClientRect(m_hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT);

    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp =
        D2D1::HwndRenderTargetProperties(
            m_hwnd,
            D2D1::SizeU(rc.right, rc.bottom),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY);

    m_d2dFactory->CreateHwndRenderTarget(&rtp, &hrtp, &m_rt);
}







void AppBootstrap::enableJS()
{
    //if (!m_jsrt) m_jsrt = std::make_unique<js_runtime>(g_doc.get());
    //if (!m_jsrt->switch_engine("duktape"))
    //    OutputDebugStringA("[Duktape] Duktape init failed\n");
    //else {
    //    OutputDebugStringA("[Duktape] Duktape init OK\n");
    //    m_jsrt->set_logger(OutputDebugStringA);
    //    m_jsrt->eval("console.log('hello from duktape\n');");
    //}
}

void AppBootstrap::disableJS()
{
    //m_jsrt.reset();   // 直接销毁即可，js_runtime 会负责 shutdown
}

void AppBootstrap::run_pending_scripts()
{
    //    if (!m_jsrt) return;          // 没有 JS 引擎就跳过
    //    for (const auto& script : m_pending_scripts)
    //    {
    //        litehtml::string code;
    //        script.el->get_text(code);  // 取出 <script> 里的纯文本
    //        if (!code.empty())
    //            m_jsrt->eval(code, "<script>");  // 交给 QuickJS / Duktape / V8
    //    }
    //    m_pending_scripts.clear();    // 执行完清空
}

void AppBootstrap::bind_host_objects()
{
    //if (!m_jsrt) return;
   // m_jsrt->bind_document(g_doc.get());   // js_runtime 内部会转发到当前引擎
}





/* ---------- 实现 ---------- */

std::string EPUBBook::extract_anchor(const char* href)
{
    if (!href) return "";
    const char* p = std::strrchr(href, '#');
    return p ? (p + 1) : "";

}
litehtml::element::ptr EPUBBook::find_link_in_chain(litehtml::element::ptr start)
{
    for (auto cur = start; cur; cur = cur->parent())
    {
        const char* tag = cur->get_tagName();
        if (std::strcmp(tag, "p") == 0) break;
        if (std::strcmp(tag, "a") == 0) return cur;
    }
    return nullptr;
}

 bool EPUBBook::skip_attr(const std::string& val)
{
    if (val.empty()) return true;
    if (val == "0")  return true;
    return std::all_of(val.begin(), val.end(),
        [](unsigned char c) { return std::isspace(c); });
}

 std::string EPUBBook::get_html(litehtml::element::ptr el)
{
    if (!el) return "";
    std::string out;
    out += "<";
    out += el->get_tagName();

    // 常见属性名，按需增删
    static const char* attr_names[] = {
        "id","class","style","title","alt","href","src","type","name","value"
    };

    for (const char* name : attr_names)
    {
        const char* val = el->get_attr(name);
        if (val && !skip_attr(val))
        {
            out += " ";
            out += name;
            out += "=\"";
            out += val;
            out += "\"";
        }
    }

    out += ">";

    for (auto child : el->children())
    {
        if (child->is_text())
        {
            std::string txt;
            child->get_text(txt);
            out += txt;
        }
        else
        {
            out += get_html(child);
        }
    }

    out += "</";
    out += el->get_tagName();
    out += ">";
    return out;
}


 // 从一段 HTML 中提取第一个 <img> 的 outerHTML；没有则返回空串
 static std::string extract_first_img(const std::string& html)
 {
     GumboOutput* out = gumbo_parse(html.c_str());
     std::string result;

     // 深度优先找 <img>
     std::function<void(const GumboNode*)> dfs = [&](const GumboNode* node)
         {
             if (!result.empty()) return;           // 已找到
             if (node->type != GUMBO_NODE_ELEMENT) return;

             const GumboElement& elem = node->v.element;
             if (elem.tag == GUMBO_TAG_IMG)
             {
                 gumbo_serialize(node, result);
                 return;
             }
             for (unsigned int i = 0; i < elem.children.length; ++i)
                 dfs(static_cast<GumboNode*>(elem.children.data[i]));
         };
     dfs(out->root);

     gumbo_destroy_output(&kGumboDefaultOptions, out);
     return result;
 }
// 3. 核心函数：用 select_one 找 id，再向上找 <p>
std::string EPUBBook::html_of_anchor_paragraph(litehtml::document* doc, const std::string& anchorId)
{
    if (anchorId.empty()) return "";
    // 构造 CSS 选择器
    std::string sel = "[id=\"" + anchorId + "\"]";
    //std::string sel = "#" + anchorId;
    auto target = doc->root()->select_one(sel);
    if (!target) return "";

    // 向上找最近的 <p>
    auto p = target;
    while (p)
    {
        if (std::strcmp(p->get_tagName(), "figure") == 0) { break; }
        const char* cls = p->get_attr("class");
        if (!cls ||
            (std::strcmp(cls, "duokan-footnote-item") != 0 &&
                std::strcmp(cls, "fig") != 0 &&
                std::strcmp(cls, "figimage") != 0 &&
                std::strcmp(cls, "figure") != 0 &&
                std::strcmp(cls, "reflist") != 0))
        {
            p = p->parent();
        }
        else
        {
            break;          // 找到目标 class
        }
    }

    if (!p) return "";          // 兜底：直接返回自身

    std::string inner = get_html(p);

    // 用 gumbo 处理
    std::string imgOnly = extract_first_img(inner);
    if (!imgOnly.empty())
    {
        return "<style>img{display:block;width:100%;height:auto;}</style>" + imgOnly;
    }
    else
    {
        return "<style>img{display:block;width:100%;height:auto;}</style>" + inner;
    }
}


std::string EPUBBook::get_html_of_image(litehtml::element::ptr start)
{
    if (!start && std::strcmp(start->get_tagName(), "img") != 0 ) { return ""; }
    std::string inner = get_html(start);
    return "<style>img{display:block;width:100%;height:auto;}</style>" + inner;
   
}
void EPUBBook::show_imageview(const litehtml::element::ptr& el)
{
    std::string html = get_html_of_image(el);
    if (html.empty()) { return; }
    POINT pt;
    GetCursorPos(&pt);   // pt.x, pt.y 为屏幕坐标
    //ScreenToClient(g_hView, &pt);   // 现在 pt.x, pt.y 是相对于窗口客户区的坐标


    g_cImage->m_doc = litehtml::document::createFromString(
        { html.c_str(), litehtml::encoding::utf_8 }, g_cImage.get());
    int width = g_cfg.tooltip_width;
    g_cImage->m_doc->render(width);

    int height = g_cImage->m_doc->height();
    auto tip_x = pt.x - width/2;
    auto tip_y = pt.y - height/2;


 
    DWORD style = GetWindowLong(g_hImageview, GWL_STYLE);
    DWORD exStyle = GetWindowLong(g_hImageview, GWL_EXSTYLE);
    UINT dpi = GetDpiForWindow(g_hImageview);
    RECT r{ 0, 0, width, height };
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    g_cImage->resize(width, height);
    SetWindowPos(g_hImageview, HWND_TOPMOST,
        tip_x, tip_y,
        r.right - r.left, r.bottom - r.top,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);

    g_imageviewRenderW = width;

    g_states.isImageviewUpdate.store(true);
    InvalidateRect(g_hImageview, nullptr, true);
}
void EPUBBook::show_tooltip(const std::string txt)
{
    auto html = txt;
    //OutputDebugStringA(("[show_tooltip] " + std::to_string(x) + " " + std::to_string(y) + "\n").c_str());
    if (html.empty()) {  return; }

    //if (g_vd && !g_vd->m_blocks.empty())
    //{
    //    html = "<html>" + g_vd->m_blocks.back().head + "<body>" + html + "</body></html>";
    //}
    POINT pt;
    GetCursorPos(&pt);   




    g_cTooltip->m_doc = litehtml::document::createFromString(
        { html.c_str(), litehtml::encoding::utf_8 }, g_cTooltip.get());
    int width = g_cfg.tooltip_width;
    g_cTooltip->m_doc->render(width);
    int height = g_cTooltip->m_doc->height();


    int tip_x = pt.x - width / 2;
    int tip_y = pt.y - height - 20;
    if (tip_y < 0) { tip_y = pt.y + 20; }
    DWORD style = GetWindowLong(g_hTooltip, GWL_STYLE);
    DWORD exStyle = GetWindowLong(g_hTooltip, GWL_EXSTYLE);
    UINT dpi = GetDpiForWindow(g_hTooltip);
    RECT r{ 0, 0, width, height };
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    g_cTooltip->resize(width, height);
    SetWindowPos(g_hTooltip, HWND_TOPMOST,
        tip_x, tip_y,
        r.right - r.left, r.bottom - r.top,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);


    g_states.isTooltipUpdate.store(true);
    InvalidateRect(g_hTooltip, nullptr, true);

}
void EPUBBook::hide_imageview()
{

    if (g_hImageview && IsWindowVisible(g_hImageview))
    {
        ShowWindow(g_hImageview, SW_HIDE);
        if (g_cImage && g_cImage->m_doc)
        {
            g_cImage->m_doc.reset();
        }

    }
}
void EPUBBook::hide_tooltip()
{

    if (g_hTooltip  && IsWindowVisible(g_hTooltip) )
    {
        ShowWindow(g_hTooltip, SW_HIDE);
        if (g_cTooltip && g_cTooltip->m_doc)
        {
            g_cTooltip->m_doc.reset();
        }

    }
}



// 把单个 element 序列化成 HTML
static void element_to_html(const litehtml::element::ptr& el,
    std::string& out)
{
    if (!el) return;

    if (el->is_text())
    {
        // 文本节点
        std::string txt;
        el->get_text(txt);
        out += txt;
        return;
    }

    // 开始标签
    out += "<";
    out += el->get_tagName();

    // 属性
    auto attrs = el->dump_get_attrs();
    for (const auto& [name, val] : attrs)
    {
        out += " " + name + "=\"" + val + "\"";
    }

    if (el->children().empty())
    {
        // 自闭合
        out += " />";
    }
    else
    {
        out += ">";

        // 子节点
        for (const auto& child : el->children())
            element_to_html(child, out);

        // 结束标签
        out += "</";
        out += el->get_tagName();
        out += ">";
    }
}

// 根据锚点 id 返回对应元素的 HTML
std::string EPUBBook::get_anchor_html(litehtml::document* doc,
    const std::string& anchor)
{
    if (!doc || anchor.empty()) return {};

    litehtml::element::ptr root = doc->root();
    if (!root) return {};

    litehtml::element::ptr el =
        root->select_one(("#" + anchor).c_str());
    if (!el) return {};

    std::string html;
    element_to_html(el, html);
    return html;
}














void EPUBBook::parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
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
            np.href = m_zipIndex.find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <navPoint>
        parse_ncx_points(pt->FirstChildElement("navPoint"), level + 1, opf_dir, out);
    }
}

void EPUBBook::parse_nav_list(tinyxml2::XMLElement* ol, int level,
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
            np.href = m_zipIndex.find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <ol>
        if (auto* sub = li->FirstChildElement("ol"))
            parse_nav_list(sub, level + 1, opf_dir, out);
    }
}

std::wstring EPUBBook::extract_text(const tinyxml2::XMLElement* a)
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



EPUBBook::~EPUBBook() {
    mz_zip_reader_end(&zip);

    //for (const auto& path : g_tempFontFiles)
    //{
    //    RemoveFontResourceExW(path.c_str(), FR_PRIVATE, 0);
    //    DeleteFileW(path.c_str());
    //}
    //g_tempFontFiles.clear();
}




SimpleContainer::SimpleContainer(int w, int h, HWND hwnd):
    m_w(w), m_h(h), m_hwnd(hwnd)
{

    /* 1) D2D 工厂（1.0 足够） */
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) {
        OutputDebugStringA("D2D1CreateFactory failed\n");
        return;
    }

    /* 2) 计算窗口 DPI 缩放 */
    float dpiX = 96.0f, dpiY = 96.0f;
    m_d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
    const float scale = dpiX / 96.0f;

    /* 3) 创建 HwndRenderTarget（直接画到窗口） */
    hr = m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED),
            dpiX, dpiY),
        D2D1::HwndRenderTargetProperties(
            m_hwnd,
            D2D1::SizeU(
                static_cast<UINT>(m_w * scale),
                static_cast<UINT>(m_h * scale))),
        &m_rt);
    if (FAILED(hr)) {
        OutputDebugStringA("CreateHwndRenderTarget failed\n");
        return;
    }
    /* 4) DirectWrite 工厂 */
    IDWriteFactory* pRaw = nullptr;
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&pRaw));   // OK
    m_dwrite.Attach(pRaw);   // 把裸指针交给 ComPtr 管理
    if (FAILED(hr)) {
        OutputDebugStringA("DWriteCreateFactory failed\n");
        return;
    }
    m_dwrite->CreateTextAnalyzer(&m_analyzer);
    /* 5) 系统字体集合 */
    hr = m_dwrite->GetSystemFontCollection(&m_sysFontColl, FALSE);
    if (FAILED(hr)) {
        OutputDebugStringA("GetSystemFontCollection failed\n");
    }

    init_dpi();
}

SimpleContainer::~SimpleContainer()
{
    clear();
}



ZipIndexW::ZipIndexW(mz_zip_archive& zip)
{
    build(zip);
}

std::wstring ZipIndexW::find(std::wstring href) const {
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


std::wstring ZipIndexW::url_decode(const std::wstring& in)
{
    wchar_t out[INTERNET_MAX_URL_LENGTH];
    DWORD len = INTERNET_MAX_URL_LENGTH;
    if (SUCCEEDED(UrlCanonicalizeW(in.c_str(), out, &len, URL_UNESCAPE)))
        return std::wstring(out, len);
    return in;
}

std::wstring ZipIndexW::normalize_key(std::wstring href)
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

void ZipIndexW::build(mz_zip_archive& zip) {
    mz_uint idx = 0;
    mz_zip_archive_file_stat st{};
    while (mz_zip_reader_file_stat(&zip, idx++, &st)) {
        // miniz 返回 UTF-8，转成 wstring
        std::wstring wpath = a2w(st.m_filename);
        std::wstring key = normalize_key(wpath);
        map_.emplace(std::move(key), std::move(wpath));
    }
}

void SimpleContainer::clear()
{

    m_img_cache.clear();

    m_anchor_map.clear();
    m_doc.reset();
    m_privateFonts.Reset();
    m_d2dBitmapCache.clear();
    m_clipStack.clear();
    m_fontCache.clear();
    m_layoutCache.clear();
    m_brushPool.clear();

}

litehtml::pixel_t SimpleContainer::get_default_font_size() const
{
    return g_cfg.default_font_size;
}
const char* SimpleContainer::get_default_font_name() const
{
    return g_cfg.default_font_name.c_str();
}

void EPUBBook::clear()
{



    mz_zip_reader_end(&zip);
    cache.clear();

    ocf_pkg_ = {};
    m_fontBin.clear();
    m_file_path = L"";
}









FontCache::FontCache() {
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dw.GetAddressOf()));   // 
    m_loader = new FileCollectionLoader();
    m_dw->RegisterFontCollectionLoader(m_loader);
}

/* ---------------------------------------------------------- */
FontCachePair
FontCache::get(std::wstring& familyName, const litehtml::font_description& descr,
     IDWriteFontCollection* sysColl) {
    // 1. 构造键
    FontKey key{
        
        std::wstring(familyName.begin(), familyName.end()),
        descr.weight,
        descr.style == litehtml::font_style_italic,
        descr.size, 
        
    };
   
    // 2. 读缓存
    {
        std::shared_lock sl(m_mtx);
        if (auto it = m_map.find(key); it != m_map.end())
            return it->second;
    }

    // 3. 未命中，创建并写入
    auto fcp = create(key, sysColl);
    {
        std::unique_lock ul(m_mtx);
        m_map[key] = fcp;          // 若并发重复，后写覆盖，无妨
    }
    return fcp;
}
 ComPtr<IDWriteFontCollection>
FontCache::CreatePrivateCollectionFromFile(IDWriteFactory* dw, const wchar_t* path)
{
     ComPtr<IDWriteFontFile> file;
     if (FAILED(dw->CreateFontFileReference(path, nullptr, &file)))
         return nullptr;
     BOOL isSupported = FALSE;
     DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
     DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
     UINT32  faceCount = 0;
     if (FAILED(file->Analyze(
                &isSupported,
                &fileType,
                &faceType,
                &faceCount)) || !isSupported || faceCount < 1)
     {
         return nullptr;
     }
     // 用系统自带的“文件集合加载器”
     ComPtr<IDWriteFontCollection> collection;
     IDWriteFontFile* files[] = { file.Get() };
     IDWriteFontFile* key[] = { file.Get() };
     if (FAILED(m_dw->CreateCustomFontCollection(
         m_loader,
         key,
         sizeof(key),
         &collection)))
         return nullptr;

     return collection;
}

 FontCachePair
     FontCache::create(const FontKey& key, IDWriteFontCollection* sysColl)
 {



     /* ---------- 1. 候选列表（路径优先） ---------- */
     std::vector<std::wstring_view> tryNames{ key.family };
     if (g_book)
     {
         FontKey exact{ key.family, key.weight, key.italic, 0 };
         if (auto it = g_book->m_fontBin.find(exact); it != g_book->m_fontBin.end())
             tryNames.insert(tryNames.end(), it->second.begin(), it->second.end());
         for (const auto& kv : g_book->m_fontBin)
             if (kv.first.family == key.family)
                 tryNames.insert(tryNames.end(), kv.second.begin(), kv.second.end());
     }

     /* ---------- 2. 工具：一次性生成 metrics ---------- */
     auto makeMetrics = [](IDWriteFont* font, float size) -> litehtml::font_metrics
         {
             DWRITE_FONT_METRICS m{};
             font->GetMetrics(&m);
             const float dip = size / static_cast<float>(m.designUnitsPerEm);
             litehtml::font_metrics fm{};
             fm.font_size = static_cast<litehtml::pixel_t>(size + 0.5f);
             fm.ascent = static_cast<litehtml::pixel_t>(m.ascent * dip + 0.5f);
             fm.descent = static_cast<litehtml::pixel_t>(m.descent * dip + 0.5f);
             fm.height = static_cast<litehtml::pixel_t>((m.ascent + m.descent + m.lineGap) * dip + 0.5f);
             fm.x_height = static_cast<litehtml::pixel_t>(m.xHeight * dip + 0.5f);
           
             fm.ch_width = fm.font_size * 3 / 5;
             fm.sub_shift = fm.super_shift = fm.x_height / 2;
  
             return fm;
         };

     /* ---------- 3. 路径字体（私有集合） ---------- */

     for (std::wstring_view name : tryNames)
     {
         if (name.find(L':') == std::wstring_view::npos) continue;
         auto& coll = collCache[name];
         if (!coll)
         {
             coll = CreatePrivateCollectionFromFile(m_dw.Get(), std::wstring(name).c_str());
             if(coll){ collCache.emplace(name, coll); }
         }
         if (!coll) continue;

         UINT32 familyCount = coll->GetFontFamilyCount();
         if (familyCount == 0) continue;
     
         ComPtr<IDWriteFontFamily> family;
         if (FAILED(coll->GetFontFamily(0, &family))) continue;

         ComPtr<IDWriteFont> font;
         if (FAILED(family->GetFirstMatchingFont(
             static_cast<DWRITE_FONT_WEIGHT>(key.weight),
             DWRITE_FONT_STRETCH_NORMAL,
             key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
             &font))) continue;

         wchar_t familyName[LF_FACESIZE]{};
         {
             ComPtr<IDWriteLocalizedStrings> names;
             if (SUCCEEDED(family->GetFamilyNames(&names)))
             {
                 UINT32 idx = 0, len = 0;
                 BOOL exists = FALSE;
                 names->FindLocaleName(L"en-us", &idx, &exists);
                 if (!exists) idx = 0;
                 names->GetStringLength(idx, &len);
                 if (len < LF_FACESIZE)
                     names->GetString(idx, familyName, len + 1);
             }
         }

         ComPtr<IDWriteTextFormat> fmt;
         if (SUCCEEDED(m_dw->CreateTextFormat(
             familyName[0] ? familyName : L"",
             coll.Get(),
             static_cast<DWRITE_FONT_WEIGHT>(key.weight),
             key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
             DWRITE_FONT_STRETCH_NORMAL,
             static_cast<float>(key.size), L"en-us", &fmt)))
         {
             FontCachePair result{ fmt, makeMetrics(font.Get(), static_cast<float>(key.size)) };
             return result;
         }
     }

     /* ---------- 4. 系统字体 ---------- */
     if (sysColl)
     {
         for (std::wstring_view name : tryNames)
         {
             UINT32 index = 0;
             BOOL exists = FALSE;
             sysColl->FindFamilyName(std::wstring(name).c_str(), &index, &exists);
             if (!exists) continue;

             ComPtr<IDWriteFontFamily> family;
             if (FAILED(sysColl->GetFontFamily(index, &family))) continue;

             ComPtr<IDWriteFont> font;
             if (FAILED(family->GetFirstMatchingFont(
                 static_cast<DWRITE_FONT_WEIGHT>(key.weight),
                 DWRITE_FONT_STRETCH_NORMAL,
                 key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                 &font))) continue;

             ComPtr<IDWriteTextFormat> fmt;
             if (SUCCEEDED(m_dw->CreateTextFormat(
                 std::wstring(name).c_str(), sysColl,
                 static_cast<DWRITE_FONT_WEIGHT>(key.weight),
                 key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                 DWRITE_FONT_STRETCH_NORMAL,
                 static_cast<float>(key.size), L"en-us", &fmt)))
             {
                 FontCachePair result{ fmt, makeMetrics(font.Get(), static_cast<float>(key.size)) };
                 return result;
             }
         }
     }

     /* ---------- 5. 失败 ---------- */
     return { nullptr, {} };
 }
/* ---------------------------------------------------------- */
//FontCachePair
//FontCache::create(const FontKey& key, IDWriteFontCollection* privateColl, IDWriteFontCollection* sysColl) {
//    // 候选家族列表：精确 → 仅 family → 默认
//    std::wstring tryName;
//    tryName = key.family;
//
//    // 若 g_book->m_fontBin 有映射，追加真实文件名
//    if (g_book) {
//        FontKey exact{ key.family, key.weight, key.italic, 0 }; // size 忽略
//        if (auto it = g_book->m_fontBin.find(exact); it != g_book->m_fontBin.end())
//            tryName = it->second;
//
//        // 退而求其次：仅 family
//        for (const auto& kv : g_book->m_fontBin)
//            if (kv.first.family == key.family) { tryName = kv.second; break; }
//    }
//
//
//    // 逐个尝试
//
//    for (auto coll : { privateColl, sysColl }) {   // 先私有，再系统
//        if (!coll) continue;
//        UINT32 index = 0;
//        Microsoft::WRL::ComPtr<IDWriteFontFamily> dwFamily;
//        if (!findFamily(coll, tryName, dwFamily, index)) continue;
//
//        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
//        if (SUCCEEDED(m_dw->CreateTextFormat(
//            tryName.c_str(), coll,
//            static_cast<DWRITE_FONT_WEIGHT>(key.weight),
//            key.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
//            DWRITE_FONT_STRETCH_NORMAL,
//            static_cast<float>(key.size),
//            L"en-us",
//            &fmt))) 
//        {
//            ComPtr<IDWriteFont>  dwFont;
//            dwFamily->GetFirstMatchingFont(
//                static_cast<DWRITE_FONT_WEIGHT>(key.weight),
//                DWRITE_FONT_STRETCH_NORMAL,
//                key.italic == litehtml::font_style_italic ? DWRITE_FONT_STYLE_ITALIC
//                : DWRITE_FONT_STYLE_NORMAL,
//                &dwFont);
//            FontCachePair fcp;
//            fcp.fmt = fmt;
//            fcp.dwFont = dwFont;
//            return std::move(fcp);   // 成功
//        }
//    }
//
//    // 理论上不会走到这里，除非默认字体也失败
//    
//    return { nullptr, nullptr };
//    
//}

/* ---------------------------------------------------------- */
bool FontCache::findFamily(IDWriteFontCollection* coll,
    const std::wstring& target,
    Microsoft::WRL::ComPtr<IDWriteFontFamily>& family, 
    UINT32& index)
{
    // 1) 快路径：DWrite 自带
    //BOOL exists = FALSE;
    //if (SUCCEEDED(coll->FindFamilyName(target.c_str(), &index, &exists)) && exists)
    //    return true;

    // 2) 慢路径：逐 family 遍历
    UINT32 count = coll->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; ++i)
    {
      
        if (FAILED(coll->GetFontFamily(i, &family)))
            continue;

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(&names)))
            continue;

        UINT32 len = 0;
        if (FAILED(names->GetStringLength(0, &len)))
            continue;

        std::wstring buf(len + 1, L'\0');
        if (FAILED(names->GetString(0, buf.data(), len + 1)))
            continue;
        buf.resize(len);

        if (_wcsicmp(buf.c_str(), target.c_str()) == 0)
        {
            index = i;
            return true;
        }
    }
    return false;   // 真没找到
}

void FontCache::clear() {
    std::unique_lock ul(m_mtx);
    m_map.clear();          // ComPtr 归零，DWrite 对象随之释放
}




VirtualDoc::VirtualDoc()
{
    m_worker = std::thread(&VirtualDoc::workerLoop, this);
}

VirtualDoc::~VirtualDoc()
{

    m_worker.detach();   // 或 join，取决于生命周期
    clear();

}

void VirtualDoc::load_book()
{
    m_book = g_book;
    m_container = g_cMain;

    m_spine = m_book->ocf_pkg_.spine;
}


// ---------- 分页 ----------
std::wstring VirtualDoc::get_href_by_id(int id)
{

    if (id < m_spine.size() && id >= 0)
    {
        return m_spine[id].href;
    }
    return L"";
}

int VirtualDoc::get_id_by_href(std::wstring& href)
{
    for (int i = 0; i < m_spine.size(); i++)
    {
        if (m_spine[i].href == href) {
            return i;
        }
    }
    return -1;
}

void VirtualDoc::merge_block(HtmlBlock& dst, HtmlBlock& src, bool isAddToBottom)
{

    dst.head = src.head;
    // 2. 把新的 body_blocks 追加到尾部
    if (isAddToBottom)
    {

        dst.body_blocks.insert(
            dst.body_blocks.end(),
            src.body_blocks.begin(),
            src.body_blocks.end());

    }
    // 追加到顶部
    else
    {
        dst.body_blocks.insert(
            dst.body_blocks.begin(),
            src.body_blocks.begin(),
            src.body_blocks.end());
    }
}


HtmlBlock VirtualDoc::get_html_block(std::string html, int spine_id)
{
    HtmlBlock block;
    block.spine_id = spine_id;
    block.head = get_head(html);
    block.body_blocks = get_body_blocks(html, spine_id);
    
    // 插入一段空白
    BodyBlock bi;
    bi.spine_id = spine_id;
    bi.height = 0;
    bi.html = "<div style = \"height:" + std::to_string(g_cfg.split_space_height) + "px; \"></div>";
    bi.block_id = block.body_blocks.back().block_id + 1;
    block.body_blocks.push_back(std::move(bi));

    return block;
}


// ---------- 工具：节点序列化 ----------
bool VirtualDoc::gumbo_tag_is_void(GumboTag tag)
{
    switch (tag)
    {
    case GUMBO_TAG_AREA:
    case GUMBO_TAG_BASE:
    case GUMBO_TAG_BR:
    case GUMBO_TAG_COL:
    case GUMBO_TAG_EMBED:
    case GUMBO_TAG_HR:
    case GUMBO_TAG_IMG:
    case GUMBO_TAG_INPUT:
    case GUMBO_TAG_LINK:
    case GUMBO_TAG_META:
    case GUMBO_TAG_PARAM:
    case GUMBO_TAG_SOURCE:
    case GUMBO_TAG_TRACK:
    case GUMBO_TAG_WBR:
        return true;
    default:
        return false;
    }
}

void VirtualDoc::serialize_element(const GumboElement& el, std::ostream& out) {
    out << '<' << gumbo_normalized_tagname(el.tag);
    for (unsigned int i = 0; i < el.attributes.length; ++i) {
        auto* attr = static_cast<GumboAttribute*>(el.attributes.data[i]);
        out << ' ' << attr->name << "=\"" << attr->value << '"';
    }
    if (el.children.length == 0 && gumbo_tag_is_void(el.tag)) {
        out << " />";
        return;
    }
    out << '>';
    for (unsigned int i = 0; i < el.children.length; ++i)
        serialize_node(static_cast<GumboNode*>(el.children.data[i]), out);
    out << "</" << gumbo_normalized_tagname(el.tag) << '>';
}

void VirtualDoc::serialize_node(const GumboNode* node, std::ostream& out) {
    if (!node) return;
    switch (node->type) {
    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_CDATA:
        out << node->v.text.text;
        break;
    case GUMBO_NODE_WHITESPACE:
        out << node->v.text.text;
        break;
    case GUMBO_NODE_ELEMENT:
        serialize_element(node->v.element, out);
        break;
    default: break;
    }
}

// ---------- 1. 提取 <head> ----------
std::string VirtualDoc::get_head(std::string& html) {
    GumboOutput* out = gumbo_parse(html.c_str());
    std::string result;
    if (out->root->type == GUMBO_NODE_ELEMENT) {
        for (unsigned int i = 0; i < out->root->v.element.children.length; ++i) {
            auto* node = static_cast<GumboNode*>(out->root->v.element.children.data[i]);
            if (node->type == GUMBO_NODE_ELEMENT &&
                node->v.element.tag == GUMBO_TAG_HEAD) {
                std::ostringstream oss;
                serialize_element(node->v.element, oss);
                result = oss.str();
                break;
            }
        }
    }
    gumbo_destroy_output(&kGumboDefaultOptions, out);
    return result;
}

std::vector<BodyBlock>
VirtualDoc::get_body_blocks(std::string& html,
     int spine_id,
     size_t max_chunk_bytes)
{
    // 1. 用 string_view 避免拷贝
    std::string_view sv(html);

    // 2. 找到 <body ...> 和 </body>
    static const std::string_view body_tag = "<body";
    static const std::string_view body_end = "</body>";

    size_t body_open = sv.find(body_tag);
    if (body_open == std::string_view::npos) return {};

    body_open = sv.find('>', body_open);          // 跳过属性
    if (body_open == std::string_view::npos) return {};
    ++body_open;                                  // 指向 '>' 之后

    size_t body_close = sv.find(body_end, body_open);
    if (body_close == std::string_view::npos) return {};

    // 3. 直接取子串（零拷贝）
    std::string_view body_content = sv.substr(body_open, body_close - body_open);

    // 4. 构造唯一块
    return { BodyBlock{0, 0, std::string(body_content)} };
}
// ---------- 2. 切 <body> ----------
//std::vector<BodyBlock> VirtualDoc::get_body_blocks(std::string& html,
//    int spine_id,
//    size_t max_chunk_bytes) {
//    std::vector<BodyBlock> blocks;
//    GumboOutput* out = gumbo_parse(html.c_str());
//    GumboNode* body = nullptr;
//
//    // 找到 body
//    if (out->root->type == GUMBO_NODE_ELEMENT) {
//        for (unsigned int i = 0; i < out->root->v.element.children.length; ++i) {
//            auto* node = static_cast<GumboNode*>(out->root->v.element.children.data[i]);
//            if (node->type == GUMBO_NODE_ELEMENT &&
//                node->v.element.tag == GUMBO_TAG_BODY) {
//                body = node;
//                break;
//            }
//        }
//    }
//    if (!body) { gumbo_destroy_output(&kGumboDefaultOptions, out); return blocks; }
//
//    // 收集 body 的直接子节点
//    std::vector<const GumboNode*> nodes;
//    auto& children = body->v.element.children;
//    for (unsigned int i = 0; i < children.length; ++i)
//        nodes.emplace_back(static_cast<GumboNode*>(children.data[i]));
//
//    // 分块
//    std::ostringstream current;
//    size_t current_bytes = 0;
//    int block_id = 0;
//
//    auto flush = [&]() {
//        if (current.str().empty()) return;
//        BodyBlock bb;
//        bb.spine_id = spine_id;
//        bb.block_id = block_id++;
//        bb.html = current.str();
//        blocks.emplace_back(std::move(bb));
//        current.str("");
//        current.clear();
//        current_bytes = 0;
//        };
//
//    for (const GumboNode* n : nodes) {
//        std::ostringstream tmp;
//        serialize_node(n, tmp);
//        std::string frag = tmp.str();
//        if (current_bytes + frag.size() > max_chunk_bytes && !current.str().empty())
//            flush();
//        current << frag;
//        current_bytes += frag.size();
//    }
//    flush(); // 最后一块
//    gumbo_destroy_output(&kGumboDefaultOptions, out);
//    return blocks;
//}

void VirtualDoc::load_html(std::wstring& href)
{

    auto id = get_id_by_href(href);
    if(id < 0)
    {
        OutputDebugStringW(href.c_str());
        OutputDebugStringW(L" 未找到\n");
        return ;
    }
 
    insert_chapter(id);
  
    //load_by_id(id, true);
    //// 先渲染好加载的章节
    //{
    //    std::string text;
    //    text += "<html>" + m_blocks.back().head + "<body>";
    //    for (auto hb : m_blocks)
    //    {
    //        for (auto b : hb.body_blocks)
    //        {
    //            text += b.html;
    //        }
    //    }

    //    text += "</body></html>";
    //    m_doc = litehtml::document::createFromString({ text.c_str(), litehtml::encoding::utf_8 }, m_container.get());
    //    m_doc->render(g_cfg.document_width);
    //    m_blocks.back().height = m_doc->height();
    //    m_height = m_doc->height();
    //}

    //id -= 1;
    //load_by_id(m_top_block, id, false);
}

float VirtualDoc::get_height_by_id(int spine_id)
{
    for (auto b: m_blocks)
    {
        if(b.spine_id == spine_id)
        {
            return b.height;
        }
    }
    return 0;
}
void VirtualDoc::reload()
{
    if (m_blocks.empty() || m_workerBusy) return;
    if (g_cMain) { g_cMain->clear_selection(); }
    // 1. 记录当前滚动百分比
    ScrollPosition old = get_scroll_position();
    double percent = 0.0;
    if ( old.height > 0)          // 旧文档高度
        m_percent = double(old.offset) / old.height;

    // 2. 重新加载
    clear();
    insert_chapter(old.spine_id);

    m_isReloading.store(true);
    // 3. 把百分比换算成新的像素值

}
bool VirtualDoc::load_by_id( int spine_id, bool isPushBack)
{
    std::wstring href = get_href_by_id(spine_id);
    if (href.empty()) { return false; }
    std::string html = m_book->load_html(href);
    if (html.empty()) { return false; }

    PreprocessHTML(html); 


    auto block = get_html_block(html, spine_id);
 
    if (isPushBack){ m_blocks.push_back(std::move(block)); }
    else
    { 
        m_blocks.emplace(m_blocks.begin(), std::move(block));
    }

    return true;
}

ScrollPosition VirtualDoc::get_scroll_position()
{

    ScrollPosition pos{};
    if (m_blocks.empty()) { return pos; }
    pos.offset = g_offsetY;
    for (auto hb: m_blocks)
    {
        pos.spine_id = hb.spine_id;
        pos.height = hb.height;
        if ((pos.offset - hb.height) < 0) {break; }
        pos.offset -= hb.height;
    }

    return pos;

}
void VirtualDoc::set_scroll_position( ScrollPosition sp)
{
    float offset = 0.0f;
    for (auto& bk: m_blocks)
    {
        if (bk.spine_id == bk.spine_id)break;
        offset += bk.height;
    }
    offset += sp.offset;
    g_offsetY = offset;
}

std::wstring seconds2string(int64_t sec)
{
    int64_t days = sec / 86400;
    int64_t hours = (sec % 86400) / 3600;
    int64_t minutes = (sec % 3600) / 60;
    int64_t seconds = sec % 60;

    std::wstring timeStr;
    if (days)    timeStr += std::to_wstring(days) + L"天 ";
    if (hours || days)   timeStr += std::to_wstring(hours) + L"时";
    if (minutes || hours || days) timeStr += std::to_wstring(minutes) + L"分";
    timeStr += std::to_wstring(seconds) + L"秒";
    return timeStr;
}
litehtml::document::ptr VirtualDoc::get_doc(int client_h, float& scrollY, float& y_offset)
{
    if (!m_book || !m_container ) { return nullptr; }


    y_offset += scrollY;
    scrollY = 0;
    OutputDebugStringA("[before] ");
    OutputDebugStringA(std::to_string(y_offset).c_str());
    OutputDebugStringA("\n");


    if (y_offset < 0)
    {
        insert_prev_chapter();

    }
    if (y_offset > static_cast<float>(m_height - client_h*2.0f))
    {
        insert_next_chapter();

    }

    if (m_height > 0 &&y_offset > (m_height - (client_h * 0.5f)))
    {
        y_offset = m_height - client_h * 0.5f;
    }
    if (m_height> 0 &&y_offset < 0) { y_offset = 0; }




    ScrollPosition p = get_scroll_position();

    g_toc->SetHighlight(p);

    SetStatus(STATUSBAR_CURRENT_SPINE, (L"当前项: " + std::to_wstring(p.spine_id)).c_str());
    SetStatus(STATUSBAR_CURRENT_OFFSET, (L"当前位移: " + std::to_wstring(p.offset)).c_str());
 
    auto time_string = seconds2string(g_recorder->getBookTotalTime());
    SetStatus(STATUSBAR_TOTAL_TIME, (L"阅读时长：" + time_string).c_str());
    SetStatus(STATUSBAR_FONT_SIZE, (L"字体大小：" + std::to_wstring(g_cfg.font_size)).c_str());
    SetStatus(STATUSBAR_LINE_HEIGHT, (L"行间距：" + std::to_wstring(g_cfg.line_height)).c_str());
    SetStatus(STATUSBAR_DOC_WIDTH, (L"文档宽度：" + std::to_wstring(g_cfg.document_width)).c_str());
    SetStatus(STATUSBAR_DOC_ZOOM, (L"文档缩放倍数：" + std::to_wstring(g_cMain->m_zoom_factor)).c_str());
    g_scrollbar->SetPosition(p.spine_id, p.height, p.offset);

    

    return nullptr;
}

//
//void VirtualDoc::draw(int x, int y, int w, int h, float offsetY)
//{
//    if (m_doc_cache.empty() || m_workerBusy) { return; }
//    auto startY = offsetY;
//    auto endY = offsetY + h;
//    float height = 0.0f;
//    float drawY = 0.0f;
//    float drawH = h;
//    g_canvas->BeginDraw();
//    for (int i=0; i<m_doc_cache.size(); i++ )
//    {
//        startY -= m_doc_cache[i].height;
//        endY -= m_doc_cache[i].height;
//        if (startY > 0) { continue; }
//        startY = startY +  m_doc_cache[i].height;
//        
//        drawH = std::min(m_doc_cache[i].height- startY, drawH);
//        g_canvas->m_doc = m_doc_cache[i].doc;
//        litehtml::position clip(x, drawY, w, drawH);
//        g_canvas->m_doc->draw(g_canvas->getContext(),   // 强制转换
//            x, -startY, &clip);
//        //g_canvas->present(x, -startY, &clip);
//
//        if (endY < 0) { break; }
//        drawY += drawH;
//        drawH = h - drawY;
//        startY = -drawY;
//    }
//    g_canvas->EndDraw();
//
//}
float VirtualDoc::get_height()
{
    float height = 0.0f;
    if (m_blocks.empty()) { return height; }
    for (auto b: m_blocks)
    {
        height += b.height;
    }
    return height;
}
bool VirtualDoc::insert_chapter(int spine_id)
{
    int id = spine_id;
    if (id < 0 || id >= static_cast<int>(m_spine.size()) || exists(id)) return false;

    if (m_workerBusy.load(std::memory_order_relaxed)) return false;

    m_workerBusy.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(m_taskMtx);
        if (!m_taskQueue.empty()) return false;
        m_taskQueue.push({ id, false });
    }
    m_taskCv.notify_one();
    return false;
}

bool VirtualDoc::insert_prev_chapter()
{
    if (m_workerBusy.load(std::memory_order_relaxed)) return false;
    int id = get_scroll_position().spine_id - 1;
    if (id < 0 || exists(id)) return false;
    m_workerBusy.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(m_taskMtx);
        if (!m_taskQueue.empty()) return false;
        m_taskQueue.push({ id, true });
    }
    m_taskCv.notify_one();
    return false;
}

bool VirtualDoc::insert_next_chapter()
{

    if (m_workerBusy.load(std::memory_order_relaxed)) return false;
    int id = get_scroll_position().spine_id + 1;
    if (id >= static_cast<int>(m_spine.size()) || exists(id)) return false;

    m_workerBusy.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(m_taskMtx);
        if (!m_taskQueue.empty()) return false;
        m_taskQueue.push({ id, false });
    }
    m_taskCv.notify_one();
    return false;
}
void VirtualDoc::workerLoop()
{
    while (true)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lk(m_taskMtx);
            m_taskCv.wait(lk, [this] { return !m_taskQueue.empty(); });

            task = m_taskQueue.front();
            m_taskQueue.pop();
        }
        BusyGuard bg(m_workerBusy);   // 从这里开始置忙，析构时自动清 0

        OutputDebugStringA("[VirtualDod thread] 开始更新\n");
        // 1. 耗时 IO
        if (!load_by_id(task.chapterId, !task.insertAtFront))
        {
            m_workerBusy.store(false, std::memory_order_relaxed);
            continue;
        }

        // 2. 组装 HTML
        float height = 0.0f;
        HtmlBlock& target = task.insertAtFront ? m_blocks.front() : m_blocks.back();
   

        std::string html = "<html>" + target.head + "<body>";
        for (auto&hb : m_blocks)
        {
            for (auto& b : hb.body_blocks) html += b.html;
        }
     
        html += "</body></html>";

        m_doc = litehtml::document::createFromString(
            { html.c_str(), litehtml::encoding::utf_8 }, m_container.get());
        m_doc->render(g_cfg.document_width);
        height = m_doc->height() - m_height;
 
        target.height = height;
        m_height = m_doc->height();
        float delta = task.insertAtFront ? height : 0.0f;
 

        PostMessage(g_hWnd, WM_EPUB_CACHE_UPDATED, 0, static_cast<LPARAM>(delta));

    }
}

bool VirtualDoc::exists(int spine_id)
{
    if (m_blocks.empty()) return false;
    for (auto b: m_blocks)
    {
        if (b.spine_id == spine_id) { return true; }
    }
    return false;
}

void VirtualDoc::clear()
{
    m_blocks.clear();
    g_offsetY = 0;
    g_scrollY = 0;
    m_height = 0;
}


/* ---------- 工具 ---------- */
static int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/* ---------- 构造/析构 ---------- */
ReadingRecorder::ReadingRecorder() 
{ 
    initDB(); 
    m_book_record = {};
    m_time_frag = {};
}
ReadingRecorder::~ReadingRecorder() 
{ 
    if (m_dbBook) sqlite3_close(m_dbBook); 
    if (m_dbTime) sqlite3_close(m_dbTime);
}

/* ---------- 初始化数据库 ---------- */
void ReadingRecorder::initDB() {
    namespace fs = std::filesystem;
    fs::path db_path = exe_dir() / "data";
    fs::create_directories(db_path);
    fs::path db_book_path = db_path / "Books.db";
    fs::path db_time_path = db_path / "Time.db";

    /* ---------- Books.db ---------- */
    if (sqlite3_open(db_book_path.generic_string().c_str(), &m_dbBook) != SQLITE_OK)
        throw std::runtime_error("sqlite open failed");

    sqlite3_exec(m_dbBook, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS books(
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            path             TEXT UNIQUE,
            title            TEXT,
            author           TEXT,
            open_count       INTEGER DEFAULT 0,
            total_words      INTEGER DEFAULT 0,
            last_spine_id    INTEGER DEFAULT 0,
            last_offset      INTEGER DEFAULT 0,
            font_size        INTEGER DEFAULT 0,
            line_height_mul  REAL    DEFAULT 0.0,
            doc_width        INTEGER DEFAULT 0,
            total_time_s     INTEGER DEFAULT 0,
            first_open_us    INTEGER DEFAULT 0,
            last_open_us     INTEGER DEFAULT 0,
            enableCSS        INTEGER DEFAULT 1,
            enableJS         INTEGER DEFAULT 0,
            enableGlobalCSS  INTEGER DEFAULT 0,
            enablePreHTML    INTEGER DEFAULT 1,
            displayTOC       INTEGER DEFAULT 1,
            displayStatus    INTEGER DEFAULT 1,
            displayMenu      INTEGER DEFAULT 1,
            displayScroll    INTEGER DEFAULT 1
        );
    )";
    sqlite3_exec(m_dbBook, sql, nullptr, nullptr, nullptr);

    /* ---------- Time.db ---------- */
    if (sqlite3_open(db_time_path.generic_string().c_str(), &m_dbTime) != SQLITE_OK)
        throw std::runtime_error("sqlite open Time.db failed");
    sqlite3_exec(m_dbTime, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    const char* sqlTime = R"(
        CREATE TABLE IF NOT EXISTS reading_time(
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            path     TEXT,
            title         TEXT,
            authors       TEXT,
            spine_id      INTEGER,
            current_chapter TEXT,
            start_time    REAL,
            end_time      REAL,
            duration      INTEGER
        );
    )";
    sqlite3_exec(m_dbTime, sqlTime, nullptr, nullptr, nullptr);
}

/* ---------- 打开书 ---------- */
void ReadingRecorder::openBook(const std::string absolutePath) {
    m_book_record = {};
    m_time_frag = {};
    BookRecord rec;
    rec.path = absolutePath;

    const char* select = "SELECT * FROM books WHERE path=?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_dbBook, select, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, absolutePath.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // 已存在
        rec.id = sqlite3_column_int64(stmt, 0);
        auto colText = [](sqlite3_stmt* s, int idx) -> std::string {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(s, idx));
            return p ? std::string(p, sqlite3_column_bytes(s, idx)) : "";
            };
        rec.title = colText(stmt, 2);
        rec.author = colText(stmt, 3);
        rec.openCount = sqlite3_column_int(stmt, 4);
        rec.totalWords = sqlite3_column_int(stmt, 5);
        rec.lastSpineId = sqlite3_column_int(stmt, 6);
        rec.lastOffset = sqlite3_column_int(stmt, 7);
        rec.fontSize = sqlite3_column_int(stmt, 8);
        rec.lineHeightMul = static_cast<float>(sqlite3_column_double(stmt, 9));
        rec.docWidth = sqlite3_column_int(stmt, 10);
        rec.totalTime = sqlite3_column_int(stmt, 11);
        rec.lastOpenTimestamp = sqlite3_column_int64(stmt, 13);
        rec.enableCSS = sqlite3_column_int(stmt, 14);
        rec.enableJS = sqlite3_column_int(stmt, 15);
        rec.enableGlobalCSS = sqlite3_column_int(stmt, 16);
        rec.enablePreHTML = sqlite3_column_int(stmt, 17);
        rec.displayTOC = sqlite3_column_int(stmt, 18);
        rec.displayStatus = sqlite3_column_int(stmt, 19);
        rec.displayMenu = sqlite3_column_int(stmt, 20);
        rec.displayScroll = sqlite3_column_int(stmt, 21);
    }
    else {
        // 新书：用当前 g_book 状态插入
        const char* insert = R"(
            INSERT INTO books(path, first_open_us, last_open_us)
            VALUES(?, ?, ?);
        )";
        sqlite3_prepare_v2(m_dbBook, insert, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, absolutePath.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, nowUs());
        sqlite3_bind_int64(stmt, 3, nowUs());
        sqlite3_step(stmt);

        rec.id = sqlite3_last_insert_rowid(m_dbBook);

    }
    sqlite3_finalize(stmt);

    // 更新打开次数 & 最后打开时间
    const char* update = "UPDATE books SET open_count=open_count+1, last_open_us=? WHERE id=?;";
    sqlite3_prepare_v2(m_dbBook, update, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, nowUs());
    sqlite3_bind_int64(stmt, 2, rec.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    m_book_record =  std::move(rec);
}
int64_t ReadingRecorder::getTotalTime()
{
    const char* sql = "SELECT COALESCE(SUM(duration),0) FROM reading_time;";
    sqlite3_stmt* stmt = nullptr;
    int64_t totalUs = 0;

    if (sqlite3_prepare_v2(m_dbTime, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            totalUs = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return totalUs / 1'000'000;   // 返回秒
}
int64_t ReadingRecorder::getBookTotalTime() const
{
    //const char* sql =
    //    "SELECT COALESCE(SUM(duration),0) FROM reading_time WHERE path = ?;";
    //sqlite3_stmt* stmt = nullptr;
    //int64_t totalUs = 0;

    //if (sqlite3_prepare_v2(m_dbTime, sql, -1, &stmt, nullptr) == SQLITE_OK)
    //{
    //    sqlite3_bind_text(stmt, 1, m_book_record.path.c_str(), -1, SQLITE_STATIC);
    //    if (sqlite3_step(stmt) == SQLITE_ROW)
    //        totalUs = sqlite3_column_int64(stmt, 0);
    //}
    //sqlite3_finalize(stmt);
    //return totalUs / 1'000'000;   // 返回秒
    return m_book_record.totalTime;
}
/* ---------- 写入 ---------- */
void ReadingRecorder::flush() {
    if (m_book_record.id < 0) return;   // 无效记录

    flushBookRecord();
    flushTimeRecord();
}

void ReadingRecorder::flushBookRecord()
{
    auto& rec = m_book_record;
    const char* sql = R"(
        UPDATE books SET
            title           = ?,
            author          = ?,
            total_words     = ?,
            last_spine_id   = ?,
            last_offset     = ?,
            font_size       = ?,
            line_height_mul = ?,
            doc_width       = ?, 
            total_time_s    = ?,
            enableCSS       = ?,
            enableJS        = ?,
            enableGlobalCSS = ?,
            enablePreHTML   = ?,
            displayTOC      = ?,
            displayStatus   = ?,
            displayMenu     = ?,
            displayScroll   = ?
        WHERE id = ?;
    )";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_dbBook, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, rec.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, rec.author.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, rec.totalWords);
    sqlite3_bind_int(stmt, 4, rec.lastSpineId);
    sqlite3_bind_int(stmt, 5, rec.lastOffset);
    sqlite3_bind_int(stmt, 6, rec.fontSize);
    sqlite3_bind_double(stmt, 7, rec.lineHeightMul);
    sqlite3_bind_double(stmt, 8, rec.docWidth);
    sqlite3_bind_int(stmt, 9, rec.totalTime);
    sqlite3_bind_int(stmt, 10, rec.enableCSS);
    sqlite3_bind_int(stmt, 11, rec.enableJS);
    sqlite3_bind_int(stmt, 12, rec.enableGlobalCSS);
    sqlite3_bind_int(stmt, 13, rec.enablePreHTML);
    sqlite3_bind_int(stmt, 14, rec.displayTOC);
    sqlite3_bind_int(stmt, 15, rec.displayStatus);
    sqlite3_bind_int(stmt, 16, rec.displayMenu);
    sqlite3_bind_int(stmt, 17, rec.displayScroll);
    sqlite3_bind_int64(stmt, 18, rec.id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void ReadingRecorder::flushTimeRecord()
{
    if (m_time_frag.empty()) return;

    /* 0. 先把缓存拿出来，防止 flush 期间又被写入 */
    std::vector<timeFragment> batch = std::move(m_time_frag);
    m_time_frag.clear();                 // 立即清空原缓存

    /* 1. 按时间升序 */
    std::sort(batch.begin(), batch.end(),
        [](const timeFragment& a, const timeFragment& b)
        { return a.timestamp < b.timestamp; });

    /* 2. 事务开始 */
    char* err = nullptr;
    if (sqlite3_exec(m_dbTime, "BEGIN;", nullptr, nullptr, &err) != SQLITE_OK)
    {
        OutputDebugStringA(("BEGIN failed: " + std::string(err) + "\n").c_str());
        sqlite3_free(err);
        return;
    }

    constexpr int64_t MERGE_THRESHOLD_US = 2'000'000;

    for (const timeFragment& frag : batch)
    {
        /* 3. 查询最近一条 */
        const char* sqlSel = R"(
            SELECT id, end_time, duration
            FROM reading_time
            WHERE path = ? AND current_chapter = ? AND spine_id = ?
            ORDER BY end_time DESC
            LIMIT 1;
        )";
        sqlite3_stmt* sel = nullptr;
        if (sqlite3_prepare_v2(m_dbTime, sqlSel, -1, &sel, nullptr) != SQLITE_OK)
        {
            OutputDebugStringA(("prepare SELECT failed\n"));
            continue;
        }
        sqlite3_bind_text(sel, 1, frag.path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(sel, 2, frag.chapter.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(sel, 3, frag.spine_id);

        bool merged = false;
        if (sqlite3_step(sel) == SQLITE_ROW)
        {
            int     oldId = sqlite3_column_int(sel, 0);
            int64_t oldEnd = sqlite3_column_int64(sel, 1);
            if (frag.timestamp - oldEnd <= MERGE_THRESHOLD_US)
            {
                const char* sqlUpd = R"(
                    UPDATE reading_time
                    SET end_time = ?,
                        duration = duration + (? - end_time)
                    WHERE id = ?;
                )";
                sqlite3_stmt* upd = nullptr;
                if (sqlite3_prepare_v2(m_dbTime, sqlUpd, -1, &upd, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int64(upd, 1, frag.timestamp);
                    sqlite3_bind_int64(upd, 2, frag.timestamp);
                    sqlite3_bind_int(upd, 3, oldId);
                    if (sqlite3_step(upd) != SQLITE_DONE)
                        OutputDebugStringA(("UPDATE step failed\n"));
                    sqlite3_finalize(upd);
                }
                else
                {
                    OutputDebugStringA(("prepare UPDATE failed\n"));
                }
                merged = true;
            }
        }
        sqlite3_finalize(sel);

        if (!merged)
        {
            const char* sqlIns = R"(
                INSERT INTO reading_time
                (path, title, authors, spine_id, current_chapter,
                 start_time, end_time, duration)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?);
            )";
            sqlite3_stmt* ins = nullptr;
            if (sqlite3_prepare_v2(m_dbTime, sqlIns, -1, &ins, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(ins, 1, frag.path.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(ins, 2, frag.title.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(ins, 3, frag.author.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(ins, 4, frag.spine_id);
                sqlite3_bind_text(ins, 5, frag.chapter.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int64(ins, 6, frag.timestamp);
                sqlite3_bind_int64(ins, 7, frag.timestamp);
                sqlite3_bind_int64(ins, 8, 0);

                if (sqlite3_step(ins) != SQLITE_DONE)
                    OutputDebugStringA(("INSERT step failed\n"));
                sqlite3_finalize(ins);
            }
            else
            {
                OutputDebugStringA(("prepare INSERT failed\n"));
            }
        }
    }

    /* 4. 提交事务 */
    if (sqlite3_exec(m_dbTime, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK)
    {
        OutputDebugStringA(("COMMIT failed: " + std::string(err) + "\n").c_str());
        sqlite3_free(err);
    }
}
void CALLBACK OnFlush(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
    // 直接在工作线程/回调里刷新
    OutputDebugStringA("OnFlush\n");
    if (g_recorder) { g_recorder->flush(); }
    g_flushTimer = 0;
}
void ReadingRecorder::updateRecord()
{
    if (m_book_record.id < 0) { return; }
 
    if (g_book)
    {
        m_book_record.displayMenu = g_cfg.displayMenuBar;
        m_book_record.displayScroll = g_cfg.displayScrollBar;
        m_book_record.displayStatus = g_cfg.displayStatusBar;
        m_book_record.displayTOC = g_cfg.displayTOC;

        m_book_record.enableCSS = g_cfg.enableCSS;
        m_book_record.enableGlobalCSS = g_cfg.enableGlobalCSS;
        m_book_record.enableJS = g_cfg.enableJS;
        m_book_record.enablePreHTML = g_cfg.enablePreprocessHTML;

        m_book_record.fontSize = g_cfg.font_size;
        m_book_record.lineHeightMul = g_cfg.line_height;
        m_book_record.docWidth = g_cfg.document_width;
        m_book_record.totalTime += 1;


        if (m_book_record.title.empty() && g_book && !g_book->ocf_pkg_.meta.empty())
        {
            auto titIt = g_book->ocf_pkg_.meta.find(L"dc:title");
            m_book_record.title = titIt != g_book->ocf_pkg_.meta.end() ? w2a(titIt->second) : "";
        }
        if(m_book_record.author.empty()&& g_book && !g_book->ocf_pkg_.meta.empty())
        {
            auto authIt = g_book->ocf_pkg_.meta.find(L"dc:creator");
            m_book_record.author = authIt != g_book->ocf_pkg_.meta.end() ? w2a(authIt->second) : "";
        }

        timeFragment tf;
        tf.path = m_book_record.path;
        tf.title = m_book_record.title;
        tf.author = m_book_record.author;
        tf.timestamp = nowUs();
        
        if (g_vd) {
            ScrollPosition p = g_vd->get_scroll_position();
            m_book_record.lastSpineId = p.spine_id;
            m_book_record.lastOffset = p.offset;
            
            tf.spine_id = p.spine_id;
            tf.chapter = w2a(g_book->get_chapter_name_by_id(p.spine_id));
        }
        m_time_frag.push_back(std::move(tf));
        if (!g_flushTimer)
        {
            g_flushTimer = timeSetEvent(g_cfg.record_flush_interval_ms, 0, OnFlush, 0, TIME_ONESHOT);
        }

    }
}

void TocPanel::clear()
{
    m_nodes.clear();
    m_roots.clear();
    m_visible.clear();      // 可见行索引
    m_lineH = 20;
    m_scrollY = 0;
    m_totalH = 0;
    m_selLine = -1;

}

void TocPanel::GetWindow(HWND hwnd)
{
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    m_hwnd = hwnd;
}


void TocPanel::Load(const OCFPackage& pkg)
{
    // 复用你原来的 BuildTree 算法
    m_nodes.clear();
    m_roots.clear();
    m_nodes.reserve(pkg.toc.size());
    std::vector<size_t> st;
    st.push_back(SIZE_MAX);
    for (const auto& np : pkg.toc)
    {
        while (st.size() > static_cast<size_t>(np.order + 1)) st.pop_back();
        size_t idx = m_nodes.size();
        m_nodes.push_back(Node{ &np });
        if (st.back() != SIZE_MAX)
            m_nodes[st.back()].childIdx.push_back(idx);
        else
            m_roots.push_back(idx);
        st.push_back(idx);
    }
    for (auto& n : m_nodes) 
    { 
        // 1. 分离锚点
        std::wstring href = n.nav->href;
        size_t pos = href.find(L'#');
        std::wstring pure = pos == std::wstring::npos ? href : href.substr(0, pos);
        for (int i = 0; i < pkg.spine.size(); i++)
        {
            if (pkg.spine[i].href == pure)
            {
                n.spineId = i;
                break;
            }
        }
        n.expanded = false; 
    }
    RebuildVisible();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

/* ---------- 内部实现 ---------- */
LRESULT CALLBACK TocPanel::WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NCCREATE)
        SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)l)->lpCreateParams);
    TocPanel* self = (TocPanel*)GetWindowLongPtr(h, GWLP_USERDATA);
    return self ? self->HandleMsg(m, w, l) : DefWindowProc(h, m, w, l);
}

LRESULT TocPanel::HandleMsg(UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; OnPaint(BeginPaint(m_hwnd, &ps)); EndPaint(m_hwnd, &ps); } return 0;
    case WM_LBUTTONDOWN: OnLButtonDown(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_MOUSEWHEEL:  OnMouseWheel(GET_WHEEL_DELTA_WPARAM(w)); return 0;
    case WM_VSCROLL:     OnVScroll(LOWORD(w), HIWORD(w)); return 0;
    case WM_KEYDOWN:
        if (w == VK_UP && m_selLine > 0) { m_selLine--; EnsureVisible(m_selLine); InvalidateRect(m_hwnd, nullptr, FALSE); }
        if (w == VK_DOWN && m_selLine + 1 < (int)m_visible.size()) { m_selLine++; EnsureVisible(m_selLine); InvalidateRect(m_hwnd, nullptr, FALSE); }
        return 0;
    }
    return DefWindowProc(m_hwnd, m, w, l);
}

TocPanel::TocPanel()
{
    // 16 px 高，默认宽度，正常粗细，不斜体，不 underline，不 strikeout
    m_hFont = CreateFontW(18, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Microsoft YaHei");   // 字体名

    // 1. 在 WM_CREATE / 初始化时创建一次
  
    m_hightlightBrush = CreateSolidBrush(g_cfg.highlight_color_cr);

}
TocPanel::~TocPanel()
{
    if (m_hFont) DeleteObject(m_hFont);
}
void TocPanel::RebuildVisible()
{
    m_visible.clear();
    std::function<void(size_t)> walk = [&](size_t idx) {
        m_visible.push_back(idx);
        const Node& n = m_nodes[idx];
        if (n.expanded)
            for (size_t c : n.childIdx) walk(c);
        };
    for (size_t r : m_roots) walk(r);

    // 1. 总高度（像素）
    m_totalH = (int)m_visible.size() * m_lineH + m_marginTop + m_marginBottom;

    // 2. 客户区高度（像素）
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int clientH = rc.bottom - rc.top;

    // 3. 设置滚动条
    SCROLLINFO si{ sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE;
    si.nMin = 0;
    si.nMax = m_totalH;          // 像素
    si.nPage = clientH;          // 像素
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

int TocPanel::HitTest(int y) const
{
    int line = (y + m_scrollY) / m_lineH;
    return (line >= 0 && line < (int)m_visible.size()) ? line : -1;
}

void TocPanel::Toggle(int line)
{
    size_t idx = m_visible[line];
    m_nodes[idx].expanded = !m_nodes[idx].expanded;
    RebuildVisible();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void TocPanel::EnsureVisible(int line)
{
    RECT rc; GetClientRect(m_hwnd, &rc);
    int y = line * m_lineH;
    if (y < m_scrollY) m_scrollY = y;
    else if (y + m_lineH > m_scrollY + rc.bottom) m_scrollY = y + m_lineH - rc.bottom;
    SetScrollPos(m_hwnd, SB_VERT, m_scrollY, TRUE);
}

void TocPanel::OnPaint(HDC hdc)
{
    RECT rc; GetClientRect(m_hwnd, &rc);
    /* 1. 先把整块客户区刷成背景色，解决残影 */
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));
    int first = m_scrollY / m_lineH;
    int last = std::min(first + rc.bottom / m_lineH + 1, (long)m_visible.size());
    HFONT hOld = (HFONT)SelectObject(hdc, m_hFont);


    for (int i = first; i < last; ++i)
    {
        const Node& n = m_nodes[m_visible[i]];

        // 行矩形：整体向下、向右各偏移 marginTop / marginLeft
        RECT r{ m_marginLeft,
                m_marginTop + i * m_lineH - m_scrollY,
                rc.right,
                m_marginTop + (i + 1) * m_lineH - m_scrollY };

        HBRUSH br = (i == m_selLine)
            ? m_hightlightBrush
            : GetSysColorBrush(COLOR_WINDOW);
        FillRect(hdc, &r, br);

        int indent = n.nav->order * 16;
        WCHAR sign[2] = L"";
        if (!n.childIdx.empty())
            sign[0] = n.expanded ? L'−' : L'+';

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(i == m_selLine
            ? COLOR_HIGHLIGHTTEXT
            : COLOR_WINDOWTEXT));

        // 文字再缩进：左侧留白 + 层级缩进
        int textLeft = m_marginLeft + indent;
        TextOutW(hdc, textLeft, r.top + 2, sign, lstrlenW(sign));
        textLeft += 12;
        TextOutW(hdc, textLeft, r.top + 2,
            n.nav->label.c_str(),
            static_cast<int>(n.nav->label.size()));
    }
    SelectObject(hdc, hOld);   // 恢复
}

void TocPanel::OnLButtonDown(int x, int y)
{
    int line = HitTest(y);
    if (line < 0) return;

    const Node& n = m_nodes[m_visible[line]];
    m_curTarget = m_visible[line];
    if (n.childIdx.empty())
    {
        m_selLine = line;
        InvalidateRect(m_hwnd, nullptr, false);
        UpdateWindow(m_hwnd);

        if (m_onNavigate) m_onNavigate(n.nav->href);
    }
    else
    {
        m_selLine = line;

        Toggle(line);
    }
}
float TocPanel::getAnchorOffsetY(const std::wstring& href)
{
    if (!g_cMain || !g_cMain->m_doc) { return 0; }
    size_t pos = href.find(L'#');
    std::wstring pure = pos == std::wstring::npos ? href : href.substr(0, pos);
    std::wstring anchor = pos == std::wstring::npos ? L"" : href.substr(pos+1);
 
    if (!anchor.empty()) {
        std::string cssSel = "[id=\"" + w2a(anchor) + "\"]";
        if (auto el = g_cMain->m_doc->root()->select_one(cssSel.c_str())) {
            return el->get_placement().y;
        }
    }
    return 0;
}
size_t TocPanel::getTargetNode(const ScrollPosition& sp)
{
    size_t target = m_nodes.size();
    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        if (m_nodes[i].nav && m_nodes[i].spineId == sp.spine_id)
        {
            target = i; break;
        }
    }
    for (size_t i = target+1; i < m_nodes.size(); ++i)
    {
        if (m_nodes[i].nav && m_nodes[i].spineId == m_nodes[target].spineId)
        {

            std::wstring href = m_nodes[i].nav->href;
            int offsetY = getAnchorOffsetY(href);
            //OutputDebugStringA("[offsetY] ");
            //OutputDebugStringA(std::to_string(offsetY).c_str());
            //OutputDebugStringA("\n");
            if (sp.offset < offsetY) { break; }
            target = i;
        }
        else { break; }
    }
    return target;
}
void TocPanel::SetHighlight(ScrollPosition sp)
{
    // ---------- 1. 找目标节点 ----------
    size_t target = getTargetNode(sp);
    if (target == m_nodes.size() || target == m_curTarget) return;

    m_curTarget = target;
    //for (auto& n : m_nodes) n.expanded = false;
    // ---------- 2. 记录路径并展开 ----------
    // path 只需存需要展开的节点，最多树高
    std::vector<size_t> path;
    std::function<bool(size_t)> dfs = [&](size_t idx) -> bool
        {
            if (idx == target) return true;          // 命中目标

            Node& n = m_nodes[idx];
            //if (!n.expanded)                       // 折叠就展开
            //    n.expanded = true;

            for (size_t c : n.childIdx)
                if (dfs(c))
                {
                    path.push_back(idx);           // 回溯时记录父节点
                    return true;
                }
            return false;
        };

    for (size_t r : m_roots)                    // 支持多根
        if (dfs(r)) break;
    for (size_t idx : path) m_nodes[idx].expanded = true;
    // 3. 重建可见表（O(N) 一次遍历）
    RebuildVisible();

    // 4. 直接取行号
    m_selLine = -1;
    for (size_t i = 0; i < m_visible.size(); ++i)
        if (m_visible[i] == target) { m_selLine = static_cast<int>(i); break; }

    if (m_selLine != -1)
        EnsureVisible(m_selLine);
    // 5. 重绘
    InvalidateRect(m_hwnd, nullptr, FALSE);
}
void TocPanel::OnVScroll(int code, int pos)
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int clientH = rc.bottom - rc.top;
    int maxY = std::max(0, m_totalH - clientH);

    switch (code)
    {
    case SB_LINEUP:      m_scrollY -= m_lineH; break;
    case SB_LINEDOWN:    m_scrollY += m_lineH; break;
    case SB_PAGEUP:      m_scrollY -= clientH; break;   // 按页滚 = 客户区高度
    case SB_PAGEDOWN:    m_scrollY += clientH; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
    {
        SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
        if (GetScrollInfo(m_hwnd, SB_VERT, &si))
            m_scrollY = si.nTrackPos;      // 拿到 32 位真实位置
        break;
    }
    }

    m_scrollY = std::max(0, std::min(m_scrollY, maxY));

    SetScrollPos(m_hwnd, SB_VERT, m_scrollY, TRUE);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}
void TocPanel::OnMouseWheel(int delta)
{
    // 每 120 单位滚一行；可根据需要改成多行或整页
    int lines = delta / WHEEL_DELTA;          // WHEEL_DELTA = 120
    for (int i = 0; i < abs(lines); ++i)
    {
        OnVScroll(lines > 0 ? SB_LINEUP : SB_LINEDOWN, 0);
    }
}

void EPUBBook::LoadToc()
{
    if (g_toc)
    {
        g_toc->Load(ocf_pkg_);                 // 代替 EPUBBook::LoadToc()
    }
}

ZipFileProvider::ZipFileProvider() {}
ZipFileProvider::~ZipFileProvider()
{
    mz_zip_reader_end(&m_zip);           // 1. 先关闭旧 zip
}
bool ZipFileProvider::load(const std::wstring& file_path)
{
    namespace fs = std::filesystem;
    if (!fs::exists(file_path))
    {
        OutputDebugStringW(L"[ZipProvider] 文件不存在\n");
        return false;
    }


    mz_zip_reader_end(&m_zip);           // 1. 先关闭旧 zip
    memset(&m_zip, 0, sizeof(m_zip));

    if (!mz_zip_reader_init_file(&m_zip, w2a(file_path).c_str(), 0))
    {
        OutputDebugStringW((L"[ZipProvider] zip 打开失败：" +
            std::to_wstring(mz_zip_get_last_error(&m_zip)) + L"\n").c_str());
        return false;
    }

    m_zipIndex = ZipIndexW(m_zip);
    return true;
}
MemFile ZipFileProvider::get( std::wstring path)  
{
    MemFile mf;
    std::string narrow_name = w2a(path);
    size_t uncomp_size = 0;
    void* p = mz_zip_reader_extract_file_to_heap(
        const_cast<mz_zip_archive*>(&m_zip),
        narrow_name.c_str(),
        &uncomp_size, 0);

    if (p) {
        mf.data.assign(static_cast<uint8_t*>(p),
            static_cast<uint8_t*>(p) + uncomp_size);
        mz_free(p);
    }
    return mf;
}

std::wstring ZipFileProvider::find(const std::wstring& path)
{
    return m_zipIndex.find(path);
}
MemFile LocalFileProvider::get( std::wstring path) 
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // 文件不存在或非普通文件
    if (!fs::is_regular_file(path, ec) || ec)
        return {};

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    file.seekg(0, std::ios::end);
    const auto len = static_cast<size_t>(file.tellg());
    file.seekg(0);

    MemFile mf;
    mf.data.resize(len);
    file.read(reinterpret_cast<char*>(mf.data.data()), len);

    if (!file)        // 读取失败
        return {};

    return mf;        // NRVO / move
}

EPUBParser::EPUBParser(){}
EPUBParser::~EPUBParser()
{
    m_fp.reset();
}
bool EPUBParser::load(std::shared_ptr<IFileProvider> fp)
{
    m_fp = fp;
    parse_ocf();
    parse_opf();
    parse_toc();
    return true;
}

bool EPUBParser::parse_ocf()
{
    m_ocf_pkg = {};  // 清空
    auto mf = m_fp->get(L"META-INF/container.xml");
    if (mf.data.empty()) return false;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(mf.begin(), mf.size()) != tinyxml2::XML_SUCCESS) { return false; }

    auto* rootfile = doc.FirstChildElement("container")
        ? doc.FirstChildElement("container")->FirstChildElement("rootfiles")
        : nullptr;
    rootfile = rootfile ? rootfile->FirstChildElement("rootfile") : nullptr;
    if (!rootfile || !rootfile->Attribute("full-path")) { return false; }

    m_ocf_pkg.rootfile = a2w(rootfile->Attribute("full-path"));
    m_ocf_pkg.opf_dir = m_ocf_pkg.rootfile.substr(0, m_ocf_pkg.rootfile.find_last_of(L'/') + 1);
    return true;
}
bool EPUBParser::parse_opf()
{
    auto opf = m_fp->get(m_ocf_pkg.rootfile.c_str());
    std::string xml(opf.begin(), opf.begin() + opf.size());
    if (opf.data.empty()) return false;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) return false;

    auto* manifest = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("manifest")
        : nullptr;

    for (auto* it = manifest ? manifest->FirstChildElement("item") : nullptr;
        it; it = it->NextSiblingElement("item"))
    {
        OCFItem item;
        item.id = a2w(it->Attribute("id") ? it->Attribute("id") : "");
        item.href = a2w(it->Attribute("href") ? it->Attribute("href") : "");
        item.media_type = a2w(it->Attribute("media-type") ? it->Attribute("media-type") : "");
        item.properties = a2w(it->Attribute("properties") ? it->Attribute("properties") : "");


        // 只在 href 非空时拼绝对路径
        if (!item.href.empty())
            item.href = m_fp->find(item.href);

        m_ocf_pkg.manifest.emplace_back(std::move(item));
    }

    // spine
    auto* spine = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("spine")
        : nullptr;
    // 先把 manifest 做成 id -> href 的映射
    std::unordered_map<std::wstring, std::wstring> id2href;
    for (const auto& m : m_ocf_pkg.manifest)
        id2href[m.id] = m.href;

    // 再解析 spine
    for (auto* it = spine ? spine->FirstChildElement("itemref") : nullptr;
        it; it = it->NextSiblingElement("itemref")) {

        OCFRef ref;
        ref.idref = a2w(it->Attribute("idref") ? it->Attribute("idref") : "");
        ref.href = id2href[ref.idref];   // 直接填进去
        ref.linear = a2w(it->Attribute("linear") ? it->Attribute("linear") : "yes");
        m_ocf_pkg.spine.emplace_back(std::move(ref));
    }
    // meta
    auto* meta = doc.RootElement()
        ? doc.RootElement()->FirstChildElement("metadata")
        : nullptr;
    for (auto* it = meta ? meta->FirstChildElement() : nullptr;
        it; it = it->NextSiblingElement()) {
        m_ocf_pkg.meta[a2w(it->Name())] = a2w(it->GetText() ? it->GetText() : "");
    }
    return true;
}
bool EPUBParser::parse_toc()
{
    std::wstring toc_path;
    for (const auto& it : m_ocf_pkg.manifest)
    {
        if (it.properties.find(L"nav") != std::wstring::npos ||
            it.id.find(L"ncx") != std::wstring::npos)
        {
            toc_path = it.href;
            break;
        }
    }
    if (toc_path.empty()) return false;

    m_ocf_pkg.toc_path = toc_path;
    auto toc = m_fp->get(toc_path.c_str());
    if (toc.data.empty()) return false;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(toc.begin(), toc.size()) != tinyxml2::XML_SUCCESS) return false;

    bool is_nav = is_xhtml(toc_path);
    std::string opf_dir = w2a(m_ocf_pkg.opf_dir);

    m_ocf_pkg.toc.clear();

    if (is_nav)
    {
        auto* body = doc.FirstChildElement("html")
            ? doc.FirstChildElement("html")->FirstChildElement("body")
            : nullptr;
        if (!body) return false;

        for (auto* nav = body->FirstChildElement("nav");
            nav;
            nav = nav->NextSiblingElement("nav"))
        {
            const char* type = nav->Attribute("epub:type");
            if (type && std::string(type) == "toc")
            {
                parse_nav_list(nav->FirstChildElement("ol"), 0, opf_dir, m_ocf_pkg.toc);
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
            parse_ncx_points(navMap->FirstChildElement("navPoint"), 0, opf_dir, m_ocf_pkg.toc);
    }
    return true;
}

void EPUBParser::parse_ncx_points(tinyxml2::XMLElement* navPoint, int level,
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
            np.href = m_fp->find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <navPoint>
        parse_ncx_points(pt->FirstChildElement("navPoint"), level + 1, opf_dir, out);
    }
}

void EPUBParser::parse_nav_list(tinyxml2::XMLElement* ol, int level,
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
            np.href = m_fp->find(np.href);
        np.order = level;               // 层级深度
        out.emplace_back(std::move(np));

        // 递归子 <ol>
        if (auto* sub = li->FirstChildElement("ol"))
            parse_nav_list(sub, level + 1, opf_dir, out);
    }
}

std::wstring EPUBParser::extract_text(const tinyxml2::XMLElement* a)
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

std::wstring EPUBBook::get_chapter_name_by_id(int spine_id)
{
    // 从给定 spine_id 开始，依次递减查找
    for (int id = spine_id; id >= 0; --id)
    {
        // 1. 取出 spine 对应的 ref
        if (id >= static_cast<int>(ocf_pkg_.spine.size()))
            continue;

        std::wstring href = ocf_pkg_.spine[id].href;


        if (href.empty())
            continue;

        // 3. 去掉锚点
        size_t pos = href.find(L'#');
        if (pos != std::wstring::npos)
            href = href.substr(0, pos);

        // 4. 与 toc 中的 href比对（同样去掉锚点）
        for (const auto& nav : ocf_pkg_.toc)
        {
            std::wstring nav_href = nav.href;
            pos = nav_href.find(L'#');
            if (pos != std::wstring::npos)
                nav_href = nav_href.substr(0, pos);

            if (nav_href == href)
                return nav.label;
        }
    }

    // 遍历到 id=0 仍未找到
    return L"";
}




// ---------- 静态 ----------



void ScrollBarEx::GetWindow(HWND hwnd)
{
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    m_hwnd = hwnd;
}
// ---------- API ----------
void ScrollBarEx::SetSpineCount(int n)
{
    m_count = n;
    m_pos = {};
    InvalidateRect(m_hwnd, nullptr, false);
    UpdateWindow(m_hwnd);
}



void ScrollBarEx::SetPosition(int spineId, float totalHeightPx, float offsetPx)
{
    if (spineId >= 0 && spineId < m_count)
    {
        RECT rc; GetClientRect(m_hwnd, &rc);
        float h = rc.bottom - rc.top;
        m_pos.spine_id = spineId;
        m_pos.height = totalHeightPx - h;
        m_pos.offset = offsetPx;

        InvalidateRect(m_hwnd, nullptr, false);
        UpdateWindow(m_hwnd);
    }
}

// ---------- 内部 ----------
LRESULT CALLBACK ScrollBarEx::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ScrollBarEx* self = (ScrollBarEx*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_PAINT:          self->OnPaint(); return 0;
    case WM_LBUTTONDOWN:  self->OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_MOUSEMOVE:      self->OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_MOUSELEAVE: self->OnMouseLeave(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
    case WM_LBUTTONUP:    self->OnLButtonUp(); return 0;
    case WM_RBUTTONUP:    self->OnRButtonUp() ; return 0;
    case SBM_SETSPINECOUNT:
        if (self) self->SetSpineCount((int)wp);

        return 0;

    case SBM_SETPOSITION:
        if (self) self->SetPosition((int)wp, (float)LOWORD(lp), (float)HIWORD(lp));
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void ScrollBarEx::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int W = rc.right - rc.left;
    const int H = rc.bottom - rc.top;
    const int CX = W / 2;

    /* ---- 双缓冲 ---- */
    HDC memDC = CreateCompatibleDC(hdc);
    BITMAPINFO bi{ 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits;
    HBITMAP bmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(memDC, bmp);
    FillRect(memDC, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

    if (H > 0 && m_count > 0)
    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        /* ---------- 滚动条模式 ---------- */
        if (m_mouseIn)
        {
            /* 3. 顶部/底部横线 */
            const int aboveCnt = m_pos.spine_id;
            const int belowCnt = m_count - aboveCnt - 1;



            /*  竖线 */
            Gdiplus::Pen linePen(Gdiplus::Color(220, 220, 220), 3);
            g.DrawLine(&linePen, CX, 0, CX, H);

            /*  章节线 */
            Gdiplus::Pen markPen(Gdiplus::Color(200, 200, 200), 1);
            for (int i = 0; i < aboveCnt; i++)
            {
                g.DrawLine(&markPen, 0, 2 * i, W, 2 * i);
            }
            for (int i = 0; i < belowCnt; i++)
            {
                g.DrawLine(&markPen, 0, H - (2 * i), W, H - (2 * i));
            }

            /*  滑块 */
            const double ratio = (m_pos.height > 0)
                ? std::max(0.0f, std::min(1.0f, m_pos.offset / m_pos.height))
                : 0.0;

            int thumbY = static_cast<int>(ratio * (H - thumbH));
            thumbY = std::max(0, std::min(thumbY, H - thumbH));

            Gdiplus::Rect r(CX - 6, thumbY, 12, thumbH);
            Gdiplus::GraphicsPath path;
            path.AddArc(r.X, r.Y, 6, 6, 180, 90);
            path.AddArc(r.X + r.Width - 6, r.Y, 6, 6, 270, 90);
            path.AddArc(r.X + r.Width - 6, r.Y + r.Height - 6, 6, 6, 0, 90);
            path.AddArc(r.X, r.Y + r.Height - 6, 6, 6, 90, 90);
            path.CloseFigure();
            Gdiplus::SolidBrush thumbBrush(g_cfg.scrollbar_slider_color);
            g.FillPath(&thumbBrush, &path);


 
        }
        /* ---------- 圆点模式 ---------- */
        else
        {
            const bool crowded = (m_count > 500);
            if (m_count >= 1 && m_count < 30) dot_r = 2;
            else if (m_count >= 30 && m_count < 100) dot_r = 2;
            else if (m_count >= 100) dot_r = 1;
            const double step = static_cast<double>(H) / m_count;

            /* 串起所有点的浅色细线 */
            if (m_count > 1)
            {
                Gdiplus::Pen linkPen(Gdiplus::Color(220, 220, 220), 1);
                g.DrawLine(&linkPen, CX, step, CX, H - step);
            }

            for (int i = 0; i < m_count; ++i)
            {
                /* 拥挤时只画当前点 */
                if (i == m_pos.spine_id) continue;
     

                const int y = static_cast<int>((i + 0.5) * step);
    

                const int r = dot_r;
                Gdiplus::Color c =  Gdiplus::Color(200, 200, 200);

                Gdiplus::SolidBrush br(c);
                g.FillEllipse(&br, CX - r, y - r, 2 * r, 2 * r);
            }
            /* 画当前章节的点 */
            {
                Gdiplus::Color c = g_cfg.scrollbar_dot_color;
               
                int r = ACTIVE_R;
                int y = (m_pos.spine_id + 0.5) * step;
                Gdiplus::SolidBrush br(c);
                g.FillEllipse(&br, CX - r, y - r, 2 * r, 2 * r);
            }
        }
    }
    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    EndPaint(m_hwnd, &ps);
}
bool ScrollBarEx::HitThumb(const POINT& pt) const
{

    if (m_count <= 0) return false;
    if (!m_mouseIn)return false;
    RECT rc; GetClientRect(m_hwnd, &rc);
    int H = rc.bottom - rc.top;
    const double ratio = (m_pos.height > 0)
        ? std::max(0.0f, std::min(1.0f, m_pos.offset / m_pos.height))
        : 0.0;

    int thumbY = static_cast<int>(ratio * (H - thumbH));
    thumbY = std::max(0, std::min(thumbY, H - thumbH));
    if (pt.y >= thumbY && (pt.y) <= (thumbY + thumbH))return true;
    return false;
}

void ScrollBarEx::OnLButtonDown(int x, int y)
{
    POINT pt{ x, y };
    if (HitThumb(pt))
    {
        SetCapture(m_hwnd);
        m_thumb.drag = true;


    }
}
void ScrollBarEx::OnMouseLeave(int x, int y)
{
}
void ScrollBarEx::OnMouseMove(int x, int y)
{

 
    /* 悬停高亮（可选） */
    POINT pt{ x, y };
    bool in = HitThumb(pt);
    if (in != m_thumb.hot)
    {
        m_thumb.hot = in;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    if (m_thumb.drag)
    {
   
        RECT rc; GetClientRect(m_hwnd, &rc);
        int h = rc.bottom - rc.top;
        float ratio = std::max(0.0f, std::min(1.0f, static_cast<float>(pt.y) / static_cast<float>(h)));

        float newTop = (m_pos.height) * ratio ;

        if (g_vd) { g_vd->set_scroll_position(ScrollPosition{m_pos.spine_id, newTop, m_pos.height}); }
        g_offsetY = newTop;
        UpdateCache();

    }
}

void ScrollBarEx::OnLButtonUp()
{
    if (m_thumb.drag)
    {
        ReleaseCapture();
        m_thumb.drag = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

}

void ScrollBarEx::OnRButtonUp()
{
     m_mouseIn = !m_mouseIn;
     InvalidateRect(m_hwnd, nullptr, false);     
     UpdateWindow(m_hwnd); 
}

int64_t SimpleContainer::hit_test(float x, float y)
{

    for (const auto& line : m_lines)
        for (const auto& cb : line)
            if (x >= cb.rect.left && x <= cb.rect.right &&
                y >= cb.rect.top && y <= cb.rect.bottom)
                return cb.offset;

    return -1;
}

void SimpleContainer::on_lbutton_down(int x, int y)
{
    m_selecting = true;
    m_selStart = m_selEnd = hit_test((float)x, (float)y);
    UpdateCache();
}

void SimpleContainer::on_mouse_move(int x, int y)
{
    if (m_selecting)
    {
        auto result = hit_test((float)x, (float)y);
        if (result >= 0) 
        { 
            if (m_selStart < 0) { m_selStart = result; }
            m_selEnd = result; 
            UpdateCache();
            InvalidateRect(m_hwnd, nullptr, false);
            UpdateWindow(m_hwnd);
        }
 



    }
}

void SimpleContainer::on_lbutton_up()
{
    m_selecting = false;
    UpdateCache();
    //copy_to_clipboard();
}
void SimpleContainer::copy_to_clipboard()
{
    if (m_selStart == m_selEnd) return;

    // 确保选区不越界
    size_t start = std::min(m_selStart, m_selEnd);
    size_t end = std::max(m_selStart, m_selEnd);
    end = std::min(end, m_plainText.size());
    if (start >= end) return;

    size_t len = end - start;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
    if (!hMem) return;                       // 内存不足
    wchar_t* dst = (wchar_t*)GlobalLock(hMem);
    if (!dst) { GlobalFree(hMem); return; }  // 锁失败

    memcpy(dst, m_plainText.c_str() + start, len * sizeof(wchar_t));
    dst[len] = L'\0';

    GlobalUnlock(hMem);

    if (OpenClipboard(g_hWnd))
    {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();
    }
    else
    {
        GlobalFree(hMem);
    }
}

std::vector<RECT> SimpleContainer::get_selection_rows() const
{
    std::vector<RECT> rows;
    if (m_selStart == m_selEnd) return rows;

    const size_t start = std::min(m_selStart, m_selEnd);
    const size_t end = std::max(m_selStart, m_selEnd);

    /* 1. 先按原逻辑收集每一“词”的矩形 */
    for (const auto& line : m_lines)
    {
        if (line.empty()) continue;

        const size_t lineFirst = line.front().offset;
        const size_t lineLast = line.back().offset;
        if (lineLast < start || lineFirst >= end) continue;

        size_t idx0 = 0;
        while (idx0 < line.size() && line[idx0].offset < start) ++idx0;

        size_t idx1 = line.size() - 1;
        while (idx1 != static_cast<size_t>(-1) && line[idx1].offset >= end) --idx1;

        if (idx0 > idx1) continue;

        const D2D1_RECT_F& r0 = line[idx0].rect;
        const D2D1_RECT_F& r1 = line[idx1].rect;

        RECT row;
        row.left = static_cast<LONG>(r0.left);
        row.top = static_cast<LONG>(r0.top);
        row.right = static_cast<LONG>(r1.right);
        row.bottom = static_cast<LONG>(std::max(r0.bottom, r1.bottom));
        rows.push_back(row);
    }

    /* 2. 把同一水平行的矩形横向合并（最小改动） */
    if (rows.empty()) return rows;

    std::vector<RECT> merged;
    RECT cur = rows.front();

    for (size_t i = 1; i < rows.size(); ++i)
    {
        const RECT& r = rows[i];
        // 同一行：top 差值 ≤ 1 像素
        if (std::abs(r.top - cur.top) <= 1)
        {
            cur.left = std::min(cur.left, r.left);
            cur.right = std::max(cur.right, r.right);
            cur.bottom = std::max(cur.bottom, r.bottom);
        }
        else
        {
            merged.push_back(cur);
            cur = r;
        }
    }
    merged.push_back(cur);
    return merged;
}

void SimpleContainer::present(float x, float y, litehtml::position* clip)
{

    m_lines.clear();
    m_plainText.clear();

    BeginDraw();

    m_doc->draw(getContext(),   // 强制转换
        x, y, clip);


    // 高亮选中行
    if (!m_selBrush)
    {
        m_rt->CreateSolidColorBrush(
            g_cfg.highlight_color_d2d,   // 半透明蓝
            &m_selBrush);
    }
    if (m_selStart != m_selEnd && m_selBrush && m_selStart >= 0 && m_selEnd >= 0)
    {
        m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        for (const auto& row : get_selection_rows())
        {
            D2D1_RECT_F r = D2D1::RectF(
                row.left ,
                row.top ,
                row.right ,
                row.bottom );
            m_rt->FillRectangle(r, m_selBrush.Get());
        }
    }

    EndDraw();



}
// 判断是否为单词边界（空格、标点、换行）
static bool is_word_boundary(wchar_t ch)
{
    return iswspace(ch) || iswpunct(ch) || ch == L'\r' || ch == L'\n';
}

//void SimpleContainer::on_lbutton_dblclk(int x, int y)
//{
//    if (m_plainText.empty()) return;
//
//    // 1. 把鼠标坐标转成逻辑字符偏移（UTF-16 code unit）
//    size_t clickPos = hit_test(x, y);
//    if (clickPos == static_cast<size_t>(-1) ||
//        clickPos >= m_plainText.size())
//        return;
//
//    // 2. 用 ICU 的 BreakIterator 找“单词”边界
//    UErrorCode err = U_ZERO_ERROR;
//    icu::UnicodeString us(m_plainText.data(), m_plainText.size());
//    UBreakIterator* bi = ubrk_open(UBRK_WORD, nullptr,
//        us.getBuffer(), us.length(),
//        &err);
//    if (U_FAILURE(err)) return;
//
//    // 3. 把 clickPos 之前最近的单词起点
//    int32_t start = ubrk_preceding(bi, static_cast<int32_t>(clickPos));
//    if (start == UBRK_DONE) start = 0;
//
//    // 4. 把 clickPos 之后最近的单词终点
//    int32_t end = ubrk_following(bi, static_cast<int32_t>(clickPos));
//    if (end == UBRK_DONE) end = static_cast<int32_t>(m_plainText.size());
//
//    ubrk_close(bi);
//
//    // 5. 赋给选区
//    m_selStart = static_cast<size_t>(start);
//    m_selEnd = static_cast<size_t>(end);
//
//    UpdateCache();
//}
void SimpleContainer::clear_selection()
{
    m_selStart = m_selEnd = -1;
    m_selecting = false;
}
void SimpleContainer::on_lbutton_dblclk(int x, int y)
{
    if (m_plainText.empty() || m_lines.empty()) return;

    /* 1. 字符偏移 */
    size_t clickPos = hit_test(x, y);
    if (clickPos == size_t(-1) || clickPos >= m_plainText.size())
        return;

    /* 2. 在 m_lines 里找到当前行 */
    size_t lineStart = 0, lineEnd = 0;
    for (const auto& line : m_lines)
    {
        if (line.empty()) continue;
        lineStart = line.front().offset;
        lineEnd = line.back().offset + 1;   // [start , end)
        if (clickPos >= lineStart && clickPos < lineEnd)
            break;
    }
    if (lineEnd <= lineStart) return;   // 没找到行

    /* 3. 在这一行里用 ICU 选词 */
    icu::UnicodeString us(m_plainText.data(), m_plainText.size());
    const UChar* buf = us.getBuffer();

    UErrorCode err = U_ZERO_ERROR;
    UBreakIterator* wordBI = ubrk_open(
        UBRK_WORD, nullptr,
        buf + lineStart,
        static_cast<int32_t>(lineEnd - lineStart),
        &err);
    if (U_FAILURE(err)) return;

    int32_t relPos = static_cast<int32_t>(clickPos - lineStart);

    int32_t wordStartRel = ubrk_preceding(wordBI, relPos);
    if (wordStartRel == UBRK_DONE) wordStartRel = 0;

    int32_t wordEndRel = ubrk_following(wordBI, relPos);
    if (wordEndRel == UBRK_DONE) wordEndRel = lineEnd - lineStart;

    ubrk_close(wordBI);

    int32_t wordStart = lineStart + wordStartRel;
    int32_t wordEnd = lineStart + wordEndRel;

    /* 4. 裁剪首尾空格/标点 */
    auto isVisible = [](UChar32 c) {
        return !u_isspace(c) && (u_isalnum(c) || c == 0x2019);
        };

    while (wordStart < wordEnd) {
        UChar32 c; int32_t idx = wordStart;
        U16_NEXT(buf, idx, wordEnd, c);
        if (isVisible(c)) break;
        wordStart = idx;
    }
    while (wordEnd > wordStart) {
        UChar32 c; int32_t idx = wordEnd;
        U16_PREV(buf, wordStart, idx, c);
        if (isVisible(c)) { wordEnd = idx + U16_LENGTH(c); break; }
        wordEnd = idx;
    }

    if (wordStart >= wordEnd) return;

    /* 5. 更新选区 */
    m_selStart = static_cast<size_t>(wordStart);
    m_selEnd = static_cast<size_t>(wordEnd);
    UpdateCache();
}


//namespace mathml2tex {
//
//
//    using namespace tinyxml2;
//
//    // ---------- 工具 ----------
//    static inline void write(std::string& out, const std::string& s) { out += s; }
//
//    static inline std::string trim(const std::string& s)
//    {
//        const char* ws = " \t\n\r\f\v";
//        size_t first = s.find_first_not_of(ws);
//        if (first == std::string::npos) return "";
//        size_t last = s.find_last_not_of(ws);
//        return s.substr(first, last - first + 1);
//    }
//    static inline std::string get_attr(const XMLElement* e,
//        const char* name,
//        const char* def = "")
//    {
//        const char* v = e->Attribute(name);
//        return v ? v : def;
//    }
//
//    static inline std::string escape_text(const std::string& s)
//    {
//        std::string r;
//        r.reserve(s.size());
//        for (char c : s)
//        {
//            switch (c)
//            {
//            case '\\': r += "\\textbackslash{}"; break;
//            case '{':  r += "\\{"; break;
//            case '}':  r += "\\}"; break;
//            case '$':  r += "\\$"; break;
//            case '&':  r += "\\&"; break;
//            case '%':  r += "\\%"; break;
//            case '#':  r += "\\#"; break;
//            case '^':  r += "\\^{}"; break;
//            case '_':  r += "\\_"; break;
//            case '~':  r += "\\textasciitilde{}"; break;
//            default:   r += c;
//            }
//        }
//        return r;
//    }
//
//    // ---------- 主转换 ----------
//    void convert_node(const XMLNode* node, std::string& out, bool display = false)
//    {
//        if (!node) return;
//        if (const XMLText* txt = node->ToText())
//        {
//            write(out, escape_text(trim(txt->Value())));
//            return;
//        }
//
//        const XMLElement* e = node->ToElement();
//        if (!e) return;
//
//        // 使用 gperf 或编译期哈希可再提速，这里用 switch-case 展开
//        switch (e->Name()[0])
//        {
//        case 'm':
//        {
//            switch (e->Name()[1])
//            {
//            case 'a': // math
//                if (std::strcmp(e->Name(), "math") == 0)
//                {
//                    bool d = display || (get_attr(e, "display") == "block");
//                    for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//                        convert_node(c, out, d);
//                    return;
//                }
//                break;
//
//            case 'f': // mfrac
//                if (std::strcmp(e->Name(), "mfrac") == 0)
//                {
//                    std::string lt = get_attr(e, "linethickness");
//                    if (!lt.empty() && lt != "1")
//                    {
//                        write(out, "\\genfrac{}{}{" + lt + "}{");
//                    }
//                    else
//                    {
//                        write(out, "\\frac{");
//                    }
//                    convert_node(e->FirstChild(), out, display);
//                    write(out, "}{");
//                    convert_node(e->FirstChild()->NextSibling(), out, display);
//                    write(out, "}");
//                    return;
//                }
//                break;
//
//            case 'r':
//                if (std::strcmp(e->Name(), "mroot") == 0)
//                {
//                    write(out, "\\sqrt[");
//                    convert_node(e->FirstChild()->NextSibling(), out, display);
//                    write(out, "]{");
//                    convert_node(e->FirstChild(), out, display);
//                    write(out, "}");
//                    return;
//                }
//                else if (std::strcmp(e->Name(), "mrow") == 0)
//                {
//                    for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//                        convert_node(c, out, display);
//                    return;
//                }
//                break;
//
//            case 's':
//                switch (e->Name()[2])
//                {
//                case 'q': // msqrt
//                    if (std::strcmp(e->Name(), "msqrt") == 0)
//                    {
//                        write(out, "\\sqrt{");
//                        for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//                            convert_node(c, out, display);
//                        write(out, "}");
//                        return;
//                    }
//                    break;
//
//                case 'u': // msub, msup, msubsup
//                    if (std::strcmp(e->Name(), "msub") == 0)
//                    {
//                        convert_node(e->FirstChild(), out, display);
//                        write(out, "_{");
//                        convert_node(e->FirstChild()->NextSibling(), out, display);
//                        write(out, "}");
//                        return;
//                    }
//                    else if (std::strcmp(e->Name(), "msup") == 0)
//                    {
//                        convert_node(e->FirstChild(), out, display);
//                        write(out, "^{");
//                        convert_node(e->FirstChild()->NextSibling(), out, display);
//                        write(out, "}");
//                        return;
//                    }
//                    else if (std::strcmp(e->Name(), "msubsup") == 0)
//                    {
//                        convert_node(e->FirstChild(), out, display);
//                        write(out, "_{");
//                        convert_node(e->FirstChild()->NextSibling(), out, display);
//                        write(out, "}^{");
//                        convert_node(e->FirstChild()->NextSibling()->NextSibling(), out, display);
//                        write(out, "}");
//                        return;
//                    }
//                    break;
//
//                case 't': // mtable, mtr, mtd
//                    if (std::strcmp(e->Name(), "mtable") == 0)
//                    {
//                        write(out, "\\begin{array}");
//                        std::string colalign = get_attr(e, "columnalign");
//                        if (!colalign.empty())
//                        {
//                            write(out, "{");
//                            for (char c : colalign)
//                            {
//                                switch (c)
//                                {
//                                case 'l': write(out, "l"); break;
//                                case 'c': write(out, "c"); break;
//                                case 'r': write(out, "r"); break;
//                                default:  write(out, "c");
//                                }
//                            }
//                            write(out, "}");
//                        }
//                        else
//                        {
//                            // 默认列数：第一行 <mtr> 的 <mtd> 数量
//                            int cols = 0;
//                            if (const XMLNode* firstRow = e->FirstChild())
//                                for (const XMLNode* cell = firstRow->FirstChild(); cell; cell = cell->NextSibling())
//                                    ++cols;
//                            write(out, std::string(std::max(cols, 1), 'c'));
//                        }
//
//                        for (const XMLNode* row = e->FirstChild(); row; row = row->NextSibling())
//                        {
//                            write(out, "\n");
//                            for (const XMLNode* cell = row->FirstChild(); cell; cell = cell->NextSibling())
//                            {
//                                if (cell != row->FirstChild()) write(out, " & ");
//                                convert_node(cell, out, display);
//                            }
//                            write(out, " \\\\");
//                        }
//                        write(out, "\n\\end{array}");
//                        return;
//                    }
//                    else if (std::strcmp(e->Name(), "mtr") == 0 || std::strcmp(e->Name(), "mtd") == 0)
//                    {
//                        for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//                            convert_node(c, out, display);
//                        return;
//                    }
//                    break;
//                }
//                break;
//
//            case 'o': // mo, mi, mn
//                if (std::strcmp(e->Name(), "mo") == 0 ||
//                    std::strcmp(e->Name(), "mi") == 0 ||
//                    std::strcmp(e->Name(), "mn") == 0)
//                {
//                    if (const char* txt = e->GetText())
//                    {
//                        std::string s = txt;
//                        if (std::strcmp(e->Name(), "mo") == 0)
//                        {
//                            // 简单映射常用符号
//                            if (s == "−") s = "-";
//                            else if (s == "×") s = "\\times";
//                            else if (s == "·") s = "\\cdot";
//                            else if (s == "→") s = "\\to";
//                            else if (s == "∞") s = "\\infty";
//                            else if (s == "≤") s = "\\leq";
//                            else if (s == "≥") s = "\\geq";
//                            else if (s == "≠") s = "\\neq";
//                            else if (s == "±") s = "\\pm";
//                        }
//                        write(out, s);
//                    }
//                    return;
//                }
//                break;
//
//            case 'e': // merror, menclose
//                if (std::strcmp(e->Name(), "menclose") == 0)
//                {
//                    std::string notation = get_attr(e, "notation");
//                    if (notation == "longdiv")
//                    {
//                        write(out, "\\longdiv{");
//                    }
//                    else
//                    {
//                        write(out, "\\boxed{");
//                    }
//                    for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//                        convert_node(c, out, display);
//                    write(out, "}");
//                    return;
//                }
//                break;
//
//            case 'i': // mspace, mstyle, mphantom, mpadded
//                if (std::strcmp(e->Name(), "mstyle") == 0)
//                {
//                    std::string scriptlevel = get_attr(e, "scriptlevel");
//                    if (!scriptlevel.empty())
//                    {
//                        int lvl = std::stoi(scriptlevel);
//                        if (lvl > 0) write(out, "\\scriptstyle ");
//                        else         write(out, "\\displaystyle ");
//                    }
//                    for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//                        convert_node(c, out, display);
//                    return;
//                }
//                else if (std::strcmp(e->Name(), "mspace") == 0)
//                {
//                    std::string w = get_attr(e, "width");
//                    if (!w.empty())
//                    {
//                        write(out, "\\hspace{" + w + "}");
//                    }
//                    return;
//                }
//                else if (std::strcmp(e->Name(), "mphantom") == 0)
//                {
//                    write(out, "\\phantom{");
//                    for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//                        convert_node(c, out, display);
//                    write(out, "}");
//                    return;
//                }
//                break;
//            }
//            break;
//        }
//
//        default:
//            break;
//        }
//
//        // 兜底：未知节点直接递归子节点
//        for (const XMLNode* c = e->FirstChild(); c; c = c->NextSibling())
//            convert_node(c, out, display);
//    }
//    /* ---------- 递归转换 ---------- */
//    std::string mathml2tex(const std::string& mathml)
//    {
//        XMLDocument doc;
//        doc.Parse(mathml.c_str());
//        const XMLElement* math = doc.RootElement();
//        if (!math || std::strcmp(math->Name(), "math") != 0)
//            return "";
//
//        std::string tex;
//        convert_node(math, tex, true);
//        return tex;
//    }
//
//    /* ---------- 对外接口 ---------- */
//    std::string convert(const std::string& mathml) {
//        return mathml2tex(mathml);
//    }
//
//} // namespace mathml2tex



// 方便打印任意字符串
inline void DbgPrint(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}




using namespace tinyxml2;


/* ---------- 内部实现 ---------- */
class MathML2SVG::Impl {
public:
    /* 样式结构体 —— 只在 Impl 内部可见 */


    /* 策略表 */
    using RenderFn = std::function<std::string(const tinyxml2::XMLElement*, const Style&)>;
    using AttrFn = void(*)(const class tinyxml2::XMLAttribute*, class Style&);

    std::unordered_map<std::string, RenderFn> tagRender;
    std::unordered_map<std::string, AttrFn>   attrApply;
    std::mutex                                mtx;
    bool m_usePath = true;   // 外部可改
    Impl() { registerAll(); }

    /* 线程安全注册 */
    void registerTag(const std::string& tag, RenderFn fn) {
        std::lock_guard<std::mutex> lock(mtx);
        tagRender[tag] = std::move(fn);
    }
    void registerAttr(const std::string& attr, AttrFn fn) {
        std::lock_guard<std::mutex> lock(mtx);
        attrApply[attr] = fn;
    }

    std::string cleanup(std::string svg) {
        std::regex re(R"(\s*data-w="[^"]*")");
        return std::regex_replace(svg, re, "");
    }

    /* 主转换 */
    std::string convert(const std::string& mathml) {
        XMLDocument doc;
        if (doc.Parse(mathml.c_str()) != XML_SUCCESS)
            return "<!-- parse error -->";

        XMLElement* root = doc.RootElement();
        if (!root) return "<!-- empty -->";

        Style st;
        std::string inner = renderElement(root, st);
        double ascent = extractAscent(inner);
        double descent = extractDescent(inner);
        double height = ascent - descent;
        double width = extractWidth(inner);
        double em = std::stod(st.fontSize);
        double margin = 0.25* em;
        double x = margin;
        double y = margin + ascent;

        std::ostringstream svg;
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<< width + margin
            <<"\" height=\"" << height + margin << "\">"
            << "<g transform = \"translate(" << x << "," << y << ")\">" << inner << "</g>"
            << "</svg>";
        return (svg.str());
    }

private:

    static std::string xmlEscape(std::string_view raw)
    {
        std::string out;
        out.reserve(raw.size() + 16);          // 小优化
        for (char c : raw)
        {
            switch (c)
            {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
            }
        }
        return out;
    }
    static std::wstring extractPlainText(const tinyxml2::XMLElement* e) {
        std::wstring out;
        if (!e) return out;

        // 深度优先收集所有文本节点
        if (const char* txt = e->GetText())
            out += a2w(txt);

        for (const tinyxml2::XMLElement* c = e->FirstChildElement();
            c; c = c->NextSiblingElement()) {
            out += extractPlainText(c);
        }
        return out;
    }

    /* ---------- 工具函数 ---------- */
    static std::string textRender(const tinyxml2::XMLElement* e, const Style& st)
    {
        std::string txt = e->GetText() ? e->GetText() : "";
        for (size_t pos = 0;
            (pos = txt.find("&nbsp;", pos)) != std::string::npos; )
        {
            txt.replace(pos, 6, " ");   // 普通空格 U+0020
            ++pos;                      // 继续向后找
        }

        std::wstring wtxt = a2w(txt);

        // 1. 精确测量
        auto si = FreeTypeTextMeasurer::instance().measure(
            wtxt, st.fontFamily, std::stof(st.fontSize));

        // 2. 生成裸 <text>（相对于基线原点）
        std::ostringstream os;
        os << "<text x=\"0\" y=\"" << 0 << "\""
            << " font-size=\"" << st.fontSize
            << "\" font-family=\"" << w2a(st.fontFamily)
            << "\" fill=\"" << st.fill << "\">"
            << xmlEscape(txt)
            << "</text>";

        // 3. 用 <g> 包一层，把尺寸放在 g 的 data-* 上
        std::ostringstream finalOSS;
        finalOSS << "<g data-w=\"" << si.width
            << "\" data-asc=\"" << si.ascent
            << "\" data-desc=\"" << si.descent << "\">"
            << os.str()
            << "</g>";
        return finalOSS.str();
    }
    static double getDimAttr(const tinyxml2::XMLElement* e,
        const char* name,
        double defVal /*em*/)
    {
        const char* v = e->Attribute(name);
        if (!v) return defVal;
        std::string s = v;
        // 去掉单位，只保留数字
        if (s.back() == 'e' || s.back() == 'm') s.pop_back();
        if (s.empty()) return defVal;
        return std::stod(s);
    }
    static double extractWidth(const std::string& svg) {
        const char* tag = "data-w=\"";
        size_t pos = svg.find(tag);      
        if (pos == std::string::npos) return 0;
        pos += strlen(tag);
        size_t end = svg.find('"', pos);
        return std::stod(svg.substr(pos, end - pos));
    }

    static double extractAscent(const std::string& svg) {
        const char* tag = "data-asc=\"";
        size_t pos = svg.find(tag);
        if (pos == std::string::npos) return 0;
        pos += strlen(tag);
        size_t end = svg.find('"', pos);
        return std::stod(svg.substr(pos, end - pos));
    }
    static double extractDescent(const std::string& svg) {
        const char* tag = "data-desc=\"";
        size_t pos = svg.find(tag);
        if (pos == std::string::npos) return 0;
        pos += strlen(tag);
        size_t end = svg.find('"', pos);
        return std::stod(svg.substr(pos, end - pos));
    }

    static std::string hbox(const std::vector<std::string>& parts,
        double dx = 2.0, std::string tag_name = "hbox")
    {
        if (parts.empty())
            return R"(<g data-w="0"  data-asc="0" data-desc="0" />)";

        double totalW = 0.0;
        double asc = 0.0, des = 0.0;

     
        for (const auto& p : parts)
        {
            totalW += extractWidth(p);
            asc = std::max(asc, extractAscent(p));
            des = std::min(des, extractDescent(p));
        }
        totalW += dx * (parts.size() - 1); 

        /* 拼 SVG：所有子元素 y=0 对齐 */
        std::ostringstream os;
        os << "<g class=\"" << tag_name << "\" data-w=\"" << totalW
            << "\" data-asc=\"" << asc
            << "\" data-desc=\"" << des << "\" >";

        double x = 0.0;
        for (const auto& p : parts)
        {
            os << "<g transform=\"translate(" << x << ",0)\">" << p << "</g>";
            x += extractWidth(p) + dx;
        }
        os << "</g>";
        return os.str();
    }
    static std::string vbox(const std::vector<std::string>& parts,
        double dy=2.0, std::string tag_name = "vbox")
    {
        if (parts.empty())
            return R"(<g data-w="0"  data-asc="0" data-desc="0" />)";

        double width = 0.0;
        double asc = 0.0, des = 0.0;
        double totalH = 0.0;

        for (const auto& p : parts)
        {
            width = std::max(width, extractWidth(p));
            totalH += (extractAscent(p) - extractDescent(p));
        }
        totalH += dy * (parts.size() - 1);
        asc = totalH * 0.5;
        des = -(totalH - asc);
        /* 拼 SVG：所有子元素 y=0 对齐 */
        std::ostringstream os;
        os << "<g class=\"" << tag_name << "\" data-w=\"" << width
            << "\" data-asc=\"" << asc
            << "\" data-desc=\"" << des << "\" >";

        double y = -asc;
        for (const auto& p : parts)
        {
            os << "<g transform=\"translate(0, " << y+ extractAscent(p) << ")\">" << p << "</g>";
            y += (extractAscent(p) - extractDescent(p)) + dy;

        }
        os << "</g>";
        return os.str();
    }


    /* ---------- 递归渲染 ---------- */
    std::string renderElement(const XMLElement* e, Style st) {
        //DbgPrint("[renderElement] <%s>\n", e->Name());
        /* 1. 处理属性 */
        for (const XMLAttribute* a = e->FirstAttribute(); a; a = a->Next()) {
            auto it = attrApply.find(a->Name());
            if (it != attrApply.end()) it->second(a, st);
        }

        /* 2. 如果是文本类节点，直接渲染，不再递归子元素 */
        const char* tag = e->Name();
        if (!strcmp(tag, "mtext") || !strcmp(tag, "mo") || !strcmp(tag, "mi") || !strcmp(tag, "mn")) {
            return textRender(e, st);
        }

        /* 3. 容器节点：递归子元素 */
        std::vector<std::string> children;
        for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
            children.push_back(renderElement(c, st));
        }

        /* 4. 查找是否有注册的渲染器（如 mfrac、msup 等） */
        auto it = tagRender.find(tag);
        if (it != tagRender.end()) {
            return it->second(e, st);
        }

        /* 5. 默认水平排列子节点 */
        return hbox(children, 2.0, "math");
    }
    /* ---------- 注册表 ---------- */
    void registerAll() {
        /* 记号类 */
        registerTag("mi", textRender);
        registerTag("mn", textRender);
        registerTag("mo", textRender);
        registerTag("ms", textRender);
        registerTag("mtext", textRender);

        /* 布局类 */
        registerTag("mrow",
            [this](const tinyxml2::XMLElement* e, const Style& st) -> std::string
            {
                std::vector<std::string> boxes;
                /* 1. 渲染所有子元素并收集尺寸 */
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                {
                    boxes.push_back(renderElement(c, st));
                }
                return hbox(boxes, 2.0, "mrow");
            });
        registerTag("mfrac",
            [this](const tinyxml2::XMLElement* e, const Style& st) -> std::string
            {
                /* ---------- 1. 子元素 ---------- */
                std::vector<std::string> kids;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    kids.push_back(renderElement(c, st));
                if (kids.size() != 2) return "<!-- mfrac needs 2 children -->";

                /* ---------- 2. 解析属性 ---------- */
                double thickness = 1.0;
                const char* lt = e->Attribute("linethickness");
                if (lt) {
                    std::string val = lt;
                    if (val == "thin")        thickness = 0.5;
                    else if (val == "medium") thickness = 1.0;
                    else if (val == "thick")  thickness = 2.0;
                    else if (val.back() == 'x' || val.back() == 'X')
                        thickness = std::stod(val.substr(0, val.size() - 2));
                    else if (val.back() == 't' || val.back() == 'T')
                        thickness = std::stod(val.substr(0, val.size() - 2)) * 1.33;
                    else if (val.back() == 'm' || val.back() == 'M')
                        thickness = std::stod(val.substr(0, val.size() - 2)) * std::stof(st.fontSize);
                    else
                        thickness = std::stod(val);
                }
                std::string numAlign = e->Attribute("numalign") ? e->Attribute("numalign") : "center";
                std::string denAlign = e->Attribute("denomalign") ? e->Attribute("denomalign") : "center";

                /* ---------- 3. 精确盒尺寸 ---------- */
                double numW = extractWidth(kids[0]);
                double numAsc = extractAscent(kids[0]);
                double numDesc = extractDescent(kids[0]);

                double denW = extractWidth(kids[1]);
                double denAsc = extractAscent(kids[1]);
                double denDesc = extractDescent(kids[1]);

                /* ---------- 4. TeX 参数 ---------- */
                double em = std::stof(st.fontSize);
                double y_shift = 0.3 * em;
                double rule = thickness;
                double gap = 0.3 * em;

                /* ---------- 5. 垂直距离（分数线 y = 0） ---------- */
     

                double ascent = (numAsc-numDesc) + gap + rule/2.0 + y_shift;   // 分子最上沿到分数线
                double descent = -((denAsc - denDesc) + gap + rule/2.0) + y_shift;   // 分数线到分母最下沿
   

                /* ---------- 6. 水平对齐 ---------- */
                double ruleW = std::max(numW, denW);
                auto offset = [](double w, double ruleW, const std::string& align)
                    {
                        if (align == "left")  return 0.0;
                        if (align == "right") return ruleW - w;
                        return (ruleW - w) * 0.5;
                    };
                double numX = offset(numW, ruleW, numAlign);
                double denX = offset(denW, ruleW, denAlign);

                /* ---------- 7. 分子、分母相对于分数线 y = 0 的 y ---------- */
                double lineY = -y_shift;
                double numY = -(-numDesc + rule * 0.5 + gap + y_shift);   // 分子基线
                double denY = (rule * 0.5 + denAsc + gap - y_shift);  // 分母基线
                
                /* ---------- 8. 组装 ---------- */
                std::ostringstream oss;
                oss << "<g class=\"mfrac\" data-w=\"" << ruleW
                    << "\" data-asc=\"" << ascent
                    << "\" data-desc=\"" << descent << "\">";
                oss << "<g transform=\"translate(" << numX << "," << numY << ")\">" << kids[0] << "</g>";
                oss << "<line x1=\"0\" y1=\""<< lineY << "\" x2=\"" << ruleW << "\" y2=\"" << lineY << "\" "
                    << "stroke=\"black\" stroke-width=\"" << rule << "\"/>";
                oss << "<g transform=\"translate(" << denX << "," << denY << ")\">" << kids[1] << "</g>";
                oss << "</g>";
                return oss.str();
            });
        registerTag("msup",
            [this](const tinyxml2::XMLElement* e, const Style& st) -> std::string
            {
                std::vector<std::string> kids;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    kids.push_back(renderElement(c, st));
                if (kids.size() != 2) return "<!-- msup needs 2 children -->";

                /* ---------- 1. 主体尺寸 ---------- */
                double baseW = extractWidth(kids[0]);
                double baseAsc = extractAscent(kids[0]);
                double baseDes = extractDescent(kids[0]);


                /* ---------- 2. 上标原始尺寸 ---------- */
                double supW0 = extractWidth(kids[1]);
                double supAsc0 = extractAscent(kids[1]);
                double supDes0 = extractDescent(kids[1]);
                
     
                double em = std::stod(st.fontSize);
                double y_shift = 0.5 * em;
                /* ---------- 3. 缩放后尺寸 ---------- */
                const double scale = 0.7;
                double supW = supW0 * scale;
                double supAsc = supAsc0 * scale;
                double supDes = supDes0 * scale;

                /* ---------- 4. 上标位置 ---------- */
   
                double supX = baseW;                               // 右上角
                /* 补偿缩放导致的基线偏移：先平移 -supBase0*scale，再整体放到 gap 上方 */
                double supY = -(baseAsc - supDes) ;

                /* ---------- 5. 整体盒 ---------- */
                double totalW = baseW + supW;
                double totalAsc = baseAsc + (supAsc-supDes);   // 最上沿
                double totalDes = baseDes;                           // 最下沿

                /* ---------- 6. 组装 ---------- */
                std::ostringstream oss;
                oss << "<g class=\"msup\" data-w=\"" << totalW
                    << "\" data-asc=\"" << totalAsc
                    << "\" data-desc=\"" << totalDes<< "\">"   
                    << kids[0]                                         // 主体：已位于 baseBaseline
                    << "<g transform=\"translate(" << supX << "," << supY << ") scale(" << scale << ")\">"
                    << kids[1]                                         // 上标：相对位移
                    << "</g>"
                    << "</g>";
                return oss.str();
            });
        registerTag("msub",
            [this](const XMLElement* e, const Style& st) -> std::string {
                std::vector<std::string> kids;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    kids.push_back(renderElement(c, st));
                if (kids.size() < 2) return kids.empty() ? "" : kids[0];

                /* ---------- 1. 主体尺寸 ---------- */
                double baseW = extractWidth(kids[0]);
                double baseAsc = extractAscent(kids[0]);
                double baseDes = extractDescent(kids[0]);
   

                /* ---------- 2. 下标原始尺寸 ---------- */
                double subW0 = extractWidth(kids[1]);
                double subAsc0 = extractAscent(kids[1]);
                double subDes0 = extractDescent(kids[1]);


                /* ---------- 3. 缩放后尺寸 ---------- */
                const double scale = 0.7;
            
                //const double drop = 0.25 * baseDes;         // 下标基线相对主体基线的下移量

                double subW = subW0 * scale;
                double subAsc = subAsc0 * scale ;
                double subDes = subDes0 * scale ;

                /* ---------- 4. 下标位置 ---------- */
                double subX = baseW ;                     // 右下角
                /* 补偿缩放导致的基线偏移：先平移 -subBaseline0*scale，再整体下移 drop */
                double subY =  subAsc - baseDes;

                /* ---------- 5. 整体盒尺寸 ---------- */
                double totalW = subX + subW;
                double totalAsc = baseAsc;                     // 最上沿
                double totalDes = baseDes - (subAsc - subDes); // 最下沿

                /* ---------- 6. 组装 ---------- */
                std::ostringstream os;
                os << "<g class=\"msub\" data-w=\"" << totalW
                    << "\" data-asc=\"" << totalAsc
                    << "\" data-desc=\"" << totalDes << "\">"
                    << kids[0]                                  // 主体：基线 y = 0
                    << "<g transform=\"translate(" << subX << "," << subY << ") scale(" << scale << ")\">"
                    << kids[1]                                  // 下标：已补偿基线
                    << "</g>"
                    << "</g>";
                return os.str();
            });
        registerTag("msubsup",
            [this](const tinyxml2::XMLElement* e, const Style& st) -> std::string
            {
                std::vector<std::string> kids;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    kids.push_back(renderElement(c, st));
                if (kids.size() < 3) return kids.empty() ? "" : kids[0];

                /* ---------- 1. 主体尺寸 ---------- */
                double baseW = extractWidth(kids[0]);
                double baseAsc = extractAscent(kids[0]);
                double baseDes = extractDescent(kids[0]);
  

                /* ---------- 2. 上标原始尺寸 ---------- */
                double supW0 = extractWidth(kids[1]);
                double supAsc0 = extractAscent(kids[1]);
                double supDes0 = extractDescent(kids[1]);
       
                /* ---------- 3. 下标原始尺寸 ---------- */
                double subW0 = extractWidth(kids[2]);
                double subAsc0 = extractAscent(kids[2]);
                double subDes0 = extractDescent(kids[2]);


                /* ---------- 4. 缩放后尺寸 ---------- */
                const double scale = 0.7;


                double supW = supW0 * scale;
                double supAsc = supAsc0 * scale ;
                double supDes = supDes0 * scale ;

                double subW = subW0 * scale;
                double subAsc = subAsc0 * scale ;
                double subDes = subDes0 * scale ;

                /* ---------- 5. 上标位置 ---------- */
                double supX = baseW ;
                /* 补偿缩放导致的基线偏移：先平移 -supBase0*scale，再整体放到 shiftUp 上方 */
                double supY = baseAsc - supDes;

                /* ---------- 6. 下标位置 ---------- */
                double subX = baseW;
                /* 补偿缩放导致的基线偏移：先平移 -subBase0*scale，再整体放到 shiftDown 下方 */
                double subY = -(supAsc-baseDes);

                /* ---------- 7. 整体盒尺寸 ---------- */
                double totalW = std::max(baseW, supX + std::max(supW, subW));
                double totalAsc = baseAsc + (supAsc - supDes);   // 最上沿
                double totalDes = baseDes - (supAsc - supDes); // 最下沿

                /* ---------- 8. 组装 ---------- */
                std::ostringstream os;
                os << "<g class=\"msubsup\" data-w=\"" << totalW
                    << "\" data-asc=\"" << totalAsc
                    << "\" data-desc=\"" << totalDes << "\">"
                    << kids[0]                                   // 主体：基线 y = 0
                    << "<g transform=\"translate(" << supX << "," << supY << ") scale(" << scale << ")\">"
                    << kids[1]                                   // 上标：已补偿基线
                    << "</g>"
                    << "<g transform=\"translate(" << subX << "," << subY << ") scale(" << scale << ")\">"
                    << kids[2]                                   // 下标：已补偿基线
                    << "</g>"
                    << "</g>";
                return os.str();
            });
        registerTag("msqrt",
            [this](const XMLElement* e, const Style& st) -> std::string
            {
                /* ---------- 1. 收集子元素 ---------- */
                std::vector<std::string> kids;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    kids.push_back(renderElement(c, st));
                if (kids.empty()) return "<!-- msqrt needs at least 1 child -->";

                /* ---------- 2. 水平拼接子元素 ---------- */
                std::string inner = hbox(kids);

                /* ---------- 3. 内容尺寸 ---------- */
                double em = std::stof(st.fontSize);
                double rule = 1.0;                       // 线厚
                double gap = 0.2 * em;                // 根号与内容间隙
                double hook = 0.4 * em;                // 左侧小勾宽度

                double contW = extractWidth(inner);
                double contAsc = extractAscent(inner);
                double contDes = extractDescent(inner);

                /* ---------- 4. 根号盒高（以内容基线为 0） ---------- */
                double boxAsc = contAsc + gap + rule;           // 内容顶部到基线
                double boxDes = contDes ;           // 基线到内容底部


                /* ---------- 5. 根号路径（相对于内容基线） ---------- */
                double left = hook;                     // 根号左侧勾起点
                double right = left + contW + gap;       // 根号横线终点
                double barY = -(contAsc + gap);                  // 横线 y 坐标（负值，在基线上方）

                std::ostringstream path;
                path << "M" << 0 << "," << -contAsc*0.5
                    << "L" << left << "," << 0
                    << "L" << (left) << "," << barY
                    << "L" << right << "," << barY ;

                /* ---------- 6. 组装 ---------- */
                std::ostringstream oss;
                oss << "<g class=\"msqrt\" data-w=\"" << (right + rule)
                    << "\" data-asc=\"" << boxAsc
                    << "\" data-desc=\"" << boxDes << "\">";
                oss << "<path d=\"" << path.str()
                    << "\" stroke=\"black\" fill=\"none\" stroke-width=\"" << rule << "\"/>";
                oss << "<g transform=\"translate(" << left + gap << ",0)\">"   // 内容基线 y = 0
                    << inner << "</g>";
                oss << "</g>";
                return oss.str();
            });
        registerTag("mroot",
            [this](const XMLElement* e, const Style& st) -> std::string
            {
                /* ---------- 1. 收集子元素 ---------- */
                std::vector<std::string> kids;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    kids.push_back(renderElement(c, st));
                if (kids.size() != 2) return "<!-- mroot needs exactly 2 children -->";

                /* ---------- 2. 被开方内容尺寸（基线 = 0） ---------- */
                double innerW = extractWidth(kids[0]);
                double innerAsc = extractAscent(kids[0]);
                double innerDes = extractDescent(kids[0]);

                /* ---------- 3. 指数原始尺寸 ---------- */
                double idxW0 = extractWidth(kids[1]);
                double idxAsc0 = extractAscent(kids[1]);
                double idxDes0 = extractDescent(kids[1]);
                double idxBase0 = 0;

                /* ---------- 4. 缩放后指数尺寸 ---------- */
                const double scale = 0.7;
                double idxW = idxW0 * scale;
                double idxAsc = idxAsc0 * scale;
                double idxDes = idxDes0 * scale;

                /* ---------- 5. 根号几何参数 ---------- */
                const double pad = 2.0;   // 内边距
                const double bar = 1.2;   // 横线超出系数
                const double vgap = 2.0;   // 横线与内容顶部间距
                const double tick = 6.0;   // 勾线水平段
                const double idxGap = 1.0;  // 指数与勾线空隙

                /* ---------- 6. 整体盒尺寸（以被开方内容基线为 0） ---------- */
                double bodyAsc = innerAsc + vgap + pad;   // 被开方内容顶部到基线
                double bodyDes = innerDes + pad;          // 被开方内容底部到基线
                double totalH = bodyAsc + bodyDes;
                double totalW = tick + innerW * bar + pad * 2;

                /* ---------- 7. 指数位置（基线对齐整体基线） ---------- */
                double idxX = -idxW - idxGap;   // 指数左上角 x
                /* 补偿缩放导致的基线偏移：先平移 -idxBase0*scale，再整体放到勾线左侧 */
                double idxY = -idxAsc - idxBase0 * scale;

                /* ---------- 8. 组装 ---------- */
                std::ostringstream oss;
                oss << "<g class=\"mroot\" data-w=\"" << totalW
                    << "\" data-h=\"" << totalH
                    << "\" data-asc=\"" << bodyAsc
                    << "\" data-desc=\"" << bodyDes
                    << "\" data-baseline=\"0\">"
                    /* 指数：基线对齐整体基线（y = 0） */
                    << "<g transform=\"translate(" << idxX << "," << idxY << ") scale(" << scale << ")\">"
                    << kids[1] << "</g>"
                    /* 根号勾线 + 横线（相对于基线） */
                    << "<path d=\"M0," << bodyAsc
                    << " L" << tick << "," << -vgap
                    << " L" << tick + innerW * bar << "," << -vgap
                    << "\" stroke=\"black\" fill=\"none\" stroke-width=\"1\"/>"
                    /* 被开方内容：左上角对齐勾线右侧，基线保持 y = 0 */
                    << "<g transform=\"translate(" << tick << ",0)\">"
                    << kids[0] << "</g>"
                    << "</g>";
                return oss.str();
            });
        /* 属性 */
        registerAttr("mathcolor", [](const XMLAttribute* a, Style& st) {
            st.fill = a->Value();
            });
        registerAttr("mathsize", [](const XMLAttribute* a, Style& st) {
            st.fontSize = a->Value();
            });
        registerAttr("mathvariant", [](const XMLAttribute* a, Style& st) {
            const char* v = a->Value();
            if (!strcmp(v, "italic")) st.fontStyle = "italic";
            else if (!strcmp(v, "bold")) st.fontWeight = "bold";
            });

/* ---------- mtd ---------- */
        registerTag("mtd",
            [this](const XMLElement* e, const Style& st) -> std::string {
                std::ostringstream inner;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    inner << renderElement(c, st);

                std::string s = inner.str();
                std::ostringstream os;
                os << "<g data-w=\"" << extractWidth(s)
                    << "\" data-asc=\"" << extractAscent(s)
                    << "\" data-desc=\"" << extractDescent(s) << "\">"
                    << s << "</g>";
                return os.str();
            });

        /* ---------- mtr ---------- */
        registerTag("mtr",
            [this](const XMLElement* e, const Style& st) -> std::string {
                std::ostringstream oss;
                for (const XMLElement* c = e->FirstChildElement("mtd"); c; c = c->NextSiblingElement("mtd"))
                    oss << renderElement(c, st);   // 每个 <mtd> 已自带 metrics
                return oss.str();
            });
        registerTag("mtable",
            [this](const XMLElement* e, const Style& st) -> std::string
            {
                /* ---------- 1. 解析属性 ---------- */
                const double defaultGap = 8.0;
                std::vector<double> colGap = { defaultGap };
                std::vector<double> rowGap = { defaultGap };

                auto parseList = [](const char* s, std::vector<double>& v, double def)
                    {
                        if (!s) return;
                        std::stringstream ss(s);
                        v.clear();
                        for (std::string t; std::getline(ss, t, ' '); )
                            v.push_back(std::stod(t));
                        if (v.empty()) v.push_back(def);
                    };
                parseList(e->Attribute("columnspacing"), colGap, defaultGap);
                parseList(e->Attribute("rowspacing"), rowGap, defaultGap);

                /* ---------- 2. 收集所有单元格 ---------- */
                std::vector<std::vector<std::string>> grid;   // 每行每列的 SVG 片段
                std::vector<std::vector<double>> wGrid, hGrid;
                size_t rows = 0, cols = 0;

                for (const XMLElement* r = e->FirstChildElement("mtr"); r; r = r->NextSiblingElement("mtr")) {
                    grid.emplace_back();
                    wGrid.emplace_back();
                    hGrid.emplace_back();
                    auto& rowCells = grid.back();
                    auto& rowW = wGrid.back();
                    auto& rowH = hGrid.back();

                    for (const XMLElement* c = r->FirstChildElement("mtd"); c; c = c->NextSiblingElement("mtd")) {
                        std::string cell = renderElement(c, st);   // 已带 data-w / data-asc / data-desc
                        rowCells.push_back(cell);
                        rowW.push_back(extractWidth(cell));
                        rowH.push_back(extractAscent(cell) - extractDescent(cell));
                    }
                    rows++;
                    cols = std::max(cols, rowCells.size());
                }
                if (rows == 0) return "<!-- empty mtable -->";

                /* ---------- 3. 统一列宽、行高 ---------- */
                std::vector<double> colW(cols, 0.0);
                std::vector<double> rowH(rows, 0.0);

                for (size_t r = 0; r < rows; ++r)
                    for (size_t c = 0; c < grid[r].size(); ++c)
                        colW[c] = std::max(colW[c], wGrid[r][c]);

                for (size_t r = 0; r < rows; ++r) {
                    double h = 0.0;
                    for (size_t c = 0; c < grid[r].size(); ++c)
                        h = std::max(h, hGrid[r][c]);
                    rowH[r] = h;
                }

                /* ---------- 4. 整体尺寸 ---------- */
                double totalW = std::accumulate(colW.begin(), colW.end(), 0.0)
                    + (cols ? (cols - 1) * colGap[0] : 0.0);
                double totalH = std::accumulate(rowH.begin(), rowH.end(), 0.0)
                    + (rows ? (rows - 1) * rowGap[0] : 0.0);

                double asc = totalH * 0.5;
                double des = -(totalH - asc);

                /* ---------- 5. 绝对坐标摆放 ---------- */
                std::ostringstream os;
                os << "<g class=\"mtable\" data-w=\"" << totalW
                    << "\" data-asc=\"" << asc
                    << "\" data-desc=\"" << des << "\">";

                double y = -asc;
                for (size_t r = 0; r < rows; ++r) {
                    double x = 0.0;
                    for (size_t c = 0; c < grid[r].size(); ++c) {
                        double dy = (rowH[r] - hGrid[r][c]) * 0.5;   // 垂直居中
                        os << "<g transform=\"translate(" << x << "," << (y + dy + extractAscent(grid[r][c])) << ")\">"
                            << grid[r][c]
                            << "</g>";
                        x += colW[c] + (c + 1 < cols ? colGap[0] : 0.0);
                    }
                    y += rowH[r] + (r + 1 < rows ? rowGap[0] : 0.0);
                }
                os << "</g>";
                return os.str();
            });
      
        registerTag("mlabeledtr",
            [this](const XMLElement* e, const Style& st) -> std::string
            {
                std::vector<std::string> cells;
                for (const XMLElement* c = e->FirstChildElement("mtd");
                    c; c = c->NextSiblingElement("mtd"))
                    cells.push_back(renderElement(c, st));

                if (cells.empty()) return "<!-- empty mlabeledtr -->";

                /* 列间距 */
                double colGap = 4.0;
                if (const char* gap = e->Attribute("columnspacing"))
                    colGap = std::stod(gap);

                /* 用 hbox 横向排布，但手动计算基线对齐 */
                double totalW = 0;
                double maxAsc = 0;
                double maxDes = 0;
                for (const auto& cell : cells) {
                    totalW += extractWidth(cell);
                    maxAsc = std::max(maxAsc, extractAscent(cell));
                    maxDes = std::max(maxDes, extractDescent(cell));
                }
                totalW += colGap * (cells.size() - 1);

                std::ostringstream oss;
                oss << "<g class=\"mlabeledtr\" data-w=\"" << totalW
                    << "\" data-h=\"" << (maxAsc + maxDes)
                    << "\" data-asc=\"" << maxAsc
                    << "\" data-desc=\"" << maxDes
                    << "\" data-baseline=\"0\">";

                double x = 0;
                for (const auto& cell : cells) {
                    double dx = x;
                    /* 垂直按基线对齐：单元格内部基线 0 → 行基线 0 */
                    double dy = maxAsc - extractAscent(cell);
                    oss << "<g transform=\"translate(" << dx << "," << dy << ")\">"
                        << cell << "</g>";
                    x += extractWidth(cell) + colGap;
                }
                oss << "</g>";
                return oss.str();
            });
        registerTag("mmultiscripts",
            [this](const XMLElement* e, const Style& st) -> std::string
            {
                /* ---------- 1. 收集节点 ---------- */
                std::vector<const XMLElement*> nodes;
                for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                    nodes.push_back(c);
                if (nodes.empty()) return "";

                /* ---------- 2. 分区 ---------- */
                size_t split = nodes.size();
                for (size_t i = 0; i < nodes.size(); ++i)
                    if (std::string(nodes[i]->Name()) == "mprescripts") { split = i; break; }

                const XMLElement* baseNode = nodes[0];
                std::vector<const XMLElement*> postNodes(nodes.begin() + 1, nodes.begin() + split);   // post: sub1 sup1 sub2 sup2 ...
                std::vector<const XMLElement*> preNodes(nodes.begin() + split + 1, nodes.end());      // pre : sup1 sub1 sup2 sub2 ...

                /* ---------- 3. 主字符 ---------- */
                std::string baseSVG = renderElement(baseNode, st);
                double baseW = extractWidth(baseSVG);
                double baseAsc = extractAscent(baseSVG);
                double baseDes = extractDescent(baseSVG);

                /* ---------- 4. 常量 ---------- */
                const double scale = 0.7;
                const double kern = 1.0;
                const double supGap = 1.5;
                const double subGap = 1.0;
                const double pairGap = 1.0;

                /* ---------- 5. 工具：一次性渲染并缓存尺寸 ---------- */
                struct ScriptRec {
                    std::string svg;
                    double w, asc, des;   // asc>0, des<0
                };
                auto makeRec = [&](const XMLElement* el) -> ScriptRec
                    {
                        if (!el || std::string(el->Name()) == "none")
                            return { "", 0, 0, 0 };

                        std::string raw = renderElement(el, st);
                        double w = extractWidth(raw) * scale;
                        double asc = extractAscent(raw) * scale;
                        double des = extractDescent(raw) * scale;   // 负值

                        std::ostringstream os;
                        os << "<g data-w=\"" << w
                            << "\" data-asc=\"" << asc
                            << "\" data-desc=\"" << des << "\">"
                            << "<g transform=\"scale(" << scale << ")\">"
                            << raw << "</g></g>";

                        return { os.str(), w, asc, des };
                    };

                /* ---------- 6. 收集列 ---------- */
                std::vector<ScriptRec> preSub, preSup, postSub, postSup;
                double preW = 0;
                double postW = 0;

                /* pre 区段：sup1 sub1 sup2 sub2 ... */
                for (size_t i = 0; i + 1 < preNodes.size(); i += 2)
                {
                    ScriptRec sup = makeRec(preNodes[i]);
                    ScriptRec sub = makeRec(preNodes[i + 1]);
                    preSup.push_back(sup);
                    preSub.push_back(sub);
                    preW += std::max(sup.w, sub.w) + pairGap;
                }
                if (!preSup.empty()) preW -= pairGap;

                /* post 区段：sub1 sup1 sub2 sup2 ... */
                for (size_t i = 0; i + 1 < postNodes.size(); i += 2)
                {
                    ScriptRec sub = makeRec(postNodes[i]);
                    ScriptRec sup = makeRec(postNodes[i + 1]);
                    postSub.push_back(sub);
                    postSup.push_back(sup);
                    postW += std::max(sub.w, sup.w) + pairGap;
                }
                if (!postSub.empty()) postW -= pairGap;

                /* ---------- 7. 整体盒尺寸 ---------- */
                double totalW = preW + kern + baseW + kern + postW;

                double maxSup = baseAsc;
                double minSub = baseDes;
                auto update = [&](const ScriptRec& r)
                    {
                        maxSup = std::max(maxSup, baseAsc+r.asc);
                        minSub = std::min(minSub, baseDes+r.des);
                    };
                for (const auto& r : preSup) update(r);
                for (const auto& r : preSub) update(r);
                for (const auto& r : postSub) update(r);
                for (const auto& r : postSup) update(r);

                /* ---------- 8. 组装 ---------- */
                std::ostringstream oss;
                oss << "<g class=\"mmultiscripts\" data-w=\"" << totalW
                    << "\" data-asc=\"" << maxSup
                    << "\" data-desc=\"" << minSub << "\">";

                double x = 0;

                /* pre 列（从右向左排） */
                for (int i = (int)preSup.size() - 1; i >= 0; --i)
                {
                    const ScriptRec& sup = preSup[i];
                    const ScriptRec& sub = preSub[i];
                    double w = std::max(sup.w, sub.w);
                    
                    double ySup = (supGap - sup.des);
                    double ySub = -(subGap + sub.asc);   // sub.des 负值
                    oss << "<g transform=\"translate(" << x << ",0)\">"
                        << "<g transform=\"translate(0," << ySup << ")\">" << sup.svg << "</g>"
                        << "<g transform=\"translate(0," << ySub << ")\">" << sub.svg << "</g>"
                        << "</g>";
                    x += w + pairGap;
                }

                /* 主字符 */
                oss << "<g transform=\"translate(" << x << ",0)\">" << baseSVG << "</g>";
                x += baseW + kern;

                /* post 列（从左向右排） */
                for (size_t i = 0; i < postSub.size(); ++i)
                {
                    const ScriptRec& sub = postSub[i];
                    const ScriptRec& sup = postSup[i];
                    double w = std::max(sub.w, sup.w);

                    double ySup = -(supGap + sup.asc);
                    double ySub = (subGap - sub.des);
                    oss << "<g transform=\"translate(" << x << ",0)\">"
                        << "<g transform=\"translate(0," << ySub << ")\">" << sub.svg << "</g>"
                        << "<g transform=\"translate(0," << ySup << ")\">" << sup.svg << "</g>"
                        << "</g>";
                    x += w + pairGap;
                }

                oss << "</g>";
                return oss.str();
            });
            registerTag("munder",
                [this](const XMLElement* e, const Style& st) -> std::string
                {
                    std::vector<std::string> kids;
                    for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                        kids.push_back(renderElement(c, st));
                    if (kids.size() != 2) return "<!-- munder needs 2 children -->";

                    const double em = std::stof(st.fontSize);
                    const double gap = 0.2 * em;
                    const double scale = 0.7;

                    const std::string& base = kids[0];
                    const std::string& sub = kids[1];

                    double baseW = extractWidth(base);
                    double baseAsc = extractAscent(base);
                    double baseDes = extractDescent(base);

                    double subW = extractWidth(sub) * scale;
                    double subAsc = extractAscent(sub) * scale;
                    double subDes = extractDescent(sub) * scale; 
        

                    /* ---------- 整体盒尺寸（以主字符基线为 0） ---------- */
                    double totalW = std::max(baseW, subW);
                    double baseX = (totalW - baseW) / 2.0;
                    double subX = (totalW - subW) / 2.0;

                    /* 下标位置：主字符底部 + 间隙，再补偿下标自身基线 */
                    double subY = (subAsc + gap - baseDes);

                    double totalAsc = baseAsc;                       // 最上沿
                    double totalDes = baseDes - gap - (subAsc - subDes); // 最下沿
     

                    std::ostringstream oss;
                    oss << "<g class=\"munder\" data-w=\"" << totalW
                        << "\" data-asc=\"" << totalAsc
                        << "\" data-desc=\"" << totalDes << "\" >";
                    oss << "<g transform=\"translate(" << baseX << ",0)\">" << base << "</g>";
                    oss << "<g transform=\"translate(" << subX << "," << subY << ")" << " scale(" << scale <<")\">" << sub << "</g>";
                    oss << "</g>";
                    return oss.str();
                });
            registerTag("munderover",
                [this](const XMLElement* e, const Style& st) -> std::string
                {
                    std::vector<std::string> kids;
                    for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                        kids.push_back(renderElement(c, st));
                    if (kids.size() != 3) return "<!-- munderover needs 3 children -->";

                    const double em = std::stof(st.fontSize);
                    const double gap = 0.2 * em;
                    const double scale = 0.7;

                    const std::string& base = kids[0];
                    const std::string& sub = kids[1];
                    const std::string& sup = kids[2];

                    /* 主字符 */
                    double baseW = extractWidth(base);
                    double baseAsc = extractAscent(base);
                    double baseDes = extractDescent(base);

                    /* 下标 */
                    double subW = extractWidth(sub) * scale;
                    double subAsc = extractAscent(sub) * scale;
                    double subDes = extractDescent(sub) * scale;


                    /* 上标 */
                    double supW = extractWidth(sup) * scale;
                    double supAsc = extractAscent(sup) * scale;
                    double supDes = extractDescent(sup) *scale;
  

                    /* ---------- 整体盒尺寸（以主字符基线为 0） ---------- */
                    double totalW = std::max({ baseW, subW, supW });
                    double baseX = (totalW - baseW) / 2.0;
                    double subX = (totalW - subW) / 2.0;
                    double supX = (totalW - supW) / 2.0;

                    /* 上标位置：主字符顶部上方 gap，再补偿上标自身基线 */
                    double supY = -(-supDes + baseAsc + gap);

                    /* 下标位置：主字符底部下方 gap，再补偿下标自身基线 */
                    double subY = (subAsc + gap - baseDes);

                    double totalAsc = baseAsc + gap + (supAsc - supDes); // 最上沿
                    double totalDes = baseDes - gap - (subAsc - subDes); // 最下沿
                    double totalH = totalAsc + totalDes;

                    std::ostringstream oss;
                    oss << "<g class=\"munderover\" data-w=\"" << totalW
                        << "\" data-asc=\"" << totalAsc
                        << "\" data-desc=\"" << totalDes << "\">";
                    oss << "<g transform=\"translate(" << supX << "," << supY << ") scale(" << scale<<  ")\">" << sup << "</g>";
                    oss << "<g transform=\"translate(" << baseX << ",0)\">" << base << "</g>";
                    oss << "<g transform=\"translate(" << subX << "," << subY << ") scale(" << scale << ")\">" << sub << "</g>";
                    oss << "</g>";
                    return oss.str();
                });
        // ------------------------------------------------------------------
// 1. mover  (上划线 / 下划线 / 上下箭头)
// 语法: <mover> base overscript </mover>
// ------------------------------------------------------------------
            registerTag("mover",
                [this](const XMLElement* e, const Style& st) -> std::string
                {
                    std::vector<std::string> kids;
                    for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                        kids.push_back(renderElement(c, st));
                    if (kids.size() != 2) return "<!-- mover needs 2 children -->";

                    const double scale = 0.7;
                    const double gap = 0.2 * std::stof(st.fontSize); // 0.2 em

                    const std::string& base = kids[0];
                    const std::string& over = kids[1];

                    /* 主字符 */
                    double baseW = extractWidth(base);
                    double baseAsc = extractAscent(base);
                    double baseDes = extractDescent(base);

                    /* 上标原始尺寸 */
                    double overW0 = extractWidth(over);
                    double overAsc0 = extractAscent(over);
                    double overDes0 = extractDescent(over);


                    /* 缩放后尺寸 */
                    double overW = overW0 * scale;
                    double overAsc = overAsc0 * scale;
                    double overDes = overDes0 * scale;

                    /* ---------- 整体盒尺寸（以主字符基线为 0） ---------- */
                    double totalW = std::max(baseW, overW);
                    double baseX = 0;                      // 主字符左上角 x
                    double overX = (totalW - overW) / 2.0;   // 上标居中

                    /* 上标位置：主字符顶部上方 gap，再补偿上标自身基线 */
                    double overY = -(-overDes + baseAsc + gap);

                    double totalAsc = baseAsc + gap + (overAsc - overDes); // 最上沿
                    double totalDes = baseDes;                           // 最下沿
                    double totalH = totalAsc + totalDes;

                    std::ostringstream oss;
                    oss << "<g class=\"mover\" data-w=\"" << totalW
                        << "\" data-asc=\"" << totalAsc
                        << "\" data-desc=\"" << totalDes
                        << "\">";
                    /* 主字符：基线 y=0 */
                    oss << "<g transform=\"translate(" << baseX << ",0)\">" << base << "</g>";
                    /* 上标：基线补偿后摆放 */
                    oss << "<g transform=\"translate(" << overX << "," << overY << ") scale(" << scale << ")\">"
                        << over << "</g>";
                    oss << "</g>";
                    return oss.str();
                });
        // ------------------------------------------------------------------
// 2. mpadded  (人工设置宽度/高度/深度)
// 语法: <mpadded width="..." height="..." depth="..."> child </mpadded>
// 单位：em，可省略符号
// ------------------------------------------------------------------
            registerTag("mpadded",
                [this](const XMLElement* e, const Style& st) -> std::string
                {
                    const XMLElement* child = e->FirstChildElement();
                    if (!child) return "";

                    std::string inner = renderElement(child, st);
                    double em = std::stof(st.fontSize);

                    /* 解析属性，缺省用子元素自身尺寸 */
                    double w = getDimAttr(e, "width", extractWidth(inner) / em) * em;
                    double asc = getDimAttr(e, "height", extractAscent(inner) / em) * em;
                    double des = getDimAttr(e, "depth", extractDescent(inner) / em) * em;

                    /* 子元素基线到盒上下沿的距离 */
                    double innerAsc = extractAscent(inner);
                    double innerDes = extractDescent(inner);

                    /* 居中放置：水平居中，垂直按基线对齐 */
                    double dx = (w - extractWidth(inner)) * 0.5;
                    double dy = asc - innerAsc;   // 使子元素基线落在 y=0

                    std::ostringstream oss;
                    oss << "<g class=\"mpadded\" data-w=\"" << w
                        << "\" data-asc=\"" << asc
                        << "\" data-desc=\"" << des
                        << "0\">"
                        << "<g transform=\"translate(" << dx << "," << dy << ")\">"
                        << inner
                        << "</g>"
                        << "</g>";
                    return oss.str();
                });

        // ------------------------------------------------------------------
        // 3. mphantom  (占位但不显示)
        // 语法: <mphantom> child </mphantom>
        // 与 mpadded 类似，但把内容设为透明
        // ------------------------------------------------------------------
            registerTag("mphantom",
                [this](const XMLElement* e, const Style& st) -> std::string
                {
                    const XMLElement* child = e->FirstChildElement();
                    if (!child) return "";

                    std::string inner = renderElement(child, st);

                    std::ostringstream oss;
                    oss << "<g class=\"mphantom\" data-w=\"" << extractWidth(inner)
                        << "\" data-asc=\"" << extractAscent(inner)
                        << "\" data-desc=\"" << extractDescent(inner) << "\">"
                        << "<g fill=\"transparent\">"
                        << inner
                        << "</g>"
                        << "</g>";
                    return oss.str();
                });

        // ----------------------------------------------------------
// 1. <none>  —— 空占位，宽 0，高/深 0
// ----------------------------------------------------------
            registerTag("none",
                [](const XMLElement*, const Style&) -> std::string
                {
                    return R"(<g class="none" data-w="0"  data-asc="0" data-desc="0" />)";
                });

            // ----------------------------------------------------------
            // mprescripts —— 占位节点，尺寸 0，基线 0
            // ----------------------------------------------------------
            registerTag("mprescripts",
                [](const XMLElement*, const Style&) -> std::string
                {
                    return R"(<g class="mprescripts" data-w="0"  data-asc="0" data-desc="0" />)";
                });

            // ----------------------------------------------------------
            // mspace —— 只产生水平间距
            // width 支持 2.5、2.5em、2.5ex 等写法
            // ----------------------------------------------------------
            registerTag("mspace",
                [](const XMLElement* e, const Style& st) -> std::string
                {
                    const char* w = e->Attribute("width");
                    double width = 0.0;

                    if (w) {
                        std::string s = w;
                        if (s.find("em") != std::string::npos) {
                            width = std::stod(s) * std::stof(st.fontSize);
                        }
                        else if (s.find("ex") != std::string::npos) {
                            width = std::stod(s) * std::stof(st.fontSize) * 0.5; // 1ex ≈ 0.5em
                        }
                        else {
                            width = std::stod(s);          // 纯数字，默认单位 em
                        }
                    }

                    std::ostringstream oss;
                    oss << "<g class=\"mspace\" data-w=\"" << width
                        << "\"  data-asc=\"0\" data-desc=\"0\" />";
                    return oss.str();
                });
    }
};

/* ---------- 单例 ---------- */
MathML2SVG& MathML2SVG::instance() {
    static MathML2SVG inst;
    return inst;
}
MathML2SVG::MathML2SVG() : pImpl(std::make_unique<Impl>()) {}
MathML2SVG::~MathML2SVG() = default;

/* 暴露接口 */
std::string MathML2SVG::convert(const std::string& mathml) {
    return pImpl->convert(mathml);
}
void MathML2SVG::registerTag(const std::string& tag, RenderFn fn) {
    pImpl->registerTag(tag, fn);
}
void MathML2SVG::registerAttr(const std::string& attr, AttrFn fn) {
    pImpl->registerAttr(attr, fn);
}



std::wstring GdiTextMeasurer::makeKey(const std::wstring& name, float size, int style) {
    return name + L'|' + std::to_wstring(size) + L'|' + std::to_wstring(style);
}

GdiTextMeasurer& GdiTextMeasurer::instance() {
    static GdiTextMeasurer inst;
    return inst;
}

GdiTextMeasurer::GdiTextMeasurer() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
}

GdiTextMeasurer::~GdiTextMeasurer() {
    // 所有 unique_ptr 会自动释放
}

GdiTextMeasurer::Size
GdiTextMeasurer::measure(const std::wstring& text,
    const std::wstring& fontName,
    float               fontSizePx,
    Gdiplus::FontStyle  style)
{
    std::wstring key = makeKey(fontName, fontSizePx, style);
    std::lock_guard lg(mtx_);

    auto& slot = cache_[key];
    if (!slot.font) {
        slot.family = std::make_unique<Gdiplus::FontFamily>(fontName.c_str());
        slot.font = std::make_unique<Gdiplus::Font>(slot.family.get(),
            fontSizePx,
            style,
            Gdiplus::UnitPixel);
    }

    HDC hdc = GetDC(nullptr);
    Gdiplus::Graphics g(hdc);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    // 测量
    Gdiplus::RectF bounds;
    g.MeasureString(text.c_str(), -1, slot.font.get(),
        Gdiplus::PointF(0, 0), &bounds);

    // 计算 ascent（像素）
    INT emHeight = slot.family->GetEmHeight(style);
    INT ascent = slot.family->GetCellAscent(style);
    float ascentPx = fontSizePx * ascent / emHeight;

    ReleaseDC(nullptr, hdc);
    return { bounds.Width, bounds.Height, ascentPx };
}

std::string GdiTextMeasurer::outlineToSVG(const std::wstring& text,
    const std::wstring& fontName,
    float fontSizePx,
    const std::string& fill)
{
    /* 1. 复用已有缓存字体 */
    auto& cached = cache_[makeKey(fontName, fontSizePx, Gdiplus::FontStyleRegular)];
    if (!cached.font) {
        cached.family = std::make_unique<Gdiplus::FontFamily>(fontName.c_str());
        cached.font = std::make_unique<Gdiplus::Font>(cached.family.get(),
            fontSizePx,
            Gdiplus::FontStyleRegular,
            Gdiplus::UnitPixel);
    }

    /* 2. 生成 GraphicsPath */
    Gdiplus::GraphicsPath path;
    path.AddString(text.c_str(), static_cast<int>(text.size()),
        cached.family.get(), Gdiplus::FontStyleRegular,
        fontSizePx, Gdiplus::PointF(0, 0), nullptr);

    /* 3. 转 SVG path */
    std::ostringstream d;
    d << "<path fill=\"" << fill << "\" d=\"";
    Gdiplus::PathData pd;
    if (path.GetPathData(&pd) != Gdiplus::Ok || pd.Count == 0) {
        d << "\"/>";
        return d.str();
    }

    for (int i = 0; i < pd.Count; ) {
        BYTE type = pd.Types[i] & Gdiplus::PathPointTypePathTypeMask;

        switch (type) {
        case Gdiplus::PathPointTypeStart:
            d << "M" << pd.Points[i].X << " " << pd.Points[i].Y;
            ++i;
            break;

        case Gdiplus::PathPointTypeLine:
            d << "L" << pd.Points[i].X << " " << pd.Points[i].Y;
            ++i;
            break;

        case Gdiplus::PathPointTypeBezier:
            if (i + 2 < pd.Count) {   // 确保有 3 个点
                d << "C"
                    << pd.Points[i].X << " " << pd.Points[i].Y << " "
                    << pd.Points[i + 1].X << " " << pd.Points[i + 1].Y << " "
                    << pd.Points[i + 2].X << " " << pd.Points[i + 2].Y;
                i += 3;
            }
            else {
                i += 1;               // 防御：点数不足，跳过
            }
            break;

        default:
            ++i;
            break;
        }

        if (pd.Types[i - 1] & Gdiplus::PathPointTypeCloseSubpath)
            d << "Z";
    }

    d << "\"/>";
    return d.str();
}

FreeTypeTextMeasurer& FreeTypeTextMeasurer::instance() {
    static FreeTypeTextMeasurer inst;
    return inst;
}

FreeTypeTextMeasurer::FreeTypeTextMeasurer() {
    if (FT_Init_FreeType(&ft_)) throw std::runtime_error("FT_Init_FreeType failed");
}

FreeTypeTextMeasurer::~FreeTypeTextMeasurer() {
    for (auto& kv : cache_) FT_Done_Face(kv.second.face);
    FT_Done_FreeType(ft_);
}

static std::wstring makeKey(const std::wstring& name, int style) {
    return name + L'|' + std::to_wstring(style);
}


FT_Face FreeTypeTextMeasurer::loadFace(const std::wstring& fontName, int style) {
    // 简单映射：Windows 字体目录
    wchar_t winFontPath[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_FONTS, nullptr, 0, winFontPath);

    std::wstring file = winFontPath;
    file += L"\\";
    if (fontName == L"Times New Roman") file += (style & 1) ? L"timesbd.ttf" : L"times.ttf";
    else if (fontName == L"Arial")      file += (style & 1) ? L"arialbd.ttf" : L"arial.ttf";
    else                                file += fontName + L".ttf";

    FT_Face face;
    if (FT_New_Face(instance().ft_, w2a(file).c_str(), 0, &face)) return nullptr;
    return face;
}

FreeTypeTextMeasurer::CachedFace& FreeTypeTextMeasurer::getFace(const std::wstring& fontName, int style) {
    std::wstring key = makeKey(fontName, style);
    std::lock_guard<std::mutex> lock(mtx_);
    auto& slot = cache_[key];
    if (!slot.face) {
        slot.face = loadFace(fontName, style);
        if (!slot.face) slot.face = loadFace(L"Times New Roman", 0); // fallback
        slot.emSize = slot.face->units_per_EM;
    }
    return slot;
}

FreeTypeTextMeasurer::Size
FreeTypeTextMeasurer::measure(const std::wstring& text,
    const std::wstring& fontName,
    float               fontSizePx,
    int                 style) {
    auto& slot = getFace(fontName, style);
    FT_Face face = slot.face;
    float scale = fontSizePx / slot.emSize;

    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)fontSizePx);

    float width = 0.f;
    float maxY = -FLT_MAX, minY = FLT_MAX;

    for (wchar_t ch : text) {
        FT_UInt idx = FT_Get_Char_Index(face, ch);
        if (FT_Load_Glyph(face, idx, FT_LOAD_DEFAULT)) continue;
        FT_GlyphSlot g = face->glyph;

        width += (g->advance.x >> 6);   // 26.6 固定小数 → 像素
        if (g->metrics.horiBearingY > maxY) maxY = g->metrics.horiBearingY >> 6;
        if (g->metrics.horiBearingY - g->metrics.height < minY)
            minY = (g->metrics.horiBearingY - g->metrics.height) >> 6;
    }

    Size sz;
    sz.width = width;
    sz.height = (maxY - minY);
    sz.ascent = maxY;
    sz.descent = minY;         // baseline → bottom（minY 为负值）
    return sz;
}

bool SimpleContainer::isImageCached(std::string src)
{
    if (m_img_cache.contains(src)) return true;
    return false;
}

void SimpleContainer::addImageCache(std::string hash, std::string svg)
{
    auto doc = lunasvg::Document::loadFromData(svg);
    if (!doc) return;

    lunasvg::Bitmap svgBmp = doc->renderToBitmap();
    if (svgBmp.isNull()) return;

    //svgBmp.convertToRGBA();   // 1. 原地转格式
    ImageFrame frame;
    frame.width = svgBmp.width();
    frame.height = svgBmp.height();
    frame.stride = frame.width * 4;
    frame.rgba.assign(
        reinterpret_cast<const uint8_t*>(svgBmp.data()),
        reinterpret_cast<const uint8_t*>(svgBmp.data()) + frame.stride * frame.height);

    m_img_cache.emplace(hash, std::move(frame));
}

std::string EPUBBook::get_title()
{
    auto titIt = ocf_pkg_.meta.find(L"dc:title");
    return titIt != ocf_pkg_.meta.end() ? w2a(titIt->second) : "";
}

std::string EPUBBook::get_author()
{
    auto titIt = ocf_pkg_.meta.find(L"dc:creator");
    return titIt != ocf_pkg_.meta.end() ? w2a(titIt->second) : "";
}

EPUBFileProvider::EPUBFileProvider()
{
    m_zfp = std::make_unique<ZipFileProvider>();
    //m_lfp = std::make_unique<LocalFileProvider>();
}

EPUBFileProvider::~EPUBFileProvider()
{
    m_file_cache.clear();
}

bool EPUBFileProvider::load(const std::wstring& file_path)
{
    m_zfp->load(file_path);
    return false;
}

MemFile EPUBFileProvider::get(std::wstring path) 
{
    auto it = m_file_cache.find(path);
    if (it != m_file_cache.end()) { return it->second; }

    MemFile mf{};
    if (fs::path(path).is_absolute()) { mf = m_lfp->get(path); }
    else { mf = m_zfp->get(path); }
 
    if (!mf.data.empty())
    {
        m_file_cache.emplace(path, mf);
    }
    return mf;
}

std::wstring EPUBFileProvider::find(const std::wstring& path)
{
    return std::wstring();
}

