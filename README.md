# windhawk-floating-dock

**English** | [한국어](README.ko.md)

Turn the Windows 11 taskbar into a floating dock that hides itself.
The rounded dock and tray slide up when you reach the bottom of the screen,
pop slightly as they land, and leave nothing on screen while hidden, which
also protects OLED displays from burn-in.

![The floating dock sliding up from the bottom edge, popping slightly as it lands, then sliding back down until nothing is left on screen](docs/reveal.gif)

Current version: **1.0.7** · Windows 11 · [Changelog](CHANGELOG.md) ·
[GPL-3.0](LICENSE)

## Contents

- [What you get](#what-you-get)
- [Before you start](#before-you-start)
- [Install](#install)
- [Choose a layout](#choose-a-layout)
- [Using the dock](#using-the-dock)
- [Settings](#settings)
- [Troubleshooting](#troubleshooting)
- [Uninstall](#uninstall)
- [Known limitations](#known-limitations)
- [For developers](#for-developers)

## What you get

- **A dock that floats.** The apps and the system tray become rounded bars
  with space around them. Clicks on the empty taskbar area go to the window
  behind it.
- **Smooth auto-hide.** The taskbar slides in and out instead of jumping, and
  you can reveal it from anywhere along the bottom edge.
- **Nothing left on screen.** Windows normally leaves a 1-2 px strip of the
  hidden taskbar visible. This project hides it completely.
- **Two layouts.** *mac*: the dock and tray sit side by side in the center.
  *left*: the dock on the left, the tray on the right.
- **Panels that behave.** Start, Search, Quick Settings, Notifications and
  hidden icons replace each other with one click, and the dock stays visible
  while you use them.
- **Hot corners.** Click the bottom left corner for Start and the bottom right
  corner to show the desktop, even though the dock no longer reaches them.

The project has three parts. You need all three for the floating dock.

| Part | What it does |
| --- | --- |
| [Taskbar Auto-Hide Motion](mods/taskbar-autohide-motion.wh.cpp) | The slide and pop animation, the fully hidden state and the reveal along the whole bottom edge. Works with any taskbar style on its own. |
| [Floating Dock Helpers](mods/floating-dock-helpers.wh.cpp) | The mac layout, hot corners, Quick Settings placement and the panel behavior. |
| [Styler presets](styler-presets) | The look itself: rounded dock and tray, spacing, and click-through empty space. |

## Before you start

- **Windows 11** with the taskbar at the **bottom** of the screen.
  Tested on 25H2 (build 26200) at 175% scaling with Windhawk 1.7.3.
- **[Windhawk](https://windhawk.net)**, a free tool that customizes Windows
  programs with small add-ons called *mods*.
- The two mods of this project are not in Windhawk's catalog. You add them as
  local mods by pasting their source code (step 4 below). No programming
  knowledge is needed.

> [!NOTE]
> Other Windows 11 builds may work. If a feature can't find what it needs
> inside the taskbar, that feature turns itself off instead of breaking the
> taskbar.

## Install

1. **Install [Windhawk](https://windhawk.net).**
2. **Install two mods from Windhawk's catalog.** In Windhawk, open *Explore*
   and install:
   - **Windows 11 Taskbar Styler**
   - **Taskbar height and icon size**
3. **Apply the presets.** Pick a [layout](#choose-a-layout) first. Then, for
   each of the two mods from step 2: open the mod, go to
   **Advanced > Mod settings**, replace the text with the contents of the
   preset file, and click **Save**.

   | Mod | Preset file |
   | --- | --- |
   | Windows 11 Taskbar Styler | [`taskbar-styler-mac.json`](styler-presets/taskbar-styler-mac.json) for mac, or [`taskbar-styler-left.json`](styler-presets/taskbar-styler-left.json) for left |
   | Taskbar height and icon size | [`taskbar-icon-size.json`](styler-presets/taskbar-icon-size.json) (72 px taskbar, 28 px icons; the dock spacing is tuned for this height) |

   **Caution: Save replaces all of that mod's settings.** To keep your
   current settings, click **Load** first and save the text somewhere.

4. **Add this project's two mods.** For each file in [mods](mods):
   1. Open the file on GitHub and copy its whole contents
      (the *Copy raw file* button does this).
   2. In Windhawk, click **Create a New Mod**.
   3. Replace the whole template with the copied text.
   4. Click **Compile Mod**, then **Exit Editing Mode**.
5. **Match the layout.** Open *Floating Dock Helpers* settings and turn
   *Tray next to the dock* **on** for mac or **off** for left.
   The Windows taskbar alignment follows automatically.
6. **Turn on auto-hide.** In Windows Settings, go to **Personalization >
   Taskbar > Taskbar behaviors** and turn on
   **Automatically hide the taskbar**.

## Choose a layout

| Layout | Looks like | Styler preset | *Tray next to the dock* |
| --- | --- | --- | --- |
| **mac** | Dock and tray side by side, centered together | `taskbar-styler-mac.json` | On |
| **left** | Dock at the left edge, tray at the right edge | `taskbar-styler-left.json` | Off |

**mac**

![mac layout: app icons and the system tray as two rounded bars side by side in the center of the screen](docs/layout-mac.png)

**left**

![left layout: app icons in a rounded bar at the left edge and the system tray in a rounded bar at the right edge](docs/layout-left.png)

To switch layouts later, repeat steps 3 and 5 with the other preset.
Both presets fix the padding inside the Start button, so the change applies
right away without signing out.

## Using the dock

- **Show the dock:** move the mouse to the bottom edge of the screen.
- **Hide the dock:** move the mouse away. It hides after a short delay.
- **Switch panels with one click.** With Start open, clicking Search, Quick
  Settings, the clock (Notifications) or the hidden-icons arrow closes Start
  and opens the new panel.
- **The dock stays while a panel is open.** Moving the mouse away doesn't
  close the panel or hide the dock.
- **Close a panel** by clicking outside it or pressing **Esc**. The dock then
  hides as usual.
- **Esc goes back one step.** In the Wi-Fi, Bluetooth or sound output lists of
  Quick Settings, Esc returns to the main page. In a tray icon's menu, Esc
  closes only that menu.
- **Hot corners:** while the dock is shown, click the bottom left corner for
  Start or the bottom right corner to show the desktop.

Opening panels with keyboard shortcuts such as Win+A or Win+N uses Windows'
own behavior, so the one-panel-at-a-time rule is not guaranteed there.
Task View (Win+Tab) also keeps Windows' own taskbar behavior.

## Settings

Open a mod in Windhawk and choose **Settings** to change these.

**Taskbar Auto-Hide Motion**

| Setting | What it does | Default |
| --- | --- | --- |
| Motion profile | *Smooth*, *Expressive* (overshoots, then settles) or *Expressive scale* (a small pop on arrival) | Expressive scale |
| Reveal / Hide duration | Animation length in ms | 260 / 200 |
| Expressive overshoot, scale pop, pop duration, pop bounce | Fine-tune the Expressive profiles | 4 DIP, 1%, 550 ms, 15 |
| Transparent while hidden | Hides the strip Windows leaves on screen | On |
| Reveal delay | How long the mouse rests on the edge before the dock appears (0 = Windows default) | 0 ms |
| Hide delay | How long after the mouse leaves before the dock hides (0 = Windows default, about 500 ms) | 300 ms |
| Reveal along the whole bottom edge | Reveal from anywhere on the bottom edge, not only below the dock and tray. Never over full screen windows | On |
| Respect Windows animation effects | If Windows animations are off, the dock appears without animating | On |
| Diagnostic trace file | Writes a log to `%TEMP%\taskbar-autohide-motion.log` | Off |

**Floating Dock Helpers**

| Setting | What it does | Default |
| --- | --- | --- |
| Tray next to the dock (macOS style) | Puts the tray next to the centered dock. Also sets Windows alignment to Center (on) or Left (off) | On |
| Gap between dock and tray | Space between the two bars, in DIP | 8 |
| Hot corners | Bottom left corner opens Start, bottom right shows the desktop | On |
| Diagnostic trace file | Writes a log to `%TEMP%\floating-dock-helpers.log` | Off |

## Troubleshooting

<details>
<summary><strong>The dock doesn't hide</strong></summary>

- Check that **Automatically hide the taskbar** is on (install step 6).
- A panel or menu may still be open. Click an empty part of the desktop or
  press Esc.
- Hovering over the dock keeps it visible. Move the mouse well above it.

</details>

<details>
<summary><strong>The dock doesn't appear at the bottom edge</strong></summary>

- Turn on *Reveal along the whole bottom edge* in Taskbar Auto-Hide Motion.
  Without it, the dock appears only when the mouse is directly below the dock
  or the tray.
- The dock never appears over a full screen window, such as a game or video.

</details>

<details>
<summary><strong>The dock and tray are misplaced or overlap</strong></summary>

- The Styler preset and the *Tray next to the dock* setting must match the
  same layout (see [Choose a layout](#choose-a-layout)).
- The presets are tuned for the 72 px height from `taskbar-icon-size.json`.

</details>

<details>
<summary><strong>Something stopped working after a Windows update</strong></summary>

Turn on *Diagnostic trace file* in both mods, reproduce the problem, and
attach `%TEMP%\taskbar-autohide-motion.log` and
`%TEMP%\floating-dock-helpers.log` to a
[new issue](https://github.com/jinSeong-P/windhawk-floating-dock/issues).
Turn the option off afterwards.

</details>

## Uninstall

1. In Windhawk, disable or remove *Taskbar Auto-Hide Motion* and
   *Floating Dock Helpers*.
2. Restore the settings you saved in install step 3 for Windows 11 Taskbar
   Styler and Taskbar height and icon size, or remove those two mods.
3. If you want, turn off **Automatically hide the taskbar** and set the
   taskbar alignment in Windows Settings again. The mod does not change the
   alignment back when it is removed.

## Known limitations

- Verified on Windows 11 build 26200, on the primary monitor, with the taskbar
  at the bottom. Other builds and multiple monitors have not been fully
  tested. Hot corners, the whole-edge reveal and the mac layout apply to the
  primary monitor.
- The one-panel-at-a-time rule is verified for the dock buttons. Keyboard
  shortcuts use Windows' own behavior.
- Battery or power savings have not been measured.

## For developers

<details>
<summary><strong>What the mods change on your PC</strong></summary>

- Both mods run inside `explorer.exe`. Floating Dock Helpers also runs in
  `ShellHost.exe`, which shows Quick Settings, to handle Esc on its subpages.
- Floating Dock Helpers writes the Windows taskbar alignment setting
  (`TaskbarAl` under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced`) when
  the layout setting differs from it.
- The two mods share state through window properties on the taskbar. If the
  Motion mod is missing or can't find its hook, Helpers keeps Windows' own
  panel behavior.

</details>

<details>
<summary><strong>How it works</strong></summary>

- The slide animates the taskbar's own XAML content with the Composition API.
  The native window moves only while the content is off screen or
  transparent, so there are no flashes between the two.
- Taskbar Styler re-applies the margins and alignment it styles, so the mac
  layout moves the dock and the tray with a RenderTransform. Styler's
  click-through region is measured with `TransformToVisual`, which sees a new
  transform only after it has been rendered; the helpers wait for that and
  then trigger a layout pass so the region never cuts the dock.
- The whole-edge reveal calls Windows' own `TrayUI::Unhide` with the arguments
  of a mouse reveal when the cursor rests on the bottom edge outside the
  click-through region.
- While a panel is open, Helpers holds the dock and Motion postpones Windows'
  own hide request; when the last panel closes, that hide resumes. Panels are
  tracked from window events, with no periodic scan while idle.

</details>

- Verification reports: [1.0.7](docs/verification-1.0.7.txt),
  [1.0.6](docs/verification-1.0.6.txt), [1.0.5](docs/verification-1.0.5.txt)
- Code style: [docs/code-style.txt](docs/code-style.txt) and
  [.clang-format](.clang-format)

## Credits

Patterns for intercepting `TrayUI::SlideWindow`, the auto-hide timers and
retrieving the taskbar's XamlRoot are adapted from GPLv3 mods in
[ramensoftware/windhawk-mods](https://github.com/ramensoftware/windhawk-mods).
Thanks to m417z for Windhawk and Windows 11 Taskbar Styler.

## License

[GPL-3.0](LICENSE)
