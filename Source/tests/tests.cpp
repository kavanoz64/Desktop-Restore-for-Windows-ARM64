// Unit tests for storage formats and the restore algorithm (mock desktop). Run: wine tests.exe
#include <shlwapi.h>

#include <cstdio>

#include "app.h"

using namespace dr;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (c) {                                                         \
            g_pass++;                                                    \
        } else {                                                         \
            g_fail++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)

static bool Eq(POINT a, POINT b) { return a.x == b.x && a.y == b.y; }

class MockDesktop : public IDesktop {
public:
    std::vector<DesktopItem> items;
    SIZE spacing{75, 100};
    bool autoArrange = false, snap = false;
    std::vector<int> selected;
    int sets = 0;
    int Count() override { return static_cast<int>(items.size()); }
    bool Item(int i, DesktopItem& out) override {
        out = items[static_cast<size_t>(i)];
        return !out.path.empty();
    }
    bool GetPos(int i, POINT& p) override {
        p = items[static_cast<size_t>(i)].pos;
        return true;
    }
    bool SetPos(int i, POINT p) override {
        if (snap) p.x = (p.x / spacing.cx) * spacing.cx;  // simulates a view that snaps to a grid
        items[static_cast<size_t>(i)].pos = p;
        sets++;
        return true;
    }
    SIZE Spacing() override { return spacing; }
    bool AutoArrange() override { return autoArrange; }
    void Select(const std::vector<int>& v) override { selected = v; }
    bool blockArrange = false;  // simulates Auto arrange that can't be switched off
    int begins = 0;
    bool BeginRestore() override {
        begins++;
        return !blockArrange;
    }
    void Add(const wchar_t* path, const wchar_t* name, int x, int y) {
        DesktopItem it;
        it.path = path;
        it.name = name;
        it.pos = POINT{x, y};
        items.push_back(it);
    }
};

static Monitor Mon(RECT rc, RECT work, bool primary) {
    Monitor m;
    m.rcMonitor = rc;
    m.rcWork = work;
    m.primary = primary;
    return m;
}

static DisplayConfig OneMonitor() {
    DisplayConfig dc;
    dc.systemMonitorCount = 1;
    dc.screenCx = 1920;
    dc.screenCy = 1080;
    dc.monitors.push_back(Mon(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1040}, true));
    dc.SetViewOrigin(POINT{0, 0});
    return dc;
}

// Primary 1920x1080 at (0,0); second monitor 2560x1440 to the left and higher.
static DisplayConfig TwoMonitors() {
    DisplayConfig dc;
    dc.systemMonitorCount = 2;
    dc.screenCx = 1920;
    dc.screenCy = 1080;
    dc.monitors.push_back(Mon(RECT{0, 0, 1920, 1080}, RECT{0, 0, 1920, 1040}, true));
    dc.monitors.push_back(Mon(RECT{-2560, -200, 0, 1240}, RECT{-2560, -200, 0, 1200}, false));
    dc.SetViewOrigin(POINT{-2560, -200});
    return dc;
}

static void TestLayoutNames() {
    CHECK(LayoutName(OneMonitor(), 48, true) == L"Layout 1920 x 1080 (48)");
    CHECK(LayoutName(OneMonitor(), 48, false) == L"Layout 1920 x 1080");
    CHECK(LayoutName(OneMonitor(), 0, true) == L"Layout 1920 x 1080");
    CHECK(LayoutName(TwoMonitors(), 32, true) == L"Multi-Monitor *[1920 x 1080][2560 x 1440] (32)");
}

static void TestRegistryRoundTrip() {
    DisplayConfig dc = TwoMonitors();
    MockDesktop d;
    d.Add(L"::{645FF040-5081-101B-9F08-00AA002F954E}", L"Recycle Bin", 2560 + 21, 200 + 2);  // primary
    d.Add(L"C:\\Users\\s\\Desktop\\Notes.txt", L"Notes", 100, 300);                        // second
    d.Add(L"C:\\Users\\s\\Desktop\\Off.lnk", L"Off", 9000, 9000);                           // nowhere
    auto recs = CaptureLayout(d, dc);
    CHECK(recs.size() == 3);
    CHECK(recs[0].monitor == 0 && recs[0].x == 21 && recs[0].y == 2 && recs[0].index == 1);
    CHECK(recs[1].monitor == 1 && recs[1].x == 100 && recs[1].y == 300);
    CHECK(recs[2].monitor == -1 && recs[2].x == 9000);
    CHECK(SaveLayoutReg(L"Test Layout", recs));
    CHECK(LayoutExists(L"Test Layout"));
    std::vector<IconRecord> back;
    CHECK(LoadLayoutReg(L"Test Layout", back));
    CHECK(back.size() == 3);
    if (back.size() == 3) {
        CHECK(back[1].path == recs[1].path && back[1].name == L"Notes" && back[1].monitor == 1);
        ToViewCoords(back, dc);
        for (size_t i = 0; i < 3; i++) CHECK(Eq(POINT{back[i].x, back[i].y}, d.items[i].pos));
    }
    // Second monitor moved to the right of the primary: its icons follow it.
    DisplayConfig moved = dc;
    moved.monitors[1].rcMonitor = RECT{1920, 0, 4480, 1440};
    moved.monitors[1].rcWork = moved.monitors[1].rcMonitor;
    moved.SetViewOrigin(POINT{0, 0});
    std::vector<IconRecord> again;
    LoadLayoutReg(L"Test Layout", again);
    ToViewCoords(again, moved);
    CHECK(again.size() == 3 && again[1].x == 1920 + 100 && again[1].y == 300);
    CHECK(again.size() == 3 && again[0].x == 21 && again[0].y == 2 && again[2].x == 9000);
    // Listing skips stray Icon#### keys; delete works.
    HKEY h;
    RegCreateKeyExW(HKEY_CURRENT_USER, (RegistryRoot() + L"\\Icon0042").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                    &h, nullptr);
    RegCloseKey(h);
    auto list = ListLayouts();
    bool hasTest = false, hasIcon = false;
    for (auto& n : list) {
        hasTest |= n == L"Test Layout";
        hasIcon |= n == L"Icon0042";
    }
    CHECK(hasTest && !hasIcon);
    CHECK(DeleteLayoutReg(L"Test Layout"));
    CHECK(!LayoutExists(L"Test Layout"));
}

static void TestLegacyRegistry() {
    HKEY h;
    RegCreateKeyExW(HKEY_CURRENT_USER, (RegistryRoot() + L"\\Old").c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h,
                    nullptr);
    DWORD v = MAKELONG(120, 340);
    RegSetValueExW(h, L"My Computer", 0, REG_DWORD, reinterpret_cast<BYTE*>(&v), 4);
    BYTE bin[8 + 12] = {};
    LONG xy[2] = {500, 600};
    memcpy(bin, xy, 8);
    const wchar_t lbl[] = L"Short";
    memcpy(bin + 8, lbl, sizeof(lbl));
    RegSetValueExW(h, L"Short.lnk", 0, REG_BINARY, bin, 8 + sizeof(lbl));
    RegCloseKey(h);
    std::vector<IconRecord> recs;
    CHECK(LoadLayoutReg(L"Old", recs));
    CHECK(recs.size() == 2);
    for (auto& r : recs) {
        if (r.legacy) CHECK(r.name == L"My Computer" && r.x == 120 && r.y == 340);
        else CHECK(r.oldFormat && r.path == L"Short.lnk" && r.name == L"Short" && r.x == 500 && r.y == 600);
    }
    // 1.5.x record matches by file name; legacy record matches by label.
    MockDesktop d;
    d.Add(L"C:\\Users\\s\\Desktop\\Short.lnk", L"Short", 0, 0);
    d.Add(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", L"My Computer", 80, 0);
    std::vector<DesktopItem> items = d.items;
    auto m = MatchRecords(recs, items);
    CHECK(m.size() == 2);
    DeleteLayoutReg(L"Old");
}

static std::wstring TempFile(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

static void TestDtrFiles() {
    DisplayConfig dc = TwoMonitors();
    MockDesktop d;
    d.Add(L"C:\\Users\\s\\Desktop\\a b=c;d.txt", L"a b=c;d", 2600, 250);
    d.Add(L"::{645FF040-5081-101B-9F08-00AA002F954E}", L"Recycle Bin", 10, 10);
    auto recs = CaptureLayout(d, dc);
    std::wstring f = TempFile(L"dr_test.dtr");
    CHECK(SaveLayoutFile(f, recs));
    HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    BYTE head[4] = {};
    DWORD got = 0;
    ReadFile(h, head, 4, &got, nullptr);
    CloseHandle(h);
    CHECK(got == 4 && head[0] == 0xFF && head[1] == 0xFE && head[2] == ';');
    std::vector<IconRecord> back;
    CHECK(LoadLayoutFile(f, back));
    CHECK(back.size() == 2);
    if (back.size() == 2) {
        CHECK(back[0].path == recs[0].path && back[0].name == recs[0].name);
        CHECK(back[0].x == recs[0].x && back[0].y == recs[0].y && back[0].monitor == recs[0].monitor);
        CHECK(back[1].monitor == 1 && !back[1].legacy);
    }
    DeleteFileW(f.c_str());

    // A file as written by the original 1.7 (WritePrivateProfileString), including an off-screen icon.
    const wchar_t* orig =
        L"\xFEFF;\r\n; Desktop Restore: 1.7.0.040. UNICODE Layout File.\r\n;\r\n\r\n"
        L"[Icon0001]\r\nLegacy=0\r\nXCoord=21\r\nYCoord=2\r\nName=Recycle Bin\r\n"
        L"Path=::{645FF040-5081-101B-9F08-00AA002F954E}\r\nMonitor=0\r\n"
        L"[Icon0002]\r\nLegacy=0\r\nXCoord=-40\r\nYCoord=5000\r\nName=Lost\r\nPath=C:\\Lost.txt\r\nMonitor=-1\r\n"
        L"[_DesktopRestore_]\r\nVersion=858673732\r\n";
    f = TempFile(L"dr_orig.dtr");
    h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    WriteFile(h, orig, static_cast<DWORD>(wcslen(orig) * 2), &got, nullptr);
    CloseHandle(h);
    CHECK(LoadLayoutFile(f, back));
    CHECK(back.size() == 2);
    if (back.size() == 2) {
        CHECK(back[0].name == L"Recycle Bin" && back[0].monitor == 0 && back[0].x == 21);
        CHECK(back[1].monitor == -1 && back[1].x == -40 && back[1].y == 5000);
    }
    DeleteFileW(f.c_str());
    CHECK(!LoadLayoutFile(TempFile(L"does_not_exist.dtr"), back));
}

static std::vector<IconRecord> Targets(const MockDesktop& d, const std::vector<POINT>& pts) {
    std::vector<IconRecord> recs;
    for (size_t i = 0; i < pts.size(); i++) {
        IconRecord r;
        r.path = d.items[i].path;
        r.name = d.items[i].name;
        r.x = pts[i].x;
        r.y = pts[i].y;
        r.index = static_cast<int>(i) + 1;
        recs.push_back(r);
    }
    return recs;
}

static void TestRestoreScrambled() {
    DisplayConfig dc = OneMonitor();
    MockDesktop d;
    std::vector<POINT> want;
    for (int i = 0; i < 12; i++) {
        d.Add(Format(L"C:\\D\\f%d.txt", i).c_str(), Format(L"f%d", i).c_str(), 0, 0);
        want.push_back(POINT{21 + (i / 5) * 75, 2 + (i % 5) * 100});
    }
    // Scramble: reverse the targets so each target is occupied by another icon.
    for (int i = 0; i < 12; i++) d.items[static_cast<size_t>(i)].pos = want[static_cast<size_t>(11 - i)];
    auto recs = Targets(d, want);
    RestoreResult r = RestoreLayout(d, dc, recs, false);
    CHECK(r.matched == 12 && r.placed == 12);
    for (int i = 0; i < 12; i++) CHECK(Eq(d.items[static_cast<size_t>(i)].pos, want[static_cast<size_t>(i)]));
}

static void TestConflictAndForceVisible() {
    DisplayConfig dc = OneMonitor();
    MockDesktop d;
    d.Add(L"C:\\D\\a.txt", L"a", 500, 500);
    d.Add(L"C:\\D\\b.txt", L"b", 600, 500);
    d.Add(L"C:\\D\\new.txt", L"new", 21, 2);  // not in the layout, sits on a's target
    std::vector<IconRecord> recs = Targets(d, {POINT{21, 2}, POINT{5000, 5000}});
    recs.resize(2);
    RestoreResult r = RestoreLayout(d, dc, recs, true);
    CHECK(r.matched == 2 && r.displaced >= 1);
    CHECK(Eq(d.items[0].pos, POINT{21, 2}));
    POINT n = d.items[2].pos;
    CHECK(!(n.x == 21 && n.y == 2));                            // moved away
    CHECK(dc.MonitorAt(d.items[1].pos) == 0 && r.madeVisible == 1);  // b pulled back on screen
    RECT a{21, 2, 21 + 75, 2 + 100}, nb{n.x, n.y, n.x + 75, n.y + 100}, tmp;
    CHECK(!IntersectRect(&tmp, &a, &nb));
    // Without Force Visible the off-screen target is kept.
    MockDesktop d2;
    d2.Add(L"C:\\D\\a.txt", L"a", 500, 500);
    RestoreLayout(d2, dc, Targets(d2, {POINT{5000, 5000}}), false);
    CHECK(Eq(d2.items[0].pos, POINT{5000, 5000}));
}

static void TestMatching() {
    // Moved Desktop folder (other PC / OneDrive): paths differ, labels match.
    MockDesktop d;
    d.Add(L"C:\\Users\\new\\OneDrive\\Desktop\\Report.docx", L"Report", 0, 0);
    d.Add(L"C:\\Users\\new\\OneDrive\\Desktop\\Report.lnk", L"Report", 0, 100);
    d.Add(L"C:\\Users\\new\\OneDrive\\Desktop\\Other.txt", L"Other", 0, 200);
    std::vector<IconRecord> recs;
    IconRecord r;
    r.path = L"C:\\Users\\new\\OneDrive\\Desktop\\Report.lnk";  // exact path wins over first label match
    r.name = L"Report";
    recs.push_back(r);
    r.path = L"C:\\Users\\old\\Desktop\\Report.docx";
    recs.push_back(r);
    r.path = L"c:\\users\\NEW\\onedrive\\desktop\\other.TXT";  // case-insensitive path
    r.name = L"renamed label";
    recs.push_back(r);
    auto m = MatchRecords(recs, d.items);
    CHECK(m.size() == 3);
    if (m.size() == 3) {
        CHECK(m[0].first == 0 && m[0].second == 1);
        CHECK(m[1].first == 1 && m[1].second == 0);
        CHECK(m[2].first == 2 && m[2].second == 2);
    }
    // Same label twice: the file name decides, not the first label.
    MockDesktop t;
    t.Add(L"C:\\Users\\new\\Desktop\\Report.lnk", L"Report", 0, 0);
    t.Add(L"C:\\Users\\new\\Desktop\\Report.docx", L"Report", 0, 100);
    std::vector<IconRecord> one(1);
    one[0].path = L"C:\\Users\\old\\Desktop\\Report.docx";
    one[0].name = L"Report";
    auto mt = MatchRecords(one, t.items);
    CHECK(mt.size() == 1 && mt[0].second == 1);
    // Highlight: the icon missing from the layout is selected.
    d.Add(L"C:\\Users\\new\\OneDrive\\Desktop\\Extra.txt", L"Extra", 0, 300);
    auto un = UnmatchedItems(d, recs);
    CHECK(un.size() == 1 && un[0] == 3);
}

static void TestTermination() {
    DisplayConfig dc = OneMonitor();
    MockDesktop d;
    d.snap = true;  // view moves icons to the grid: targets can never be met exactly
    d.Add(L"C:\\D\\a.txt", L"a", 300, 300);
    d.Add(L"C:\\D\\b.txt", L"b", 400, 400);
    RestoreResult r = RestoreLayout(d, dc, Targets(d, {POINT{33, 2}, POINT{140, 6}}), false);
    CHECK(r.passes == 2 && r.placed == 0 && d.sets <= 4);  // stops once nothing moves any more
    CHECK(Eq(d.items[0].pos, POINT{0, 2}) && Eq(d.items[1].pos, POINT{75, 6}));
    MockDesktop e;  // two targets closer than one cell: both placed, no ping-pong
    e.Add(L"C:\\D\\a.txt", L"a", 300, 300);
    e.Add(L"C:\\D\\b.txt", L"b", 400, 400);
    r = RestoreLayout(e, dc, Targets(e, {POINT{100, 100}, POINT{110, 110}}), false);
    CHECK(r.placed == 2 && r.passes <= 2);
}

static void TestSettingsAndMenu() {
    Settings s;
    s.Load();
    CHECK(s.cascadeSave && s.confirmSave && !s.confirmRestore && !s.forceVisible);
    s.confirmRestore = true;
    s.cascadeSave = false;
    CHECK(s.Save());
    Settings t;
    t.Load();
    CHECK(t.confirmRestore && !t.cascadeSave && t.cascadeRestore);

    CHECK(InstallMenu(t, L"C:\\Tools\\DesktopRestore.exe"));
    std::wstring exe;
    CHECK(MenuInstalled(&exe) && exe == L"C:\\Tools\\DesktopRestore.exe");
    wchar_t buf[512];
    DWORD cb = sizeof(buf);
    const wchar_t* base = L"Software\\DesktopRestoreUnitTest\\Shell\\DesktopRestore";
    CHECK(RegGetValueW(HKEY_CURRENT_USER, base, L"SubCommands", RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS);
    cb = sizeof(buf);
    CHECK(RegGetValueW(HKEY_CURRENT_USER,
                       L"Software\\DesktopRestoreUnitTest\\Shell\\DesktopRestore\\shell\\2Restore", L"MUIVerb",
                       RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS &&
          wcscmp(buf, L"Restore &Desktop...") == 0);
    cb = sizeof(buf);
    CHECK(RegGetValueW(HKEY_CURRENT_USER,
                       L"Software\\DesktopRestoreUnitTest\\Shell\\DesktopRestore.1Save\\command", nullptr,
                       RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS &&
          wcscmp(buf, L"\"C:\\Tools\\DesktopRestore.exe\" /save") == 0);
    HKEY h;
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DesktopRestoreUnitTest\\Shell\\DesktopRestore\\shell\\1Save",
                        0, KEY_READ, &h) != ERROR_SUCCESS);
    CHECK(UninstallMenu());
    CHECK(!MenuInstalled(nullptr));
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DesktopRestoreUnitTest\\Shell\\DesktopRestore.1Save", 0,
                        KEY_READ, &h) != ERROR_SUCCESS);
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER,
                        L"Software\\DesktopRestoreUnitTest\\Uninstall", 0, KEY_READ,
                        &h) != ERROR_SUCCESS);
}

static void TestSafety() {
    // An empty capture never replaces a saved layout; reserved names are refused.
    MockDesktop d;
    d.Add(L"C:\\D\\a.txt", L"a", 21, 2);
    auto recs = CaptureLayout(d, OneMonitor());
    CHECK(SaveLayoutReg(L"Keep", recs));
    CHECK(!SaveLayoutReg(L"Keep", std::vector<IconRecord>()));
    std::vector<IconRecord> back;
    CHECK(LoadLayoutReg(L"Keep", back) && back.size() == 1);
    CHECK(IsReservedLayoutName(L"Icon0001") && !IsReservedLayoutName(L"Icons"));
    CHECK(!SaveLayoutReg(L"Icon0001", recs));
    CHECK(!SaveLayoutReg(L"a\\b", recs));
    HKEY h;
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, (RegistryRoot() + L"\\_DesktopRestore_Saving_").c_str(), 0, KEY_READ,
                        &h) != ERROR_SUCCESS);  // temporary key cleaned up
    DeleteLayoutReg(L"Keep");
}

static void TestMissingMonitor() {
    // Saved on two monitors, restored on one: icons of the missing monitor must not stack.
    MockDesktop src;
    src.Add(L"C:\\D\\a.txt", L"a", 2560 + 21, 200 + 2);  // primary, relative (21,2)
    src.Add(L"C:\\D\\b.txt", L"b", 21, 2);  // second monitor, relative (21,2)
    auto recs = CaptureLayout(src, TwoMonitors());
    CHECK(recs.size() == 2 && recs[0].monitor == 0 && recs[1].monitor == 1);
    DisplayConfig one = OneMonitor();
    ToViewCoords(recs, one);
    CHECK(!recs[0].floating && recs[1].floating);
    MockDesktop d;
    d.Add(L"C:\\D\\a.txt", L"a", 500, 500);
    d.Add(L"C:\\D\\b.txt", L"b", 600, 500);
    RestoreResult r = RestoreLayout(d, one, recs, false);
    CHECK(r.matched == 2 && r.placed == 2);
    CHECK(Eq(d.items[0].pos, POINT{21, 2}));
    RECT a{21, 2, 96, 102}, b{d.items[1].pos.x, d.items[1].pos.y, d.items[1].pos.x + 75, d.items[1].pos.y + 100}, t;
    CHECK(!IntersectRect(&t, &a, &b));
}

static void TestNoNeedlessMoves() {
    DisplayConfig dc = OneMonitor();
    MockDesktop d;
    d.Add(L"C:\\D\\b.txt", L"b", 600, 600);
    d.Add(L"C:\\D\\a.txt", L"a", 100, 100);  // already at its target
    RestoreResult r = RestoreLayout(d, dc, Targets(d, {POINT{110, 110}, POINT{100, 100}}), false);
    CHECK(r.placed == 2 && d.sets == 1 && r.displaced == 0);
}

static CommandLine P(std::initializer_list<const wchar_t*> a) {
    std::vector<std::wstring> v;
    for (auto x : a) v.emplace_back(x);
    return ParseCommandLine(v);
}

static void TestCommandLine() {
    CommandLine c = P({});
    CHECK(c.verb.empty() && !c.bad);
    c = P({L"/save"});
    CHECK(c.verb == L"save" && c.layout.empty() && !c.quiet);
    c = P({L"/RESTORE", L"Work", L"/q"});
    CHECK(c.verb == L"restore" && c.layout == L"Work" && c.quiet);
    c = P({L"/savefile", L"a.dtr"});
    CHECK(c.verb == L"savefile" && c.file == L"a.dtr");
    c = P({L"/restorefile"});
    CHECK(c.bad && c.verb == L"help");
    // DesktopCmd syntax
    c = P({L"save", L"/n", L"Layout 1920 x 1080 (48)", L"/y"});
    CHECK(c.verb == L"save" && c.layout == L"Layout 1920 x 1080 (48)" && c.quiet);
    c = P({L"restore", L"/f", L"C:\\x.dtr"});
    CHECK(c.verb == L"restorefile" && c.file == L"C:\\x.dtr" && !c.quiet);
    c = P({L"Restore", L"/Y"});
    CHECK(c.verb == L"restore" && c.layout.empty() && c.quiet);
    c = P({L"query"});
    CHECK(c.verb == L"custom");
    c = P({L"/?"});
    CHECK(c.verb == L"help" && !c.bad);
    // Errors
    CHECK(P({L"save", L"/n"}).bad);
    CHECK(P({L"save", L"/n", L"A", L"/f", L"b.dtr"}).bad);
    CHECK(P({L"/save", L"/restore"}).bad);
    c = P({L"save", L"restore"});  // only the first bare word is an action; the rest is a layout name
    CHECK(c.verb == L"save" && c.layout == L"restore" && !c.bad);
    CHECK(P({L"/highlight", L"extra"}).bad);
    CHECK(P({L"stray"}).bad);
    CHECK(P({L"/bogus"}).bad);
    CHECK(P({L"/diag"}).verb == L"diag" && P({L"/diag", L"r.txt"}).file == L"r.txt");
    // Review fixes: bare verbs only first, switch-like values refused, no repeats.
    c = P({L"/save", L"save"});
    CHECK(c.verb == L"save" && c.layout == L"save" && !c.bad);
    c = P({L"/restore", L"Restore", L"/q"});
    CHECK(c.verb == L"restore" && c.layout == L"Restore" && c.quiet);
    CHECK(P({L"save", L"/n", L"/y"}).bad);
    CHECK(P({L"save", L"/n", L"A", L"/n", L"B"}).bad);
    c = P({L"save", L"/n", L"-old layout"});
    CHECK(c.verb == L"save" && c.layout == L"-old layout");
}

static void TestOverlapAndOldIds() {
    // An unmatched icon offset below-right of a target still counts as sitting on it.
    DisplayConfig dc = OneMonitor();
    MockDesktop d;
    d.Add(L"C:\\D\\a.txt", L"a", 600, 600);
    d.Add(L"C:\\D\\new.txt", L"new", 121, 132);  // overlaps the cell at (100,100) by more than half
    std::vector<IconRecord> recs = Targets(d, {POINT{100, 100}});
    recs.resize(1);
    RestoreResult r = RestoreLayout(d, dc, recs, false);
    CHECK(r.placed == 1 && r.displaced == 1);
    POINT n = d.items[1].pos;
    CHECK(!(4 * labs(n.x - 100) < 3 * 75 && 4 * labs(n.y - 100) < 3 * 100));
    // Neighbours one cell away are left alone.
    MockDesktop e;
    e.Add(L"C:\\D\\a.txt", L"a", 600, 600);
    e.Add(L"C:\\D\\b.txt", L"b", 175, 100);
    recs = Targets(e, {POINT{100, 100}});
    recs.resize(1);
    r = RestoreLayout(e, dc, recs, false);
    CHECK(r.placed == 1 && r.displaced == 0 && Eq(e.items[1].pos, POINT{175, 100}));
    // Nothing matched: the view is not touched. Auto arrange stuck on: nothing moves.
    MockDesktop f;
    f.Add(L"C:\\D\\x.txt", L"x", 300, 300);
    std::vector<IconRecord> none(1);
    none[0].path = L"C:\\D\\other.txt";
    none[0].name = L"other";
    r = RestoreLayout(f, dc, none, true);
    CHECK(r.matched == 0 && f.begins == 0 && f.sets == 0);
    f.blockArrange = true;
    r = RestoreLayout(f, dc, Targets(f, {POINT{21, 2}}), true);
    CHECK(r.blocked && f.begins == 1 && f.sets == 0 && Eq(f.items[0].pos, POINT{300, 300}));
    // An icon up-left of a target, overlapping it by about half, is moved too.
    MockDesktop g;
    g.Add(L"C:\\D\\a.txt", L"a", 600, 600);
    g.Add(L"C:\\D\\new.txt", L"new", 100 - 38, 100);
    recs = Targets(g, {POINT{100, 100}});
    recs.resize(1);
    r = RestoreLayout(g, dc, recs, false);
    CHECK(r.placed == 1 && r.displaced == 1);
    // 1.5.x stored CLSIDs without the "::" prefix.
    std::vector<IconRecord> old(1);
    old[0].path = L"{645FF040-5081-101B-9F08-00AA002F954E}";
    old[0].name = L"Trash";  // label differs on purpose
    old[0].oldFormat = true;
    std::vector<DesktopItem> items(1);
    items[0].path = L"::{645FF040-5081-101B-9F08-00AA002F954E}";
    items[0].name = L"Recycle Bin";
    CHECK(MatchRecords(old, items).size() == 1);
}

int wmain() {
    g_hInst = GetModuleHandleW(nullptr);
    SetRegistryRoot(L"Software\\JOConnell\\DeskTopUnitTest");
    SetMenuRoots(L"Software\\DesktopRestoreUnitTest\\Shell", L"Software\\DesktopRestoreUnitTest\\Uninstall");
    SHDeleteKeyW(HKEY_CURRENT_USER, RegistryRoot().c_str());
    TestLayoutNames();
    TestRegistryRoundTrip();
    TestLegacyRegistry();
    TestDtrFiles();
    TestRestoreScrambled();
    TestConflictAndForceVisible();
    TestMatching();
    TestTermination();
    TestSettingsAndMenu();
    TestSafety();
    TestMissingMonitor();
    TestNoNeedlessMoves();
    TestCommandLine();
    TestOverlapAndOldIds();
    SHDeleteKeyW(HKEY_CURRENT_USER, RegistryRoot().c_str());
    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\DesktopRestoreUnitTest");
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
