#include "text.h"

#include <mutex>
#include <unordered_map>

namespace nebula::text {

namespace {

wstring Unescape(const wstring& cell) { return Replace(Replace(cell, L"\\n", L"\n"), L"\\\\", L"\\"); }

// The table's column for the language; empty for English, the words the code already has.
wstring Column() {
    wstring language = L"auto";
    try {
        wstring settings = PathCombine(paths::DataRoot(), L"runtime_settings.ini");
        if (FileExists(settings)) {
            for (const wstring& line : ReadAllLines(settings)) {
                size_t equals = line.find(L'=');
                if (equals != wstring::npos && equals > 0 && Trim(line.substr(0, equals)) == L"language") language = ToLower(Trim(line.substr(equals + 1)));
            }
        }
    } catch (const std::exception&) {
    }
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    wstring name;
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0) name = locale;
    ULONG count = 0, size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0) {
        std::vector<wchar_t> languages(size);
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, languages.data(), &size) && languages[0] != 0) name = languages.data();
    }
    if (language == L"auto") language = ToLower(name.substr(0, name.find(L'-')));
    const bool americas = !StartsWith(name, L"es-ES", true);
    if (language == L"french" || language == L"fr") return EndsWith(name, L"-CA", true) || EndsWith(name, L"-US", true) ? L"UsFrench" : L"EuFrench";
    if (language == L"spanish" || language == L"es") return americas ? L"UsSpanish" : L"EuSpanish";
    if (language == L"german" || language == L"de") return L"EuGerman";
    if (language == L"italian" || language == L"it") return L"EuItalian";
    if (language == L"dutch" || language == L"nl") return L"EuDutch";
    if (language == L"japanese" || language == L"ja") return L"JpJapanese";
    if (language == L"portuguese" || language == L"pt") return L"UsPortuguese";
    if (language == L"korean" || language == L"ko") return L"KrKorean";
    if (language == L"chinese" || language == L"zh" || language == L"simplifiedchinese" || language == L"traditionalchinese") {
        bool traditional = language == L"traditionalchinese" || IFind(name, L"Hant") != wstring::npos || EndsWith(name, L"-TW", true) ||
                           EndsWith(name, L"-HK", true) || EndsWith(name, L"-MO", true);
        if (language == L"simplifiedchinese") traditional = false;
        return traditional ? L"TwTradChinese" : L"CnSimpChinese";
    }
    return L"";
}

wstring Sister(const wstring& column) {
    if (StartsWith(column, L"Us")) return L"Eu" + column.substr(2);
    if (StartsWith(column, L"Eu")) return L"Us" + column.substr(2);
    return column;
}

const std::unordered_map<wstring, wstring>& Words() {
    static const std::unordered_map<wstring, wstring> words = [] {
        std::unordered_map<wstring, wstring> table;
        try {
            const wstring column = Column();
            if (column.empty()) return table;
            HMODULE module = GetModuleHandleW(nullptr);
            HRSRC resource = FindResourceW(module, L"NEBULATEXT", MAKEINTRESOURCEW(10));
            if (!resource) return table;
            const char* data = static_cast<const char*>(LockResource(LoadResource(module, resource)));
            const DWORD size = SizeofResource(module, resource);
            if (!data || size == 0) return table;
            const std::vector<wstring> lines = Split(Widen(std::string(data, size)), L'\n');
            if (lines.empty()) return table;
            const std::vector<wstring> header = Split(TrimEnd(lines[0], L"\r"), L'\t');
            auto index = [&](const wstring& name) -> size_t {
                for (size_t i = 0; i < header.size(); i++)
                    if (header[i] == name) return i;
                return wstring::npos;
            };
            const size_t own = index(column), sister = index(Sister(column)), english = index(L"UsEnglish");
            if (own == wstring::npos || english == wstring::npos) return table;
            for (size_t n = 1; n < lines.size(); n++) {
                const std::vector<wstring> cells = Split(TrimEnd(lines[n], L"\r"), L'\t');
                if (english >= cells.size()) continue;
                const wstring key = Unescape(cells[english]);
                if (key.empty() || table.count(key)) continue;  // the first row with these words
                wstring word = own < cells.size() ? cells[own] : L"";
                if (word.empty() && sister != wstring::npos && sister < cells.size()) word = cells[sister];
                if (!word.empty()) table.emplace(key, Unescape(word));
            }
        } catch (const std::exception&) {
            // the English given
        }
        return table;
    }();
    return words;
}

}  // namespace

wstring Of(const wchar_t* english) {
    const auto& words = Words();
    auto found = words.find(english);
    return found != words.end() ? found->second : wstring(english);
}

wstring Of(const wchar_t* english, std::initializer_list<wstring> values) {
    wstring text = Of(english);
    size_t index = 0;
    for (const wstring& value : values) text = Replace(text, L"{" + Num(static_cast<long long>(index++)) + L"}", value);
    return text;
}

}  // namespace nebula::text
