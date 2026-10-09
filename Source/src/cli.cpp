// Command line parsing. Accepts this program's own switches and the syntax of the author's
// DesktopCmd.exe 1.7.2, so existing scripts keep working:
//   DesktopCmd [<save | restore | query> </n "Layout" | /f "filepath.dtr"> /y /?]
#include "app.h"

#include <cwctype>

namespace dr {

static std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static bool IsVerb(const std::wstring& v) {
    static const wchar_t* verbs[] = {L"save",     L"restore",     L"query", L"custom",  L"highlight", L"settings",
                                     L"savefile", L"restorefile", L"diag",  L"install", L"uninstall"};
    for (const wchar_t* k : verbs)
        if (v == k) return true;
    return false;
}

// True for a token that is one of our switches, so "/n /y" can't swallow "/y" as a layout name.
static bool IsSwitchToken(const std::wstring& a) {
    if (a.size() < 2 || (a[0] != L'/' && a[0] != L'-')) return false;
    const std::wstring k = Lower(a.substr(1));
    return IsVerb(k) || k == L"q" || k == L"quiet" || k == L"y" || k == L"n" || k == L"f" || k == L"?" ||
           k == L"help";
}

CommandLine ParseCommandLine(const std::vector<std::wstring>& args) {
    CommandLine c;
    std::wstring operand, layoutArg, fileArg;
    bool hasLayoutArg = false, hasFileArg = false;
    auto setVerb = [&c](const std::wstring& v) {
        if (!c.verb.empty() && c.verb != v) c.bad = true;
        c.verb = v;
    };
    for (size_t i = 0; i < args.size(); i++) {
        const std::wstring& a = args[i];
        const bool isSwitch = a.size() > 1 && (a[0] == L'/' || a[0] == L'-');
        const std::wstring key = Lower(isSwitch ? a.substr(1) : a);
        if (isSwitch && (key == L"q" || key == L"quiet" || key == L"y")) {
            c.quiet = true;
        } else if (isSwitch && (key == L"n" || key == L"f")) {
            bool& has = key == L"n" ? hasLayoutArg : hasFileArg;
            if (has || i + 1 >= args.size() || IsSwitchToken(args[i + 1])) {
                c.bad = true;
                break;
            }
            (key == L"n" ? layoutArg : fileArg) = args[++i];
            has = true;
        } else if (isSwitch && (key == L"?" || key == L"help")) {
            setVerb(L"help");
        } else if (isSwitch && IsVerb(key)) {
            setVerb(key);
        } else if (!isSwitch && i == 0 && (key == L"save" || key == L"restore" || key == L"query")) {
            setVerb(key);  // DesktopCmd style: the action comes first, without a slash
        } else if (!isSwitch && operand.empty()) {
            operand = a;
        } else {
            c.bad = true;
        }
    }

    if (c.verb == L"query") c.verb = L"custom";  // DesktopCmd lists layouts and asks which to restore
    if (hasFileArg && (c.verb == L"save" || c.verb == L"restore")) c.verb += L"file";  // save /f x.dtr
    const bool fileVerb = c.verb == L"savefile" || c.verb == L"restorefile" || c.verb == L"diag";
    const bool layoutVerb = c.verb == L"save" || c.verb == L"restore";
    if (fileVerb) {
        if (hasLayoutArg || (hasFileArg && !operand.empty())) c.bad = true;
        c.file = hasFileArg ? fileArg : operand;
        if (c.file.empty() && c.verb != L"diag") c.bad = true;
    } else if (layoutVerb) {
        if (hasLayoutArg && !operand.empty()) c.bad = true;
        c.layout = hasLayoutArg ? layoutArg : operand;
    } else if (!operand.empty() || hasLayoutArg || hasFileArg) {
        c.bad = true;  // nothing else takes an argument
    }
    if (c.bad) c.verb = L"help";
    return c;
}

}  // namespace dr
