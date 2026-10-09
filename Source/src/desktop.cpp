// Desktop access and the save / restore / highlight algorithms.
//
// The original ran inside Explorer and talked to the desktop list view directly. This port runs in
// its own process, so it reaches the same view through COM: IShellWindows -> IShellBrowser ->
// IShellView -> IFolderView. Only pointer-free list view messages are sent across processes.
#include "dr.h"

#include <commctrl.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdlib>

namespace dr {

static bool Same(POINT a, POINT b) { return a.x == b.x && a.y == b.y; }

// ------------------------------------------------------------- algorithms

std::vector<IconRecord> CaptureLayout(IDesktop& d, const DisplayConfig& dc) {
    std::vector<IconRecord> recs;
    int n = d.Count();
    for (int i = 0; i < n; i++) {
        DesktopItem it;
        if (!d.Item(i, it) || it.path.empty()) continue;
        IconRecord r;
        r.index = i + 1;
        r.path = it.path;
        r.name = it.name;
        POINT p = it.pos;
        int m = dc.MonitorAt(p);
        if (m >= 0) {  // store relative to the monitor, so moving monitors around does not matter
            p.x -= dc.monitors[static_cast<size_t>(m)].rcView.left;
            p.y -= dc.monitors[static_cast<size_t>(m)].rcView.top;
        }
        r.monitor = m;
        r.x = p.x;
        r.y = p.y;
        recs.push_back(r);
    }
    return recs;
}

static std::wstring FileNamePart(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

// How 1.5.x identified an icon: its file name, or "{CLSID}" (no "::") for virtual items.
static std::wstring OldStyleId(const std::wstring& path) {
    return path.compare(0, 3, L"::{") == 0 ? path.substr(2) : FileNamePart(path);
}

std::vector<std::pair<int, int>> MatchRecords(const std::vector<IconRecord>& recs,
                                              const std::vector<DesktopItem>& items) {
    std::vector<std::pair<int, int>> out;
    std::vector<bool> itemUsed(items.size()), recUsed(recs.size());
    // 1) Identity: the shell parsing name (what the original verified against).
    for (size_t r = 0; r < recs.size(); r++) {
        const IconRecord& rec = recs[r];
        if (rec.legacy || rec.path.empty() || rec.path == L"Empty") continue;
        for (size_t i = 0; i < items.size(); i++) {
            if (itemUsed[i] || items[i].path.empty()) continue;
            bool eq = SameText(rec.path, items[i].path) ||
                      (rec.oldFormat && SameText(rec.path, OldStyleId(items[i].path)));
            if (eq) {
                out.emplace_back(static_cast<int>(r), static_cast<int>(i));
                itemUsed[i] = recUsed[r] = true;
                break;
            }
        }
    }
    // 2) File name, for icons whose folder changed (other PC, Desktop moved to OneDrive).
    for (size_t r = 0; r < recs.size(); r++) {
        const IconRecord& rec = recs[r];
        if (recUsed[r] || rec.legacy || rec.path.empty() || rec.path == L"Empty") continue;
        const std::wstring file = FileNamePart(rec.path);
        for (size_t i = 0; i < items.size(); i++) {
            if (itemUsed[i] || items[i].path.empty()) continue;
            if (SameText(file, FileNamePart(items[i].path))) {
                out.emplace_back(static_cast<int>(r), static_cast<int>(i));
                itemUsed[i] = recUsed[r] = true;
                break;
            }
        }
    }
    // 3) Label, for legacy records and anything else left.
    for (size_t r = 0; r < recs.size(); r++) {
        if (recUsed[r] || recs[r].name.empty()) continue;
        for (size_t i = 0; i < items.size(); i++) {
            if (itemUsed[i] || items[i].path.empty()) continue;
            if (SameText(recs[r].name, items[i].name)) {
                out.emplace_back(static_cast<int>(r), static_cast<int>(i));
                itemUsed[i] = recUsed[r] = true;
                break;
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

static void ReadItems(IDesktop& d, std::vector<DesktopItem>& items, std::vector<bool>& valid) {
    int n = d.Count();
    items.assign(static_cast<size_t>(n), DesktopItem());
    valid.assign(static_cast<size_t>(n), false);
    for (int i = 0; i < n; i++) valid[static_cast<size_t>(i)] = d.Item(i, items[static_cast<size_t>(i)]);
}

RestoreResult RestoreLayout(IDesktop& d, const DisplayConfig& dc, const std::vector<IconRecord>& recs,
                            bool forceVisible) {
    RestoreResult res;
    std::vector<DesktopItem> items;
    std::vector<bool> valid;
    ReadItems(d, items, valid);
    const int n = static_cast<int>(items.size());
    auto matches = MatchRecords(recs, items);
    res.matched = static_cast<int>(matches.size());
    if (matches.empty()) return res;  // nothing to do: leave the view alone

    SIZE sp = d.Spacing();
    if (sp.cx <= 0 || sp.cy <= 0) sp = SIZE{75, 75};
    std::vector<POINT> pos(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) pos[static_cast<size_t>(i)] = items[static_cast<size_t>(i)].pos;
    std::vector<int> matchOf(static_cast<size_t>(n), -1);
    std::vector<POINT> tg(matches.size());
    for (size_t k = 0; k < matches.size(); k++) {
        matchOf[static_cast<size_t>(matches[k].second)] = static_cast<int>(k);
        const IconRecord& r = recs[static_cast<size_t>(matches[k].first)];
        tg[k] = POINT{r.x, r.y};
    }
    auto target = [&](size_t k) { return tg[k]; };
    auto box = [&](POINT p) { return RECT{p.x, p.y, p.x + sp.cx, p.y + sp.cy}; };
    // Icon (other than self) whose cell overlaps the cell at t by more than a quarter in both
    // directions, so it would collide with an icon restored there. Grid neighbours never do.
    auto occupant = [&](POINT t, int self) {
        for (int j = 0; j < n; j++) {
            if (j == self || !valid[static_cast<size_t>(j)]) continue;
            const POINT p = pos[static_cast<size_t>(j)];
            const long long dx = std::llabs(static_cast<long long>(p.x) - t.x);
            const long long dy = std::llabs(static_cast<long long>(p.y) - t.y);
            if (4 * dx < 3LL * sp.cx && 4 * dy < 3LL * sp.cy) return j;
        }
        return -1;
    };
    // First free cell on the primary work area, column by column like the desktop fills.
    // Cells used by icons or still waiting for a restored icon are skipped.
    auto freeSpot = [&](POINT& out) {
        RECT wa = dc.PrimaryWorkView();
        for (int x = wa.left + 21; x + sp.cx <= wa.right + 10; x += sp.cx) {
            for (int y = wa.top + 2; y + sp.cy <= wa.bottom + 10; y += sp.cy) {
                RECT cand = box(POINT{x, y}), tmp;
                bool busy = false;
                for (int j = 0; j < n && !busy; j++) {
                    if (!valid[static_cast<size_t>(j)]) continue;
                    RECT b = box(pos[static_cast<size_t>(j)]);
                    busy = IntersectRect(&tmp, &cand, &b) != FALSE;
                }
                for (size_t k = 0; k < matches.size() && !busy; k++) {
                    RECT b = box(target(k));
                    busy = IntersectRect(&tmp, &cand, &b) != FALSE;
                }
                if (!busy) {
                    out = POINT{x, y};
                    return true;
                }
            }
        }
        return false;
    };

    // Icons from a monitor that is gone were aimed at the primary; where that spot belongs to
    // another icon, send them to a free spot instead of stacking them.
    for (size_t k = 0; k < matches.size(); k++) {
        if (!recs[static_cast<size_t>(matches[k].first)].floating) continue;
        RECT b = box(tg[k]), tmp;
        bool clash = false;
        for (size_t k2 = 0; k2 < matches.size() && !clash; k2++) {
            if (k2 == k || (recs[static_cast<size_t>(matches[k2].first)].floating && k2 > k)) continue;
            RECT o = box(tg[k2]);
            clash = IntersectRect(&tmp, &b, &o) != FALSE;
        }
        POINT f;
        if (clash && freeSpot(f)) tg[k] = f;
    }

    // Passes repeat while icons still move, because clearing a spot can disturb another icon.
    // A restored icon is never pushed aside again, so overlapping targets cannot ping-pong.
    std::vector<bool> placed(static_cast<size_t>(n), false);
    auto move = [&](int i, POINT p) {
        POINT before = pos[static_cast<size_t>(i)];
        if (d.SetPos(i, p)) d.GetPos(i, pos[static_cast<size_t>(i)]);
        return !Same(before, pos[static_cast<size_t>(i)]);
    };
    if (!d.BeginRestore()) {
        d.EndRestore();
        res.blocked = true;
        return res;
    }
    const ULONGLONG deadline = GetTickCount64() + 5000;  // bound on extra passes
    for (int pass = 0; pass < kMaxPasses && (pass == 0 || GetTickCount64() < deadline); pass++) {
        res.passes = pass + 1;
        for (int i = 0; i < n; i++)
            if (valid[static_cast<size_t>(i)]) d.GetPos(i, pos[static_cast<size_t>(i)]);
        for (size_t k = 0; k < matches.size(); k++)  // icons already in place stay put
            if (Same(pos[static_cast<size_t>(matches[k].second)], tg[k]))
                placed[static_cast<size_t>(matches[k].second)] = true;
        bool allOk = true, changed = false;
        for (size_t k = 0; k < matches.size(); k++) {
            const int i = matches[k].second;
            const POINT t = target(k);
            if (Same(pos[static_cast<size_t>(i)], t)) continue;
            int j = occupant(t, i);  // move whatever sits on the spot out of the way
            if (j >= 0 && !(matchOf[static_cast<size_t>(j)] >= 0 && placed[static_cast<size_t>(j)])) {
                POINT f;
                if (freeSpot(f)) {
                    changed |= move(j, f);
                    res.displaced++;
                }
            }
            changed |= move(i, t);
            placed[static_cast<size_t>(i)] = true;
            if (!Same(pos[static_cast<size_t>(i)], t)) allOk = false;
        }
        if (allOk || !changed) break;  // done, or the view keeps adjusting (e.g. snapping)
    }
    for (size_t k = 0; k < matches.size(); k++)
        if (Same(pos[static_cast<size_t>(matches[k].second)], target(k))) res.placed++;

    if (forceVisible) {  // bring icons that ended up off every monitor back on screen
        for (int i = 0; i < n; i++) {
            if (!valid[static_cast<size_t>(i)] || dc.MonitorAt(pos[static_cast<size_t>(i)]) >= 0) continue;
            POINT f;
            if (!freeSpot(f)) f = dc.PrimaryCenterView();
            if (move(i, f)) res.madeVisible++;
        }
    }
    d.EndRestore();
    return res;
}

std::vector<int> UnmatchedItems(IDesktop& d, const std::vector<IconRecord>& recs) {
    std::vector<DesktopItem> items;
    std::vector<bool> valid;
    ReadItems(d, items, valid);
    std::vector<bool> matched(items.size());
    for (const auto& m : MatchRecords(recs, items)) matched[static_cast<size_t>(m.second)] = true;
    std::vector<int> out;
    for (size_t i = 0; i < items.size(); i++)
        if (valid[i] && !matched[i]) out.push_back(static_cast<int>(i));
    return out;
}

// ------------------------------------------------------- live desktop (COM)

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T** put() {
        reset();
        return &p_;
    }
    explicit operator bool() const { return p_ != nullptr; }
    void reset() {
        if (p_) p_->Release();
        p_ = nullptr;
    }

private:
    T* p_ = nullptr;
};

class ShellDesktop : public IDesktop {
public:
    ~ShellDesktop() override {
        RestoreFlags();
        Unlock();
        for (auto p : pidls_) CoTaskMemFree(p);
    }

    HRESULT Open() {
        ComPtr<IShellWindows> windows;
        HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(windows.put()));
        if (FAILED(hr)) return hr;
        VARIANT loc, empty;
        VariantInit(&loc);
        VariantInit(&empty);
        loc.vt = VT_I4;
        loc.lVal = CSIDL_DESKTOP;
        long hwnd = 0;
        ComPtr<IDispatch> disp;
        hr = windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, disp.put());
        if (hr != S_OK || !disp) return FAILED(hr) ? hr : E_FAIL;
        ComPtr<IServiceProvider> sp;
        hr = disp->QueryInterface(IID_PPV_ARGS(sp.put()));
        if (FAILED(hr)) return hr;
        ComPtr<IShellBrowser> browser;
        hr = sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(browser.put()));
        if (FAILED(hr)) return hr;
        hr = browser->QueryActiveShellView(view_.put());
        if (FAILED(hr) || !view_) return FAILED(hr) ? hr : E_FAIL;
        hr = view_->QueryInterface(IID_PPV_ARGS(fv_.put()));
        if (FAILED(hr)) return hr;
        hr = SHGetDesktopFolder(desktop_.put());
        if (FAILED(hr)) return hr;

        HWND defView = nullptr;
        if (SUCCEEDED(view_->GetWindow(&defView)) && defView)
            listView_ = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
        if (SUCCEEDED(view_->QueryInterface(IID_PPV_ARGS(fv2_.put())))) {
            FOLDERVIEWMODE mode = FVM_AUTO;
            int size = -1;
            if (SUCCEEDED(fv2_->GetViewModeAndIconSize(&mode, &size)) && size > 0) iconSize_ = size;
        }
        int count = 0;
        hr = fv_->ItemCount(SVGIO_ALLVIEW, &count);
        if (FAILED(hr)) return hr;
        pidls_.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; i++) {
            PITEMID_CHILD pidl = nullptr;
            if (FAILED(fv_->Item(i, &pidl))) pidl = nullptr;
            pidls_.push_back(pidl);
        }
        return S_OK;
    }

    HWND ListView() const { return listView_; }
    int IconSize() const { return iconSize_; }

    int Count() override { return static_cast<int>(pidls_.size()); }

    bool Item(int i, DesktopItem& out) override {
        out = DesktopItem();
        if (i < 0 || i >= Count() || !pidls_[static_cast<size_t>(i)]) return false;
        PCUITEMID_CHILD pidl = pidls_[static_cast<size_t>(i)];
        out.path = Name(pidl, SHGDN_FORPARSING);
        if (out.path.empty()) {  // virtual items without a parsing name: use their CLSID
            SHDESCRIPTIONID did{};
            if (SUCCEEDED(SHGetDataFromIDListW(desktop_.get(), pidl, SHGDFIL_DESCRIPTIONID, &did, sizeof(did))) &&
                !IsEqualCLSID(did.clsid, CLSID_NULL)) {
                const GUID& g = did.clsid;
                out.path = Format(L"::{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                                  static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0], g.Data4[1],
                                  g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
            }
        }
        out.name = Name(pidl, SHGDN_INFOLDER);
        return GetPos(i, out.pos) && !out.path.empty();
    }

    bool GetPos(int i, POINT& pt) override {
        if (i < 0 || i >= Count() || !pidls_[static_cast<size_t>(i)]) return false;
        return SUCCEEDED(fv_->GetItemPosition(pidls_[static_cast<size_t>(i)], &pt));
    }

    bool SetPos(int i, POINT pt) override {
        if (i < 0 || i >= Count() || !pidls_[static_cast<size_t>(i)]) return false;
        PCUITEMID_CHILD pidl = pidls_[static_cast<size_t>(i)];
        return SUCCEEDED(fv_->SelectAndPositionItems(1, &pidl, &pt, SVSI_POSITIONITEM));
    }

    SIZE Spacing() override {
        POINT pt{};
        if (SUCCEEDED(fv_->GetSpacing(&pt)) && pt.x > 0 && pt.y > 0) return SIZE{pt.x, pt.y};
        DWORD_PTR r = 0;
        if (listView_ && SendMessageTimeoutW(listView_, LVM_GETITEMSPACING, FALSE, 0, SMTO_ABORTIFHUNG, 2000, &r) &&
            LOWORD(r) && HIWORD(r))
            return SIZE{LOWORD(r), HIWORD(r)};
        return SIZE{GetSystemMetrics(SM_CXICONSPACING), GetSystemMetrics(SM_CYICONSPACING)};
    }

    bool AutoArrange() override { return fv_->GetAutoArrange() == S_OK; }

    void Select(const std::vector<int>& items) override {
        bool first = true;
        for (int i : items) {
            DWORD flags = SVSI_SELECT | (first ? SVSI_DESELECTOTHERS : 0);
            if (SUCCEEDED(fv_->SelectItem(i, flags))) first = false;
        }
    }

    // As the author's DesktopCmd 1.7.2 does from outside Explorer: switch off Auto arrange and
    // Align to grid while placing icons (otherwise the view moves them again), then switch Align to
    // grid back on. Auto arrange stays off, or it would undo the restore. The desktop is locked
    // meanwhile so icons don't flicker (Windows releases the lock if this process ends).
    bool BeginRestore() override {
        if (listView_ && LockWindowUpdate(listView_)) locked_ = true;
        const DWORD mask = FWF_AUTOARRANGE | FWF_SNAPTOGRID;
        if (fv2_ && SUCCEEDED(fv2_->GetCurrentFolderFlags(&savedFlags_)) && (savedFlags_ & mask)) {
            flagsChanged_ = true;  // even if the call fails, put snapping back afterwards
            fv2_->SetCurrentFolderFlags(mask, 0);
        }
        return !AutoArrange();
    }

    void EndRestore() override {
        RestoreFlags();
        view_->SaveViewState();  // persist positions, as the original did
        Unlock();
    }

private:
    void RestoreFlags() {
        if (flagsChanged_) fv2_->SetCurrentFolderFlags(FWF_SNAPTOGRID, savedFlags_ & FWF_SNAPTOGRID);
        flagsChanged_ = false;
    }

    void Unlock() {
        if (locked_) LockWindowUpdate(nullptr);
        locked_ = false;
    }

    std::wstring Name(PCUITEMID_CHILD pidl, SHGDNF flags) {
        STRRET sr{};
        if (FAILED(desktop_->GetDisplayNameOf(pidl, flags, &sr))) return std::wstring();
        PWSTR s = nullptr;
        if (FAILED(StrRetToStrW(&sr, pidl, &s)) || !s) return std::wstring();
        std::wstring r = s;
        CoTaskMemFree(s);
        return r;
    }

    ComPtr<IShellView> view_;
    ComPtr<IFolderView> fv_;
    ComPtr<IFolderView2> fv2_;
    ComPtr<IShellFolder> desktop_;
    HWND listView_ = nullptr;
    int iconSize_ = 0;
    std::vector<PITEMID_CHILD> pidls_;
    DWORD savedFlags_ = 0;
    bool flagsChanged_ = false, locked_ = false;
};

void ShellDesktopDeleter::operator()(ShellDesktop* p) const { delete p; }

ShellDesktopPtr OpenShellDesktop(HRESULT* phr) {
    ShellDesktopPtr d(new ShellDesktop());
    HRESULT hr = d->Open();
    if (phr) *phr = hr;
    if (FAILED(hr)) d.reset();
    return d;
}

HWND ShellDesktopListView(ShellDesktop& d) { return d.ListView(); }
int ShellDesktopIconSize(ShellDesktop& d) { return d.IconSize(); }
IDesktop& AsDesktop(ShellDesktop& d) { return d; }

}  // namespace dr
