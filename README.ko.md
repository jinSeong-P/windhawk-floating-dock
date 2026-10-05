# windhawk-floating-dock

[English](README.md) | **한국어**

Windows 11 작업 표시줄을 스스로 숨는 떠 있는 독으로 바꿉니다.
화면 아래 끝에 마우스를 가져가면 둥근 독과 트레이가 부드럽게 올라와 살짝
튀어 오르며 자리를 잡고, 숨을 때는 화면에 아무것도 남기지 않습니다. 그래서
OLED 화면의 번인 걱정도 덜 수 있습니다.

![떠 있는 독이 화면 아래 끝에서 올라와 살짝 튀어 오르며 자리를 잡은 뒤, 다시 내려가 화면에 아무것도 남지 않는 모습](docs/reveal.gif)

현재 버전: **1.0.7** · Windows 11 · [변경 기록](CHANGELOG.md) ·
[GPL-3.0](LICENSE)

## 목차

- [주요 기능](#주요-기능)
- [시작하기 전에](#시작하기-전에)
- [설치](#설치)
- [배치 고르기](#배치-고르기)
- [사용 방법](#사용-방법)
- [설정](#설정)
- [문제 해결](#문제-해결)
- [제거](#제거)
- [알려진 제한](#알려진-제한)
- [개발자 정보](#개발자-정보)

## 주요 기능

- **떠 있는 독.** 앱 아이콘과 시스템 트레이가 주변에 여백이 있는 둥근 막대로
  바뀝니다. 작업 표시줄의 빈 공간을 클릭하면 뒤에 있는 창이 클릭됩니다.
- **부드러운 자동 숨김.** 작업 표시줄이 툭 나타나지 않고 미끄러지듯 오르내리며,
  화면 아래 끝 어디에서나 불러낼 수 있습니다.
- **흔적 없는 숨김.** Windows는 숨긴 작업 표시줄을 1-2 px 정도 화면에 남기지만,
  이 프로젝트는 완전히 감춥니다.
- **두 가지 배치.** *mac*: 독과 트레이를 나란히 붙여 가운데에 둡니다.
  *left*: 독은 왼쪽, 트레이는 오른쪽 끝에 둡니다.
- **깔끔한 패널 전환.** 시작·검색·빠른 설정·알림·숨김 아이콘 패널이 한 번의
  클릭으로 서로 바뀌고, 패널을 쓰는 동안 독이 계속 보입니다.
- **핫 코너.** 독이 화면 구석까지 닿지 않아도, 왼쪽 아래 구석을 클릭하면 시작,
  오른쪽 아래 구석을 클릭하면 바탕 화면 보기가 실행됩니다.

프로젝트는 세 부분으로 이루어져 있고, 떠 있는 독을 쓰려면 셋 다 필요합니다.

| 구성 | 하는 일 |
| --- | --- |
| [Taskbar Auto-Hide Motion](mods/taskbar-autohide-motion.wh.cpp) | 슬라이드와 팝 애니메이션, 완전히 숨기기, 아래 가장자리 어디서나 나타나기. 이 모드만 따로 써도 어떤 작업 표시줄 스타일과도 동작합니다. |
| [Floating Dock Helpers](mods/floating-dock-helpers.wh.cpp) | mac 배치, 핫 코너, 빠른 설정 위치 맞춤, 패널 동작. |
| [Styler 프리셋](styler-presets) | 독의 모양: 둥근 독과 트레이, 여백, 클릭이 통과하는 빈 공간. |

## 시작하기 전에

- 작업 표시줄이 화면 **아래쪽**에 있는 **Windows 11**.
  25H2(빌드 26200), 배율 175%, Windhawk 1.7.3에서 테스트했습니다.
- **[Windhawk](https://windhawk.net)**: *모드*라는 작은 확장 기능으로 Windows
  프로그램을 꾸미는 무료 도구입니다.
- 이 프로젝트의 두 모드는 Windhawk 카탈로그에 없습니다. 아래 4단계처럼 소스 코드를
  붙여 넣어 로컬 모드로 추가합니다. 프로그래밍 지식은 필요 없습니다.

> [!NOTE]
> 다른 Windows 11 빌드에서도 동작할 수 있습니다. 작업 표시줄 안에서 필요한
> 요소를 찾지 못한 기능은 작업 표시줄을 망가뜨리지 않고 스스로 꺼집니다.

## 설치

1. **[Windhawk](https://windhawk.net)를 설치합니다.**
2. **Windhawk 카탈로그에서 모드 두 개를 설치합니다.** Windhawk의 *탐색*(Explore)에서
   다음을 설치합니다.
   - **Windows 11 Taskbar Styler**
   - **Taskbar height and icon size**
3. **프리셋을 적용합니다.** 먼저 [배치](#배치-고르기)를 고릅니다. 그다음 2단계의
   두 모드 각각에서 **고급 > 모드 설정**(Advanced > Mod settings)을 열고, 내용을
   프리셋 파일 내용으로 바꾼 뒤 **저장**(Save)을 누릅니다.

   | 모드 | 프리셋 파일 |
   | --- | --- |
   | Windows 11 Taskbar Styler | mac은 [`taskbar-styler-mac.json`](styler-presets/taskbar-styler-mac.json), left는 [`taskbar-styler-left.json`](styler-presets/taskbar-styler-left.json) |
   | Taskbar height and icon size | [`taskbar-icon-size.json`](styler-presets/taskbar-icon-size.json) (작업 표시줄 72 px, 아이콘 28 px. 독 여백이 이 높이에 맞춰져 있습니다) |

   **주의: 저장하면 그 모드의 설정 전체가 바뀝니다.** 지금 설정을 보관하려면
   먼저 **불러오기**(Load)를 눌러 나온 내용을 따로 저장해 두세요.

4. **이 프로젝트의 모드 두 개를 추가합니다.** [mods](mods)의 파일마다:
   1. GitHub에서 파일을 열고 내용 전체를 복사합니다
      (*Copy raw file* 버튼을 누르면 됩니다).
   2. Windhawk에서 **새 모드 만들기**(Create a New Mod)를 누릅니다.
   3. 템플릿 전체를 복사한 내용으로 바꿉니다.
   4. **모드 컴파일**(Compile Mod), **편집 모드 나가기**(Exit Editing Mode)를
      차례로 누릅니다.
5. **배치를 맞춥니다.** *Floating Dock Helpers* 설정에서 *Tray next to the dock*을
   mac이면 **켜고**, left면 **끕니다**. Windows 작업 표시줄 맞춤은 자동으로
   따라 바뀝니다.
6. **자동 숨김을 켭니다.** Windows 설정의 **개인 설정 > 작업 표시줄 >
   작업 표시줄 동작**에서 **작업 표시줄 자동 숨기기**를 켭니다.

## 배치 고르기

| 배치 | 모양 | Styler 프리셋 | *Tray next to the dock* |
| --- | --- | --- | --- |
| **mac** | 독과 트레이를 나란히 붙여 함께 가운데 | `taskbar-styler-mac.json` | 켬 |
| **left** | 독은 왼쪽 끝, 트레이는 오른쪽 끝 | `taskbar-styler-left.json` | 끔 |

**mac**

![mac 배치: 앱 아이콘과 시스템 트레이가 둥근 막대 두 개로 화면 가운데에 나란히 놓인 모습](docs/layout-mac.png)

**left**

![left 배치: 앱 아이콘은 왼쪽 끝의 둥근 막대에, 시스템 트레이는 오른쪽 끝의 둥근 막대에 놓인 모습](docs/layout-left.png)

나중에 배치를 바꾸려면 다른 프리셋으로 3단계와 5단계를 다시 하면 됩니다.
두 프리셋 모두 시작 버튼 안쪽 여백을 고정하므로, 로그아웃하지 않아도 바로
적용됩니다.

## 사용 방법

- **독 보이기:** 마우스를 화면 아래 끝으로 옮깁니다.
- **독 숨기기:** 마우스를 독에서 떼면 잠시 뒤 숨습니다.
- **패널은 한 번에 바뀝니다.** 시작 메뉴가 열린 상태에서 검색, 빠른 설정,
  시계(알림), 숨김 아이콘 화살표를 누르면 시작 메뉴가 닫히고 새 패널이 열립니다.
- **패널을 쓰는 동안 독이 유지됩니다.** 마우스를 밖으로 옮겨도 패널이 닫히거나
  독이 숨지 않습니다.
- **패널 닫기:** 패널 바깥을 클릭하거나 **Esc**를 누릅니다. 그러면 독도 평소처럼
  숨습니다.
- **Esc는 한 단계씩 돌아갑니다.** 빠른 설정의 Wi-Fi·Bluetooth·소리 출력 목록에서는
  주 화면으로 돌아가고, 트레이 아이콘 메뉴에서는 그 메뉴만 닫습니다.
- **핫 코너:** 독이 보일 때 왼쪽 아래 구석을 클릭하면 시작, 오른쪽 아래 구석을
  클릭하면 바탕 화면 보기가 실행됩니다.

Win+A, Win+N 같은 단축키로 패널을 열면 Windows 기본 동작을 따르므로, 패널이
하나만 열리도록 하는 규칙이 보장되지 않습니다. 작업 보기(Win+Tab)도 Windows의
기본 작업 표시줄 동작을 따릅니다.

## 설정

Windhawk에서 모드를 열고 **설정**(Settings)을 누르면 바꿀 수 있습니다.

**Taskbar Auto-Hide Motion**

| 설정 | 하는 일 | 기본값 |
| --- | --- | --- |
| Motion profile | *Smooth*, *Expressive*(지나쳤다 돌아오기), *Expressive scale*(도착할 때 살짝 튀어 오르기) | Expressive scale |
| Reveal / Hide duration | 애니메이션 길이(ms) | 260 / 200 |
| Expressive overshoot, scale pop, pop duration, pop bounce | Expressive 계열의 세부 조정 | 4 DIP, 1%, 550 ms, 15 |
| Transparent while hidden | Windows가 화면에 남기는 줄을 감춤 | 켬 |
| Reveal delay | 마우스가 가장자리에 머문 뒤 독이 나타나기까지의 시간(0 = Windows 기본값) | 0 ms |
| Hide delay | 마우스가 떠난 뒤 독이 숨기까지의 시간(0 = Windows 기본값, 약 500 ms) | 300 ms |
| Reveal along the whole bottom edge | 독과 트레이 아래뿐 아니라 아래 가장자리 어디서나 나타남. 전체 화면 창 위에서는 나타나지 않음 | 켬 |
| Respect Windows animation effects | Windows 애니메이션 효과가 꺼져 있으면 애니메이션 없이 나타남 | 켬 |
| Diagnostic trace file | `%TEMP%\taskbar-autohide-motion.log`에 기록을 남김 | 끔 |

**Floating Dock Helpers**

| 설정 | 하는 일 | 기본값 |
| --- | --- | --- |
| Tray next to the dock (macOS style) | 트레이를 가운데 독 옆에 붙임. Windows 맞춤도 켜면 가운데, 끄면 왼쪽으로 설정 | 켬 |
| Gap between dock and tray | 두 막대 사이 간격(DIP) | 8 |
| Hot corners | 왼쪽 아래 구석은 시작, 오른쪽 아래 구석은 바탕 화면 보기 | 켬 |
| Diagnostic trace file | `%TEMP%\floating-dock-helpers.log`에 기록을 남김 | 끔 |

## 문제 해결

<details>
<summary><strong>독이 숨지 않아요</strong></summary>

- **작업 표시줄 자동 숨기기**가 켜져 있는지 확인합니다(설치 6단계).
- 패널이나 메뉴가 아직 열려 있을 수 있습니다. 바탕 화면의 빈 곳을 클릭하거나
  Esc를 누릅니다.
- 마우스가 독 위에 있으면 독이 계속 보입니다. 마우스를 독보다 충분히 위로
  옮깁니다.

</details>

<details>
<summary><strong>화면 아래 끝에 마우스를 대도 독이 나타나지 않아요</strong></summary>

- Taskbar Auto-Hide Motion의 *Reveal along the whole bottom edge*를 켭니다.
  끄면 마우스가 독이나 트레이 바로 아래에 있을 때만 나타납니다.
- 게임이나 동영상 같은 전체 화면 창 위에서는 독이 나타나지 않습니다.

</details>

<details>
<summary><strong>독과 트레이 위치가 어긋나거나 겹쳐요</strong></summary>

- Styler 프리셋과 *Tray next to the dock* 설정이 같은 배치여야 합니다
  ([배치 고르기](#배치-고르기) 참고).
- 프리셋은 `taskbar-icon-size.json`의 72 px 높이에 맞춰져 있습니다.

</details>

<details>
<summary><strong>Windows 업데이트 뒤 기능이 동작하지 않아요</strong></summary>

두 모드에서 *Diagnostic trace file*을 켜고 문제를 재현한 뒤,
`%TEMP%\taskbar-autohide-motion.log`와 `%TEMP%\floating-dock-helpers.log`를
[새 이슈](https://github.com/jinSeong-P/windhawk-floating-dock/issues)에
첨부해 주세요. 확인이 끝나면 옵션을 다시 꺼 주세요.

</details>

## 제거

1. Windhawk에서 *Taskbar Auto-Hide Motion*과 *Floating Dock Helpers*를 끄거나
   삭제합니다.
2. Windows 11 Taskbar Styler와 Taskbar height and icon size는 설치 3단계에서
   보관한 설정을 다시 저장하거나, 두 모드를 삭제합니다.
3. 필요하면 Windows 설정에서 **작업 표시줄 자동 숨기기**를 끄고 작업 표시줄
   맞춤을 다시 고릅니다. 모드를 제거해도 맞춤은 원래대로 돌아가지 않습니다.

## 알려진 제한

- Windows 11 빌드 26200, 주 모니터, 아래쪽 작업 표시줄에서 검증했습니다. 다른
  빌드와 다중 모니터는 충분히 검증하지 않았습니다. 핫 코너, 가장자리 전체에서
  나타나기, mac 배치는 주 모니터에 적용됩니다.
- 패널이 하나만 열리는 규칙은 독 버튼으로 열 때 검증했습니다. 단축키로 열 때는
  Windows 기본 동작을 따릅니다.
- 배터리나 전력 절감 효과는 측정하지 않았습니다.

## 개발자 정보

<details>
<summary><strong>모드가 PC에서 바꾸는 것</strong></summary>

- 두 모드는 `explorer.exe` 안에서 동작합니다. Floating Dock Helpers는 빠른 설정
  하위 화면의 Esc를 처리하기 위해 빠른 설정을 띄우는 `ShellHost.exe`에서도
  동작합니다.
- Floating Dock Helpers는 배치 설정과 다를 때 Windows 작업 표시줄 맞춤 설정
  (`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced`의
  `TaskbarAl`)을 기록합니다.
- 두 모드는 작업 표시줄 창의 window property로 상태를 주고받습니다. Motion 모드가
  없거나 필요한 훅을 찾지 못하면 Helpers는 Windows 기본 패널 동작을 유지합니다.

</details>

<details>
<summary><strong>동작 원리</strong></summary>

- 슬라이드는 작업 표시줄 자체의 XAML 콘텐츠를 Composition API로 움직입니다.
  실제 창은 콘텐츠가 화면 밖에 있거나 투명할 때만 옮기므로, 둘이 바뀌는 순간
  깜빡임이 없습니다.
- Taskbar Styler는 자신이 지정한 여백과 맞춤을 다시 적용하므로, mac 배치는 독과
  트레이를 RenderTransform으로 옮깁니다. Styler의 클릭 통과 영역은
  `TransformToVisual`로 측정되는데, 새 변환은 렌더링된 뒤에야 반영됩니다. 그래서
  Helpers는 렌더링을 기다렸다가 레이아웃을 다시 계산하게 해, 클릭 통과 영역이 독을
  자르지 않게 합니다.
- 가장자리 전체에서 나타나기는 커서가 클릭 통과 영역 밖의 아래 끝에 머물면
  Windows 자체의 `TrayUI::Unhide`를 마우스로 불러낸 것과 같은 인자로 호출합니다.
- 패널이 열려 있는 동안 Helpers가 독을 고정하고, Motion은 Windows의 숨김 요청을
  미뤄 둡니다. 마지막 패널이 닫히면 그 숨김을 이어서 실행합니다. 패널은 창 이벤트로
  추적하며, 쓰지 않을 때는 주기적으로 검색하지 않습니다.

</details>

- 검증 보고서(영문): [1.0.7](docs/verification-1.0.7.txt),
  [1.0.6](docs/verification-1.0.6.txt), [1.0.5](docs/verification-1.0.5.txt)
- 코드 스타일: [docs/code-style.txt](docs/code-style.txt),
  [.clang-format](.clang-format)

## 크레딧

`TrayUI::SlideWindow` 가로채기, 자동 숨김 타이머, 작업 표시줄 XamlRoot 가져오기
방식은 [ramensoftware/windhawk-mods](https://github.com/ramensoftware/windhawk-mods)의
GPLv3 모드에서 가져와 고쳐 썼습니다. Windhawk와 Windows 11 Taskbar Styler를 만든
m417z에게 감사드립니다.

## 라이선스

[GPL-3.0](LICENSE)
