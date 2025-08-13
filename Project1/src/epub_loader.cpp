#include "epub_loader.h"
#include <miniz/miniz.h>
#include <tinyxml2.h>
#include <shlobj.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace tinyxml2;

static std::wstring temp_dir() {
    wchar_t buf[MAX_PATH]{};
    GetTempPath(MAX_PATH, buf);
    return buf;
}

static bool unzip(const wchar_t* zip, const wchar_t* dest) {
    mz_zip_archive ar{};
    if (!mz_zip_reader_init_file(&ar, zip, 0)) return false;
    mz_zip_reader_extract_all(&ar, dest);
    mz_zip_reader_end(&ar);
    return true;
}

static std::wstring combine(const std::wstring& base, const std::wstring& rel) {
    wchar_t out[MAX_PATH]{};
    PathCombine(out, base.c_str(), rel.c_str());
    return out;
}

void epub_book::load(const wchar_t* epub_path) {
    // 1. ½âÑ¹
    wchar_t uuid[64]; swprintf_s(uuid, L"%08x", GetTickCount());
    unzip_dir = temp_dir() + L"epub_" + uuid + L"\\";
    CreateDirectory(unzip_dir.c_str(), nullptr);
    unzip(epub_path, unzip_dir.c_str());

    // 2. container.xml
    std::wstring container = unzip_dir + L"META-INF\\container.xml";
    XMLDocument doc; doc.LoadFile(ws2s(container).c_str());
    XMLElement* rootfile = doc.FirstChildElement("container")
        ->FirstChildElement("rootfiles")
        ->FirstChildElement("rootfile");
    std::string opf_path = rootfile->Attribute("full-path");
    std::wstring opf = combine(unzip_dir, s2ws(opf_path));

    // 3. content.opf
    XMLDocument opf_doc; opf_doc.LoadFile(ws2f(opf).c_str());
    XMLElement* manifest = opf_doc.RootElement()->FirstChildElement("manifest");
    for (XMLElement* item = manifest->FirstChildElement("item"); item;
        item = item->NextSiblingElement("item")) {
        id2path[item->Attribute("id")] = combine(unzip_dir,
            s2ws(item->Attribute("href")));
    }
    XMLElement* spine_el = opf_doc.RootElement()->FirstChildElement("spine");
    for (XMLElement* item = spine_el->FirstChildElement("itemref"); item;
        item = item->NextSiblingElement("itemref")) {
        std::string id = item->Attribute("idref");
        spine.push_back(id2path[id]);
    }
}
