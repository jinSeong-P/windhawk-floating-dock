// ==WindhawkMod==
// @id              taskbar-autohide-motion
// @name            Taskbar Auto-Hide Motion
// @description     Smooth slide and pop for the auto-hidden Windows 11 taskbar, invisible while hidden (OLED friendly), revealed along the whole bottom edge
// @version         1.0.7
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
# Taskbar Auto-Hide Motion

Replaces the reveal and hide movement of the auto-hidden Windows 11 taskbar
with a timed Composition slide of the taskbar's own XAML content. Icons,
colors and hover effects are left alone, so it combines with
[Windows 11 Taskbar Styler](https://windhawk.net/mods/windows-11-taskbar-styler).

Requires **Automatically hide the taskbar** to be on (Settings > Personalization
> Taskbar > Taskbar behaviors).

- **Consistent motion.** Reveal and hide are one pair of curves (Material 3
  emphasized decelerate / accelerate) over the same distance, and a reveal
  that interrupts a hide continues from where the taskbar is.
- **Dock pop.** With the *Expressive scale* profile the dock and the system
  tray grow slightly as they land, following a SwiftUI-style spring.
- **Invisible while hidden.** Windows keeps a 1-2 px strip of the hidden
  taskbar on screen so the mouse can reach it, which is a burn-in risk on OLED
  panels. The content is fully transparent while hidden; the strip still
  reacts to the mouse.
- **Reveal along the whole edge.** Taskbar Styler's click-through option
  clips the taskbar window to the dock and the tray, so Windows only reveals
  the taskbar there. This mod reveals it anywhere along the bottom edge
  (except over full screen windows).
- **Delays.** The reveal and hide delays of auto-hide are configurable.
- Any failure falls back to Windows' own animation.

Part of [windhawk-floating-dock](https://github.com/jinSeong-P/windhawk-floating-dock),
together with the *Floating Dock Helpers* mod and Taskbar Styler presets for a
floating, macOS-style dock.

## Compatibility

- Tested on Windows 11 25H2 (build 26200) at 175% scaling. Other builds may
  work; symbols that can't be found disable the matching feature only.
- Bottom taskbar. The whole-edge reveal covers the primary monitor.

## Motion profiles

- **Smooth**: emphasized decelerate on reveal, emphasized accelerate on hide.
- **Expressive**: the reveal travels past its resting position by a few DIPs
  and settles back (the top edge is clipped for a moment unless there is room
  above the dock).
- **Expressive scale**: the slide lands exactly and the dock and the tray pop.

## Notes

- If Windows' animation effects are off, the taskbar snaps (configurable).
- *Diagnostic trace file* writes every decision to
  `%TEMP%\taskbar-autohide-motion.log` for bug reports.

Implementation patterns for TrayUI::SlideWindow interception, the auto-hide
timers and retrieving the existing taskbar XamlRoot are adapted from GPLv3
Windhawk taskbar mods in the official ramensoftware/windhawk-mods repository.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- motionProfile: expressiveScale
  $name: Motion profile
  $options:
    - smooth: Smooth
    - expressive: Expressive (overshoot and settle on reveal)
    - expressiveScale: Expressive scale (pop on arrival)
- revealDurationMs: 260
  $name: Reveal duration in ms
  $description: The Expressive profile takes 1.5x this for the overshoot and settle.
- hideDurationMs: 200
  $name: Hide duration in ms
- expressiveOvershoot: 4
  $name: Expressive overshoot (DIP)
  $description: How far past its resting position the taskbar travels before settling.
- scaleBouncePercent: 1
  $name: Expressive scale pop (%)
  $description: How much the dock and the tray grow as they arrive. 0 turns the pop off.
- scalePopDurationMs: 550
  $name: Expressive scale pop duration in ms
  $description: Perceptual duration of the pop spring, as in SwiftUI's spring(duration:bounce:).
- scalePopBounceX100: 15
  $name: Expressive scale pop bounce x100
  $description: Spring bounce, as in SwiftUI. 0 settles without undershoot, 15 barely bounces, 30 bounces noticeably.
- fadeHidden: true
  $name: Transparent while hidden
  $description: Hides the 1-2 px strip Windows leaves on screen (OLED burn-in protection).
- unhideDelayMs: 0
  $name: Reveal delay in ms
  $description: Time the mouse must rest on the screen edge before the taskbar appears. 0 keeps the Windows default.
- hideDelayMs: 300
  $name: Hide delay in ms
  $description: Time after the mouse leaves before the taskbar hides. 0 keeps the Windows default (about 500 ms).
- revealAlongWholeEdge: true
  $name: Reveal along the whole bottom edge
  $description: With Taskbar Styler's click-through option, reveals the taskbar anywhere along the bottom edge instead of only below the dock and the tray. Never over full screen windows.
- respectSystemAnimations: true
  $name: Respect Windows animation effects
  $description: If Windows animation effects are off, the taskbar snaps instead of animating.
- traceToFile: false
  $name: Diagnostic trace file
  $description: Writes every slide request and decision to %TEMP%\taskbar-autohide-motion.log.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <array>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

#undef GetCurrentTime

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

using namespace winrt::Windows::UI::Xaml;
namespace composition = winrt::Windows::UI::Composition;
namespace hosting = winrt::Windows::UI::Xaml::Hosting;
namespace numerics = winrt::Windows::Foundation::Numerics;

namespace {

// TrayUI auto-hide timer ids on Shell_TrayWnd / Shell_SecondaryTrayWnd.
constexpr UINT_PTR kTrayUITimerHide = 2;
constexpr UINT_PTR kTrayUITimerUnhide = 3;

constexpr wchar_t kPanelHoldProperty[] = L"FloatingDock.PanelHold.v1";
constexpr wchar_t kPanelEpochProperty[] = L"FloatingDock.PanelEpoch.v1";
constexpr wchar_t kPanelBridgeReadyProperty[] =
    L"FloatingDock.PanelBridgeReady.v1";
constexpr wchar_t kLegacyQuickSettingsHoldProperty[] =
    L"FloatingDock.QuickSettingsHoldOpen";
constexpr wchar_t kPanelSessionClosedMessageName[] =
    L"FloatingDock.PanelSessionClosed.v1";

// hideDelayMs=0 keeps Windows' default auto-hide delay, approximately 500ms.
constexpr UINT kDefaultNativeHideDelayMs = 500;

// Delay between committing the hidden HWND position and resetting the
// translation, so the two never land in the same compositor frame.
constexpr int kHideCleanupMs = 40;

struct CubicBezier {
    float x1, y1, x2, y2;
};

// Material 3 emphasized decelerate / accelerate.
constexpr CubicBezier kRevealCurve{0.05f, 0.7f, 0.1f, 1.0f};
constexpr CubicBezier kHideCurve{0.3f, 0.0f, 0.8f, 0.15f};

// Expressive reveal: decelerate past the resting position, then settle back
// with the standard curve.
constexpr float kOvershootAt = 0.55f;
constexpr CubicBezier kSettleCurve{0.3f, 0.0f, 0.2f, 1.0f};

enum class MotionProfile {
    Smooth,
    Expressive,
    ExpressiveScale,
};

// Expressive scale: the pop follows the impulse response of a spring defined
// like SwiftUI's spring(duration:bounce:), sampled into linear keyframes.
constexpr int kScalePopKeyframes = 24;

struct Settings {
    MotionProfile profile = MotionProfile::ExpressiveScale;
    int revealDurationMs = 260;
    int expressiveOvershoot = 4;
    int scaleBouncePercent = 1;
    int scalePopDurationMs = 550;
    int scalePopBounceX100 = 15;
    int hideDurationMs = 200;
    bool fadeHidden = true;
    int unhideDelayMs = 0;
    int hideDelayMs = 300;
    bool revealAlongWholeEdge = true;
    bool respectSystemAnimations = true;
    bool traceToFile = false;
};

Settings g_settings;
std::atomic<bool> g_unloading{false};

// Diagnostic trace to %TEMP%\taskbar-autohide-motion.log (explorer's temp
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
    wcscat_s(path, L"taskbar-autohide-motion.log");

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

bool MotionAllowed() {
    if (!g_settings.respectSystemAnimations) {
        return true;
    }

    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        return true;
    }

    return enabled != FALSE;
}

bool IsTaskbarWindow(HWND hWnd) {
    if (!hWnd) {
        return false;
    }

    WCHAR className[32]{};
    if (!GetClassNameW(hWnd, className, ARRAYSIZE(className))) {
        return false;
    }

    return _wcsicmp(className, L"Shell_TrayWnd") == 0 ||
           _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0;
}

HWND GetTaskbarDispatchWindow(HWND taskbarWnd) {
    HWND uiWindow = FindWindowExW(
        taskbarWnd,
        nullptr,
        L"Windows.UI.Composition.DesktopWindowContentBridge",
        nullptr);
    return uiWindow ? uiWindow : taskbarWnd;
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
// Adapted from the official taskbar-multirow/taskbar-separators pattern.
// This avoids installing a second XAML diagnostics client, keeping the mod
// compatible with Windows 11 Taskbar Styler.
// -----------------------------------------------------------------------------

void* CTaskBand_ITaskListWndSite_vftable = nullptr;
void* CSecondaryTaskBand_ITaskListWndSite_vftable = nullptr;

using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void* pThis, void** result);
CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original = nullptr;

using CSecondaryTaskBand_GetTaskbarHost_t =
    void*(WINAPI*)(void* pThis, void** result);
CSecondaryTaskBand_GetTaskbarHost_t CSecondaryTaskBand_GetTaskbarHost_Original =
    nullptr;

void* TaskbarHost_FrameHeight_Original = nullptr;

using std__Ref_count_base__Decref_t = void(WINAPI*)(void* pThis);
std__Ref_count_base__Decref_t std__Ref_count_base__Decref_Original = nullptr;

// Why the last XamlRoot lookup failed, for the diagnostic trace.
const wchar_t* g_targetFailure = L"";

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
#error "This local mod currently targets x86-64 only"
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
        g_targetFailure = L"primary symbols missing";
        return nullptr;
    }

    HWND taskSwitchWnd =
        reinterpret_cast<HWND>(GetPropW(taskbarWnd, L"TaskbandHWND"));
    if (!taskSwitchWnd) {
        g_targetFailure = L"no TaskbandHWND";
        return nullptr;
    }

    void* taskBand =
        reinterpret_cast<void*>(GetWindowLongPtrW(taskSwitchWnd, 0));
    if (!taskBand) {
        g_targetFailure = L"no task band";
        return nullptr;
    }

    void* site =
        FindTaskListWndSite(taskBand, CTaskBand_ITaskListWndSite_vftable);
    if (!site) {
        g_targetFailure = L"ITaskListWndSite vftable not found";
        return nullptr;
    }

    void* taskbarHostSharedPtr[2]{};
    CTaskBand_GetTaskbarHost_Original(site, taskbarHostSharedPtr);
    if (!taskbarHostSharedPtr[0]) {
        g_targetFailure = L"GetTaskbarHost returned null";
    }
    return XamlRootFromTaskbarHostSharedPtr(taskbarHostSharedPtr);
}

XamlRoot GetSecondaryTaskbarXamlRoot(HWND taskbarWnd) {
    if (!CSecondaryTaskBand_ITaskListWndSite_vftable ||
        !CSecondaryTaskBand_GetTaskbarHost_Original) {
        g_targetFailure = L"secondary symbols missing";
        return nullptr;
    }

    HWND taskSwitchWnd =
        FindWindowExW(taskbarWnd, nullptr, L"WorkerW", nullptr);
    if (!taskSwitchWnd) {
        g_targetFailure = L"no secondary WorkerW";
        return nullptr;
    }

    void* taskBand =
        reinterpret_cast<void*>(GetWindowLongPtrW(taskSwitchWnd, 0));
    if (!taskBand) {
        g_targetFailure = L"no secondary task band";
        return nullptr;
    }

    void* site = FindTaskListWndSite(
        taskBand, CSecondaryTaskBand_ITaskListWndSite_vftable);
    if (!site) {
        g_targetFailure = L"secondary ITaskListWndSite vftable not found";
        return nullptr;
    }

    void* taskbarHostSharedPtr[2]{};
    CSecondaryTaskBand_GetTaskbarHost_Original(site, taskbarHostSharedPtr);
    return XamlRootFromTaskbarHostSharedPtr(taskbarHostSharedPtr);
}

XamlRoot GetTaskbarXamlRootForWindow(HWND taskbarWnd) {
    WCHAR className[32]{};
    if (!GetClassNameW(taskbarWnd, className, ARRAYSIZE(className))) {
        return nullptr;
    }

    if (_wcsicmp(className, L"Shell_TrayWnd") == 0) {
        return GetPrimaryTaskbarXamlRoot(taskbarWnd);
    }

    if (_wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0) {
        return GetSecondaryTaskbarXamlRoot(taskbarWnd);
    }

    return nullptr;
}

struct MotionTarget {
    UIElement element{nullptr};
    double rasterizationScale = 1.0;

    explicit operator bool() const {
        return element != nullptr;
    }
};

// Must run on the taskbar XAML thread. Never throws.
MotionTarget GetTaskbarMotionTarget(HWND hWnd) {
    try {
        g_targetFailure = L"no XamlRoot from taskbar element";
        auto xamlRoot = GetTaskbarXamlRootForWindow(hWnd);
        if (!xamlRoot) {
            return {};
        }

        auto element = xamlRoot.Content().try_as<UIElement>();
        if (!element) {
            g_targetFailure = L"XamlRoot has no content";
            return {};
        }

        double scale = xamlRoot.RasterizationScale();
        if (!std::isfinite(scale) || scale <= 0.01) {
            scale = static_cast<double>(GetDpiForWindow(hWnd)) / 96.0;
        }
        if (!std::isfinite(scale) || scale <= 0.01) {
            scale = 1.0;
        }

        return MotionTarget{element, scale};
    } catch (winrt::hresult_error const& e) {
        Trace(L"XamlRoot lookup threw 0x%08X %s",
              static_cast<unsigned>(e.code()), e.message().c_str());
        g_targetFailure = L"exception";
    } catch (...) {
        Trace(L"XamlRoot lookup threw");
        g_targetFailure = L"exception";
    }

    return {};
}

// -----------------------------------------------------------------------------
// Animation primitives.
//
// Translation and Opacity of the taskbar XAML root are animated with
// UIElement::StartAnimation. Stopping such an animation leaves the property at
// its current animated value, so the final values are always set explicitly.
// -----------------------------------------------------------------------------

// The compositor of the taskbar XAML thread, taken from a detached element of
// our own so the taskbar tree is never switched to handout-visual mode.
composition::Compositor g_compositor{nullptr};

composition::Compositor GetCompositor() {
    if (!g_compositor) {
        try {
            g_compositor = hosting::ElementCompositionPreview::GetElementVisual(
                               winrt::Windows::UI::Xaml::Controls::Border())
                               .Compositor();
        } catch (winrt::hresult_error const& e) {
            Trace(L"getting the compositor threw 0x%08X %s",
                  static_cast<unsigned>(e.code()), e.message().c_str());
        } catch (...) {
            Trace(L"getting the compositor threw");
        }
    }
    return g_compositor;
}

winrt::Windows::Foundation::TimeSpan Milliseconds(int ms) {
    return std::chrono::milliseconds(std::max(ms, 1));
}

composition::CompositionEasingFunction MakeEasing(
    composition::Compositor const& compositor,
    CubicBezier const& curve) {
    return compositor.CreateCubicBezierEasingFunction(
        numerics::float2{curve.x1, curve.y1},
        numerics::float2{curve.x2, curve.y2});
}

// -----------------------------------------------------------------------------
// Rendered-value tracking.
//
// Neither the XAML property getters nor "this.StartingValue" report the
// rendered value of a UIElement composition animation (StartingValue reads 0
// for Opacity). To continue smoothly from an interrupted animation, each
// animation segment is recorded and evaluated on the CPU with the same curve.
// -----------------------------------------------------------------------------

float CubicBezierAt(CubicBezier const& c, float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }

    auto sample = [](float p1, float p2, float t) {
        const float u = 1.0f - t;
        return 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t;
    };

    float lo = 0.0f;
    float hi = 1.0f;
    float t = x;
    for (int i = 0; i < 24; i++) {
        t = (lo + hi) / 2.0f;
        if (sample(c.x1, c.x2, t) < x) {
            lo = t;
        } else {
            hi = t;
        }
    }
    return sample(c.y1, c.y2, t);
}

// A keyframe the motion passes through on its way to the end value (the
// Expressive overshoot). `curve` eases the approach to it, and the motion's
// own curve eases the rest.
struct Waypoint {
    float progress;
    numerics::float3 value;
    CubicBezier curve;
};

struct Motion {
    numerics::float3 from{};
    numerics::float3 to{};
    int durationMs = 0;
    CubicBezier curve{};
    std::optional<Waypoint> waypoint;

    numerics::float3 At(float progress) const {
        if (waypoint && progress < waypoint->progress) {
            return from + (waypoint->value - from) *
                              CubicBezierAt(waypoint->curve,
                                            progress / waypoint->progress);
        }
        const numerics::float3 start = waypoint ? waypoint->value : from;
        const float local =
            waypoint ? (progress - waypoint->progress) / (1.0f - waypoint->progress)
                     : progress;
        return start + (to - start) * CubicBezierAt(curve, local);
    }
};

struct Segment {
    Motion motion;
    ULONGLONG startTick = 0;

    numerics::float3 At(ULONGLONG now) const {
        if (motion.durationMs <= 0 || now >= startTick + motion.durationMs) {
            return motion.to;
        }
        return motion.At(static_cast<float>(now - startTick) /
                         motion.durationMs);
    }
};

// The translation segment currently rendered for each taskbar.
std::unordered_map<HWND, Segment> g_rendered;

numerics::float3 RenderedTranslation(HWND hWnd) {
    auto it = g_rendered.find(hWnd);
    return it == g_rendered.end() ? numerics::float3{}
                                  : it->second.At(GetTickCount64());
}

void RecordSegment(HWND hWnd, Motion const& motion) {
    g_rendered[hWnd] = Segment{motion, GetTickCount64()};
}

// Animates Translation along `motion`. Returns null on failure.
composition::CompositionAnimation AnimateTranslation(UIElement const& element,
                                                     Motion const& motion) {
    const numerics::float3& from = motion.from;
    try {
        auto compositor = GetCompositor();
        if (!compositor) {
            return nullptr;
        }

        auto animation = compositor.CreateVector3KeyFrameAnimation();
        animation.Target(L"Translation");
        animation.InsertKeyFrame(0.0f, from);
        if (motion.waypoint) {
            animation.InsertKeyFrame(motion.waypoint->progress,
                                     motion.waypoint->value,
                                     MakeEasing(compositor,
                                                motion.waypoint->curve));
        }
        animation.InsertKeyFrame(1.0f, motion.to,
                                 MakeEasing(compositor, motion.curve));
        animation.Duration(Milliseconds(motion.durationMs));

        // An idle XAML tree doesn't render a frame, so an animation started
        // while nothing else changes (like at the start of a hide) would never
        // be committed. Nudging the base Translation, which the animation
        // overrides anyway, forces that frame. (Opacity can't be used: any
        // value below 1 makes the taskbar root invisible.)
        element.Translation({from.x, from.y + 0.01f, from.z});

        element.StartAnimation(animation);
        return animation;
    } catch (winrt::hresult_error const& e) {
        Trace(L"Translation animation threw 0x%08X %s",
              static_cast<unsigned>(e.code()), e.message().c_str());
    } catch (...) {
        Trace(L"Translation animation threw");
    }

    return nullptr;
}

// Switches the content opacity. Only called while the content is off screen:
// Opacity animations on the taskbar XAML root jump to their final value.
void SetContentOpacity(UIElement const& element, bool visible) {
    if (!element) {
        return;
    }

    try {
        element.Opacity((visible || !g_settings.fadeHidden) ? 1.0 : 0.0);
    } catch (...) {
        Trace(L"setting opacity threw");
    }
}

void StopAnimation(UIElement const& element,
                   composition::CompositionAnimation const& animation) {
    if (!element || !animation) {
        return;
    }

    try {
        element.StopAnimation(animation);
    } catch (...) {
    }
}

// -----------------------------------------------------------------------------
// Expressive scale pop.
//
// Scaling the whole root would push the system tray at the far edge off
// screen, so each top-level part (the dock and the system tray) grows around
// its own center.
// -----------------------------------------------------------------------------

struct ScaleAnimation {
    winrt::weak_ref<UIElement> element;
    composition::CompositionAnimation animation{nullptr};
};

std::unordered_map<HWND, std::vector<ScaleAnimation>> g_scaleAnimations;

// The system tray's visual is driven by Windows (handout visual), which rules
// out the UIElement Scale; it pops through a RenderTransform instead.
using winrt::Windows::UI::Xaml::Media::CompositeTransform;
namespace animation = winrt::Windows::UI::Xaml::Media::Animation;

std::unordered_map<HWND, animation::Storyboard> g_trayPops;

// The Floating Dock Helpers mod moves the tray with the same transform
// (TranslateX), so it is shared: each mod only touches its own properties and
// leaves the transform attached when it unloads.
CompositeTransform PartTransform(FrameworkElement const& part) {
    if (auto current = part.RenderTransform().try_as<CompositeTransform>()) {
        return current;
    }
    CompositeTransform transform;
    part.RenderTransform(transform);
    return transform;
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

void StopTrayPop(HWND hWnd) {
    auto it = g_trayPops.find(hWnd);
    if (it == g_trayPops.end()) {
        return;
    }
    try {
        it->second.Stop();
    } catch (...) {
    }
    g_trayPops.erase(it);
}

void StopScalePop(HWND hWnd) {
    StopTrayPop(hWnd);

    auto it = g_scaleAnimations.find(hWnd);
    if (it == g_scaleAnimations.end()) {
        return;
    }

    for (auto& scale : it->second) {
        try {
            if (auto element = scale.element.get()) {
                StopAnimation(element, scale.animation);
                element.Scale({1.0f, 1.0f, 1.0f});
            }
        } catch (...) {
        }
    }
    g_scaleAnimations.erase(it);
}

// The visible shape of a top-level taskbar part: one level down (the dock's
// RootGrid, the tray's SystemTrayFrameGrid). The top-level frames span the
// full taskbar height and don't react to Scale.
FrameworkElement VisiblePart(FrameworkElement const& part) {
    using winrt::Windows::UI::Xaml::Media::VisualTreeHelper;
    if (VisualTreeHelper::GetChildrenCount(part) > 0) {
        if (auto inner =
                VisualTreeHelper::GetChild(part, 0).try_as<FrameworkElement>()) {
            return inner;
        }
    }
    return part;
}

// Impulse response of a spring with SwiftUI's duration/bounce
// parameterization: stiffness = (2π/duration)², damping ratio = 1 - bounce.
double SpringImpulse(double t, double durationSeconds, double bounce) {
    const double omega = 2.0 * 3.14159265358979323846 / durationSeconds;
    const double zeta = 1.0 - bounce;
    if (zeta >= 0.999) {
        return t * std::exp(-omega * t);
    }
    const double omegaD = omega * std::sqrt(1.0 - zeta * zeta);
    return std::exp(-zeta * omega * t) * std::sin(omegaD * t) / omegaD;
}

// Starts the pop and returns its duration in ms (0 if nothing started).
int StartScalePop(HWND hWnd, UIElement const& root) {
    using winrt::Windows::UI::Xaml::Media::VisualTreeHelper;

    StopScalePop(hWnd);

    const float amplitude = g_settings.scaleBouncePercent / 100.0f;
    auto compositor = GetCompositor();
    if (!compositor || amplitude <= 0.0f) {
        return 0;
    }

    int count = 0;
    try {
        count = VisualTreeHelper::GetChildrenCount(root);
    } catch (...) {
        return 0;
    }

    // Normalize the impulse response to its peak, and end the pop once it
    // stays within 2% of the peak.
    const double springDuration = g_settings.scalePopDurationMs / 1000.0;
    const double bounce = g_settings.scalePopBounceX100 / 100.0;
    double peakValue = 0.0;
    int durationMs = 0;
    for (int ms = 0; ms <= 2000; ms += 2) {
        peakValue = std::max(peakValue, SpringImpulse(ms / 1000.0,
                                                      springDuration, bounce));
    }
    for (int ms = 0; ms <= 2000; ms += 2) {
        if (std::abs(SpringImpulse(ms / 1000.0, springDuration, bounce)) >
            peakValue * 0.02) {
            durationMs = ms;
        }
    }
    durationMs = std::clamp(durationMs + 2, 100, 2000);
    if (peakValue <= 0.0) {
        return 0;
    }

    // The same curve for both paths.
    std::vector<float> scales;
    for (int k = 1; k < kScalePopKeyframes; k++) {
        const float progress = static_cast<float>(k) / kScalePopKeyframes;
        scales.push_back(1.0f + amplitude * static_cast<float>(
                                                SpringImpulse(
                                                    progress * durationMs / 1000.0,
                                                    springDuration, bounce) /
                                                peakValue));
    }

    auto& started = g_scaleAnimations[hWnd];
    bool trayStarted = false;
    for (int i = 0; i < count; i++) {
        try {
            auto top = VisualTreeHelper::GetChild(root, i)
                           .try_as<FrameworkElement>();
            if (!top) {
                continue;
            }

            auto part = VisiblePart(top);
            if (part.ActualWidth() <= 0 || part.ActualHeight() <= 0) {
                continue;
            }

            if (std::wstring_view(winrt::get_class_name(top))
                    .find(L"SystemTray") != std::wstring_view::npos) {
                // Scale the tray's frame around the center of its visible
                // pill (inside the transparent border).
                const Thickness border = PanelBorder(part);
                const auto offset = part.ActualOffset();
                auto transform = PartTransform(top);
                transform.CenterX(offset.x + border.Left +
                                  (part.ActualWidth() - border.Left -
                                   border.Right) / 2);
                transform.CenterY(offset.y + border.Top +
                                  (part.ActualHeight() - border.Top -
                                   border.Bottom) / 2);

                animation::Storyboard storyboard;
                for (const wchar_t* property : {L"ScaleX", L"ScaleY"}) {
                    animation::DoubleAnimationUsingKeyFrames keyframes;
                    auto add = [&](double value, int ms) {
                        animation::LinearDoubleKeyFrame frame;
                        frame.Value(value);
                        frame.KeyTime(animation::KeyTimeHelper::FromTimeSpan(
                            Milliseconds(ms)));
                        keyframes.KeyFrames().Append(frame);
                    };
                    add(1.0, 0);
                    for (size_t k = 0; k < scales.size(); k++) {
                        add(scales[k], static_cast<int>(durationMs * (k + 1) /
                                                        kScalePopKeyframes));
                    }
                    add(1.0, durationMs);
                    animation::Storyboard::SetTarget(keyframes, transform);
                    animation::Storyboard::SetTargetProperty(keyframes,
                                                             property);
                    storyboard.Children().Append(keyframes);
                }
                storyboard.Begin();
                g_trayPops[hWnd] = storyboard;
                trayStarted = true;

                Trace(L"scale pop on the tray (RenderTransform) %.0fx%.0f, "
                      L"%dms",
                      part.ActualWidth(), part.ActualHeight(), durationMs);
                continue;
            }

            const numerics::float3 center{
                static_cast<float>(part.ActualWidth() / 2),
                static_cast<float>(part.ActualHeight() / 2), 0.0f};

            auto animation = compositor.CreateVector3KeyFrameAnimation();
            auto linear = compositor.CreateLinearEasingFunction();
            animation.InsertKeyFrame(0.0f, {1.0f, 1.0f, 1.0f});
            for (size_t k = 0; k < scales.size(); k++) {
                const float progress =
                    static_cast<float>(k + 1) / kScalePopKeyframes;
                animation.InsertKeyFrame(
                    progress, {scales[k], scales[k], 1.0f}, linear);
            }
            animation.InsertKeyFrame(1.0f, {1.0f, 1.0f, 1.0f}, linear);
            animation.Duration(Milliseconds(durationMs));

            part.CenterPoint(center);
            animation.Target(L"Scale");
            part.StartAnimation(animation);
            started.push_back({winrt::make_weak<UIElement>(part), animation});

            Trace(L"scale pop on %s#%s %.0fx%.0f, %dms",
                  winrt::get_class_name(part).c_str(), part.Name().c_str(),
                  part.ActualWidth(), part.ActualHeight(), durationMs);
        } catch (winrt::hresult_error const& e) {
            Trace(L"scale pop skipped a part: 0x%08X %s",
                  static_cast<unsigned>(e.code()), e.message().c_str());
        } catch (...) {
            Trace(L"scale pop skipped a part");
        }
    }

    return started.empty() && !trayStarted ? 0 : durationMs;
}

// Places the content at its resting state: untranslated, and opaque unless
// hidden with fading enabled.
void SetRestingState(HWND hWnd, UIElement const& element, bool visible) {
    g_rendered.erase(hWnd);
    StopScalePop(hWnd);

    if (!element) {
        return;
    }

    try {
        element.Translation({0.0f, 0.0f, 0.0f});
        SetContentOpacity(element, visible);
    } catch (winrt::hresult_error const& e) {
        Trace(L"setting resting state threw 0x%08X %s",
              static_cast<unsigned>(e.code()), e.message().c_str());
    } catch (...) {
        Trace(L"setting resting state threw");
    }
}

// -----------------------------------------------------------------------------
// TrayUI::SlideWindow handoff.
//
// Reveal: the content is transparent while hidden. Start the slide from the
// hidden offset and the fade-in, then move the HWND to its final rect without
// the native slide.
//
// Hide: slide the content to the hidden offset while fading out, keeping the
// HWND in place. Once done (content transparent), commit the native hidden
// rect, and a moment later reset the translation.
// -----------------------------------------------------------------------------

using TrayUI_SlideWindow_t = void(WINAPI*)(void* pThis,
                                           HWND hWnd,
                                           const RECT* rect,
                                           HMONITOR monitor,
                                           bool show,
                                           bool animate);
TrayUI_SlideWindow_t TrayUI_SlideWindow_Original = nullptr;

enum class PendingKind {
    RevealSettle,
    HideCommit,
    HideCleanup,
};

struct PendingAnimation {
    PendingKind kind;
    UINT_PTR timerId = 0;
    HWND hWnd = nullptr;
    void* trayUi = nullptr;
    RECT endRect{};
    HMONITOR monitor = nullptr;
    winrt::weak_ref<UIElement> element;
    composition::CompositionAnimation translation{nullptr};
};

std::unordered_map<HWND, PendingAnimation> g_pendingAnimations;

// Set while Hide pumps messages inside SlideWindow. This is thread-local
// because taskbar windows can have separate UI threads.
struct SyncHideState {
    bool active = false;
    bool revealRequested = false;
    HWND hWnd = nullptr;
    void* trayUi = nullptr;
    RECT revealRect{};
    HMONITOR revealMonitor = nullptr;
};
thread_local SyncHideState g_syncHide;

struct SyncHideGuard {
    explicit SyncHideGuard(HWND hWnd) {
        g_syncHide = {};
        g_syncHide.active = true;
        g_syncHide.hWnd = hWnd;
    }

    SyncHideGuard(SyncHideGuard const&) = delete;
    SyncHideGuard& operator=(SyncHideGuard const&) = delete;

    ~SyncHideGuard() {
        Release();
    }

    SyncHideState Release() {
        SyncHideState state = g_syncHide;
        g_syncHide = {};
        return state;
    }
};

enum class SyncHideWaitResult {
    Complete,
    RevealRequested,
    QuitRequested,
    Unloading,
    WaitFailed,
};

// Keeps the taskbar UI thread responsive while the composition animation runs.
// WM_QUIT is reposted before returning so the enclosing message loop still sees
// it; the caller then commits the native hide handoff immediately.
SyncHideWaitResult WaitForHideAnimation(ULONGLONG start,
                                        int durationMs,
                                        bool& rendered) {
    const ULONGLONG renderDeadline = start + 60;
    const ULONGLONG deadline = start + durationMs;
    MSG msg{};

    for (;;) {
        if (g_unloading.load(std::memory_order_acquire)) {
            return SyncHideWaitResult::Unloading;
        }
        if (g_syncHide.revealRequested) {
            return SyncHideWaitResult::RevealRequested;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            return SyncHideWaitResult::Complete;
        }

        ULONGLONG waitMs = deadline - now;
        if (!rendered && now < renderDeadline) {
            waitMs = std::min(waitMs, renderDeadline - now);
        }

        const DWORD waitResult = MsgWaitForMultipleObjectsEx(
            0, nullptr, static_cast<DWORD>(waitMs), QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_FAILED) {
            Trace(L"hide message wait failed: %lu", GetLastError());
            return SyncHideWaitResult::WaitFailed;
        }

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(msg.wParam));
                return SyncHideWaitResult::QuitRequested;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);

            if (g_unloading.load(std::memory_order_acquire)) {
                return SyncHideWaitResult::Unloading;
            }
            if (g_syncHide.revealRequested) {
                return SyncHideWaitResult::RevealRequested;
            }
        }
    }
}

std::optional<PendingKind> PendingKindFor(HWND hWnd) {
    auto it = g_pendingAnimations.find(hWnd);
    if (it == g_pendingAnimations.end()) {
        return std::nullopt;
    }
    return it->second.kind;
}

void StopPendingAnimations(PendingAnimation const& pending) {
    if (auto element = pending.element.get()) {
        StopAnimation(element, pending.translation);
    }
}

// Removes the pending entry. With stopAnimations the running animations are
// detached (leaving their current values); otherwise they keep running so
// new ones can continue from the rendered values.
void CancelPendingAnimation(HWND hWnd, bool stopAnimations) {
    auto it = g_pendingAnimations.find(hWnd);
    if (it == g_pendingAnimations.end()) {
        return;
    }

    PendingAnimation pending = std::move(it->second);
    g_pendingAnimations.erase(it);

    if (pending.timerId) {
        KillTimer(nullptr, pending.timerId);
    }

    if (stopAnimations) {
        StopPendingAnimations(pending);
    }
}

void CALLBACK AnimationTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD);

bool SchedulePending(PendingAnimation pending, int delayMs) {
    UINT_PTR timerId =
        SetTimer(nullptr, 0, static_cast<UINT>(delayMs), AnimationTimerProc);
    if (!timerId) {
        return false;
    }

    pending.timerId = timerId;
    g_pendingAnimations[pending.hWnd] = std::move(pending);
    return true;
}

void CALLBACK AnimationTimerProc(HWND, UINT, UINT_PTR idEvent, DWORD) {
    KillTimer(nullptr, idEvent);

    PendingAnimation pending{};
    bool found = false;

    for (auto it = g_pendingAnimations.begin();
         it != g_pendingAnimations.end(); ++it) {
        if (it->second.timerId == idEvent) {
            pending = std::move(it->second);
            g_pendingAnimations.erase(it);
            found = true;
            break;
        }
    }

    if (!found || g_unloading.load(std::memory_order_acquire)) {
        return;
    }

    auto element = pending.element.get();

    switch (pending.kind) {
        case PendingKind::RevealSettle:
            StopPendingAnimations(pending);
            SetRestingState(pending.hWnd, element, true);
            Trace(L"reveal done");
            break;

        case PendingKind::HideCommit: {
            // The content has slid to the hidden position, where only the
            // thin strip is on screen. Make it transparent and move the HWND
            // to Windows' hidden rect.
            SetContentOpacity(element, false);

            TrayUI_SlideWindow_Original(pending.trayUi,
                                        pending.hWnd,
                                        &pending.endRect,
                                        pending.monitor,
                                        false,
                                        false);

            // Reset the translation a few frames later, so the HWND move and
            // the reset can't be composed into the same frame.
            PendingAnimation cleanup = pending;
            cleanup.kind = PendingKind::HideCleanup;
            cleanup.timerId = 0;
            if (!SchedulePending(std::move(cleanup), kHideCleanupMs)) {
                StopPendingAnimations(pending);
                SetRestingState(pending.hWnd, element, false);
            }

            Trace(L"hide committed");
            break;
        }

        case PendingKind::HideCleanup:
            StopPendingAnimations(pending);
            SetRestingState(pending.hWnd, element, false);
            Trace(L"hide done");
            break;
    }
}

void SlideNative(void* pThis,
                 HWND hWnd,
                 const RECT* rect,
                 HMONITOR monitor,
                 bool show,
                 bool animate,
                 UIElement const& element) {
    CancelPendingAnimation(hWnd, true);
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
    SetRestingState(hWnd, element, show);
}

void Reveal(void* pThis,
            HWND hWnd,
            const RECT* rect,
            HMONITOR monitor,
            RECT const& currentRect,
            MotionTarget const& target) {
    UIElement const& element = target.element;
    const auto pendingKind = PendingKindFor(hWnd);

    // A reveal is already running towards this rect. Windows calls
    // SlideWindow again in some cases, restarting would make the taskbar jump.
    if (pendingKind == PendingKind::RevealSettle &&
        EqualRect(&currentRect, rect)) {
        TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, true, false);
        return;
    }

    // Reversing an in-flight hide: the HWND is still at its visible rect,
    // continue from the currently rendered values.
    const bool fromRendered = pendingKind == PendingKind::RevealSettle ||
                              pendingKind == PendingKind::HideCommit;

    numerics::float3 from = RenderedTranslation(hWnd);
    if (!fromRendered) {
        from = numerics::float3{
            static_cast<float>((currentRect.left - rect->left) /
                               target.rasterizationScale),
            static_cast<float>((currentRect.top - rect->top) /
                               target.rasterizationScale),
            0.0f,
        };
    }

    CancelPendingAnimation(hWnd, true);

    Motion motion{from, {0.0f, 0.0f, 0.0f}, g_settings.revealDurationMs,
                  kRevealCurve};

    const float travel = numerics::length(from);
    if (g_settings.profile == MotionProfile::Expressive &&
        g_settings.expressiveOvershoot > 0 && travel >= 1.0f) {
        // Travel past the resting position by the overshoot, then settle.
        const numerics::float3 direction = -from / travel;
        motion.durationMs = g_settings.revealDurationMs * 3 / 2;
        motion.curve = kSettleCurve;
        motion.waypoint = Waypoint{
            kOvershootAt,
            direction * static_cast<float>(g_settings.expressiveOvershoot),
            kRevealCurve,
        };
    }

    auto translation = AnimateTranslation(element, motion);
    if (!translation) {
        SlideNative(pThis, hWnd, rect, monitor, true, true, element);
        return;
    }
    RecordSegment(hWnd, motion);
    int durationMs = motion.durationMs;

    // Expressive scale: the slide lands exactly, and the dock pops as it
    // arrives, carrying on after the slide has ended.
    if (g_settings.profile == MotionProfile::ExpressiveScale) {
        durationMs = std::max(durationMs, StartScalePop(hWnd, element));
    }

    // The opacity change and the translation are committed in the same XAML
    // frame, so the content either stays transparent or starts at the hidden
    // offset, whichever frame the HWND move below is composed in.
    SetContentOpacity(element, true);

    // Commit Explorer's visible state and HWND rect without the native slide.
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, true, false);

    PendingAnimation pending{
        PendingKind::RevealSettle,
        0,
        hWnd,
        pThis,
        *rect,
        monitor,
        winrt::make_weak(element),
        translation,
    };

    if (!SchedulePending(std::move(pending), durationMs + 32)) {
        StopAnimation(element, translation);
        SetRestingState(hWnd, element, true);
    }

    Trace(L"reveal %dms from=(%.1f,%.1f)%s", durationMs, from.x, from.y,
          fromRendered ? L" (reversed)" : L"");
}

void Hide(void* pThis,
          HWND hWnd,
          const RECT* rect,
          HMONITOR monitor,
          RECT const& currentRect,
          MotionTarget const& target) {
    UIElement const& element = target.element;
    const auto pendingKind = PendingKindFor(hWnd);

    // Already hidden (HWND at the hidden rect), or the HWND already moved to
    // the hidden position: let Windows place it directly.
    if (EqualRect(&currentRect, rect) ||
        pendingKind == PendingKind::HideCleanup) {
        SlideNative(pThis, hWnd, rect, monitor, false, false, element);
        return;
    }

    // Continue from the rendered values (reversing a reveal, or a repeated
    // hide call).
    const numerics::float3 from = RenderedTranslation(hWnd);
    CancelPendingAnimation(hWnd, true);
    StopScalePop(hWnd);

    const numerics::float3 hiddenOffset{
        static_cast<float>((rect->left - currentRect.left) /
                           target.rasterizationScale),
        static_cast<float>((rect->top - currentRect.top) /
                           target.rasterizationScale),
        0.0f,
    };

    const int durationMs = g_settings.hideDurationMs;

    const Motion motion{from, hiddenOffset, durationMs, kHideCurve};
    auto translation = AnimateTranslation(element, motion);
    if (!translation) {
        SlideNative(pThis, hWnd, rect, monitor, false, true, element);
        return;
    }
    RecordSegment(hWnd, motion);

    // Like the native slide, finish the hide before returning: Explorer treats
    // the taskbar as hidden once SlideWindow returns, and the content vanished
    // mid-animation when this returned early. The animation itself runs in
    // the compositor, independent of this thread; keep pumping messages until
    // the full duration has elapsed so XAML and Explorer stay responsive.
    const ULONGLONG start = GetTickCount64();
    SyncHideGuard syncHide(hWnd);
    SyncHideWaitResult waitResult = SyncHideWaitResult::Complete;
    {
        bool rendered = false;
        winrt::event_token token{};
        try {
            token = winrt::Windows::UI::Xaml::Media::CompositionTarget::Rendering(
                [&rendered](auto const&, auto const&) { rendered = true; });
        } catch (...) {
            rendered = true;
        }

        waitResult = WaitForHideAnimation(start, durationMs, rendered);

        if (token) {
            try {
                winrt::Windows::UI::Xaml::Media::CompositionTarget::Rendering(
                    token);
            } catch (...) {
            }
        }

        if (waitResult == SyncHideWaitResult::RevealRequested &&
            !g_unloading.load(std::memory_order_acquire)) {
            SyncHideState deferredReveal = syncHide.Release();
            // The pointer came back while the hide was being committed. The
            // HWND never moved; reveal from the rendered translation.
            Trace(L"hide reversed during commit");
            g_pendingAnimations[hWnd] = PendingAnimation{
                PendingKind::HideCommit,
                0,
                hWnd,
                pThis,
                *rect,
                monitor,
                winrt::make_weak<UIElement>(element),
                translation,
            };
            RECT revealCurrentRect{};
            if (!GetWindowRect(hWnd, &revealCurrentRect)) {
                revealCurrentRect = currentRect;
            }
            const RECT& revealRect = deferredReveal.revealRequested
                                         ? deferredReveal.revealRect
                                         : currentRect;
            Reveal(deferredReveal.trayUi ? deferredReveal.trayUi : pThis,
                   hWnd, &revealRect,
                   deferredReveal.revealRequested
                       ? deferredReveal.revealMonitor
                       : monitor,
                   revealCurrentRect, target);
            return;
        }
    }

    if (waitResult != SyncHideWaitResult::Complete) {
        Trace(L"hide wait interrupted (%d); committing the native handoff",
              static_cast<int>(waitResult));
    }

    // The content is at the hidden offset, where only the thin strip is on
    // screen. Hide it and reset the translation in the same XAML frame, then
    // move the HWND to Windows' hidden rect.
    StopAnimation(element, translation);
    SetRestingState(hWnd, element, false);
    TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, false, false);

    // A reveal received while the native handoff ran is started from the new
    // hidden HWND position. Earlier requests were handled before committing.
    SyncHideState deferredReveal = syncHide.Release();
    if (deferredReveal.revealRequested &&
        !g_unloading.load(std::memory_order_acquire)) {
        RECT revealCurrentRect{};
        if (!GetWindowRect(hWnd, &revealCurrentRect)) {
            revealCurrentRect = *rect;
        }
        Reveal(deferredReveal.trayUi ? deferredReveal.trayUi : pThis,
               hWnd, &deferredReveal.revealRect,
               deferredReveal.revealMonitor ? deferredReveal.revealMonitor
                                            : monitor,
               revealCurrentRect, target);
        return;
    }

    Trace(L"hide %dms from=(%.1f,%.1f), committed after %dms", durationMs,
          from.x, from.y, static_cast<int>(GetTickCount64() - start));
}

// -----------------------------------------------------------------------------
// Whole-edge reveal.
// With Taskbar Styler's click-through option the taskbar window region only
// covers the dock and the system tray, so the hidden strip beside them passes
// the mouse to the window below and Windows never notices it. While the taskbar
// is hidden, poll the cursor and start Windows' own unhide path when it reaches
// the bottom edge outside the region.
// -----------------------------------------------------------------------------

constexpr UINT kEdgePollMs = 40;

// The arguments Windows passes when the mouse reveals the taskbar
// (TrayUnhideFlags 0, UnhideRequest 15), as traced on 26200.
constexpr int kUnhideFlagsNone = 0;
constexpr int kUnhideRequestMouse = 15;

using TrayUI_Unhide_t = void(WINAPI*)(void* pThis, int flags, int request);
TrayUI_Unhide_t TrayUI_Unhide_Original = nullptr;

using TrayUI_WndProc_t = LRESULT(WINAPI*)(void* pThis,
                                          HWND hWnd,
                                          UINT message,
                                          WPARAM wParam,
                                          LPARAM lParam,
                                          bool* handled);
TrayUI_WndProc_t TrayUI_WndProc_Original = nullptr;

using TrayUI__Hide_t = void(WINAPI*)(void* pThis);
TrayUI__Hide_t TrayUI__Hide_Original = nullptr;

// The primary taskbar's TrayUI interface that Unhide expects. WndProc and
// SlideWindow receive the same object through neighboring interfaces (on 26200
// WndProc's is 8 bytes before Unhide's), so the right one is found by looking
// for Unhide itself in the vtables around the pointer they receive. Only
// touched on the taskbar UI thread.
std::atomic<void*> g_trayUi{nullptr};
std::atomic<HWND> g_primaryTaskbar{nullptr};
std::atomic<DWORD> g_primaryTrayUiThreadId{0};
std::atomic<UINT> g_panelSessionClosedMessage{0};
void* g_primaryWndProcThis = nullptr;
void* g_failedTrayUiLookupThis = nullptr;
void* g_lastPrimaryHideThis = nullptr;
void* g_lastPrimaryHideTrayUi = nullptr;
UINT_PTR g_lastPrimaryHideEpoch = 0;
UINT_PTR g_primaryTrayUiRevision = 0;
UINT_PTR g_lastCompletedPanelEpoch = 0;

struct PendingPanelResume {
    HWND taskbar = nullptr;
    UINT_PTR timerId = 0;
    UINT_PTR epoch = 0;
    UINT_PTR trayUiRevision = 0;
    void* trayUi = nullptr;
    void* hideThis = nullptr;
};

// Accessed only by the primary taskbar UI thread. The close message and the
// window timer both arrive on that thread.
PendingPanelResume g_pendingPanelResume;
HWND g_edgeTaskbar = nullptr;
UINT_PTR g_edgePollTimer = 0;
// Set once the unhide was requested for the current visit to the edge.
bool g_edgeRequested = false;
// Geometry only: keep testing the current cursor, taskbar and fullscreen
// window each tick. A stationary cursor never suppresses reveal checks.
bool g_edgeGeometryValid = false;
POINT g_edgeGeometryCursor{};
RECT g_edgeGeometryTaskbar{};
HMONITOR g_edgeGeometryMonitor{};
MONITORINFO g_edgeGeometry{sizeof(MONITORINFO)};

// Executable sections of taskbar.dll, where every vtable entry points.
std::vector<std::pair<uintptr_t, uintptr_t>> g_taskbarCode;
uintptr_t g_taskbarImageBegin = 0;
uintptr_t g_taskbarImageEnd = 0;

void RecordTaskbarImage(HMODULE module) {
    auto base = reinterpret_cast<const BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    g_taskbarImageBegin = reinterpret_cast<uintptr_t>(base);
    g_taskbarImageEnd = g_taskbarImageBegin + nt->OptionalHeader.SizeOfImage;

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            uintptr_t begin = g_taskbarImageBegin + section->VirtualAddress;
            g_taskbarCode.emplace_back(begin,
                                       begin + section->Misc.VirtualSize);
        }
    }
}

bool IsTaskbarCode(void* address) {
    auto value = reinterpret_cast<uintptr_t>(address);
    for (auto const& [begin, end] : g_taskbarCode) {
        if (value >= begin && value < end) {
            return true;
        }
    }
    return false;
}

// The slot of `function` in the vtable of the interface at `object`, or -1.
// taskbar.dll has no RTTI, so the vtables of one class follow each other
// without a separator and a scan can run on into the next one; the scan stops
// at the first entry that isn't taskbar.dll code.
int VtableSlotOf(const BYTE* object, void* function) {
    auto vtable = *reinterpret_cast<void* const* const*>(object);
    auto vtableAddress = reinterpret_cast<uintptr_t>(vtable);
    if (vtableAddress < g_taskbarImageBegin ||
        vtableAddress >= g_taskbarImageEnd ||
        vtableAddress % sizeof(void*) != 0) {
        return -1;
    }

    const size_t available =
        (g_taskbarImageEnd - vtableAddress) / sizeof(void*);
    for (size_t i = 0; i < std::min<size_t>(available, 1024); i++) {
        if (vtable[i] == function) {
            return static_cast<int>(i);
        }
        if (!IsTaskbarCode(vtable[i])) {
            return -1;
        }
    }
    return -1;
}

void* FindTrayUiInterface(void* received,
                          int* bestOffsetOut = nullptr,
                          int* bestSlotOut = nullptr) {
    if (!received || !TrayUI_Unhide_Original || g_taskbarCode.empty()) {
        return nullptr;
    }

    // The interfaces of one object sit next to each other around the one
    // received. An earlier interface's scan can run into the vtable that
    // declares Unhide, but only that vtable has it at its lowest slot.
    auto bytes = static_cast<const BYTE*>(received);
    int bestOffset = 0;
    int bestSlot = -1;
    for (int offset = -0x20; offset <= 0x40; offset += sizeof(void*)) {
        int slot = VtableSlotOf(
            bytes + offset, reinterpret_cast<void*>(TrayUI_Unhide_Original));
        if (slot >= 0 && (bestSlot < 0 || slot < bestSlot)) {
            bestSlot = slot;
            bestOffset = offset;
        }
    }

    if (bestSlot < 0) {
        return nullptr;
    }

    if (bestOffsetOut) {
        *bestOffsetOut = bestOffset;
    }
    if (bestSlotOut) {
        *bestSlotOut = bestSlot;
    }
    return const_cast<BYTE*>(bytes + bestOffset);
}

bool IsPrimaryTaskbarWindow(HWND hWnd) {
    if (!hWnd) {
        return false;
    }

    HWND primaryTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (primaryTaskbar && primaryTaskbar == hWnd) {
        return true;
    }

    return g_primaryTaskbar.load(std::memory_order_acquire) == hWnd;
}

bool IsTrayUiPointerFor(void* candidate, void* trayUi) {
    if (!candidate || !trayUi) {
        return false;
    }

    if (FindTrayUiInterface(candidate) == trayUi) {
        return true;
    }

    // Some private TrayUI methods use a neighboring interface subobject whose
    // vtable does not contain Unhide. Their `this` pointers still sit within
    // the adjacent-interface block that RecordTrayUi already validated.
    const uintptr_t candidateAddress = reinterpret_cast<uintptr_t>(candidate);
    const uintptr_t trayUiAddress = reinterpret_cast<uintptr_t>(trayUi);
    const uintptr_t distance = candidateAddress > trayUiAddress
                                   ? candidateAddress - trayUiAddress
                                   : trayUiAddress - candidateAddress;
    return distance <= 0x40 && distance % sizeof(void*) == 0;
}

void CancelPendingPanelResume(const wchar_t* reason) {
    if (!g_pendingPanelResume.timerId) {
        g_pendingPanelResume = {};
        return;
    }

    KillTimer(nullptr, g_pendingPanelResume.timerId);
    Trace(L"panel bridge: cancel epoch=%llu (%s)",
          static_cast<unsigned long long>(g_pendingPanelResume.epoch), reason);
    g_pendingPanelResume = {};
}

void ResetPrimaryTrayUi(HWND taskbar, const wchar_t* reason) {
    if (g_pendingPanelResume.taskbar == taskbar) {
        CancelPendingPanelResume(reason);
    }

    if (g_primaryTaskbar.load(std::memory_order_acquire) == taskbar) {
        RemovePropW(taskbar, kPanelBridgeReadyProperty);
        g_trayUi.store(nullptr, std::memory_order_release);
        g_primaryTaskbar.store(nullptr, std::memory_order_release);
        g_primaryTrayUiThreadId.store(0, std::memory_order_release);
        g_primaryWndProcThis = nullptr;
        g_failedTrayUiLookupThis = nullptr;
        g_lastPrimaryHideThis = nullptr;
        g_lastPrimaryHideTrayUi = nullptr;
        g_lastPrimaryHideEpoch = 0;
        g_lastCompletedPanelEpoch = 0;
        g_primaryTrayUiRevision++;
        Trace(L"panel bridge: primary TrayUI context cleared (%s)", reason);
    }
}

void PublishPanelBridgeReady(HWND taskbar) {
    if (g_unloading.load(std::memory_order_acquire) ||
        !g_trayUi.load(std::memory_order_acquire) ||
        !g_primaryWndProcThis ||
        g_primaryTaskbar.load(std::memory_order_acquire) != taskbar ||
        g_primaryTrayUiThreadId.load(std::memory_order_acquire) !=
            GetCurrentThreadId() ||
        !g_panelSessionClosedMessage.load(std::memory_order_acquire) ||
        !TrayUI__Hide_Original || !TrayUI_WndProc_Original) {
        return;
    }

    if (!GetPropW(taskbar, kPanelBridgeReadyProperty) &&
        SetPropW(taskbar, kPanelBridgeReadyProperty,
                 reinterpret_cast<HANDLE>(1))) {
        Trace(L"panel bridge: ready on primary taskbar %p", taskbar);
    }
}

void RecordTrayUi(void* received, const wchar_t* source, HWND taskbar) {
    if (!received || !TrayUI_Unhide_Original || g_taskbarCode.empty()) {
        return;
    }

    const bool fromWndProc = _wcsicmp(source, L"WndProc") == 0;
    void* currentTrayUi = g_trayUi.load(std::memory_order_acquire);
    const DWORD currentThreadId = GetCurrentThreadId();
    const DWORD cachedThreadId =
        g_primaryTrayUiThreadId.load(std::memory_order_acquire);
    if (fromWndProc && received == g_primaryWndProcThis && currentTrayUi &&
        taskbar && taskbar == g_primaryTaskbar.load(std::memory_order_acquire) &&
        cachedThreadId && cachedThreadId == currentThreadId) {
        // These values were validated when this primary TrayUI context was
        // recorded; stable WndProc messages can skip repeated window lookups.
        PublishPanelBridgeReady(taskbar);
        return;
    }

    if (!IsPrimaryTaskbarWindow(taskbar)) {
        return;
    }

    DWORD pid = 0;
    DWORD threadId = GetWindowThreadProcessId(taskbar, &pid);
    if (pid != GetCurrentProcessId() || threadId != currentThreadId) {
        return;
    }

    if (fromWndProc && received == g_failedTrayUiLookupThis) {
        return;
    }

    if (!fromWndProc && currentTrayUi &&
        g_primaryTaskbar.load(std::memory_order_acquire) == taskbar &&
        g_primaryTrayUiThreadId.load(std::memory_order_acquire) == threadId) {
        return;
    }

    int bestOffset = 0;
    int bestSlot = -1;
    void* trayUi = FindTrayUiInterface(received, &bestOffset, &bestSlot);
    if (!trayUi) {
        if (fromWndProc && received != g_primaryWndProcThis && currentTrayUi) {
            ResetPrimaryTrayUi(taskbar, L"primary TrayUI interface changed");
        }
        if (fromWndProc) {
            g_failedTrayUiLookupThis = received;
        }
        Trace(L"TrayUI from %s: %p has no Unhide interface nearby", source,
              received);
        return;
    }

    bool contextChanged =
        currentTrayUi != trayUi ||
        g_primaryTaskbar.load(std::memory_order_acquire) != taskbar ||
        g_primaryTrayUiThreadId.load(std::memory_order_acquire) != threadId;
    if (fromWndProc && g_primaryWndProcThis &&
        g_primaryWndProcThis != received) {
        contextChanged = true;
    }

    if (contextChanged) {
        CancelPendingPanelResume(L"primary TrayUI context changed");
        g_primaryTrayUiRevision++;
        g_lastPrimaryHideThis = nullptr;
        g_lastPrimaryHideTrayUi = nullptr;
        g_lastPrimaryHideEpoch = 0;
        g_lastCompletedPanelEpoch = 0;
        g_failedTrayUiLookupThis = nullptr;
        g_primaryWndProcThis = nullptr;
        Trace(L"TrayUI from %s: %p, Unhide interface at %+d (slot %d), "
              L"primary context=%llu",
              source, received, bestOffset, bestSlot,
              static_cast<unsigned long long>(g_primaryTrayUiRevision));
    }

    g_trayUi.store(trayUi, std::memory_order_release);
    g_primaryTaskbar.store(taskbar, std::memory_order_release);
    g_primaryTrayUiThreadId.store(threadId, std::memory_order_release);
    if (fromWndProc) {
        g_primaryWndProcThis = received;
    }

    if (fromWndProc) {
        PublishPanelBridgeReady(taskbar);
    }
}

bool HasPanelHold(HWND taskbar) {
    return GetPropW(taskbar, kPanelHoldProperty) != nullptr ||
           GetPropW(taskbar, kLegacyQuickSettingsHoldProperty) != nullptr;
}

bool IsCurrentUnheldPanelEpoch(HWND taskbar, UINT_PTR epoch) {
    if (!taskbar || !epoch || !IsPrimaryTaskbarWindow(taskbar) ||
        g_primaryTaskbar.load(std::memory_order_acquire) != taskbar ||
        GetPropW(taskbar, kPanelEpochProperty) !=
            reinterpret_cast<HANDLE>(epoch) ||
        HasPanelHold(taskbar)) {
        return false;
    }

    return true;
}

LRESULT CompletePanelResume(void* wndProcThis,
                            HWND taskbar,
                            UINT_PTR timerId) {
    PendingPanelResume pending = g_pendingPanelResume;
    if (!pending.timerId || pending.timerId != timerId ||
        pending.taskbar != taskbar) {
        return 0;
    }

    KillTimer(nullptr, timerId);
    g_pendingPanelResume = {};

    void* trayUi = g_trayUi.load(std::memory_order_acquire);
    DWORD pid = 0;
    DWORD windowThread = GetWindowThreadProcessId(taskbar, &pid);
    const bool currentContext =
        !g_unloading.load(std::memory_order_acquire) &&
        pid == GetCurrentProcessId() && taskbar == FindWindowW(L"Shell_TrayWnd", nullptr) &&
        windowThread == GetCurrentThreadId() && TrayUI__Hide_Original &&
        windowThread == g_primaryTrayUiThreadId.load(std::memory_order_acquire) &&
        pending.trayUiRevision == g_primaryTrayUiRevision &&
        pending.trayUi == trayUi &&
        pending.hideThis && IsTrayUiPointerFor(pending.hideThis, trayUi) &&
        IsTrayUiPointerFor(wndProcThis, trayUi);
    const bool currentEpoch =
        IsCurrentUnheldPanelEpoch(taskbar, pending.epoch);
    if (!currentContext || !currentEpoch) {
        Trace(L"panel bridge: resume expired epoch=%llu context=%d epochValid=%d",
              static_cast<unsigned long long>(pending.epoch), currentContext,
              currentEpoch);
        return 0;
    }

    if (pending.epoch == g_lastCompletedPanelEpoch) {
        Trace(L"panel bridge: duplicate resume ignored epoch=%llu",
              static_cast<unsigned long long>(pending.epoch));
        return 0;
    }

    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        HWND hit = WindowFromPoint(cursor);
        if (hit && GetAncestor(hit, GA_ROOT) == taskbar) {
            // A fresh interaction with the visible dock supersedes the old
            // panel-close request. Native leave handling will hide it later.
            g_lastCompletedPanelEpoch = pending.epoch;
            Trace(L"panel bridge: resume cancelled by dock interaction");
            return 0;
        }
    }

    if (IsTaskbarHiddenOnScreen(taskbar) ||
        PendingKindFor(taskbar) == PendingKind::HideCommit) {
        g_lastCompletedPanelEpoch = pending.epoch;
        Trace(L"panel bridge: native hide already in progress epoch=%llu",
              static_cast<unsigned long long>(pending.epoch));
        return 0;
    }

    // Mark before entering native code because _Hide may pump the taskbar
    // message queue and deliver a duplicate session-close message reentrantly.
    g_lastCompletedPanelEpoch = pending.epoch;
    void* hideThis = pending.hideThis;
    Trace(L"panel bridge: resume native _Hide epoch=%llu this=%p",
          static_cast<unsigned long long>(pending.epoch), hideThis);
    TrayUI__Hide_Original(hideThis);
    return 0;
}

void CALLBACK PanelResumeTimerProc(HWND, UINT, UINT_PTR timer, DWORD) {
    if (g_pendingPanelResume.timerId == timer)
        CompletePanelResume(g_primaryWndProcThis, g_pendingPanelResume.taskbar, timer);
}
LRESULT HandlePanelSessionClosed(void* wndProcThis,
                                 HWND taskbar,
                                 WPARAM wParam,
                                 LPARAM lParam) {
    DWORD pid = 0;
    DWORD windowThread = GetWindowThreadProcessId(taskbar, &pid);
    UINT_PTR epoch = static_cast<UINT_PTR>(wParam);
    void* trayUi = g_trayUi.load(std::memory_order_acquire);
    const bool validTarget =
        !g_unloading.load(std::memory_order_acquire) && lParam == 0 && epoch &&
        pid == GetCurrentProcessId() &&
        taskbar == FindWindowW(L"Shell_TrayWnd", nullptr) &&
        windowThread == GetCurrentThreadId() &&
        windowThread == g_primaryTrayUiThreadId.load(std::memory_order_acquire) &&
        g_primaryTaskbar.load(std::memory_order_acquire) == taskbar && trayUi &&
        IsTrayUiPointerFor(wndProcThis, trayUi);
    if (!validTarget) {
        Trace(L"panel bridge: reject close request hwnd=%p epoch=%llu targetValid=0",
              taskbar, static_cast<unsigned long long>(epoch));
        return 0;
    }

    if (!IsCurrentUnheldPanelEpoch(taskbar, epoch)) {
        Trace(L"panel bridge: reject close request epoch=%llu current=%llu held=%d",
              static_cast<unsigned long long>(epoch),
              static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(
                  GetPropW(taskbar, kPanelEpochProperty))),
              HasPanelHold(taskbar));
        return 0;
    }

    if (epoch == g_lastCompletedPanelEpoch) {
        Trace(L"panel bridge: duplicate close request ignored epoch=%llu",
              static_cast<unsigned long long>(epoch));
        return 0;
    }

    if (IsTaskbarHiddenOnScreen(taskbar)) {
        g_lastCompletedPanelEpoch = epoch;
        Trace(L"panel bridge: taskbar already hidden at close epoch=%llu",
              static_cast<unsigned long long>(epoch));
        return 0;
    }

    if (g_pendingPanelResume.timerId) {
        if (g_pendingPanelResume.epoch == epoch) {
            Trace(L"panel bridge: duplicate queued request ignored epoch=%llu",
                  static_cast<unsigned long long>(epoch));
            return 0;
        }
        CancelPendingPanelResume(L"newer close request");
    }

    if (!g_lastPrimaryHideThis || g_lastPrimaryHideTrayUi != trayUi ||
        g_lastPrimaryHideEpoch != epoch) {
        Trace(L"panel bridge: no observed native _Hide context for epoch=%llu",
              static_cast<unsigned long long>(epoch));
        return 0;
    }

    const UINT delayMs = g_settings.hideDelayMs > 0
                             ? static_cast<UINT>(g_settings.hideDelayMs)
                             : kDefaultNativeHideDelayMs;
    void* hideThis = g_lastPrimaryHideThis;
    const UINT_PTR timerId = SetTimer(nullptr, 0, delayMs, PanelResumeTimerProc);
    if (!timerId) {
        Trace(L"panel bridge: unable to schedule native resume epoch=%llu error=%lu",
              static_cast<unsigned long long>(epoch), GetLastError());
        return 0;
    }

    g_pendingPanelResume = {taskbar, timerId, epoch,
                            g_primaryTrayUiRevision, trayUi, hideThis};
    // Consume the exact _Hide call that was suppressed for this epoch. A
    // later close notification must not reuse it for another panel session.
    g_lastPrimaryHideThis = nullptr;
    g_lastPrimaryHideTrayUi = nullptr;
    g_lastPrimaryHideEpoch = 0;
    Trace(L"panel bridge: queued native resume epoch=%llu delay=%u this=%p",
          static_cast<unsigned long long>(epoch), delayMs, hideThis);
    return 0;
}

LRESULT WINAPI TrayUI_WndProc_Hook(void* pThis,
                                   HWND hWnd,
                                   UINT message,
                                   WPARAM wParam,
                                   LPARAM lParam,
                                   bool* handled) {
    RecordTrayUi(pThis, L"WndProc", hWnd);

    if (hWnd == g_edgeTaskbar &&
        (message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE ||
         message == WM_DPICHANGED)) g_edgeGeometryValid = false;

    const UINT closeMessage =
        g_panelSessionClosedMessage.load(std::memory_order_acquire);
    if (closeMessage && message == closeMessage) {
        if (handled) {
            *handled = true;
        }
        return HandlePanelSessionClosed(pThis, hWnd, wParam, lParam);
    }

    if (message == WM_NCDESTROY && IsPrimaryTaskbarWindow(hWnd)) {
        ResetPrimaryTrayUi(hWnd, L"primary taskbar destroyed");
    }

    return TrayUI_WndProc_Original(pThis, hWnd, message, wParam, lParam,
                                   handled);
}

// Gate only the primary TrayUI's native hide while the shared panel hold is
// active. Secondary TrayUI instances never consult the primary taskbar prop.
void WINAPI TrayUI__Hide_Hook(void* pThis) {
    HWND primaryTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid = 0;
    DWORD threadId = primaryTaskbar
                         ? GetWindowThreadProcessId(primaryTaskbar, &pid)
                         : 0;
    void* knownTrayUi = g_trayUi.load(std::memory_order_acquire);
    const bool isPrimaryTrayUi =
        !g_unloading.load(std::memory_order_acquire) && knownTrayUi &&
        primaryTaskbar && pid == GetCurrentProcessId() &&
        threadId == GetCurrentThreadId() &&
        threadId == g_primaryTrayUiThreadId.load(std::memory_order_acquire) &&
        primaryTaskbar == g_primaryTaskbar.load(std::memory_order_acquire) &&
        IsTrayUiPointerFor(pThis, knownTrayUi);

    if (isPrimaryTrayUi) {
        if (HasPanelHold(primaryTaskbar)) {
            const UINT_PTR epoch = reinterpret_cast<UINT_PTR>(
                GetPropW(primaryTaskbar, kPanelEpochProperty));
            if (epoch) {
                g_lastPrimaryHideThis = pThis;
                g_lastPrimaryHideTrayUi = knownTrayUi;
                g_lastPrimaryHideEpoch = epoch;
            } else {
                g_lastPrimaryHideThis = nullptr;
                g_lastPrimaryHideTrayUi = nullptr;
                g_lastPrimaryHideEpoch = 0;
            }
            Trace(L"_Hide held for primary TrayUI this=%p taskbar=%p", pThis,
                  primaryTaskbar);
            return;
        }

        // This native hide is proceeding normally, so it supersedes any
        // previously suppressed request.
        g_lastPrimaryHideThis = nullptr;
        g_lastPrimaryHideTrayUi = nullptr;
        g_lastPrimaryHideEpoch = 0;
    }
    TrayUI__Hide_Original(pThis);
}

bool IsDesktopWindow(HWND hWnd) {
    WCHAR className[16]{};
    GetClassNameW(hWnd, className, ARRAYSIZE(className));
    return _wcsicmp(className, L"Progman") == 0 ||
           _wcsicmp(className, L"WorkerW") == 0;
}

// Games, videos and presentations: Windows keeps the taskbar hidden there.
bool IsFullScreenWindow(HWND hWnd, RECT const& monitorRect) {
    RECT rect{};
    if (!hWnd || IsZoomed(hWnd) || IsDesktopWindow(hWnd) ||
        !GetWindowRect(hWnd, &rect)) {
        return false;
    }
    return rect.left <= monitorRect.left && rect.top <= monitorRect.top &&
           rect.right >= monitorRect.right && rect.bottom >= monitorRect.bottom;
}

// The poll is fast only while the taskbar is hidden. Once it has been shown
// for a while the poll slows down to an idle rate, which only covers the
// taskbar getting hidden without passing through SlideWindow.
constexpr UINT kIdlePollMs = 500;
constexpr int kShownTicksBeforeIdle = 25;
constexpr ULONGLONG kEdgePollMetricsWindowMs = 60000;
UINT g_edgePollInterval = 0;
int g_shownTicks = 0;
ULONGLONG g_edgePollCallbacks = 0;
ULONGLONG g_edgePollMetricsStart = 0;

void CALLBACK EdgePollTimerProc(HWND, UINT, UINT_PTR, DWORD);

// Same id: SetTimer replaces the interval of the running timer.
void SetEdgePollInterval(UINT ms) {
    if (g_edgePollTimer && g_edgePollInterval != ms) {
        g_edgePollTimer =
            SetTimer(nullptr, g_edgePollTimer, ms, EdgePollTimerProc);
        g_edgePollInterval = ms;
    }
}

void CALLBACK EdgePollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_settings.traceToFile) {
        const ULONGLONG now = GetTickCount64();
        if (!g_edgePollMetricsStart) {
            g_edgePollMetricsStart = now;
        }
        ++g_edgePollCallbacks;
        const ULONGLONG elapsed = now - g_edgePollMetricsStart;
        if (elapsed >= kEdgePollMetricsWindowMs) {
            Trace(L"metrics window_ms=%llu edge_timer_callbacks=%llu",
                  static_cast<unsigned long long>(elapsed),
                  static_cast<unsigned long long>(g_edgePollCallbacks));
            g_edgePollCallbacks = 0;
            g_edgePollMetricsStart = now;
        }
    }

    HWND taskbar = g_edgeTaskbar;
    if (g_unloading.load(std::memory_order_acquire) || !IsWindow(taskbar)) {
        g_edgeRequested = false;
        return;
    }

    if (!IsTaskbarHiddenOnScreen(taskbar)) {
        g_edgeGeometryValid = false;
        g_edgeRequested = false;
        if (++g_shownTicks >= kShownTicksBeforeIdle) {
            SetEdgePollInterval(kIdlePollMs);
        }
        return;
    }
    g_shownTicks = 0;
    SetEdgePollInterval(kEdgePollMs);

    if (!g_primaryWndProcThis) {
        g_edgeRequested = false;
        return;
    }

    POINT pt{};
    RECT taskbarRect{};
    if (!GetCursorPos(&pt) || !GetWindowRect(taskbar, &taskbarRect)) {
        return;
    }
    const HMONITOR monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST);
    if (!g_edgeGeometryValid || pt.x != g_edgeGeometryCursor.x ||
        pt.y != g_edgeGeometryCursor.y || monitor != g_edgeGeometryMonitor ||
        !EqualRect(&taskbarRect, &g_edgeGeometryTaskbar)) {
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(monitor, &info)) return;
        g_edgeGeometry = info;
        g_edgeGeometryCursor = pt;
        g_edgeGeometryTaskbar = taskbarRect;
        g_edgeGeometryMonitor = monitor;
        g_edgeGeometryValid = true;
    }
    RECT const& monitorRect = g_edgeGeometry.rcMonitor;

    // Bottom taskbars only; the hidden window hangs below the monitor. Keep
    // the cursor on the monitor's final row so another monitor below cannot
    // trigger this taskbar's reveal.
    const bool onEdge = taskbarRect.bottom > monitorRect.bottom &&
                        pt.y >= monitorRect.bottom - 1 &&
                        pt.y < monitorRect.bottom &&
                        pt.x >= monitorRect.left && pt.x < monitorRect.right;
    if (!onEdge) {
        g_edgeRequested = false;
        return;
    }
    if (g_edgeRequested) {
        return;
    }
    g_edgeRequested = true;

    HWND under = GetAncestor(WindowFromPoint(pt), GA_ROOT);
    if (under == taskbar) {
        // Inside the region: Windows handles it.
        return;
    }
    if (IsFullScreenWindow(under, monitorRect)) {
        Trace(L"edge (%ld,%ld): full screen window, no reveal", pt.x, pt.y);
        return;
    }

    Trace(L"edge (%ld,%ld) outside the taskbar region: unhide", pt.x, pt.y);
    TrayUI_Unhide_Original(g_trayUi.load(std::memory_order_acquire),
                           kUnhideFlagsNone, kUnhideRequestMouse);
}

// Must run on the taskbar UI thread; the timer fires there.
void StartEdgePoll(HWND taskbar) {
    g_edgeGeometryValid = false;
    if (!g_settings.revealAlongWholeEdge || !TrayUI_Unhide_Original ||
        !TrayUI_WndProc_Original) {
        return;
    }
    WCHAR className[32]{};
    GetClassNameW(taskbar, className, ARRAYSIZE(className));
    if (_wcsicmp(className, L"Shell_TrayWnd") != 0) {
        return;
    }

    g_edgeTaskbar = taskbar;
    if (!g_primaryWndProcThis) {
        // After a reload nothing has reached TrayUI::WndProc yet; a no-op
        // message gets it the TrayUI pointer right away.
        SendMessageW(taskbar, WM_NULL, 0, 0);
    }
    if (!g_edgePollTimer) {
        g_edgeRequested = false;
        g_shownTicks = 0;
        g_edgePollInterval = kEdgePollMs;
        g_edgePollTimer =
            SetTimer(nullptr, 0, kEdgePollMs, EdgePollTimerProc);
    }
}

void StopEdgePoll() {
    g_edgeGeometryValid = false;
    if (g_edgePollTimer) {
        KillTimer(nullptr, g_edgePollTimer);
        g_edgePollTimer = 0;
    }
}

void WINAPI TrayUI_SlideWindow_Hook(void* pThis,
                                    HWND hWnd,
                                    const RECT* rect,
                                    HMONITOR monitor,
                                    bool show,
                                    bool animate) {
    if (rect && IsTaskbarWindow(hWnd) &&
        !g_unloading.load(std::memory_order_acquire) &&
        GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId()) {
        WCHAR className[32]{};
        GetClassNameW(hWnd, className, ARRAYSIZE(className));
        if (_wcsicmp(className, L"Shell_TrayWnd") == 0) {
            RecordTrayUi(pThis, L"SlideWindow", hWnd);
            if (show && g_pendingPanelResume.taskbar == hWnd)
                CancelPendingPanelResume(L"new native reveal");
            StartEdgePoll(hWnd);
            // Fast from the start of a hide, idle once shown.
            g_shownTicks = 0;
            SetEdgePollInterval(show ? kIdlePollMs : kEdgePollMs);
        }
    }

    RECT currentRect{};
    if (rect) {
        GetWindowRect(hWnd, &currentRect);
        Trace(L"SlideWindow this=%p show=%d animate=%d cur=(%ld,%ld,%ld,%ld) "
              L"target=(%ld,%ld,%ld,%ld) pending=%d",
              pThis, show, animate, currentRect.left, currentRect.top,
              currentRect.right, currentRect.bottom, rect->left, rect->top,
              rect->right, rect->bottom,
              PendingKindFor(hWnd) ? static_cast<int>(*PendingKindFor(hWnd))
                                   : -1);
    }

    if (!rect || !IsTaskbarWindow(hWnd) ||
        g_unloading.load(std::memory_order_acquire)) {
        TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
        return;
    }

    // Re-entered while a hide pumps messages to commit its animation.
    if (g_syncHide.active) {
        if (hWnd != g_syncHide.hWnd) {
            Trace(L"-> native: another taskbar hide is committing");
            TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show,
                                        animate);
            return;
        }
        if (show) {
            g_syncHide.revealRequested = true;
            g_syncHide.trayUi = pThis;
            g_syncHide.revealRect = *rect;
            g_syncHide.revealMonitor = monitor;
        }
        Trace(L"-> deferred until the running hide is committed");
        return;
    }

    // XAML objects may only be touched from the taskbar UI thread.
    if (GetWindowThreadProcessId(GetTaskbarDispatchWindow(hWnd), nullptr) !=
        GetCurrentThreadId()) {
        Trace(L"-> native: SlideWindow on a non-UI thread");
        TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
        return;
    }

    MotionTarget target = GetTaskbarMotionTarget(hWnd);
    if (!target) {
        Trace(L"-> native: no motion target (%s)", g_targetFailure);
        CancelPendingAnimation(hWnd, false);
        TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, show, animate);
        return;
    }

    if (!animate) {
        // Windows sometimes repeats the request without animation while ours
        // is still running. Keep the running motion instead of snapping.
        const auto pendingKind = PendingKindFor(hWnd);
        if (show && pendingKind == PendingKind::RevealSettle) {
            Trace(L"-> keep running reveal");
            TrayUI_SlideWindow_Original(pThis, hWnd, rect, monitor, true,
                                        false);
            return;
        }
        if (!show && pendingKind == PendingKind::HideCommit &&
            EqualRect(&g_pendingAnimations[hWnd].endRect, rect)) {
            Trace(L"-> keep running hide");
            return;
        }
    }

    if (!animate || !MotionAllowed() || !currentRect.bottom) {
        Trace(L"-> snap (animate=%d motionAllowed=%d)", animate,
              MotionAllowed());
        SlideNative(pThis, hWnd, rect, monitor, show, false, target.element);
        return;
    }

    if (show) {
        Reveal(pThis, hWnd, rect, monitor, currentRect, target);
    } else {
        Hide(pThis, hWnd, rect, monitor, currentRect, target);
    }
}

// -----------------------------------------------------------------------------
// Auto-hide delays.
// -----------------------------------------------------------------------------

using SetTimer_t = decltype(&SetTimer);
SetTimer_t SetTimer_Original = nullptr;

UINT_PTR WINAPI SetTimer_Hook(HWND hWnd,
                              UINT_PTR nIDEvent,
                              UINT uElapse,
                              TIMERPROC lpTimerFunc) {
    if (hWnd &&
        (nIDEvent == kTrayUITimerHide || nIDEvent == kTrayUITimerUnhide) &&
        !g_unloading.load(std::memory_order_acquire) &&
        IsTaskbarWindow(hWnd)) {
        const int delay = nIDEvent == kTrayUITimerUnhide
                              ? g_settings.unhideDelayMs
                              : g_settings.hideDelayMs;
        if (delay > 0) {
            uElapse = static_cast<UINT>(delay);
        }
    }

    return SetTimer_Original(hWnd, nIDEvent, uElapse, lpTimerFunc);
}

// -----------------------------------------------------------------------------
// UI-thread helpers.
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

std::vector<HWND> EnumerateTaskbars() {
    std::vector<HWND> result;

    EnumWindows(
        [](HWND hWnd, LPARAM lParam) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hWnd, &pid);
            if (pid != GetCurrentProcessId() || !IsTaskbarWindow(hWnd)) {
                return TRUE;
            }

            reinterpret_cast<std::vector<HWND>*>(lParam)->push_back(hWnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));

    return result;
}

// Brings the content to the resting state matching the taskbar's current
// position. Also repairs leftovers of earlier versions (a translation stuck
// mid-way, or an OpacityTransition).
void WINAPI ApplyInitialStateOnUiThread(PVOID parameter) {
    HWND hWnd = reinterpret_cast<HWND>(parameter);

    if (IsPrimaryTaskbarWindow(hWnd)) {
        // Drop any readiness marker left by an interrupted prior load. The
        // WndProc hook republishes it only after discovering this TrayUI.
        RemovePropW(hWnd, kPanelBridgeReadyProperty);
        if (!g_primaryWndProcThis) {
            SendMessageW(hWnd, WM_NULL, 0, 0);
        }
    }

    MotionTarget target = GetTaskbarMotionTarget(hWnd);
    const bool hidden = IsTaskbarHiddenOnScreen(hWnd);
    Trace(L"load: taskbar %p hidden=%d target=%d (%s)", hWnd, hidden,
          static_cast<bool>(target), target ? L"ok" : g_targetFailure);

    if (!target) {
        return;
    }

    try {
        target.element.OpacityTransition(nullptr);
    } catch (...) {
    }
    SetRestingState(hWnd, target.element, !hidden);

    StartEdgePoll(hWnd);
}

void WINAPI CleanupTaskbarOnUiThread(PVOID parameter) {
    HWND hWnd = reinterpret_cast<HWND>(parameter);

    if (IsPrimaryTaskbarWindow(hWnd)) {
        ResetPrimaryTrayUi(hWnd, L"mod unload");
        RemovePropW(hWnd, kPanelBridgeReadyProperty);
    }

    if (hWnd == g_edgeTaskbar) {
        StopEdgePoll();
    }

    auto it = g_pendingAnimations.find(hWnd);
    if (it != g_pendingAnimations.end()) {
        PendingAnimation pending = std::move(it->second);
        g_pendingAnimations.erase(it);
        if (pending.timerId) {
            KillTimer(nullptr, pending.timerId);
        }

        if (pending.kind == PendingKind::HideCommit &&
            TrayUI_SlideWindow_Original) {
            TrayUI_SlideWindow_Original(pending.trayUi,
                                        pending.hWnd,
                                        &pending.endRect,
                                        pending.monitor,
                                        false,
                                        false);
        }

        StopPendingAnimations(pending);
    }

    if (MotionTarget target = GetTaskbarMotionTarget(hWnd)) {
        try {
            target.element.Translation({0.0f, 0.0f, 0.0f});
            target.element.Opacity(1.0);
        } catch (...) {
        }
        StopTrayPop(hWnd);
    }

    g_rendered.erase(hWnd);
    StopScalePop(hWnd);

    // Release the compositor on its own thread.
    g_compositor = nullptr;
}

void ForEachTaskbarOnUiThread(RunFromWindowThreadProc_t proc) {
    for (HWND taskbarWnd : EnumerateTaskbars()) {
        RunFromWindowThread(GetTaskbarDispatchWindow(taskbarWnd), proc,
                            taskbarWnd);
    }
}

// -----------------------------------------------------------------------------
// Settings and hooks.
// -----------------------------------------------------------------------------

void LoadSettings() {
    LPCWSTR profile = Wh_GetStringSetting(L"motionProfile");
    g_settings.profile = MotionProfile::Smooth;
    if (profile && _wcsicmp(profile, L"expressive") == 0) {
        g_settings.profile = MotionProfile::Expressive;
    } else if (profile && _wcsicmp(profile, L"expressiveScale") == 0) {
        g_settings.profile = MotionProfile::ExpressiveScale;
    }
    if (profile) {
        Wh_FreeStringSetting(profile);
    }

    g_settings.revealDurationMs =
        std::clamp(Wh_GetIntSetting(L"revealDurationMs"), 60, 1000);
    g_settings.expressiveOvershoot =
        std::clamp(Wh_GetIntSetting(L"expressiveOvershoot"), 0, 16);
    g_settings.scaleBouncePercent =
        std::clamp(Wh_GetIntSetting(L"scaleBouncePercent"), 0, 15);
    g_settings.scalePopDurationMs =
        std::clamp(Wh_GetIntSetting(L"scalePopDurationMs"), 150, 2000);
    g_settings.scalePopBounceX100 =
        std::clamp(Wh_GetIntSetting(L"scalePopBounceX100"), 0, 60);
    g_settings.hideDurationMs =
        std::clamp(Wh_GetIntSetting(L"hideDurationMs"), 60, 1000);
    g_settings.fadeHidden = Wh_GetIntSetting(L"fadeHidden") != 0;
    g_settings.unhideDelayMs =
        std::clamp(Wh_GetIntSetting(L"unhideDelayMs"), 0, 2000);
    g_settings.hideDelayMs =
        std::clamp(Wh_GetIntSetting(L"hideDelayMs"), 0, 5000);
    g_settings.revealAlongWholeEdge =
        Wh_GetIntSetting(L"revealAlongWholeEdge") != 0;
    g_settings.respectSystemAnimations =
        Wh_GetIntSetting(L"respectSystemAnimations") != 0;
    g_settings.traceToFile = Wh_GetIntSetting(L"traceToFile") != 0;
}

bool HookTaskbarSymbols() {
    HMODULE module =
        LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        Wh_Log(L"couldn't load taskbar.dll");
        return false;
    }
    RecordTaskbarImage(module);

    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {
            {LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"},
            &CTaskBand_ITaskListWndSite_vftable,
        },
        {
            {LR"(const CSecondaryTaskBand::`vftable'{for `ITaskListWndSite'})"},
            &CSecondaryTaskBand_ITaskListWndSite_vftable,
            nullptr,
            true,
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
            {LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CSecondaryTaskBand::GetTaskbarHost(void)const )"},
            &CSecondaryTaskBand_GetTaskbarHost_Original,
            nullptr,
            true,
        },
        {
            {LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"},
            &std__Ref_count_base__Decref_Original,
        },
        {
            {LR"(public: virtual void __cdecl TrayUI::SlideWindow(struct HWND__ *,struct tagRECT const *,struct HMONITOR__ *,bool,bool))"},
            &TrayUI_SlideWindow_Original,
            TrayUI_SlideWindow_Hook,
        },
        {
            {LR"(public: void __cdecl TrayUI::_Hide(void))"},
            &TrayUI__Hide_Original,
            TrayUI__Hide_Hook,
            true,
        },
        {
            {LR"(public: virtual void __cdecl TrayUI::Unhide(enum TrayCommon::TrayUnhideFlags,enum TrayCommon::UnhideRequest))"},
            &TrayUI_Unhide_Original,
            nullptr,
            true,
        },
        {
            {LR"(public: virtual __int64 __cdecl TrayUI::WndProc(struct HWND__ *,unsigned int,unsigned __int64,__int64,bool *))"},
            &TrayUI_WndProc_Original,
            TrayUI_WndProc_Hook,
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
    g_edgePollCallbacks = 0;
    g_edgePollMetricsStart = 0;
    g_pendingPanelResume = {};
    g_primaryWndProcThis = nullptr;
    g_failedTrayUiLookupThis = nullptr;
    g_lastPrimaryHideThis = nullptr;
    g_lastPrimaryHideTrayUi = nullptr;
    g_lastPrimaryHideEpoch = 0;
    g_primaryTrayUiRevision = 0;
    g_lastCompletedPanelEpoch = 0;
    g_trayUi.store(nullptr, std::memory_order_release);
    g_primaryTaskbar.store(nullptr, std::memory_order_release);
    g_primaryTrayUiThreadId.store(0, std::memory_order_release);
    const UINT sessionClosedMessage =
        RegisterWindowMessageW(kPanelSessionClosedMessageName);
    g_panelSessionClosedMessage.store(sessionClosedMessage,
                                      std::memory_order_release);

    Trace(L"v" WH_MOD_VERSION L" init");
    if (!sessionClosedMessage) {
        Trace(L"panel bridge: could not register session-close message (%lu)",
              GetLastError());
    }

    if (!HookTaskbarSymbols()) {
        Trace(L"symbol hooks failed");
        return FALSE;
    }

    WindhawkUtils::SetFunctionHook(SetTimer, SetTimer_Hook,
                                   &SetTimer_Original);

    return TRUE;
}

void Wh_ModAfterInit() {
    ForEachTaskbarOnUiThread(ApplyInitialStateOnUiThread);
}

void Wh_ModBeforeUninit() {
    g_unloading.store(true, std::memory_order_release);

    ForEachTaskbarOnUiThread(CleanupTaskbarOnUiThread);

    Trace(L"cleanup complete");
}

void Wh_ModUninit() {
    Trace(L"uninit");
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    // Reload so cleanup restores the content and init re-applies the resting
    // state with the new settings.
    *bReload = TRUE;
    return TRUE;
}
