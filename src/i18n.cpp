#include "i18n.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace i18n {
namespace {

// Deutsch -> English. Eintraege sind exakt die deutschen Texte aus dem Quelltext (inkl. Satzzeichen); die Reihenfolge folgt dem Fenster.
const Pair kTable[] = {
#include "i18n_table.inc"
};

std::atomic<int> g_setting{0};

bool WindowsIsGerman() { return (GetUserDefaultUILanguage() & 0xFF) == LANG_GERMAN; }

struct Lookup {
    std::mutex                                                  mutex;
    std::unordered_map<std::wstring, const wchar_t*>            byText;
    std::unordered_map<const wchar_t*, const wchar_t*>          byPointer; // Zeiger der Quelltext-Literale -> Uebersetzung (schnell)
    Lookup()
    {
        for (const Pair& p : kTable) byText.emplace(p.de, p.en);
    }
};
Lookup& L()
{
    static Lookup l;
    return l;
}

} // namespace

void SetSetting(int setting) { g_setting = setting < 0 || setting > 2 ? 0 : setting; }
int  Setting() { return g_setting.load(); }

Lang Current()
{
    const int s = g_setting.load();
    if (s == 1) return Lang::German;
    if (s == 2) return Lang::English;
    return WindowsIsGerman() ? Lang::German : Lang::English;
}

const wchar_t* Tr(const wchar_t* de)
{
    if (!de || Current() == Lang::German) return de;
    Lookup&                     l = L();
    std::lock_guard<std::mutex> lk(l.mutex);
    const auto                  p = l.byPointer.find(de);
    if (p != l.byPointer.end()) return p->second;
    const auto t = l.byText.find(de);
    const wchar_t* en = t != l.byText.end() ? t->second : de;
    l.byPointer.emplace(de, en);
    return en;
}

std::wstring Tr(const std::wstring& de)
{
    if (Current() == Lang::German) return de;
    Lookup&                     l = L();
    std::lock_guard<std::mutex> lk(l.mutex);
    const auto                  t = l.byText.find(de);
    return t != l.byText.end() ? std::wstring(t->second) : de;
}

std::string Tr8(const char* deUtf8)
{
    if (!deUtf8) return "";
    if (Current() == Lang::German) return deUtf8;
    const int n = MultiByteToWideChar(CP_UTF8, 0, deUtf8, -1, nullptr, 0);
    if (n <= 1) return deUtf8;
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, deUtf8, -1, &w[0], n);
    const std::wstring en = Tr(w);
    if (en == w) return deUtf8;
    const int m = WideCharToMultiByte(CP_UTF8, 0, en.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(m > 1 ? m - 1 : 0), '\0');
    if (m > 1) WideCharToMultiByte(CP_UTF8, 0, en.c_str(), -1, &out[0], m, nullptr, nullptr);
    return out;
}

const Pair* Table(size_t* count)
{
    *count = sizeof(kTable) / sizeof(kTable[0]);
    return kTable;
}

} // namespace i18n
