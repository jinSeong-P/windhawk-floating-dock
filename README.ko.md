# windhawk-floating-dock

[English](README.md)

OLED 화면에 부담을 주지 않는, 떠 있는 자동 숨김 Windows 11 작업 표시줄입니다.
둥근 독과 트레이가 부드럽게 올라오고, 도착할 때 살짝 커졌다 돌아오며, 숨어 있을
때는 화면에 아무것도 남기지 않습니다. [Windhawk](https://windhawk.net) 모드 두 개와
[Windows 11 Taskbar Styler](https://windhawk.net/mods/windows-11-taskbar-styler)
프리셋으로 구성됩니다.

![작업 표시줄이 올라와 도착할 때 살짝 커지고 다시 숨는 모습](docs/reveal.gif)

| 구성 | 하는 일 |
| --- | --- |
| [Taskbar Auto-Hide Motion](mods/taskbar-autohide-motion.wh.cpp) | 자동 숨김 작업 표시줄의 부드러운 슬라이드와 팝 애니메이션, 숨었을 때 완전히 투명 (화면에 1-2 px 줄이 남지 않음), 아래 가장자리 어디서나 나타나기, 지연 시간 설정. 작업 표시줄 스타일과 관계없이 동작합니다. |
| [Floating Dock Helpers](mods/floating-dock-helpers.wh.cpp) | macOS식 배치 (가운데 독 바로 옆에 트레이), 시작/바탕 화면 보기 핫 코너, 옮겨진 트레이 위에서 열리는 빠른 설정. |
| [Styler 프리셋](styler-presets) | 떠 있는 독 자체: 주변에 여백이 있는 둥근 독과 트레이, 클릭이 통과하는 빈 공간. |

## 배치

| 배치 | Windows 작업 표시줄 맞춤 | Styler 프리셋 | Helpers: *Tray next to the dock* |
| --- | --- | --- | --- |
| **mac** (독과 트레이를 나란히 붙여 함께 가운데) | 가운데 | `taskbar-styler-mac.json` | 켬 |
| **left** (독은 왼쪽 끝, 트레이는 오른쪽 끝) | 왼쪽 | `taskbar-styler-left.json` | 끔 |

**mac**

![mac 배치](docs/layout-mac.png)

**left**

![left 배치](docs/layout-left.png)

## 요구 사항

- Windows 11. **25H2 (빌드 26200), 배율 175%**, Windhawk 1.7.3에서
  테스트했습니다. 다른 빌드에서도 동작할 수 있으며, 작업 표시줄 내부 요소를
  찾지 못한 기능은 작업 표시줄을 망가뜨리지 않고 꺼집니다.
- 아래쪽 작업 표시줄. 핫 코너, 가장자리 전체에서 나타나기, mac 배치는 주
  모니터에 적용됩니다.

## 설치

1. [Windhawk](https://windhawk.net)를 설치합니다.
2. Windhawk의 *탐색*에서 **Windows 11 Taskbar Styler**와 **Taskbar height and
   icon size**를 설치합니다.
3. 프리셋을 적용합니다. 두 모드 각각에서 **고급 > 모드 설정**에 프리셋 파일
   내용을 붙여 넣고 **저장**을 누릅니다.
   - Taskbar Styler: `taskbar-styler-mac.json` (mac) 또는
     `taskbar-styler-left.json` (left).
   - Taskbar height and icon size: `taskbar-icon-size.json` (작업 표시줄 72 px,
     아이콘 28 px. Styler 여백이 이 높이에 맞춰져 있습니다).

   **저장하면 그 모드의 설정 전체가 바뀝니다.** 되돌리고 싶다면 먼저
   **불러오기**를 눌러 나온 내용을 보관해 두세요.
4. 두 모드를 설치합니다. [mods](mods)의 `.wh.cpp` 파일마다 Windhawk에서
   **새 모드 만들기**를 누르고, 템플릿 전체를 파일 내용으로 바꾼 뒤
   **모드 컴파일**, **편집 모드 나가기**를 누릅니다.
5. 배치에 맞게 Windows 맞춤을 설정합니다 (설정 > 개인 설정 > 작업 표시줄 >
   작업 표시줄 동작 > 작업 표시줄 맞춤). left 배치라면 Floating Dock
   Helpers의 *Tray next to the dock*을 끕니다.
6. 같은 화면에서 **작업 표시줄 자동 숨기기**를 켭니다.

두 프리셋 모두 시작 버튼 안쪽 여백을 고정합니다. Windows는 이 여백을 Explorer가
시작될 때 정하고 왼쪽 맞춤이면 왼쪽을 10 DIP 넓히기 때문에, 고정하지 않으면
로그아웃 후에야 독 여백이 맞습니다. 고정해 두었으므로 배치를 바꾸면 바로
적용됩니다.

## 설정

**Taskbar Auto-Hide Motion**

- *Motion profile*: Smooth, Expressive (지나쳤다 돌아오기), Expressive scale
  (도착할 때 팝, 기본값).
- 나타나기/숨기기 시간, 지나치는 거리, 팝 크기, 팝 스프링 시간과 탄성.
- *Transparent while hidden*: Windows가 화면에 남기는 줄을 숨깁니다.
- 나타나기/숨기기 지연.
- *Reveal along the whole bottom edge*: 클릭 통과 옵션을 쓸 때 필요합니다.
  이 옵션이 없으면 독과 트레이 아래에서만 작업 표시줄이 나타납니다.
- *Respect Windows animation effects*.

**Floating Dock Helpers**

- *Tray next to the dock (macOS style)*과 둘 사이 간격.
- *Hot corners*: 왼쪽 아래 구석을 클릭하면 시작, 오른쪽 아래는
  바탕 화면 보기. 크기는 독과 트레이 위치에서 계산하며, 작업 표시줄이 보이고
  화면 가장자리에서 떠 있을 때만 동작합니다.

두 모드 모두 *Diagnostic trace file* 옵션으로 동작 기록을 `%TEMP%`
(`taskbar-autohide-motion.log`, `floating-dock-helpers.log`)에 남길 수 있습니다.
버그를 제보할 때 첨부해 주세요.

## 제거

Windhawk에서 두 모드를 끄거나 삭제합니다. Taskbar Styler와 Taskbar height and
icon size는 이전 설정을 다시 저장하거나 (3단계) 모드를 삭제합니다.

## 크레딧

`TrayUI::SlideWindow` 가로채기, 자동 숨김 타이머, 작업 표시줄 XamlRoot 가져오기
방식은 [ramensoftware/windhawk-mods](https://github.com/ramensoftware/windhawk-mods)의
GPLv3 모드에서 가져와 고쳐 썼습니다. Windhawk와 Windows 11 Taskbar Styler를
만든 m417z에게 감사드립니다.

## 라이선스

[GPL-3.0](LICENSE)
