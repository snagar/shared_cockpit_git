// shared_cockpit - open a web page in the computer's browser (0.9.23)
#include "shared/ui/Links.h"

#include "shared/log/Log.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace sc::ui {

namespace {

// True for a plain http(s) address without characters a shell would read.
bool SafeUrl_cai(const std::string& url)
{
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
        return false;
    for (const char c : url)
        if (c <= ' ' || c == '\'' || c == '"' || c == '\\' || c == '`' || c == '$' || c == ';' || c == '|' || c == '<' ||
            c == '>' || c == '(' || c == ')')
            return false;
    return true;
}

} // namespace

// Opens `url` in the default browser.
bool OpenUrl_cai(const std::string& url)
{
    if (!SafeUrl_cai(url)) {
        LOG_WARN("open link: refused '{}'", url);
        return false;
    }
#if defined(_WIN32)
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    const bool ok = result > 32;                    // ShellExecute: above 32 is success
#elif defined(__APPLE__)
    const bool ok = std::system(("open '" + url + "' >/dev/null 2>&1 &").c_str()) == 0;
#else
    // X-Plane's LD_LIBRARY_PATH must not leak into the browser.
    const bool ok = std::system(("env -u LD_LIBRARY_PATH xdg-open '" + url + "' >/dev/null 2>&1 &").c_str()) == 0;
#endif
    if (ok)
        LOG_INFO("open link: {}", url);
    else
        LOG_WARN("open link: could not start the browser for {}", url);
    return ok;
}

} // namespace sc::ui
