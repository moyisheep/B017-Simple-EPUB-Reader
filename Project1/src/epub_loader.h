#pragma once
#include <string>
#include <vector>
#include <map>

struct epub_book {
    std::wstring unzip_dir;
    std::vector<std::wstring> spine;
    std::map<std::wstring, std::wstring> id2path;
    void load(const wchar_t* epub_path);
};