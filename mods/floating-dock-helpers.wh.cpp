// ==WindhawkMod==
// @id              floating-dock-helpers
// @name            Floating Dock Helpers
// @description     macOS-style layout for a floating Windows 11 taskbar (tray next to the centered dock), hot corners for Start and Show desktop, Quick Settings that follows the tray
// @version         1.0.7
// @author          jinSeong-P
// @github          https://github.com/jinSeong-P
// @homepage        https://github.com/jinSeong-P/windhawk-floating-dock
// @license         GPL-3.0
// @include         explorer.exe
// @include         ShellHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lgdi32 -ladvapi32 -ldwmapi
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
  apps and tray icons come and go. Windows alignment is set to Center for this
  layout and Left when this layout is disabled.
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
  $description: Places the system tray next to the dock. Windows taskbar alignment is automatically Center when enabled, or Left when disabled.
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
#include <mutex>
#include <string_view>
#include <vector>

#include <windows.h>
#include <dwmapi.h>
#include <uiautomationcore.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Automation.Peers.h>
#include <winrt/Windows.UI.Xaml.Automation.Provider.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/base.h>

// ShellHost Quick Settings advanced-page Escape adapter.

#include <cwchar>
#include <initializer_list>
#include <windhawk_api.h>
#include <winrt/Windows.System.h>

// Narrow ShellHost-side support for Escape on Quick Settings advanced pages.
// Initialized only in ShellHost.exe; late-loaded ControlCenter hooks are
// applied after the loader returns and drained before unload.
namespace QsPanelEscape {
namespace detail {

using OnPreviewKeyDown_t = void (*)(
    void* page,
    winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& args);
using NavigateToAdvancedPage_t = void (*)(void* view,
                                          void* typeName,
                                          void* advancedPageInfo);
using ResetAdvancedPageFrame_t = void (*)(void* view);
using ControlCenterViewDestructor_t = void (*)(void* view);
using ControlCenterHide_t = void (*)(void* app, void* dismissArgs);
inline ControlCenterHide_t g_hideOriginal{};
inline void* g_hideTarget{};

inline OnPreviewKeyDown_t g_onPreviewKeyDownOriginal = nullptr;
inline NavigateToAdvancedPage_t g_navigateToAdvancedPageOriginal = nullptr;
inline ResetAdvancedPageFrame_t g_resetAdvancedPageFrameOriginal = nullptr;
inline ControlCenterViewDestructor_t g_controlCenterViewDestructorOriginal =
    nullptr;

inline void* g_onPreviewKeyDownTarget = nullptr;
inline void* g_navigateToAdvancedPageTarget = nullptr;
inline void* g_resetAdvancedPageFrameTarget = nullptr;
inline void* g_controlCenterViewDestructorTarget = nullptr;

inline HMODULE g_controlCenterModule = nullptr;
inline std::atomic<void*> g_advancedView{nullptr};
inline std::atomic<DWORD> g_advancedViewThreadId{0};
inline std::atomic<bool> g_enabled{false};
inline bool g_initialized = false;

constexpr wchar_t kControlCenterWindowClass[] = L"ControlCenterWindow";

constexpr wchar_t kOnPreviewKeyDownSymbol[] =
    L"?OnPreviewKeyDown@ControlCenterPage@implementation@ControlCenter@winrt@@QEAAXAEBUKeyRoutedEventArgs@Input@Xaml@UI@Windows@4@@Z";
constexpr wchar_t kNavigateToAdvancedPageSymbol[] =
    L"?NavigateToAdvancedPage@ControlCenterView@implementation@ControlCenter@winrt@@QEAAXUTypeName@Interop@Xaml@UI@Windows@4@UAdvancedPageInfo@34@@Z";
constexpr wchar_t kResetAdvancedPageFrameSymbol[] =
    L"?ResetAdvancedPageFrame@ControlCenterView@implementation@ControlCenter@winrt@@AEAAXXZ";
constexpr wchar_t kControlCenterViewDestructorSymbol[] =
    L"??1ControlCenterView@implementation@ControlCenter@winrt@@EEAA@XZ";

bool IsShellHostProcess() {
    wchar_t imagePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, imagePath,
                                            ARRAYSIZE(imagePath));
    if (!length || length >= ARRAYSIZE(imagePath)) {
        return false;
    }

    const wchar_t* imageName = wcsrchr(imagePath, L'\\');
    imageName = imageName ? imageName + 1 : imagePath;
    return _wcsicmp(imageName, L"ShellHost.exe") == 0;
}

void* FindExactDecoratedSymbol(HMODULE module, PCWSTR decoratedName) {
    WH_FIND_SYMBOL_OPTIONS options{};
    options.optionsSize = sizeof(options);
    options.noUndecoratedSymbols = TRUE;

    WH_FIND_SYMBOL symbol{};
    HANDLE search = Wh_FindFirstSymbol(module, &options, &symbol);
    if (!search) {
        return nullptr;
    }

    void* address = nullptr;
    do {
        if (symbol.symbolDecorated &&
            wcscmp(symbol.symbolDecorated, decoratedName) == 0) {
            address = symbol.address;
            break;
        }
    } while (Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);
    return address;
}

void ClearAdvancedView(void* view) {
    void* expected = view;
    if (g_advancedView.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        g_advancedViewThreadId.store(0, std::memory_order_release);
    }
}

bool IsForegroundQuickSettingsWindow(DWORD expectedThreadId) {
    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }

    HWND quickSettings = GetAncestor(foreground, GA_ROOTOWNER);
    if (!quickSettings) {
        quickSettings = foreground;
    }

    wchar_t className[64]{};
    if (!IsWindow(quickSettings) || !IsWindowVisible(quickSettings) ||
        !GetClassNameW(quickSettings, className, ARRAYSIZE(className)) ||
        wcscmp(className, kControlCenterWindowClass) != 0) {
        return false;
    }

    DWORD processId = 0;
    const DWORD windowThreadId =
        GetWindowThreadProcessId(quickSettings, &processId);
    if (processId != GetCurrentProcessId() ||
        windowThreadId != expectedThreadId ||
        GetCurrentThreadId() != expectedThreadId) {
        return false;
    }

    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(quickSettings, DWMWA_CLOAKED,
                                           &cloaked, sizeof(cloaked))) &&
           cloaked == 0;
}

void OnPreviewKeyDownHook(
    void* page,
    winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& args) {
    // This event receiver is ControlCenterPage; the navigation controller is
    // captured separately from ControlCenterView::NavigateToAdvancedPage.
    // The active view, its UI thread, and the foreground host window together
    // identify the current Quick Settings session.
    (void)page;

    if (g_enabled.load(std::memory_order_acquire)) {
        void* view = g_advancedView.load(std::memory_order_acquire);
        const DWORD viewThreadId =
            g_advancedViewThreadId.load(std::memory_order_acquire);

        if (view && viewThreadId && viewThreadId == GetCurrentThreadId() &&
            IsForegroundQuickSettingsWindow(viewThreadId)) {
            bool isEscape = false;
            try {
                isEscape =
                    args.Key() == winrt::Windows::System::VirtualKey::Escape;
            } catch (...) {
                // Let the native handler process the event if XAML rejects the
                // argument query.
            }

            if (isEscape) {
                try {
                    // The native back-request handler is a verified tail jump
                    // to this no-argument frame reset entry point.
                    g_resetAdvancedPageFrameOriginal(view);
                    ClearAdvancedView(view);
                    args.Handled(true);
                    return;
                } catch (...) {
                    // Preserve Windows' native Escape behavior on failure.
                }
            }
        }
    }

    g_onPreviewKeyDownOriginal(page, args);
}

void NavigateToAdvancedPageHook(void* view,
                                void* typeName,
                                void* advancedPageInfo) {
    // ControlCenter.dll disassembly confirms the two by-value UDT parameters
    // arrive indirectly in RDX and R8 on x64. They are forwarded unchanged.
    g_navigateToAdvancedPageOriginal(view, typeName, advancedPageInfo);

    if (g_enabled.load(std::memory_order_acquire)) {
        g_advancedViewThreadId.store(GetCurrentThreadId(),
                                     std::memory_order_relaxed);
        g_advancedView.store(view, std::memory_order_release);
    }
}

void ResetAdvancedPageFrameHook(void* view) {
    ClearAdvancedView(view);
    g_resetAdvancedPageFrameOriginal(view);
}

void ControlCenterViewDestructorHook(void* view) {
    ClearAdvancedView(view);
    g_controlCenterViewDestructorOriginal(view);
}

void ControlCenterHideHook(void* app, void* dismissArgs) {
    // Public PDB and disassembly confirm the aggregate is indirect in RDX.
    g_hideOriginal(app, dismissArgs);
    const DWORD thread = g_advancedViewThreadId.load(std::memory_order_acquire);
    if (thread == GetCurrentThreadId() && !IsForegroundQuickSettingsWindow(thread)) {
        g_advancedView.store(nullptr, std::memory_order_release);
        g_advancedViewThreadId.store(0, std::memory_order_release);
    }
}

void RemoveQueuedHooks() {
    for (void* target : {g_onPreviewKeyDownTarget,
                         g_navigateToAdvancedPageTarget,
                         g_resetAdvancedPageFrameTarget,
                         g_controlCenterViewDestructorTarget, g_hideTarget}) {
        if (target) {
            Wh_RemoveFunctionHook(target);
        }
    }
}

}  // namespace detail

inline BOOL InitializeQuickSettingsHost() {
    using namespace detail;

    if (g_initialized) {
        return TRUE;
    }
    if (!IsShellHostProcess()) {
        Wh_Log(L"QS Escape: skipped outside ShellHost.exe");
        return FALSE;
    }

    HMODULE module = GetModuleHandleW(L"ControlCenter.dll");
    if (!module) {
        Wh_Log(L"QS Escape: ControlCenter.dll is not loaded");
        return FALSE;
    }

    void* onPreviewKeyDown =
        FindExactDecoratedSymbol(module, kOnPreviewKeyDownSymbol);
    void* navigateToAdvancedPage =
        FindExactDecoratedSymbol(module, kNavigateToAdvancedPageSymbol);
    void* resetAdvancedPageFrame =
        FindExactDecoratedSymbol(module, kResetAdvancedPageFrameSymbol);
    void* controlCenterViewDestructor =
        FindExactDecoratedSymbol(module, kControlCenterViewDestructorSymbol);
    void* hide = FindExactDecoratedSymbol(module,
        L"?Hide@ControlCenterApplication@ControlCenter@winrt@@QEAAXUControlCenterDismissArgs@23@@Z");

    if (!onPreviewKeyDown || !navigateToAdvancedPage ||
        !resetAdvancedPageFrame || !controlCenterViewDestructor || !hide) {
        Wh_Log(L"QS Escape: required ControlCenter.dll symbol lookup failed");
        return FALSE;
    }

    g_onPreviewKeyDownTarget = onPreviewKeyDown;
    g_navigateToAdvancedPageTarget = navigateToAdvancedPage;
    g_resetAdvancedPageFrameTarget = resetAdvancedPageFrame;
    g_controlCenterViewDestructorTarget = controlCenterViewDestructor;
    g_hideTarget = hide;

    if (!Wh_SetFunctionHook(
            onPreviewKeyDown,
            reinterpret_cast<void*>(OnPreviewKeyDownHook),
            reinterpret_cast<void**>(&g_onPreviewKeyDownOriginal)) ||
        !Wh_SetFunctionHook(
            navigateToAdvancedPage,
            reinterpret_cast<void*>(NavigateToAdvancedPageHook),
            reinterpret_cast<void**>(&g_navigateToAdvancedPageOriginal)) ||
        !Wh_SetFunctionHook(
            resetAdvancedPageFrame,
            reinterpret_cast<void*>(ResetAdvancedPageFrameHook),
            reinterpret_cast<void**>(&g_resetAdvancedPageFrameOriginal)) ||
        !Wh_SetFunctionHook(
            controlCenterViewDestructor,
            reinterpret_cast<void*>(ControlCenterViewDestructorHook),
            reinterpret_cast<void**>(&g_controlCenterViewDestructorOriginal)) ||
        !Wh_SetFunctionHook(hide, reinterpret_cast<void*>(ControlCenterHideHook),
            reinterpret_cast<void**>(&g_hideOriginal))) {
        RemoveQueuedHooks();
        g_onPreviewKeyDownTarget = nullptr;
        g_navigateToAdvancedPageTarget = nullptr;
        g_resetAdvancedPageFrameTarget = nullptr;
        g_controlCenterViewDestructorTarget = nullptr;
        g_hideTarget = nullptr;
        Wh_Log(L"QS Escape: ControlCenter.dll hook registration failed");
        return FALSE;
    }

    g_controlCenterModule = module;
    g_enabled.store(true, std::memory_order_release);
    g_initialized = true;
    Wh_Log(L"QS Escape: registered ShellHost ControlCenter hooks");
    return TRUE;
}

inline BOOL AfterInitQuickSettingsHost() {
    using namespace detail;

    if (!g_initialized || !g_controlCenterModule ||
        !g_onPreviewKeyDownOriginal || !g_navigateToAdvancedPageOriginal ||
        !g_resetAdvancedPageFrameOriginal ||
        !g_controlCenterViewDestructorOriginal) {
        Wh_Log(L"QS Escape: hook trampolines were not initialized");
        return FALSE;
    }

    Wh_Log(L"QS Escape: ControlCenter hooks are active");
    return TRUE;
}

inline void UninitializeQuickSettingsHost() {
    using namespace detail;

    if (!g_initialized) {
        return;
    }

    g_enabled.store(false, std::memory_order_release);
    g_advancedView.store(nullptr, std::memory_order_release);
    g_advancedViewThreadId.store(0, std::memory_order_release);
    RemoveQueuedHooks();

    // Keep trampolines intact until Windhawk applies the queued removals after
    // Wh_ModBeforeUninit returns. A concurrent callback can still need to
    // forward to the original while teardown is in progress.
    g_onPreviewKeyDownTarget = nullptr;
    g_navigateToAdvancedPageTarget = nullptr;
    g_resetAdvancedPageFrameTarget = nullptr;
    g_controlCenterViewDestructorTarget = nullptr;
    g_hideTarget = nullptr;
    g_controlCenterModule = nullptr;
    g_initialized = false;
}

}  // namespace QsPanelEscape

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
bool g_alignmentApplied = false;

void ApplyNativeTaskbarAlignment(HWND taskbar) {
    if (g_alignmentApplied || !IsWindow(taskbar)) {
        return;
    }
    constexpr wchar_t key[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
    const DWORD desired = g_settings.groupedDock ? 1 : 0;
    DWORD current = 0, bytes = sizeof(current);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, key, L"TaskbarAl",
        RRF_RT_REG_DWORD, nullptr, &current, &bytes);
    if (status == ERROR_SUCCESS && current == desired) {
        g_alignmentApplied = true;
        return;
    }
    status = RegSetKeyValueW(HKEY_CURRENT_USER, key, L"TaskbarAl",
                             REG_DWORD, &desired, sizeof(desired));
    if (status != ERROR_SUCCESS) {
        Trace(L"native alignment write failed: %ld", status);
        return;
    }
    g_alignmentApplied = true;
    DWORD_PTR notificationResult = 0;
    SendMessageTimeoutW(taskbar, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"TraySettings"), SMTO_ABORTIFHUNG, 1000,
        &notificationResult);
    // This reaches TrayUI::_HandleSettingChange, the same refresh entry used
    // by the official taskbar-icon-size mod. Merely broadcasting TraySettings
    // doesn't refresh all of Windows' cached taskbar layout state.
    PostMessageW(taskbar, WM_SETTINGCHANGE, SPI_SETLOGICALDPIOVERRIDE, 0);
    Trace(L"native alignment set to %s", desired ? L"center" : L"left");
}



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
    if (g_parts.root.get() && g_parts.dock.get() && g_parts.tray.get() &&
        g_parts.startButton.get() && g_parts.showDesktop.get()) {
        return true;
    }
    if (GetTickCount64() < g_parts.nextLookup) {
        return g_parts.root.get() && g_parts.dock.get() && g_parts.tray.get();
    }
    const ULONGLONG nextLookup = GetTickCount64() + 1000;
    try {
        if (FindParts(g_taskbar)) {
            if (!g_parts.startButton.get() || !g_parts.showDesktop.get())
                g_parts.nextLookup = nextLookup;
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
void RequestCornerStart();
void InvokeCorner(CornerKind kind) {
    BOOL posted = FALSE;
    if (kind == CornerKind::Start) {
        RequestCornerStart();
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
constexpr wchar_t kQuickSettingsHoldProperty[] = L"FloatingDock.QuickSettingsHoldOpen";

HWINEVENTHOOK g_flyoutShowHook = nullptr;
HWINEVENTHOOK g_flyoutUncloakHook = nullptr;
HWINEVENTHOOK g_flyoutMenuHook = nullptr;
UINT_PTR g_panelEventTimer{};
HWND g_quickSettingsDirty{};
bool g_panelEventDirty{}, g_xamlPopupDirty{}, g_panelCoordinatorStarted{};
void SyncPanelTimer();
void StartFlyoutPlacement();
void StopFlyoutPlacement();
HWND g_quickSettings = nullptr;
UINT_PTR g_quickSettingsTimer = 0;
int g_quickSettingsChecksLeft = 0;

// Included inside the Explorer implementation namespace. All mutable state
// and callbacks belong to the primary taskbar's XAML dispatcher thread.
enum class PanelKind { None, Start, Search, Quick, Notifications, Overflow, Menu, TaskView };
constexpr wchar_t kPanelHold[] = L"FloatingDock.PanelHold.v1";
constexpr wchar_t kPanelEpoch[] = L"FloatingDock.PanelEpoch.v1";
constexpr wchar_t kPanelReady[] = L"FloatingDock.PanelBridgeReady.v1";
struct PanelRecord {
    HWND window{};
    DWORD process{}, thread{};
    PanelKind kind{};
};
struct PanelSession {
    PanelKind current{}, requested{};
    HWND window{};
    ULONGLONG deadline{};
    bool held{};
};
PanelSession g_panelSession;
UINT_PTR g_panelRequestId{};
UINT_PTR g_holdSessionId{};
std::vector<PanelRecord> g_panelRegistry;
UINT_PTR g_panelTimer{};
UINT g_panelClosedMessage{};
winrt::weak_ref<FrameworkElement> g_panelInputRoot;
Input::PointerEventHandler g_panelPointerHandler{nullptr};
winrt::Windows::Foundation::IInspectable g_panelHandlerBox{nullptr};
HHOOK g_panelKeyboardHook{};
HHOOK g_panelMouseHook{};
const wchar_t* g_panelCancellation{};
UINT_PTR g_panelCancelTimer{};
struct PanelCounters {
    ULONGLONG searches{}, timers{}, events{}, coalesced{}, mouse{}, keyboard{};
    ULONGLONG nextSummary{};
} g_panelCounters;
bool g_panelReconciling{};
bool g_trayClicksHooked{};
bool g_trayClickModuleSeen{};
struct PendingPanelActivation {
    winrt::weak_ref<FrameworkElement> button;
    HWND oldWindow{};
    UINT_PTR requestId{};
    PanelKind target{};
    bool released{}, closeCompleted{};
};
PendingPanelActivation g_pendingPanelActivation;

void CALLBACK PanelCloseCompleted(HWND, UINT, ULONG_PTR requestId, LRESULT) {
    if (!g_unloading.load() && g_pendingPanelActivation.requestId == requestId)
        g_pendingPanelActivation.closeCompleted = true;
}

void ClearPendingPanelActivation() {
    g_pendingPanelActivation = {};
    if (g_panelKeyboardHook) UnhookWindowsHookEx(g_panelKeyboardHook);
    if (g_panelMouseHook) UnhookWindowsHookEx(g_panelMouseHook);
    g_panelKeyboardHook = g_panelMouseHook = nullptr;
    if (g_panelCancelTimer) KillTimer(nullptr, g_panelCancelTimer);
    g_panelCancelTimer = 0;
    g_panelCancellation = nullptr;
}

bool PanelVisible(HWND window) {
    if (!IsWindow(window) || !IsWindowVisible(window)) return false;
    DWORD cloak = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloak,
                                           sizeof(cloak))) && !cloak;
}

struct PanelProcessRecord {
    DWORD pid{};
    HANDLE handle{};
    std::wstring name;
};
std::vector<PanelProcessRecord> g_panelProcesses;

void ClearPanelProcessCache() {
    for (auto const& record : g_panelProcesses) CloseHandle(record.handle);
    g_panelProcesses.clear();
}

std::wstring PanelProcessName(DWORD pid) {
    for (auto it = g_panelProcesses.begin(); it != g_panelProcesses.end();) {
        // A retained process handle identifies this creation, even after PID
        // reuse. Query a new process only once the retained instance exits.
        if (WaitForSingleObject(it->handle, 0) == WAIT_TIMEOUT) {
            if (it->pid == pid) return it->name;
            ++it;
        } else {
            CloseHandle(it->handle);
            it = g_panelProcesses.erase(it);
        }
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, pid);
    if (!process) return {};
    wchar_t path[MAX_PATH]{};
    DWORD length = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) {
        CloseHandle(process);
        return {};
    }
    const wchar_t* filename = wcsrchr(path, L'\\');
    std::wstring name = filename ? filename + 1 : path;
    g_panelProcesses.push_back({pid, process, name});
    return name;
}

bool PanelPopupOwnedBy(HWND window, HWND owner) {
    if (!IsWindow(owner)) return false;
    for (HWND current = GetWindow(window, GW_OWNER); current;
         current = GetWindow(current, GW_OWNER)) {
        if (current == owner) return true;
    }
    return false;
}

bool TaskbarHasMenuFlyout() {
    // Tooltips and menus can share the native popup class. Require an open
    // MenuFlyoutPresenter in the taskbar's XAML tree, not just HWND ownership.
    // This also covers keyboard Win+X, which has no Win32 menu-mode flag.
    try {
        auto root = g_parts.root.get();
        if (!root) return false;
        auto xamlRoot = root.XamlRoot();
        if (!xamlRoot) return false;
        for (auto const& popup : VisualTreeHelper::GetOpenPopupsForXamlRoot(xamlRoot)) {
            if (!popup.IsOpen()) continue;
            std::vector<DependencyObject> pending;
            if (auto child = popup.Child()) pending.push_back(child);
            // A flyout presenter is near the popup root. Bound the traversal
            // so unrelated popup content cannot turn this into a tree scan.
            for (size_t index = 0; index < pending.size() && index < 32; ++index) {
                auto const element = pending[index];
                if (element.try_as<Controls::MenuFlyoutPresenter>()) return true;
                const int count = VisualTreeHelper::GetChildrenCount(element);
                for (int child = 0; child < count && pending.size() < 32; ++child)
                    pending.push_back(VisualTreeHelper::GetChild(element, child));
            }
        }
    } catch (winrt::hresult_error const&) {
        // Unknown popup kinds retain Windows' native behavior.
    }
    return false;
}

void DiscoverPanelWindows() {
    // Validate HWND, PID and thread every reconciliation. A recycled handle
    // cannot inherit an old record. Unknown CoreWindows are never dismissed.
    if (g_settings.traceToFile) ++g_panelCounters.searches;
    g_panelRegistry.clear();
    std::optional<bool> menuFlyoutOpen;
    for (HWND window = FindWindowExW(nullptr, nullptr, nullptr, nullptr); window;
         window = FindWindowExW(nullptr, window, nullptr, nullptr)) {
        wchar_t cls[96]{};
        GetClassNameW(window, cls, ARRAYSIZE(cls));
        const bool core = !wcscmp(cls, L"Windows.UI.Core.CoreWindow");
        const bool quick = !wcscmp(cls, L"ControlCenterWindow");
        const bool overflow = !wcscmp(cls, L"TopLevelWindowForOverflowXamlIsland");
        const bool xamlPopup = !wcscmp(cls, L"Xaml_WindowedPopupClass");
        const bool menu = !wcscmp(cls, L"#32768") || xamlPopup;
        if (!core && !quick && !overflow && !menu) continue;
        if (!PanelVisible(window)) continue;
        DWORD pid{};
        DWORD thread = GetWindowThreadProcessId(window, &pid);
        auto process = PanelProcessName(pid);
        PanelKind kind = PanelKind::None;
        if (quick && !_wcsicmp(process.c_str(), L"ShellHost.exe")) kind = PanelKind::Quick;
        else if (overflow && pid == GetCurrentProcessId()) kind = PanelKind::Overflow;
        else if (core && !_wcsicmp(process.c_str(), L"StartMenuExperienceHost.exe")) kind = PanelKind::Start;
        else if (core && !_wcsicmp(process.c_str(), L"SearchHost.exe")) kind = PanelKind::Search;
        else if (core && !_wcsicmp(process.c_str(), L"ShellExperienceHost.exe")) {
            RECT rect{};
            MONITORINFO monitor{sizeof(monitor)};
            if (GetWindowRect(window, &rect) && GetMonitorInfoW(
                    MonitorFromWindow(g_taskbar, MONITOR_DEFAULTTONEAREST), &monitor)) {
                // Notification host is a narrow, work-area-height right column.
                // Jump lists share the process/class but have a compact host.
                const int height = monitor.rcWork.bottom - monitor.rcWork.top;
                const int width = monitor.rcWork.right - monitor.rcWork.left;
                RECT taskbarRect{};
                GetWindowRect(g_taskbar, &taskbarRect);
                const int taskbarHeight = taskbarRect.bottom - taskbarRect.top;
                if (rect.top <= monitor.rcWork.top + 2 &&
                    rect.bottom >= monitor.rcWork.bottom - taskbarHeight - 2 &&
                    rect.right >= monitor.rcWork.right - 2 &&
                    rect.right - rect.left < width / 2) kind = PanelKind::Notifications;
                else if (rect.bottom > rect.top && rect.right > rect.left &&
                    (g_panelSession.requested == PanelKind::Menu ||
                     g_panelSession.current == PanelKind::Menu)) kind = PanelKind::Menu;
            }
        } else if (menu && pid == GetCurrentProcessId()) {
            // Explorer owns both classic and XAML menus. Count only popups
            // attached to the active panel or primary taskbar.
            const bool attachedToActiveUi =
                PanelPopupOwnedBy(window, g_panelSession.window) ||
                PanelPopupOwnedBy(window, g_taskbar);
            if (attachedToActiveUi) {
                if (xamlPopup && !menuFlyoutOpen)
                    menuFlyoutOpen = TaskbarHasMenuFlyout();
                if (!xamlPopup || *menuFlyoutOpen) kind = PanelKind::Menu;
            }
        }
        if (kind != PanelKind::None) g_panelRegistry.push_back({window, pid, thread, kind});
    }
}

HWND PanelWindow(PanelKind kind) {
    for (auto const& record : g_panelRegistry) {
        if (record.kind == kind) return record.window;
    }
    return nullptr;
}

void SetPanelHold(bool hold) {
    if (!IsWindow(g_taskbar)) return;
    if (!GetPropW(g_taskbar, kPanelReady)) {
        // Mixed versions retain the existing Quick Settings bridge only.
        if (PanelWindow(PanelKind::Quick))
            SetPropW(g_taskbar, kQuickSettingsHoldProperty, reinterpret_cast<HANDLE>(1));
        else RemovePropW(g_taskbar, kQuickSettingsHoldProperty);
        return;
    }
    RemovePropW(g_taskbar, kQuickSettingsHoldProperty);
    if (hold) {
        if (!g_panelSession.held) {
            if (!++g_holdSessionId) ++g_holdSessionId;
            SetPropW(g_taskbar, kPanelEpoch, reinterpret_cast<HANDLE>(g_holdSessionId));
            SetPropW(g_taskbar, kPanelHold, reinterpret_cast<HANDLE>(g_holdSessionId));
            g_panelSession.held = true;
        }
    } else if (g_panelSession.held) {
        RemovePropW(g_taskbar, kPanelHold);
        g_panelSession.held = false;
        PostMessageW(g_taskbar, g_panelClosedMessage, g_holdSessionId, 0);
    }
}

void ReconcilePanels() {
    if (!g_panelCoordinatorStarted || g_panelReconciling || g_unloading.load()) return;
    g_panelReconciling = true;
    DiscoverPanelWindows();
    PanelKind actual = PanelKind::None;
    // Prefer the requested target, then the current target; Start's SearchHost
    // proxy isn't a second visible panel. We never count it beside Start.
    if (g_panelSession.requested != PanelKind::None &&
        PanelWindow(g_panelSession.requested)) actual = g_panelSession.requested;
    else if (PanelWindow(PanelKind::Start)) {
        actual = (g_panelSession.current == PanelKind::Search && PanelWindow(PanelKind::Search))
                    ? PanelKind::Search : PanelKind::Start;
    } else {
        for (PanelKind kind : {PanelKind::Search, PanelKind::Quick,
                             PanelKind::Notifications, PanelKind::Overflow, PanelKind::Menu}) {
            if (PanelWindow(kind)) { actual = kind; break; }
        }
    }
    const ULONGLONG now = GetTickCount64();
    if (g_panelSession.requested == PanelKind::TaskView && actual == PanelKind::None) {
        g_panelSession.requested = PanelKind::None;
        g_panelSession.deadline = 0;
    }
    if (actual == g_panelSession.requested && actual != PanelKind::None) {
        g_panelSession.requested = PanelKind::None;
        g_panelSession.deadline = 0;
    } else if (g_panelSession.deadline && now >= g_panelSession.deadline) {
        Trace(L"panel transition expired request=%llu requested=%d actual=%d",
              static_cast<unsigned long long>(g_panelRequestId),
              static_cast<int>(g_panelSession.requested), static_cast<int>(actual));
        g_panelSession.requested = PanelKind::None;
        g_panelSession.deadline = 0;
    }
    if (actual != g_panelSession.current) {
        Trace(L"panel state request=%llu %d -> %d window=%p transition=%d",
              static_cast<unsigned long long>(g_panelRequestId),
              static_cast<int>(g_panelSession.current), static_cast<int>(actual),
              PanelWindow(actual), g_panelSession.deadline != 0);
    }
    g_panelSession.current = actual;
    g_panelSession.window = PanelWindow(actual);
    if (g_panelSession.requested != PanelKind::TaskView)
        SetPanelHold(actual != PanelKind::None || g_panelSession.deadline != 0);
    g_panelReconciling = false;
    SyncPanelTimer();
}

LRESULT CALLBACK PanelKeyboardProc(int, WPARAM, LPARAM);
LRESULT CALLBACK PanelMouseProc(int, WPARAM, LPARAM);

bool InstallPendingPanelHooks() {
    g_panelKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, PanelKeyboardProc,
                                          ModuleInstance(), 0);
    g_panelMouseHook = SetWindowsHookExW(WH_MOUSE_LL, PanelMouseProc,
                                       ModuleInstance(), 0);
    if (g_panelKeyboardHook && g_panelMouseHook) return true;
    ClearPendingPanelActivation();
    return false;
}

void RequestPanel(PanelKind target, FrameworkElement const& button = nullptr) {
    if (g_unloading.load() || !GetPropW(g_taskbar, kPanelReady)) return;
    ReconcilePanels();
    const PanelKind old = g_panelSession.current;
    ClearPendingPanelActivation();
    ++g_panelRequestId;
    if (!g_panelRequestId) ++g_panelRequestId;
    if (target == PanelKind::TaskView) {
        // Full-screen Task View owns the native taskbar policy. This is a
        // release of our override, not a request to force-hide the taskbar.
        g_panelSession.requested = target;
        g_panelSession.deadline = GetTickCount64() + 1000;
        SetPanelHold(false);
        SyncPanelTimer();
        return;
    }
    if (old == target) {
        // Let the native toggle close it; no second toggle and no grace delay.
        g_panelSession.requested = PanelKind::None;
        g_panelSession.deadline = 0;
        return;
    }
    g_panelSession.requested = target;
    g_panelSession.deadline = GetTickCount64() + 1000;
    SetPanelHold(true);
    Trace(L"panel input request=%llu old=%d target=%d",
          static_cast<unsigned long long>(g_panelRequestId),
          static_cast<int>(old), static_cast<int>(target));
    if (button && g_trayClicksHooked &&
        (old == PanelKind::Start || old == PanelKind::Search) &&
        (target == PanelKind::Quick || target == PanelKind::Notifications) &&
        InstallPendingPanelHooks()) {
        g_pendingPanelActivation = {winrt::make_weak(button), g_panelSession.window,
                                    g_panelRequestId, target, false, false};
    }
    // Only pre-close when the target's native IconView activation is safely
    // deferred. Keyboard shortcuts and unrecognized/unhooked tray buttons
    // keep their native shell behavior.
    const bool canDeferNativeActivation = g_trayClicksHooked &&
        g_panelKeyboardHook && g_panelMouseHook &&
        g_pendingPanelActivation.requestId == g_panelRequestId;
    if (canDeferNativeActivation &&
        (target == PanelKind::Quick || target == PanelKind::Notifications) &&
        (old == PanelKind::Start || old == PanelKind::Search)) {
        if (old == PanelKind::Start && PanelVisible(g_panelSession.window)) {
            // Same Explorer UI thread, before PointerReleased activates B.
            SendMessageW(g_taskbar, WM_SYSCOMMAND, SC_TASKLIST, 0);
        } else if (old == PanelKind::Search && PanelVisible(g_panelSession.window)) {
            // Exact Search root only; proven not to dismiss the new QS root.
            if (!SendMessageCallbackW(g_panelSession.window, WM_SYSCOMMAND,
                                      SC_CLOSE, 0, PanelCloseCompleted, g_panelRequestId))
                ClearPendingPanelActivation();
        }
        if (old == PanelKind::Start && g_pendingPanelActivation.requestId == g_panelRequestId)
            g_pendingPanelActivation.closeCompleted = true;
    } else if ((target == PanelKind::Quick || target == PanelKind::Notifications) &&
               (old == PanelKind::Start || old == PanelKind::Search)) {
        Trace(L"panel native fallback old=%d target=%d: no deferred tray entry",
              static_cast<int>(old), static_cast<int>(target));
    }
    SyncPanelTimer();
}

void CancelPendingPanelActivation(const wchar_t* reason) {
    const UINT_PTR requestId = g_pendingPanelActivation.requestId;
    if (!requestId) return;
    Trace(L"panel deferred activation canceled request=%llu reason=%s",
          static_cast<unsigned long long>(requestId), reason);
    ClearPendingPanelActivation();
    if (requestId == g_panelRequestId && g_panelSession.deadline) {
        g_panelSession.requested = PanelKind::None;
        g_panelSession.deadline = 0;
        SetPanelHold(g_panelSession.current != PanelKind::None);
    }
    SyncPanelTimer();
}

PanelKind PanelButton(DependencyObject source) {
    for (int depth = 0; source && depth < 24; ++depth) {
        if (auto element = source.try_as<FrameworkElement>()) {
            auto id = Automation::AutomationProperties::GetAutomationId(element);
            auto name = element.Name();
            auto cls = winrt::get_class_name(element);
            if (id == L"StartButton") return PanelKind::Start;
            if (id == L"SearchButton") return PanelKind::Search;
            if (id == L"TaskViewButton") return PanelKind::TaskView;
            if (name == L"ControlCenterButton") return PanelKind::Quick;
            if (name == L"NotificationCenterButton") return PanelKind::Notifications;
            if (cls == L"SystemTray.ChevronIconView") return PanelKind::Overflow;
        }
        source = VisualTreeHelper::GetParent(source);
    }
    return PanelKind::None;
}

bool PanelContextButton(DependencyObject source) {
    for (int depth = 0; source && depth < 24; ++depth) {
        if (auto element = source.try_as<FrameworkElement>()) {
            auto id = Automation::AutomationProperties::GetAutomationId(element);
            if (id == L"StartButton" || std::wstring_view(id).starts_with(L"Appid:")) return true;
        }
        source = VisualTreeHelper::GetParent(source);
    }
    return false;
}

Automation::Provider::IInvokeProvider PanelInvokeProvider(FrameworkElement const& element) {
    auto peer = Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element);
    if (!peer) return nullptr;
    if (auto outer = peer.try_as<Automation::Provider::IInvokeProvider>()) return outer;
    auto pattern = peer.GetPattern(Automation::Peers::PatternInterface::Invoke);
    if (!pattern) return nullptr;
    Trace(L"panel invoke pattern=%s", winrt::get_class_name(pattern).c_str());
    if (auto direct = pattern.try_as<Automation::Provider::IInvokeProvider>()) return direct;
    if (auto boxed = pattern.try_as<winrt::Windows::Foundation::IReference<Automation::Provider::IInvokeProvider>>())
        return boxed.Value();
    return nullptr;
}

FrameworkElement PanelInvokableButton(DependencyObject source) {
    for (int depth = 0; source && depth < 24; ++depth) {
        if (auto element = source.try_as<FrameworkElement>()) {
            if (PanelInvokeProvider(element)) return element;
        }
        source = VisualTreeHelper::GetParent(source);
    }
    return nullptr;
}

void CompletePanelActivation() {
    auto pending = g_pendingPanelActivation;
    if (!pending.requestId) return;
    if (g_panelCancellation) {
        CancelPendingPanelActivation(g_panelCancellation);
        return;
    }
    if (pending.requestId != g_panelRequestId) {
        ClearPendingPanelActivation();
        return;
    }
    // Another native path may already have opened the requested target.
    // Never toggle that panel closed during timeout recovery.
    if (g_panelSession.current == pending.target) {
        ClearPendingPanelActivation();
        return;
    }
    const bool expired = !g_panelSession.deadline || GetTickCount64() >= g_panelSession.deadline;
    if (!expired && (!pending.released || !pending.closeCompleted || PanelVisible(pending.oldWindow))) return;
    // Clear and unhook before Invoke: activation can reenter the dispatcher.
    ClearPendingPanelActivation();
    if (!pending.released) return;
    if (auto button = pending.button.get()) {
        try {
            if (auto invoke = PanelInvokeProvider(button)) {
                Trace(L"panel deferred activation %s request=%llu",
                      expired ? L"recovered" : L"completed",
                      static_cast<unsigned long long>(pending.requestId));
                invoke.Invoke();
            }
        } catch (winrt::hresult_error const& error) {
            Trace(L"panel invoke failed 0x%08X", static_cast<unsigned>(error.code()));
        }
    }
}

using TrayClickPoint_t = bool (*)(void*, winrt::Windows::Foundation::Point const&, bool, bool, int);
using TrayClick_t = bool (*)(void*, bool, bool);
TrayClickPoint_t g_trayClickPointOriginal{};
TrayClick_t g_trayClickOriginal{};

bool DeferNativeTrayClick(bool right) {
    if (right || g_unloading.load() || !g_pendingPanelActivation.requestId ||
        g_pendingPanelActivation.requestId != g_panelRequestId ||
        !g_panelKeyboardHook || !g_panelMouseHook ||
        !OnTaskbarUiThread(g_taskbar)) return false;
    g_pendingPanelActivation.released = true;
    Trace(L"panel native activation intercepted request=%llu",
          static_cast<unsigned long long>(g_panelRequestId));
    CompletePanelActivation();
    return true;
}
bool TrayClickPointHook(void* object, winrt::Windows::Foundation::Point const& point,
                        bool right, bool doubleClick, int device) {
    if (DeferNativeTrayClick(right)) return true;
    return g_trayClickPointOriginal(object, point, right, doubleClick, device);
}
bool TrayClickHook(void* object, bool right, bool doubleClick) {
    if (DeferNativeTrayClick(right)) return true;
    return g_trayClickOriginal(object, right, doubleClick);
}
bool HookTrayClicks() {
    if (g_trayClicksHooked) return true;
    HMODULE module = GetModuleHandleW(L"SystemTray.dll");
    if (!module) return false;
    g_trayClickModuleSeen = true;
    void* point = QsPanelEscape::detail::FindExactDecoratedSymbol(module,
        L"?OnClicked@IconView@implementation@SystemTray@winrt@@IEAA_NAEBUPoint@Foundation@Windows@4@_N1W4PointerDeviceType@Input@Devices@74@@Z");
    void* click = QsPanelEscape::detail::FindExactDecoratedSymbol(module,
        L"?OnClicked@IconView@implementation@SystemTray@winrt@@QEAA_N_N0@Z");
    if (!point || !click) return false;
    if (!Wh_SetFunctionHook(point, reinterpret_cast<void*>(TrayClickPointHook), reinterpret_cast<void**>(&g_trayClickPointOriginal)) ||
        !Wh_SetFunctionHook(click, reinterpret_cast<void*>(TrayClickHook), reinterpret_cast<void**>(&g_trayClickOriginal))) {
        Wh_RemoveFunctionHook(point);
        Wh_RemoveFunctionHook(click);
        return false;
    }
    g_trayClicksHooked = true;
    Trace(L"panel native tray activation hooks registered");
    return true;
}

winrt::Windows::Foundation::IInspectable PanelHandlerInspectable() {
    return g_panelHandlerBox;
}
void AttachPanelInput() {
    auto root = g_parts.root.get();
    if (!root || root == g_panelInputRoot.get()) return;
    if (auto old = g_panelInputRoot.get(); old && g_panelPointerHandler)
        old.RemoveHandler(UIElement::PointerPressedEvent(), PanelHandlerInspectable());
    g_panelPointerHandler = Input::PointerEventHandler([](auto const&, Input::PointerRoutedEventArgs const& args) {
        try {
            if (g_unloading.load()) return;
            if (g_pendingPanelActivation.requestId)
                CancelPendingPanelActivation(L"taskbar pointer button");
            auto root = g_panelInputRoot.get();
            if (!root) return;
            auto point = args.GetCurrentPoint(root);
            PanelKind target = PanelButton(args.OriginalSource().try_as<DependencyObject>());
            if (point.Properties().IsRightButtonPressed())
                target = PanelContextButton(args.OriginalSource().try_as<DependencyObject>())
                            ? PanelKind::Menu : PanelKind::None;
            if (target != PanelKind::None) {
                FrameworkElement button{nullptr};
                if (!point.Properties().IsRightButtonPressed() &&
                    (target == PanelKind::Quick || target == PanelKind::Notifications))
                    button = PanelInvokableButton(args.OriginalSource().try_as<DependencyObject>());
                RequestPanel(target, button);
            }
        } catch (winrt::hresult_error const& error) {
            Trace(L"panel input failed 0x%08X", static_cast<unsigned>(error.code()));
        }
    });
    g_panelHandlerBox = winrt::box_value(g_panelPointerHandler);
    root.AddHandler(UIElement::PointerPressedEvent(), PanelHandlerInspectable(), true);
    g_panelInputRoot = winrt::make_weak(root);
    Trace(L"panel input attached root=%p", winrt::get_abi(root));
}

void CALLBACK PanelTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_unloading.load() || !g_panelCoordinatorStarted) return;
    if (g_settings.traceToFile) ++g_panelCounters.timers;
    ReconcilePanels();
    CompletePanelActivation();
    SyncPanelTimer();
}

void SyncPanelTimer() {
    const bool active = g_panelCoordinatorStarted &&
        (g_panelSession.held || g_panelSession.deadline ||
         g_pendingPanelActivation.requestId);
    if (active && !g_panelTimer)
        g_panelTimer = SetTimer(nullptr, 0, 100, PanelTimerProc);
    else if (!active && g_panelTimer) {
        KillTimer(nullptr, g_panelTimer);
        g_panelTimer = 0;
    }
}

void RequestCornerStart() { RequestPanel(PanelKind::Start); }

void CALLBACK PanelCancelTimerProc(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    g_panelCancelTimer = 0;
    const auto reason = g_panelCancellation;
    g_panelCancellation = nullptr;
    if (reason && !g_unloading.load()) CancelPendingPanelActivation(reason);
}

void MarkPanelCancellation(const wchar_t* reason) {
    if (!g_pendingPanelActivation.requestId) return;
    g_panelCancellation = reason;
    if (!g_panelCancelTimer)
        g_panelCancelTimer = SetTimer(nullptr, 0, 1, PanelCancelTimerProc);
}

LRESULT CALLBACK PanelKeyboardProc(int code, WPARAM message, LPARAM data) {
    if (g_settings.traceToFile) ++g_panelCounters.keyboard;
    if (code == HC_ACTION && !g_unloading.load() &&
        (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
        const DWORD key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data)->vkCode;
        if (key == VK_ESCAPE) MarkPanelCancellation(L"Escape");
        else if (key == VK_LWIN || key == VK_RWIN)
            MarkPanelCancellation(L"new shortcut");
    }
    return CallNextHookEx(nullptr, code, message, data);
}

LRESULT CALLBACK PanelMouseProc(int code, WPARAM message, LPARAM data) {
    if (g_settings.traceToFile) ++g_panelCounters.mouse;
    if (code == HC_ACTION && !g_unloading.load() &&
        (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
         message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN))
        MarkPanelCancellation(L"mouse button");
    return CallNextHookEx(nullptr, code, message, data);
}

void StartPanelCoordinator() {
    if (g_panelCoordinatorStarted || !GetPropW(g_taskbar, kPanelReady)) return;
    if (!g_panelClosedMessage)
        g_panelClosedMessage = RegisterWindowMessageW(L"FloatingDock.PanelSessionClosed.v1");
    if (!g_panelClosedMessage) return;
    g_panelCoordinatorStarted = true;
    g_holdSessionId = reinterpret_cast<UINT_PTR>(GetPropW(g_taskbar, kPanelEpoch));
    if (!g_trayClicksHooked && !g_trayClickModuleSeen && HookTrayClicks())
        Wh_ApplyHookOperations();
    StopFlyoutPlacement();
    StartFlyoutPlacement();
    ReconcilePanels();
    try { AttachPanelInput(); } catch (winrt::hresult_error const& error) {
        Trace(L"panel attach failed 0x%08X", static_cast<unsigned>(error.code()));
    }
}

void StopPanelCoordinator() {
    g_panelCoordinatorStarted = false;
    ClearPendingPanelActivation();
    if (g_panelTimer) KillTimer(nullptr, g_panelTimer);
    g_panelTimer = 0;
    if (auto root = g_panelInputRoot.get(); root && g_panelPointerHandler)
        root.RemoveHandler(UIElement::PointerPressedEvent(), PanelHandlerInspectable());
    g_panelInputRoot = {};
    g_panelPointerHandler = nullptr;
    g_panelHandlerBox = nullptr;
    // Unload is not proof that a panel closed; no forced-hide notification.
    RemovePropW(g_taskbar, kPanelHold);
    RemovePropW(g_taskbar, kQuickSettingsHoldProperty);
    g_panelRegistry.clear();
    ClearPanelProcessCache();
    g_panelSession = {};
    StopFlyoutPlacement();
    if (!g_unloading.load()) StartFlyoutPlacement();
}

void StopQuickSettingsWatch() {
    if (g_quickSettingsTimer) {
        KillTimer(nullptr, g_quickSettingsTimer);
        g_quickSettingsTimer = 0;
    }
    g_quickSettings = nullptr;
    g_quickSettingsChecksLeft = 0;

}

void PlaceQuickSettings(HWND hWnd) {
    if (!g_settings.groupedDock) return;
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
    if (g_unloading.load() || !PanelVisible(g_quickSettings)) {
        StopQuickSettingsWatch();
        return;
    }
    PlaceQuickSettings(g_quickSettings);
    if (--g_quickSettingsChecksLeft <= 0) StopQuickSettingsWatch();
}

void UpdateLegacyQuickSettingsHold(HWND window) {
    if (g_panelCoordinatorStarted) return;
    if (PanelVisible(window))
        SetPropW(g_taskbar, kQuickSettingsHoldProperty, reinterpret_cast<HANDLE>(1));
    else RemovePropW(g_taskbar, kQuickSettingsHoldProperty);
}

void CALLBACK PanelEventTimerProc(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    g_panelEventTimer = 0;
    if (g_unloading.load()) return;
    if (g_xamlPopupDirty) {
        g_xamlPopupDirty = false;
        // Hover-only popups must not start a full window discovery or hold.
        // An existing session still needs reconciliation when its menu closes.
        if (g_panelCoordinatorStarted &&
            (g_panelSession.held || TaskbarHasMenuFlyout()))
            g_panelEventDirty = true;
    }
    const HWND quick = g_quickSettingsDirty;
    g_quickSettingsDirty = nullptr;
    if (quick) {
        UpdateLegacyQuickSettingsHold(quick);
        if (PanelVisible(quick)) {
            StopQuickSettingsWatch();
            g_quickSettings = quick;
            PlaceQuickSettings(quick);
            g_quickSettingsChecksLeft = kQuickSettingsChecks;
            g_quickSettingsTimer = SetTimer(nullptr, 0, kQuickSettingsCheckMs,
                                            QuickSettingsTimerProc);
        } else if (quick == g_quickSettings) StopQuickSettingsWatch();
    }
    if (g_panelEventDirty) {
        g_panelEventDirty = false;
        if (g_panelCoordinatorStarted) {
            if (g_settings.traceToFile) ++g_panelCounters.coalesced;
            ReconcilePanels();
            CompletePanelActivation();
            SyncPanelTimer();
        }
    }
}

void CALLBACK FlyoutEventProc(HWINEVENTHOOK, DWORD event, HWND window,
                              LONG object, LONG child, DWORD, DWORD) {
    if (g_settings.traceToFile) ++g_panelCounters.events;
    if (g_unloading.load() || !window) return;
    const bool menuEvent = event == EVENT_SYSTEM_MENUPOPUPSTART ||
                           event == EVENT_SYSTEM_MENUPOPUPEND;
    if (!menuEvent && (object != OBJID_WINDOW || child != CHILDID_SELF)) return;
    if (GetAncestor(window, GA_ROOT) != window) return;
    wchar_t cls[96]{};
    if (!GetClassNameW(window, cls, ARRAYSIZE(cls))) return;
    const bool quick = !wcscmp(cls, kQuickSettingsClass);
    const bool xamlPopup = !wcscmp(cls, L"Xaml_WindowedPopupClass");
    const bool relevant = quick || !wcscmp(cls, L"Windows.UI.Core.CoreWindow") ||
        !wcscmp(cls, L"TopLevelWindowForOverflowXamlIsland") ||
        !wcscmp(cls, L"#32768") || xamlPopup ||
        window == g_panelSession.window;
    if (!relevant || (!g_panelCoordinatorStarted && !quick)) return;
    if (xamlPopup) {
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        if (pid != GetCurrentProcessId() ||
            (!PanelPopupOwnedBy(window, g_taskbar) &&
             !PanelPopupOwnedBy(window, g_panelSession.window))) return;
        g_xamlPopupDirty = true;
    }
    if (quick) g_quickSettingsDirty = window;
    if (g_panelCoordinatorStarted && !xamlPopup) g_panelEventDirty = true;
    if (!g_panelEventTimer)
        g_panelEventTimer = SetTimer(nullptr, 0, 20, PanelEventTimerProc);
}

// Must run on the taskbar UI thread; the events are delivered there.
void StartFlyoutPlacement() {
    if (g_flyoutShowHook) {
        return;
    }
    const DWORD flags = WINEVENT_OUTOFCONTEXT;
    g_flyoutShowHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE,
                                       nullptr, FlyoutEventProc, 0, 0, flags);
    // Legacy Quick Settings can close by cloaking without a HIDE event.
    g_flyoutUncloakHook = SetWinEventHook(EVENT_OBJECT_CLOAKED,
        EVENT_OBJECT_UNCLOAKED, nullptr, FlyoutEventProc, 0, 0, flags);
    if (g_panelCoordinatorStarted) {
        g_flyoutMenuHook = SetWinEventHook(EVENT_SYSTEM_MENUPOPUPSTART,
            EVENT_SYSTEM_MENUPOPUPEND, nullptr, FlyoutEventProc,
            GetCurrentProcessId(), 0, flags);
    }
    UpdateGroupedLayout(true);
    if (HWND flyout = FindWindowW(kQuickSettingsClass, nullptr);
        PanelVisible(flyout)) {
        FlyoutEventProc(nullptr, EVENT_OBJECT_SHOW, flyout, OBJID_WINDOW,
                        CHILDID_SELF, 0, GetCurrentThreadId());
    }
}

void StopFlyoutPlacement() {
    for (HWINEVENTHOOK* hook : {&g_flyoutShowHook, &g_flyoutUncloakHook, &g_flyoutMenuHook}) {
        if (*hook) {
            UnhookWinEvent(*hook);
            *hook = nullptr;
        }
    }
    if (g_panelEventTimer) KillTimer(nullptr, g_panelEventTimer);
    g_panelEventTimer = 0;
    g_panelEventDirty = false;
    g_xamlPopupDirty = false;
    g_quickSettingsDirty = nullptr;
    StopQuickSettingsWatch();
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
    const bool ready = GetPropW(taskbar, kPanelReady) != nullptr;
    if (ready && !g_panelCoordinatorStarted) StartPanelCoordinator();
    else if (!ready && g_panelCoordinatorStarted) StopPanelCoordinator();
    if (g_panelCoordinatorStarted) {
        if (!g_trayClicksHooked && !g_trayClickModuleSeen &&
            GetModuleHandleW(L"SystemTray.dll") && HookTrayClicks())
            Wh_ApplyHookOperations();
        try { AttachPanelInput(); } catch (...) {}
    }
    if (g_settings.traceToFile) {
        const auto now = GetTickCount64();
        if (!g_panelCounters.nextSummary) g_panelCounters.nextSummary = now + 60000;
        if (now >= g_panelCounters.nextSummary) {
            Trace(L"efficiency searches=%llu timers=%llu events=%llu coalesced=%llu mouse=%llu keyboard=%llu hooks=%d active=%d bridge=%d",
                g_panelCounters.searches, g_panelCounters.timers, g_panelCounters.events,
                g_panelCounters.coalesced, g_panelCounters.mouse, g_panelCounters.keyboard,
                !!g_panelMouseHook + !!g_panelKeyboardHook, !!g_panelTimer, ready);
            g_panelCounters = {};
            g_panelCounters.nextSummary = now + 60000;
        }
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
    ApplyNativeTaskbarAlignment(taskbar);
    if (!g_pollTimer) {
        g_pollTimer = SetTimer(nullptr, 0, kPollMs, PollTimerProc);
    }
    StartFlyoutPlacement();
    StartPanelCoordinator();
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
    StopPanelCoordinator();
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

namespace QsHostLoader {
decltype(&LoadLibraryExW) original{};
void* target{};
std::mutex lifecycle;
std::atomic<bool> stopping{false}, installing{false}, active{false};
void Activate() {
    if (installing.exchange(true)) return;
    {
        std::lock_guard guard(lifecycle);
        if (!stopping.load() && GetModuleHandleW(L"ControlCenter.dll") &&
                QsPanelEscape::InitializeQuickSettingsHost()) {
            Wh_ApplyHookOperations();
            QsPanelEscape::AfterInitQuickSettingsHost();
            active.store(true);
        }
    }
    installing.store(false);
}
HMODULE WINAPI LoadLibraryHook(LPCWSTR path, HANDLE file, DWORD flags) {
    HMODULE module = original(path, file, flags);
    if (!module || stopping.load() || active.load()) return module;
    if (GetModuleHandleW(L"ControlCenter.dll")) Activate();
    return module;
}
BOOL Initialize() {
    stopping.store(false);
    HMODULE base = GetModuleHandleW(L"kernelbase.dll");
    target = base ? reinterpret_cast<void*>(GetProcAddress(base, "LoadLibraryExW")) : nullptr;
    if (!target || !Wh_SetFunctionHook(target,
            reinterpret_cast<void*>(LoadLibraryHook), reinterpret_cast<void**>(&original))) return FALSE;
    // ShellHost may receive the mod before its Quick Settings module loads.
    // Keep the loader hook active, then register exact ControlCenter hooks.
    if (GetModuleHandleW(L"ControlCenter.dll")) {
        if (QsPanelEscape::InitializeQuickSettingsHost()) return TRUE;
        Wh_RemoveFunctionHook(target);
        target = nullptr;
        return FALSE;
    }
    Wh_Log(L"QS Escape: waiting for ControlCenter.dll");
    return TRUE;
}
void Uninitialize() {
    stopping.store(true);
    std::lock_guard guard(lifecycle);
    if (target) Wh_RemoveFunctionHook(target);
    target = nullptr;
    active.store(false);
    QsPanelEscape::UninitializeQuickSettingsHost();
}
}

BOOL Wh_ModInit() {
    if (QsPanelEscape::detail::IsShellHostProcess())
        return QsHostLoader::Initialize();
    LoadSettings();
    g_unloading.store(false, std::memory_order_release);
    // Windhawk may reuse the same mapped DLL during settings reload. Hooks
    // have been removed by the engine, so registration flags must be reset.
    g_trayClicksHooked = false;
    g_trayClickModuleSeen = false;
    g_alignmentApplied = false;
    g_panelCounters = {};

    Trace(L"v" WH_MOD_VERSION L" init");

    if (!HookTaskbarSymbols()) {
        return FALSE;
    }
    if (!HookTrayClicks()) Trace(L"panel native tray activation hooks unavailable at init");

    return TRUE;
}

void Wh_ModAfterInit() {
    if (QsPanelEscape::detail::IsShellHostProcess()) {
        QsHostLoader::Activate();
        return;
    }
    if (HWND taskbar = FindPrimaryTaskbar()) {
        RunFromWindowThread(GetTaskbarDispatchWindow(taskbar), StartOnUiThread,
                            taskbar);
    }
}

void Wh_ModBeforeUninit() {
    if (QsPanelEscape::detail::IsShellHostProcess()) {
        QsHostLoader::Uninitialize();
        return;
    }
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
