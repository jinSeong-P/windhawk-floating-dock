# Changelog

## 1.0.8 (2026-10-08)

- Stop app notifications from pinning the dock. When an app flashes its
  taskbar button, Windows kept the taskbar up until that app was activated
  or closed. Motion now hides it after *Attention reveal duration* (3000 ms by
  default; 0 keeps the Windows behavior). The button keeps its highlight, and
  mouse hover and open menus still keep the dock up.
- Give each app one reveal per notification. Apps that keep flashing repeat
  their reveal request every 2 to 10 s; repeated requests from an app whose
  reveal is over are dropped until it stops flashing or stays quiet for 15 s.
  Another app's notification still gets its own reveal.
- Hook `TrayUI::Unhide` by address so the TrayUI interface lookup used by the
  whole-edge reveal keeps working.
- Helpers' behavior is unchanged from 1.0.7; its version follows the pair.
  See `docs/verification-1.0.8.txt`.

## 1.0.7 (2026-10-06)

- Require an open taskbar XAML menu presenter before recognizing an owned
  XAML popup as a menu. Preserve Win+X keyboard entry and right-click menus.
- Coalesce XAML popup events before checking their content. An idle popup
  without menu content does not start full panel discovery or a panel hold.
- Remove the unused process creation timestamp and its query; retained
  process handles still protect the cache against PID reuse.
- Label panel request generations as `request=` in diagnostic logs, keeping
  the shared hold-session epoch distinct. Remove leftover blank lines.
- Clarify the E5 bridge result, conditional Task View resume and the limits
  of the geometry cache measurements in the 1.0.6 report. Record focused
  1.0.7 verification and the remaining native tooltip coverage gap.
- Motion's behavior is unchanged from 1.0.6; its version follows the pair.

## 1.0.6 (2026-10-06)

- Stop periodic panel discovery while idle. Coalesce shell window events and
  use a 100 ms safety check only during an active panel session.
- Install low-level input hooks only while a tray activation is deferred;
  keep native clicks intact if hook installation fails.
- Separate request IDs from dock hold sessions and recover a valid intercepted
  click exactly once when a transition times out.
- Reconnect the panel coordinator when Motion becomes available, and preserve
  the legacy Quick Settings path when the shared bridge is unavailable.
- Make the native `_Hide` hook optional and cache the primary taskbar context.
- Remove duplicate Quick Settings visibility polling, cache shell process
  identities and limit searches for missing optional XAML elements.
- Keep the 40 ms edge poll and reuse monitor geometry for a stationary cursor.
  Display changes and taskbar transitions invalidate that cache.
- Recognize taskbar-owned XAML menus, including Win+X on build 26200, and
  cancel a deferred activation when a newer Windows shortcut arrives.
- Verify 75 dock panel transitions plus recovery, cancellation, reload and
  native fallback cases. See `docs/verification-1.0.6.txt` for measurement
  scope and limitations.

## 1.0.5 (2026-10-06)

- Replace the five dock panels with one click and keep the dock visible
  throughout panel transitions and child-page interaction.
- Resume native auto-hide when the final panel closes, preserving the
  configured animation and hover behavior.
- Return from Quick Settings Wi-Fi, Bluetooth and audio output subpages
  with Escape; associate tray menus with their parent panel.
- Cancel stale deferred activation on outside clicks, Escape or newer input.
- Support delayed ControlCenter.dll loading and re-register hooks after
  mod reload; verify Explorer and ShellHost restart behavior.
- Set Windows alignment automatically to Center for the macOS layout and
  Left when that layout is disabled.
- Keep shortcut entry and Task View on native fallback paths. See
  [validation and scope](docs/verification-1.0.5.txt).

## 1.0.0 (2026-10-01)

First public release. Split from a single local mod (0.2 to 0.11.7) into:

**Taskbar Auto-Hide Motion 1.0.0**

- Timed Composition slide for reveal and hide (Material 3 emphasized curves);
  a reveal that interrupts a hide continues from where the taskbar is.
- Expressive scale profile: the dock and the system tray pop as they land,
  following a SwiftUI-style spring.
- Content fully transparent while hidden (no strip left on OLED screens).
- Reveal along the whole bottom edge with Taskbar Styler's click-through
  option, via Windows' own `TrayUI::Unhide`; never over full screen windows.
- The edge poll runs at 40 ms only while the taskbar is hidden and idles at
  500 ms otherwise.
- Configurable reveal and hide delays.

**Floating Dock Helpers 1.0.0**

- macOS-style layout: the tray right next to the centered dock, following
  their widths, moved with a RenderTransform. The click-through region is
  brought up to date once the moved parts have rendered, so a reveal right
  after loading isn't cut.
- Hot corners for Start and Show desktop, sized from
  the measured dock, tray and buttons instead of fixed values, and only while
  the taskbar floats above the screen edge.
- Quick Settings opens above the moved tray.
- Event-driven placement (size changes, reveal) with a 250 ms poll while the
  taskbar is shown, nothing while it's hidden.

**Presets**

- Taskbar Styler presets for the mac and left layouts, with
  6 DIP of room around the dock so the pop stays inside the click-through
  region. The padding inside the Start button is pinned (Width 53, Padding
  2,4,2,4): Windows widens it by 10 DIP when Explorer starts with left
  alignment, so switching layouts needs no restart. The dock and the tray
  have the same visible corner radius (10 DIP; the tray sets 16 because its
  background is drawn inside a 12 DIP transparent border).
- Taskbar height and icon size preset (72 px taskbar, 28 px icons).
