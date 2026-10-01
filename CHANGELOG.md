# Changelog

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
