// ==WindhawkMod==
// @id              floating-dock-helpers
// @name            Floating Dock Helpers
// @description     macOS-style layout for a floating Windows 11 taskbar (tray next to the centered dock), hot corners for Start and Show desktop, Quick Settings that follows the tray
// @version         1.0.0
// @author          jinSeong-P
// @github          https://github.com/jinSeong-P
// @homepage        https://github.com/jinSeong-P/windhawk-floating-dock
// @license         GPL-3.0
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lgdi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Floating Dock Helpers

Completes a floating dock made with
[Windows 11 Taskbar Styler](https://windhawk.net/mods/windows-11-taskbar-styler)
(rounded, detached dock and tray with the *click-through* option). Ready-made
Styler presets are in the
[windhawk-floating-dock](https://github.com/jinSeong-P/windhawk-floating-dock)
repository, together with the *Taskbar Auto-Hide Motion* mod.

- **Tray next to the dock (macOS style).** The system tray sits right next to
  the dock and the pair is centered on the screen, following their widths as
  apps and tray icons come and go. Use it with Windows' center alignment.
- **Hot corners.** A floating dock leaves a gap between the screen corners and
  the Start / Show desktop buttons, so throwing the mouse into a corner no
  longer hits them. While the taskbar is shown, clicking the bottom left
  corner opens Start and clicking the bottom right corner shows the desktop.
  The corner areas are measured from the dock and the tray, and only exist
  while there is a gap below the dock.
- **Quick Settings follows the tray.** Windows opens Quick Settings (network,
  sound, battery) against the right edge of the screen. With the tray moved
  next to the dock, it is moved above the tray as it opens.

## Compatibility

- Tested on Windows 11 25H2 (build 26200) at 175% scaling. Other builds may
  work; parts that can't be found leave the taskbar as it is.
- Primary monitor, bottom taskbar.

## Notes

- The dock and the tray are moved with a RenderTransform, because Taskbar
  Styler re-applies the margin and alignment it styles. The click-through
  region is brought up to date once the moved parts have been rendered.
- *Diagnostic trace file* writes every decision to
  `%TEMP%\floating-dock-helpers.log` for bug reports.

Retrieving the existing taskbar XamlRoot is adapted from GPLv3 Windhawk
taskbar mods in the official ramensoftware/windhawk-mods repository.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- groupedDock: true
  $name: Tray next to the dock (macOS style)
  $description: Places the system tray right next to the dock and centers the pair on the screen. Use it with Windows' center alignment.
- groupGapDip: 8
  $name: Gap between dock and tray (DIP)
- cornerTargets: true
  $name: Hot corners
  $description: Clicking the bottom left corner opens Start, the bottom right corner shows the desktop. Only while the taskbar is shown and floats above the screen edge.
- traceToFile: false
  $name: Diagnostic trace file
  $description: Writes every decision to %TEMP%\floating-dock-helpers.log.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <optional>
#include <string_view>
#include <vector>

#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

using namespace winrt::Windows::UI::Xaml;
using winrt::Windows::UI::Xaml::Media::CompositeTransform;
using winrt::Windows::UI::Xaml::Media::VisualTreeHelper;

namespace {

struct Settings {
    bool groupedDock = true;
    int groupGapDip = 8;
    bool cornerTargets = true;
    bool traceToFile = false;
};

Settings g_settings;
std::atomic<bool> g_unloading{false};

// Diagnostic trace to %TEMP%\floating-dock-helpers.log (explorer's temp
// folder).
void Trace(const wchar_t* format, ...) {
    WCHAR message[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _TRUNCATE, format, args);
    va_end(args);

    Wh_Log(L"%s", message);

    if (!g_settings.traceToFile) {
        return;
    }

    WCHAR path[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, path);
    if (!len || len > MAX_PATH - 32) {
        return;
    }
    wcscat_s(path, L"floating-dock-helpers.log");

    WCHAR line[1200];
    _snwprintf_s(line, _TRUNCATE, L"%llu [%lu] %s\r\n", GetTickCount64(),
                 GetCurrentThreadId(), message);

    char utf8[2400];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8),
                                    nullptr, nullptr);
    if (bytes <= 1) {
        return;
    }

    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, utf8, bytes - 1, &written, nullptr);
    CloseHandle(file);
}

bool IsPrimaryTaskbar(HWND hWnd) {
    WCHAR className[32]{};
    return hWnd && GetClassNameW(hWnd, className, ARRAYSIZE(className)) &&
           _wcsicmp(className, L"Shell_TrayWnd") == 0;
}

HWND GetTaskbarDispatchWindow(HWND taskbarWnd) {
    HWND uiWindow = FindWindowExW(
        taskbarWnd,
        nullptr,
        L"Windows.UI.Composition.DesktopWindowContentBridge",
        nullptr);
    return uiWindow ? uiWindow : taskbarWnd;
}

bool OnTaskbarUiThread(HWND taskbar) {
    return GetWindowThreadProcessId(GetTaskbarDispatchWindow(taskbar),
                                    nullptr) == GetCurrentThreadId();
}

// A hidden auto-hide taskbar keeps only a thin strip on its monitor.
bool IsTaskbarHiddenOnScreen(HWND hWnd) {
    RECT windowRect{};
    if (!GetWindowRect(hWnd, &windowRect)) {
        return false;
    }

    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (!GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST),
                         &monitorInfo)) {
        return false;
    }

    RECT visible{};
    if (!IntersectRect(&visible, &windowRect, &monitorInfo.rcMonitor)) {
        return true;
    }

    const LONGLONG windowArea =
        static_cast<LONGLONG>(windowRect.right - windowRect.left) *
        (windowRect.bottom - windowRect.top);
    const LONGLONG visibleArea =
        static_cast<LONGLONG>(visible.right - visible.left) *
        (visible.bottom - visible.top);

    return windowArea > 0 && visibleArea * 4 < windowArea;
}

// -----------------------------------------------------------------------------
// Existing taskbar XamlRoot access.
// Adapted from the official taskbar-multirow/taskbar-separators pattern, which
// avoids a second XAML diagnostics client and keeps the mod compatible with
// Windows 11 Taskbar Styler.
// -----------------------------------------------------------------------------

void* CTaskBand_ITaskListWndSite_vftable = nullptr;

using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void* pThis, void** result);
CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original = nullptr;

void* TaskbarHost_FrameHeight_Original = nullptr;

using std__Ref_count_base__Decref_t = void(WINAPI*)(void* pThis);
std__Ref_count_base__Decref_t std__Ref_count_base__Decref_Original = nullptr;

XamlRoot XamlRootFromTaskbarHostSharedPtr(void* taskbarHostSharedPtr[2]) {
    if (!taskbarHostSharedPtr[0]) {
        if (taskbarHostSharedPtr[1] && std__Ref_count_base__Decref_Original) {
            std__Ref_count_base__Decref_Original(taskbarHostSharedPtr[1]);
        }
        return nullptr;
    }

    size_t taskbarElementIUnknownOffset = 0x10;

#if defined(_M_X64)
    {
        const BYTE* b = static_cast<const BYTE*>(TaskbarHost_FrameHeight_Original);
        if (b && b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC &&
            b[3] == 0x28 && b[4] == 0x48 && b[5] == 0x83 &&
            b[6] == 0xC1 && b[7] <= 0x7F) {
            taskbarElementIUnknownOffset = b[7];
        }
    }
#else
#error "This mod targets x86-64 only"
#endif

    auto* taskbarElementIUnknown = *reinterpret_cast<IUnknown**>(
        static_cast<BYTE*>(taskbarHostSharedPtr[0]) +
        taskbarElementIUnknownOffset);

    FrameworkElement taskbarElement = nullptr;
    if (taskbarElementIUnknown) {
        taskbarElementIUnknown->QueryInterface(
            winrt::guid_of<FrameworkElement>(), winrt::put_abi(taskbarElement));
    }

    XamlRoot result = taskbarElement ? taskbarElement.XamlRoot() : nullptr;

    if (taskbarHostSharedPtr[1] && std__Ref_count_base__Decref_Original) {
        std__Ref_count_base__Decref_Original(taskbarHostSharedPtr[1]);
    }

    return result;
}

void* FindTaskListWndSite(void* taskBand, void* vftable) {
    void* site = taskBand;
    for (int i = 0; *reinterpret_cast<void**>(site) != vftable; i++) {
        if (i == 20) {
            return nullptr;
        }
        site = reinterpret_cast<void**>(site) + 1;
    }
    return site;
}

XamlRoot GetPrimaryTaskbarXamlRoot(HWND taskbarWnd) {
    if (!CTaskBand_ITaskListWndSite_vftable ||
        !CTaskBand_GetTaskbarHost_Original) {
        return nullptr;
    }

    HWND taskSwitchWnd =
        reinterpret_cast<HWND>(GetPropW(taskbarWnd, L"TaskbandHWND"));
    if (!taskSwitchWnd) {
        return nullptr;
    }

    void* taskBand =
        reinterpret_cast<void*>(GetWindowLongPtrW(taskSwitchWnd, 0));
    if (!taskBand) {
        return nullptr;
    }

    void* site =
        FindTaskListWndSite(taskBand, CTaskBand_ITaskListWndSite_vftable);
    if (!site) {
        return nullptr;
    }

    void* taskbarHostSharedPtr[2]{};
    CTaskBand_GetTaskbarHost_Original(site, taskbarHostSharedPtr);
    return XamlRootFromTaskbarHostSharedPtr(taskbarHostSharedPtr);
}

// Must run on the taskbar XAML thread.
FrameworkElement GetTaskbarRoot(HWND taskbar) {
    auto xamlRoot = GetPrimaryTaskbarXamlRoot(taskbar);
    return xamlRoot ? xamlRoot.Content().try_as<FrameworkElement>() : nullptr;
}

// -----------------------------------------------------------------------------
// Taskbar parts.
// The taskbar root holds the dock (Taskbar.TaskbarFrame) and the system tray
// (SystemTray.SystemTrayFrame). Their visible pills are one level down: the
// dock's RootGrid, and the tray's SystemTrayFrameGrid inside a transparent
// border.
// -----------------------------------------------------------------------------

// The Taskbar Auto-Hide Motion mod pops the tray through the same transform
// (ScaleX/ScaleY), so it is shared: each mod only touches its own properties
// and leaves the transform attached when it unloads.
CompositeTransform PartTransform(FrameworkElement const& part) {
    if (auto current = part.RenderTransform().try_as<CompositeTransform>()) {
        return current;
    }
    CompositeTransform transform;
    part.RenderTransform(transform);
    return transform;
}

double RenderedShift(FrameworkElement const& part) {
    auto transform = part.RenderTransform().try_as<CompositeTransform>();
    return transform ? transform.TranslateX() : 0.0;
}

Thickness PanelBorder(FrameworkElement const& element) {
    if (auto grid = element.try_as<Controls::Grid>()) {
        return grid.BorderThickness();
    }
    if (auto stack = element.try_as<Controls::StackPanel>()) {
        return stack.BorderThickness();
    }
    return {};
}

FrameworkElement VisiblePart(FrameworkElement const& part) {
    if (VisualTreeHelper::GetChildrenCount(part) > 0) {
        if (auto inner =
                VisualTreeHelper::GetChild(part, 0).try_as<FrameworkElement>()) {
            return inner;
        }
    }
    return part;
}

// Depth-first search with a depth limit; the buttons sit a few levels down.
template <typename Match>
FrameworkElement FindDescendant(DependencyObject const& parent,
                                Match const& match,
                                int depth) {
    const int count = VisualTreeHelper::GetChildrenCount(parent);
    for (int i = 0; i < count; i++) {
        auto child = VisualTreeHelper::GetChild(parent, i);
        if (auto element = child.try_as<FrameworkElement>();
            element && match(element)) {
            return element;
        }
        if (depth > 0) {
            if (auto found = FindDescendant(child, match, depth - 1)) {
                return found;
            }
        }
    }
    return nullptr;
}

struct DockParts {
    winrt::weak_ref<FrameworkElement> root;
    winrt::weak_ref<FrameworkElement> dock;
    winrt::weak_ref<FrameworkElement> tray;
    winrt::weak_ref<FrameworkElement> startButton;
    winrt::weak_ref<FrameworkElement> showDesktop;
    // Placement follows size changes within the same layout pass, before the
    // frame renders, so the parts and the click-through region never show out
    // of place. The poll only covers what these miss.
    FrameworkElement::SizeChanged_revoker rootSizeChanged;
    FrameworkElement::SizeChanged_revoker dockSizeChanged;
    FrameworkElement::SizeChanged_revoker traySizeChanged;
    // Inputs of the last placement, to skip ticks where nothing changed.
    std::array<double, 5> lastInputs{};
    ULONGLONG nextLookup = 0;
};
DockParts g_parts;
HWND g_taskbar = nullptr;

void UpdateGroupedLayout(bool syncLayout);

bool FindParts(HWND taskbar) {
    auto root = GetTaskbarRoot(taskbar);
    if (!root) {
        return false;
    }

    FrameworkElement dock{nullptr};
    FrameworkElement tray{nullptr};
    const int count = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        auto part = VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
        if (!part) {
            continue;
        }
        const auto className = winrt::get_class_name(part);
        if (className == L"Taskbar.TaskbarFrame") {
            dock = part;
        } else if (std::wstring_view(className).find(L"SystemTray") !=
                   std::wstring_view::npos) {
            tray = part;
        }
    }
    if (!dock || !tray) {
        Trace(L"parts: dock %d, tray %d", dock != nullptr, tray != nullptr);
        return false;
    }

    auto startButton = FindDescendant(
        dock,
        [](FrameworkElement const& e) {
            return Automation::AutomationProperties::GetAutomationId(e) ==
                   L"StartButton";
        },
        16);
    auto showDesktop = FindDescendant(
        tray,
        [](FrameworkElement const& e) {
            return e.Name() == L"ShowDesktopStack";
        },
        16);
    Trace(L"parts: dock pill %s, tray pill %s, start %d, show desktop %d",
          winrt::get_class_name(VisiblePart(dock)).c_str(),
          winrt::get_class_name(VisiblePart(tray)).c_str(),
          startButton != nullptr, showDesktop != nullptr);

    g_parts = DockParts{};
    g_parts.root = winrt::make_weak(root);
    g_parts.dock = winrt::make_weak(dock);
    g_parts.tray = winrt::make_weak(tray);
    if (startButton) {
        g_parts.startButton = winrt::make_weak(startButton);
    }
    if (showDesktop) {
        g_parts.showDesktop = winrt::make_weak(showDesktop);
    }

    if (g_settings.groupedDock) {
        auto onSizeChanged = [](auto const&, auto const&) {
            if (!g_unloading.load(std::memory_order_acquire)) {
                UpdateGroupedLayout(false);
            }
        };
        g_parts.rootSizeChanged =
            root.SizeChanged(winrt::auto_revoke, onSizeChanged);
        g_parts.dockSizeChanged =
            VisiblePart(dock).SizeChanged(winrt::auto_revoke, onSizeChanged);
        g_parts.traySizeChanged =
            VisiblePart(tray).SizeChanged(winrt::auto_revoke, onSizeChanged);
    }
    return true;
}

// Finds the parts if they're missing (not found yet, or the taskbar was
// rebuilt), looking again now and then, not on every call.
bool EnsureParts() {
    if (g_parts.root.get() && g_parts.dock.get() && g_parts.tray.get()) {
        return true;
    }
    if (GetTickCount64() < g_parts.nextLookup) {
        return false;
    }
    const ULONGLONG nextLookup = GetTickCount64() + 1000;
    try {
        if (FindParts(g_taskbar)) {
            return true;
        }
    } catch (winrt::hresult_error const& e) {
        Trace(L"finding the parts threw 0x%08X %s",
              static_cast<unsigned>(e.code()), e.message().c_str());
    } catch (...) {
        Trace(L"finding the parts threw");
    }
    g_parts = DockParts{};
    g_parts.nextLookup = nextLookup;
    return false;
}

// -----------------------------------------------------------------------------
// Grouped dock layout (macOS style).
// The tray sits right next to the dock and the pair is centered on the screen.
// Both widths change (apps, tray icons, clock), so the positions are
// recomputed whenever they do. Taskbar Styler re-applies the properties it
// styles (Margin, alignment), so the parts are moved with a RenderTransform,
// which it doesn't touch.
// -----------------------------------------------------------------------------

// The tray's current shift in DIPs, for placing Quick Settings.
double g_trayShiftDip = 0;

// Taskbar Styler computes the click-through region with TransformToVisual,
// which reflects a newly attached RenderTransform only once the part has been
// rendered with it. A hidden taskbar's content is off screen and not
// rendered, so after a (re)load the region kept the unshifted placement into
// the first reveal. Each frame after a placement change or a reveal, check
// whether TransformToVisual has caught up; when it has, a layout pass lets
// the Styler recompute right away.
constexpr int kRegionWatchFrames = 120;
winrt::event_token g_regionNudgeToken{};
int g_regionNudgeFrames = 0;

void StopRegionNudge() {
    if (g_regionNudgeToken) {
        try {
            winrt::Windows::UI::Xaml::Media::CompositionTarget::Rendering(
                g_regionNudgeToken);
        } catch (...) {
        }
        g_regionNudgeToken = {};
    }
}

void NudgeRegionAfterRender() {
    using winrt::Windows::UI::Xaml::Media::CompositionTarget;
    g_regionNudgeFrames = 0;
    if (g_regionNudgeToken) {
        return;
    }
    try {
        g_regionNudgeToken =
            CompositionTarget::Rendering([](auto const&, auto const&) {
                try {
                    auto root = g_parts.root.get();
                    auto dock = g_parts.dock.get();
                    if (!root || !dock ||
                        g_unloading.load(std::memory_order_acquire) ||
                        ++g_regionNudgeFrames > kRegionWatchFrames) {
                        StopRegionNudge();
                        return;
                    }

                    // Checked on the dock: the tray's transform also scales
                    // during its pop, and both are attached together.
                    const double measured =
                        dock.TransformToVisual(root).TransformPoint({0, 0}).X;
                    if (std::abs(measured - (dock.ActualOffset().x +
                                             RenderedShift(dock))) >= 0.5) {
                        return;
                    }

                    StopRegionNudge();
                    dock.InvalidateArrange();
                    root.InvalidateArrange();
                    root.UpdateLayout();
                    Trace(L"region settled after %d frame(s)",
                          g_regionNudgeFrames);
                } catch (...) {
                    StopRegionNudge();
                }
            });
    } catch (...) {
        g_regionNudgeToken = {};
    }
}

// syncLayout runs the layout pass right away, which also brings Taskbar
// Styler's click-through region up to date; not allowed from inside a layout
// pass (SizeChanged), where the next pass is scheduled instead.
void UpdateGroupedLayout(bool syncLayout) {
    if (!g_settings.groupedDock || !EnsureParts()) {
        return;
    }
    try {
        auto root = g_parts.root.get();
        auto dock = g_parts.dock.get();
        auto tray = g_parts.tray.get();
        if (!root || !dock || !tray) {
            return;
        }

        // Layout offsets leave out our shift.
        auto dockPill = VisiblePart(dock);
        auto trayPill = VisiblePart(tray);
        const Thickness trayBorder = PanelBorder(trayPill);

        const double rootWidth = root.ActualWidth();
        const double dockLeft = dock.ActualOffset().x + dockPill.ActualOffset().x;
        const double dockWidth = dockPill.ActualWidth();
        const double trayLeft = tray.ActualOffset().x +
                                trayPill.ActualOffset().x + trayBorder.Left;
        const double trayWidth =
            trayPill.ActualWidth() - trayBorder.Left - trayBorder.Right;

        const std::array<double, 5> inputs{rootWidth, dockLeft, dockWidth,
                                           trayLeft, trayWidth};
        if (inputs == g_parts.lastInputs) {
            return;
        }
        const double gap = g_settings.groupGapDip;

        // While the taskbar is being built the widths can be partial (the
        // tray then landed at the screen's left edge); wait for a sane
        // layout: both parts on the taskbar, the tray after the dock, and
        // room for the pair.
        if (rootWidth <= 0 || dockWidth <= 0 || trayWidth <= 0 ||
            dockLeft < -1 || trayLeft + trayWidth > rootWidth + 1 ||
            trayLeft < dockLeft + dockWidth ||
            dockWidth + gap + trayWidth > rootWidth) {
            return;
        }

        const double groupLeft =
            std::round((rootWidth - (dockWidth + gap + trayWidth)) / 2);

        const double trayShift = groupLeft + dockWidth + gap - trayLeft;
        PartTransform(dock).TranslateX(groupLeft - dockLeft);
        PartTransform(tray).TranslateX(trayShift);
        g_parts.lastInputs = inputs;
        g_trayShiftDip = trayShift;

        // A layout pass, so Taskbar Styler's click-through region follows.
        root.InvalidateArrange();
        dock.InvalidateArrange();
        if (syncLayout) {
            root.UpdateLayout();
        }
        NudgeRegionAfterRender();

        Trace(L"grouped layout: width %.1f, dock %.1f+%.1f, tray %.1f+%.1f "
              L"-> group at %.1f",
              rootWidth, dockLeft, dockWidth, trayLeft, trayWidth, groupLeft);
    } catch (winrt::hresult_error const& e) {
        Trace(L"grouped layout failed: 0x%08X %s",
              static_cast<unsigned>(e.code()), e.message().c_str());
        g_parts = DockParts{};
        g_parts.nextLookup = GetTickCount64() + 1000;
    }
}

// Puts the parts back. Only our own property is reset; the transform stays
// attached (the motion mod may be using it).
void ResetGroupedLayout() {
    StopRegionNudge();
    try {
        for (auto const& weak : {g_parts.dock, g_parts.tray}) {
            if (auto part = weak.get()) {
                if (auto transform =
                        part.RenderTransform().try_as<CompositeTransform>()) {
                    transform.TranslateX(0);
                }
            }
        }
        if (auto root = g_parts.root.get()) {
            root.InvalidateArrange();
        }
    } catch (...) {
    }
    g_trayShiftDip = 0;
}

// -----------------------------------------------------------------------------
// Hot corners.
// A floating dock leaves a gap between the screen corners and the Start button
// / Show desktop button, so pushing the mouse into a corner no longer hits
// them. While the taskbar is shown, invisible windows owned by the taskbar
// cover each corner (L-shaped: a strip along the side plus a band along the
// bottom) and act like those buttons.
//
// The sizes come from the parts: the band's height is the gap below the dock.
// When the dock (or the tray) is near a corner, the side strip fills the gap
// beside it and the band reaches the far edge of the Start (or Show desktop)
// button; otherwise the corner is a small area of a few gaps.
// -----------------------------------------------------------------------------

enum class CornerKind : LONG_PTR {
    Start = 0,
    Desktop = 1,
};

constexpr wchar_t kCornerClassName[] = L"FloatingDockHelpersCornerTarget";

// A corner away from the dock spans this many bottom gaps along the edge.
constexpr double kLoneCornerGaps = 4;

// The class is registered for this DLL, so a reloaded mod never finds the
// previous load's class (and its unloaded window procedure).
HINSTANCE ModuleInstance() {
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}
ATOM g_cornerClass = 0;
HWND g_cornerWnd[2] = {};

// What the targets were last placed for, in pixels.
struct CornerPlacement {
    RECT taskbar{};
    int gap = 0;
    int side[2] = {};
    int band[2] = {};

    // The dock and tray pops scale the measured parts by a few pixels; a hot
    // corner doesn't need to follow that.
    bool CloseTo(CornerPlacement const& other, int tolerance) const {
        auto close = [tolerance](int a, int b) {
            return std::abs(a - b) <= tolerance;
        };
        return EqualRect(&taskbar, &other.taskbar) && close(gap, other.gap) &&
               close(side[0], other.side[0]) && close(side[1], other.side[1]) &&
               close(band[0], other.band[0]) && close(band[1], other.band[1]);
    }
};
constexpr double kCornerToleranceDip = 4;
std::optional<CornerPlacement> g_cornerPlacement;

// Taskbar command that toggles Show desktop, like the button.
constexpr WPARAM kTrayCommandToggleDesktop = 407;

// Start and Show desktop are asked of the taskbar directly. Injected Win key
// presses (Win, Win+D) were sent but had no effect on 26200.
void InvokeCorner(CornerKind kind) {
    BOOL posted = FALSE;
    if (kind == CornerKind::Start) {
        posted = PostMessageW(g_taskbar, WM_SYSCOMMAND, SC_TASKLIST, 0);
    } else {
        posted = PostMessageW(g_taskbar, WM_COMMAND, kTrayCommandToggleDesktop,
                              0);
    }
    Trace(L"corner %s click: posted %d (error %lu)",
          kind == CornerKind::Start ? L"Start" : L"Desktop", posted,
          posted ? 0 : GetLastError());
}

LRESULT CALLBACK CornerWndProc(HWND hWnd,
                               UINT message,
                               WPARAM wParam,
                               LPARAM lParam) {
    switch (message) {
        case WM_MOUSEACTIVATE:
            // Keep the focus (and an open Start menu) where it is.
            return MA_NOACTIVATE;

        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;

        case WM_LBUTTONDOWN:
            SetCapture(hWnd);
            return 0;

        case WM_LBUTTONUP: {
            if (GetCapture() != hWnd) {
                return 0;
            }
            ReleaseCapture();
            // Like a button: only act if released over the target.
            POINT pt{static_cast<short>(LOWORD(lParam)),
                     static_cast<short>(HIWORD(lParam))};
            ClientToScreen(hWnd, &pt);
            if (WindowFromPoint(pt) == hWnd) {
                InvokeCorner(static_cast<CornerKind>(
                    GetWindowLongPtrW(hWnd, GWLP_USERDATA)));
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

HWND CreateCornerWindow(HWND taskbar, CornerKind kind) {
    if (!g_cornerClass) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = CornerWndProc;
        wc.hInstance = ModuleInstance();
        wc.lpszClassName = kCornerClassName;
        g_cornerClass = RegisterClassExW(&wc);
        if (!g_cornerClass) {
            return nullptr;
        }
    }

    // Owned by the taskbar, so it always stays above it. No redirection
    // bitmap: nothing is drawn, the window only takes the mouse.
    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
            WS_EX_NOREDIRECTIONBITMAP,
        kCornerClassName, nullptr, WS_POPUP, 0, 0, 0, 0, taskbar, nullptr,
        ModuleInstance(), nullptr);
    if (hWnd) {
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, static_cast<LONG_PTR>(kind));
    }
    return hWnd;
}

void HideCornerTargets() {
    for (HWND hWnd : g_cornerWnd) {
        if (hWnd && IsWindowVisible(hWnd)) {
            ShowWindow(hWnd, SW_HIDE);
        }
    }
    g_cornerPlacement.reset();
}

void DestroyCornerTargets() {
    for (HWND& hWnd : g_cornerWnd) {
        if (hWnd) {
            DestroyWindow(hWnd);
            hWnd = nullptr;
        }
    }
    if (g_cornerClass) {
        UnregisterClassW(kCornerClassName, ModuleInstance());
        g_cornerClass = 0;
    }
    g_cornerPlacement.reset();
}

winrt::Windows::Foundation::Rect BoundsIn(FrameworkElement const& element,
                                          UIElement const& root,
                                          Thickness const& inset = {}) {
    const float width = static_cast<float>(element.ActualWidth() -
                                           inset.Left - inset.Right);
    const float height = static_cast<float>(element.ActualHeight() -
                                            inset.Top - inset.Bottom);
    return element.TransformToVisual(root).TransformBounds(
        {static_cast<float>(inset.Left), static_cast<float>(inset.Top),
         std::max(width, 0.0f), std::max(height, 0.0f)});
}

// The corner sizes in DIPs, measured from the parts.
std::optional<CornerPlacement> MeasureCorners(double scale) {
    auto root = g_parts.root.get();
    auto dock = g_parts.dock.get();
    auto tray = g_parts.tray.get();
    if (!root || !dock || !tray) {
        return std::nullopt;
    }

    const double rootWidth = root.ActualWidth();
    const double rootHeight = root.ActualHeight();
    auto trayPill = VisiblePart(tray);
    const auto dockBounds = BoundsIn(VisiblePart(dock), root);
    const auto trayBounds = BoundsIn(trayPill, root, PanelBorder(trayPill));
    if (rootWidth <= 0 || rootHeight <= 0 || dockBounds.Width <= 0 ||
        trayBounds.Width <= 0) {
        return std::nullopt;
    }

    // Not floating: the dock reaches the bottom edge, and Windows' own
    // buttons work as they are.
    const double gap = rootHeight - (dockBounds.Y + dockBounds.Height);
    if (gap < 2) {
        return std::nullopt;
    }

    auto px = [scale](double dip) {
        return static_cast<int>(std::lround(dip * scale));
    };

    CornerPlacement placement;
    placement.gap = px(gap);

    // Bottom left: Start.
    {
        const double sideGap = dockBounds.X;
        const bool nearDock = sideGap <= 2 * gap;
        double band = kLoneCornerGaps * gap;
        double side = gap;
        if (nearDock) {
            side = std::max(sideGap, 2.0);
            band = side;
            if (auto start = g_parts.startButton.get()) {
                const auto bounds = BoundsIn(start, root);
                band = bounds.X + bounds.Width;
            }
        }
        placement.side[0] = px(side);
        placement.band[0] = std::max(px(band), placement.side[0]);
    }

    // Bottom right: Show desktop.
    {
        const double sideGap = rootWidth - (trayBounds.X + trayBounds.Width);
        const bool nearTray = sideGap <= 2 * gap;
        double band = kLoneCornerGaps * gap;
        double side = gap;
        if (nearTray) {
            side = std::max(sideGap, 2.0);
            band = side;
            if (auto showDesktop = g_parts.showDesktop.get()) {
                band = rootWidth - BoundsIn(showDesktop, root).X;
            }
        }
        placement.side[1] = px(side);
        placement.band[1] = std::max(px(band), placement.side[1]);
    }

    return placement;
}

// Places the targets for the shown taskbar; cheap when nothing moved.
void SyncCornerTargets(HWND taskbar) {
    if (!g_settings.cornerTargets) {
        return;
    }

    RECT taskbarRect{};
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (!GetWindowRect(taskbar, &taskbarRect) ||
        !GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST),
                         &monitorInfo)) {
        return;
    }
    RECT const& monitorRect = monitorInfo.rcMonitor;

    // Bottom taskbars only.
    if (taskbarRect.bottom != monitorRect.bottom || !EnsureParts()) {
        HideCornerTargets();
        return;
    }

    const double scale = GetDpiForWindow(taskbar) / 96.0;
    std::optional<CornerPlacement> placement;
    try {
        placement = MeasureCorners(scale);
    } catch (...) {
    }
    if (!placement) {
        HideCornerTargets();
        return;
    }
    placement->taskbar = taskbarRect;
    if (g_cornerPlacement &&
        g_cornerPlacement->CloseTo(
            *placement,
            static_cast<int>(std::lround(kCornerToleranceDip * scale)))) {
        return;
    }

    const int height = taskbarRect.bottom - taskbarRect.top;
    const int gap = std::min(placement->gap, height);
    for (int i = 0; i < 2; i++) {
        const auto kind = static_cast<CornerKind>(i);
        if (!g_cornerWnd[i]) {
            g_cornerWnd[i] = CreateCornerWindow(taskbar, kind);
            if (!g_cornerWnd[i]) {
                Trace(L"corner window creation failed: %lu", GetLastError());
                return;
            }
        }

        const int width = placement->band[i];
        const int side = placement->side[i];
        const int left = kind == CornerKind::Start ? monitorRect.left
                                                   : monitorRect.right - width;

        // The side strip at full height plus the band along the bottom.
        const int sideLeft = kind == CornerKind::Start ? 0 : width - side;
        HRGN region = CreateRectRgn(sideLeft, 0, sideLeft + side, height);
        HRGN band = CreateRectRgn(0, height - gap, width, height);
        CombineRgn(region, region, band, RGN_OR);
        DeleteObject(band);

        SetWindowPos(g_cornerWnd[i], HWND_TOPMOST, left, taskbarRect.top,
                     width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowRgn(g_cornerWnd[i], region, FALSE);
    }

    g_cornerPlacement = placement;
    Trace(L"corners: gap %dpx, Start %dx%dpx, Show desktop %dx%dpx",
          placement->gap, placement->side[0], placement->band[0],
          placement->side[1], placement->band[1]);
}

// -----------------------------------------------------------------------------
// Quick Settings placement.
// Windows opens Quick Settings (ShellHost's ControlCenterWindow) against the
// right edge of the work area, where the tray normally is. With the tray moved
// next to the dock, the window is moved by the same distance as it appears, so
// the flyout opens above the tray again.
// -----------------------------------------------------------------------------

constexpr wchar_t kQuickSettingsClass[] = L"ControlCenterWindow";
constexpr int kQuickSettingsChecks = 12;
constexpr UINT kQuickSettingsCheckMs = 25;

HWINEVENTHOOK g_flyoutShowHook = nullptr;
HWINEVENTHOOK g_flyoutUncloakHook = nullptr;
HWND g_quickSettings = nullptr;
UINT_PTR g_quickSettingsTimer = 0;
int g_quickSettingsChecksLeft = 0;

void PlaceQuickSettings(HWND hWnd) {
    HWND taskbar = g_taskbar;
    RECT rect{};
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (!IsWindow(hWnd) || !IsWindow(taskbar) || !GetWindowRect(hWnd, &rect) ||
        !GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST),
                         &monitorInfo)) {
        return;
    }

    // Windows' own position is against the work area's right edge; move it
    // by the tray's shift from there.
    const double scale = GetDpiForWindow(taskbar) / 96.0;
    const int width = rect.right - rect.left;
    const int left = monitorInfo.rcWork.right - width +
                     static_cast<int>(std::lround(g_trayShiftDip * scale));
    if (rect.left == left) {
        return;
    }

    SetWindowPos(hWnd, nullptr, left, rect.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    Trace(L"quick settings moved: x %ld -> %d (tray shift %.1f DIP)",
          rect.left, left, g_trayShiftDip);
}

// Windows may still place the window while its entrance animation starts;
// keep it in place for a moment.
void CALLBACK QuickSettingsTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    PlaceQuickSettings(g_quickSettings);
    if (--g_quickSettingsChecksLeft <= 0) {
        KillTimer(nullptr, g_quickSettingsTimer);
        g_quickSettingsTimer = 0;
    }
}

void CALLBACK FlyoutEventProc(HWINEVENTHOOK,
                              DWORD,
                              HWND hWnd,
                              LONG idObject,
                              LONG,
                              DWORD,
                              DWORD) {
    if (idObject != OBJID_WINDOW || !hWnd ||
        g_unloading.load(std::memory_order_acquire)) {
        return;
    }
    WCHAR className[32]{};
    GetClassNameW(hWnd, className, ARRAYSIZE(className));
    if (wcscmp(className, kQuickSettingsClass) != 0) {
        return;
    }

    g_quickSettings = hWnd;
    PlaceQuickSettings(hWnd);
    g_quickSettingsChecksLeft = kQuickSettingsChecks;
    if (!g_quickSettingsTimer) {
        g_quickSettingsTimer =
            SetTimer(nullptr, 0, kQuickSettingsCheckMs, QuickSettingsTimerProc);
    }
}

// Must run on the taskbar UI thread; the events are delivered there.
void StartFlyoutPlacement() {
    if (g_flyoutShowHook || !g_settings.groupedDock) {
        return;
    }
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    g_flyoutShowHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
                                       nullptr, FlyoutEventProc, 0, 0, flags);
    g_flyoutUncloakHook =
        SetWinEventHook(EVENT_OBJECT_UNCLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr,
                        FlyoutEventProc, 0, 0, flags);
}

void StopFlyoutPlacement() {
    for (HWINEVENTHOOK* hook : {&g_flyoutShowHook, &g_flyoutUncloakHook}) {
        if (*hook) {
            UnhookWinEvent(*hook);
            *hook = nullptr;
        }
    }
    if (g_quickSettingsTimer) {
        KillTimer(nullptr, g_quickSettingsTimer);
        g_quickSettingsTimer = 0;
    }
}

// -----------------------------------------------------------------------------
// Taskbar state.
// Placement is driven by size changes and by the taskbar being shown
// (TrayUI::SlideWindow, when auto-hide is on). A slow poll covers the rest:
// finding the parts after the taskbar is rebuilt, and the hot corners when
// the taskbar moves without sliding. While hidden it does nothing.
// -----------------------------------------------------------------------------

constexpr UINT kPollMs = 250;
UINT_PTR g_pollTimer = 0;
// Set while a hide is in progress: the window only moves at its end (the
// motion mod animates the content meanwhile), so the hot corners stay away.
bool g_hiding = false;

void CALLBACK PollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    HWND taskbar = g_taskbar;
    if (g_unloading.load(std::memory_order_acquire) || !IsWindow(taskbar)) {
        return;
    }
    if (IsTaskbarHiddenOnScreen(taskbar)) {
        HideCornerTargets();
        return;
    }
    if (g_hiding) {
        return;
    }
    UpdateGroupedLayout(true);
    SyncCornerTargets(taskbar);
}

// Must run on the taskbar UI thread.
void StartTaskbarWatch(HWND taskbar) {
    if (!IsPrimaryTaskbar(taskbar)) {
        return;
    }
    g_taskbar = taskbar;
    if (!g_pollTimer) {
        g_pollTimer = SetTimer(nullptr, 0, kPollMs, PollTimerProc);
    }
    StartFlyoutPlacement();
}

using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis,
                                           HWND hWnd,
                                           const RECT* rect,
                                           HMONITOR monitor,
                                           bool show,
                                           bool animate);
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original = nullptr;

void WINAPI TrayUI_SlideWindow_Hook(void* pThis,
                                    HWND hWnd,
                                    const RECT* rect,
                                    HMONITOR monitor,
                                    bool show,
                                    bool animate) {
    const bool ours = rect && IsPrimaryTaskbar(hWnd) &&
                      !g_unloading.load(std::memory_order_acquire) &&
                      OnTaskbarUiThread(hWnd);
    if (!ours) {
        TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
        return;
    }

    StartTaskbarWatch(hWnd);
    if (show) {
        // While hidden the taskbar doesn't run layout, so the click-through
        // region can lag behind the grouped placement. Settle both before the
        // content comes into view.
        if (g_settings.groupedDock) {
            g_parts.lastInputs = {};
            UpdateGroupedLayout(true);
            NudgeRegionAfterRender();
        }
    } else {
        // The targets go away before a hide starts moving the content.
        HideCornerTargets();
    }

    // A reveal can arrive while the motion mod's hide is still committing
    // (nested in the call below); either way nothing is hiding afterwards.
    g_hiding = !show;
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
    g_hiding = false;

    if (show && !IsTaskbarHiddenOnScreen(hWnd)) {
        SyncCornerTargets(hWnd);
    }
}

// -----------------------------------------------------------------------------
// Load and unload.
// -----------------------------------------------------------------------------

using RunFromWindowThreadProc_t = void(WINAPI*)(PVOID parameter);

bool RunFromWindowThread(HWND hWnd,
                         RunFromWindowThreadProc_t proc,
                         PVOID procParam) {
    static const UINT registeredMsg =
        RegisterWindowMessageW(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct Param {
        RunFromWindowThreadProc_t proc;
        PVOID param;
        bool ran;
    };

    DWORD threadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (!threadId) {
        return false;
    }

    if (threadId == GetCurrentThreadId()) {
        proc(procParam);
        return true;
    }

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (code == HC_ACTION) {
                const CWPSTRUCT* cwp =
                    reinterpret_cast<const CWPSTRUCT*>(lParam);
                if (cwp->message == registeredMsg) {
                    auto* p = reinterpret_cast<Param*>(cwp->lParam);
                    p->ran = true;
                    p->proc(p->param);
                }
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        },
        nullptr,
        threadId);

    if (!hook) {
        return false;
    }

    Param param{proc, procParam, false};
    SendMessageW(hWnd, registeredMsg, 0, reinterpret_cast<LPARAM>(&param));
    UnhookWindowsHookEx(hook);
    return param.ran;
}

HWND FindPrimaryTaskbar() {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid = 0;
    if (taskbar) {
        GetWindowThreadProcessId(taskbar, &pid);
    }
    return pid == GetCurrentProcessId() ? taskbar : nullptr;
}

void WINAPI StartOnUiThread(PVOID parameter) {
    HWND taskbar = reinterpret_cast<HWND>(parameter);
    StartTaskbarWatch(taskbar);
    if (!IsTaskbarHiddenOnScreen(taskbar)) {
        UpdateGroupedLayout(true);
        SyncCornerTargets(taskbar);
    } else if (g_settings.groupedDock) {
        // Placed now so the first reveal is right; the region follows once
        // the parts are rendered.
        UpdateGroupedLayout(true);
    }
    Trace(L"started: taskbar %p, parts %d", taskbar,
          g_parts.root.get() != nullptr);
}

void WINAPI CleanupOnUiThread(PVOID) {
    if (g_pollTimer) {
        KillTimer(nullptr, g_pollTimer);
        g_pollTimer = 0;
    }
    DestroyCornerTargets();
    StopFlyoutPlacement();
    ResetGroupedLayout();
    g_parts = DockParts{};
}

void LoadSettings() {
    g_settings.groupedDock = Wh_GetIntSetting(L"groupedDock") != 0;
    g_settings.groupGapDip =
        std::clamp(Wh_GetIntSetting(L"groupGapDip"), 0, 48);
    g_settings.cornerTargets = Wh_GetIntSetting(L"cornerTargets") != 0;
    g_settings.traceToFile = Wh_GetIntSetting(L"traceToFile") != 0;
}

bool HookTaskbarSymbols() {
    HMODULE module =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        Wh_Log(L"couldn't load taskbar.dll");
        return false;
    }

    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {
            {LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"},
            &CTaskBand_ITaskListWndSite_vftable,
        },
        {
            {LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"},
            &CTaskBand_GetTaskbarHost_Original,
        },
        {
            {LR"(public: int __cdecl TaskbarHost::FrameHeight(void)const )"},
            &TaskbarHost_FrameHeight_Original,
        },
        {
            {LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"},
            &std__Ref_count_base__Decref_Original,
        },
        {
            {LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))"},
            &TrayUI_SlideWindow_Original,
            TrayUI_SlideWindow_Hook,
            true,
        },
    };

    if (!WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks))) {
        Wh_Log(L"taskbar.dll symbol hook failed");
        return false;
    }

    return true;
}

}  // namespace

BOOL Wh_ModInit() {
    LoadSettings();
    g_unloading.store(false, std::memory_order_release);

    Trace(L"v" WH_MOD_VERSION L" init");

    if (!HookTaskbarSymbols()) {
        return FALSE;
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    if (HWND taskbar = FindPrimaryTaskbar()) {
        RunFromWindowThread(GetTaskbarDispatchWindow(taskbar), StartOnUiThread,
                            taskbar);
    }
}

void Wh_ModBeforeUninit() {
    g_unloading.store(true, std::memory_order_release);
    if (HWND taskbar = g_taskbar ? g_taskbar : FindPrimaryTaskbar()) {
        RunFromWindowThread(GetTaskbarDispatchWindow(taskbar),
                            CleanupOnUiThread, nullptr);
    }
    Trace(L"cleanup complete");
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    *bReload = TRUE;
    return TRUE;
}
