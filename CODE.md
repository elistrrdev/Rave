\# KEEP THIS FILE UP TO DATE WITH CODE TREE, IN WHAT FILES YOU CAN FIND WHAT CODE AND WHAT CODE WHAT DOES

# Rave code map

Rave is a Roblox launcher written in C++ (Win32 + DirectX 11 + Dear ImGui). It always downloads the latest
Roblox client into its own folder and starts it from there.

## Build

- Requirements: Visual Studio Build Tools 2022 (MSVC v143, Windows 10/11 SDK). Nothing else.
- Run `build.bat` in the repo root. It builds `Rave.sln` as **Release | x64** and copies:
  - `out\Release\Rave.exe` to `builds\test\Test-Rave.exe` (portable, single exe, static CRT, fonts and logo embedded)
  - `out\Release\Rave-Installer.exe` to `builds\installer\Rave-Installer.exe` (embeds Rave.exe + LICENSE)
- `out\` holds compiler output (`out\obj\<project>\<config>\` for intermediates). `out\` and `builds\` are git ignored.
- The Installer project depends on the Rave project inside the solution, so Rave always builds first.

## Tree

```
.rave
|-- AI.md                  rules for the AI
|-- CODE.md                this file
|-- PLAN.md                roadmap and status
|-- LICENSE                Rave End User License Agreement and Terms of Use + Appendix A (third-party notices)
|-- build.bat              release build + copy into builds\
|-- Rave.sln               solution: Rave + Installer
|-- assets\                logos and icons (used by both projects)
|   |-- logo.png           logo without background: in-app header logo (embedded as RCDATA 3)
|   |-- logo-bg.png        logo with background and rounded corners: source of logo-bg.ico
|   |-- logo.ico           made from logo.png: Rave window/taskbar icon, installer taskbar icon, shortcut icon
|   |-- logo-bg.ico        made from logo-bg.png: .exe file icon of Rave.exe, Test-Rave.exe, Rave-Installer.exe,
|                          installer title bar icon and installer header image
|-- Rave\                  the launcher
|   |-- Rave.vcxproj
|   |-- rave.rc            icons (1 = logo-bg.ico, 2 = logo.ico), logo.png (RCDATA 3), version info
|   |-- rave.manifest      supportedOS (Windows 10/11), merged with the asInvoker UAC manifest
|   |-- version.hpp        single source of the version, name, publisher, public releases repo url + release API
|                          (shared with Installer: the url is the Apps and Features website/help link)
|   |-- main.hpp           shared ImGui helpers, window size, fonts, tabs enum
|   |-- main.cpp           WinMain, startup order, D3D11 device, message loop, WIC logo loader
|   |-- ui.cpp             pages: Home, Launch, State, header (tabs, logo, close), FFlags editor and Settings popups,
|                          accent tinting of the header logo
|   |-- custom\            ImGui widgets
|   |   |-- colors.hpp     color palette, setAccent() (Main + derived hover color), color helpers
|   |   |-- custom.hpp     widget declarations, logType enum
|   |   |-- misc.cpp       window begin, style, blur, rounded corners, child boxes, announcement, label/value rows
|   |   |-- button.cpp     buttons, choice buttons (segmented options) and accent swatches
|   |   |-- tab.cpp        tab labels in the header
|   |   |-- closebutton.cpp close X in the header (drawn with two lines, no icon font)
|   |   |-- input.cpp      single line text input (not used on a page right now)
|   |   |-- logs.cpp       thread safe log list shown on the Launch page (memory only, never written to disk)
|   |-- core\              non UI logic
|   |   |-- net.*          WinHTTP GET into memory with progress, internet check
|   |   |-- json.*         small JSON parser (Roblox version API, GitHub release API, FFlags validation)
|   |   |-- inflate.*      raw DEFLATE decoder (used by zip)
|   |   |-- zip.*          zip reader/extractor with CRC32 check and path traversal guard
|   |   |-- paths.*        %LOCALAPPDATA%\Rave, Versions folder, %TEMP%\rave-temp, folder helpers
|   |   |-- system.*       admin check (crash report), Windows version, unelevated launch, process lookup, md5
|   |   |-- roblox.*       latest version lookup, package download/verify/extract, launch, running check
|   |   |-- launcher.*     background launch worker, Roblox watcher (start behavior, "closed" log), latest version state
|   |   |-- fflags.*       FFlags storage (fflags.json), validation, writing ClientAppSettings.json
|   |   |-- settings.*     settings.json (start behavior, accent color) and the accent palette list
|   |   |-- news.*         latest GitHub release for the Home page
|   |   |-- report.*       crash handler (minidump + txt) and bug reports, both in %TEMP%\rave-temp
|   |-- __ext\             third party: Dear ImGui 1.91.3 (with shadows), FreeType (win64 static lib), fonts.h (Montserrat Medium + SemiBold)
|-- Installer\             Rave-Installer.exe
    |-- Installer.vcxproj
    |-- installer.cpp      setup wizard window and message boxes (own drawing, light/dark), install steps, uninstaller
    |-- installer.rc       payload (Rave.exe), LICENSE, icons, version info
    |-- installer.manifest common controls v6, DPI aware (requireAdministrator comes from the project)
    |-- resource.h         resource ids
```

## Startup order (main.cpp)

1. `report::installCrashHandler()` - unhandled exceptions write a crash report (see below).
2. `net::hasInternet()` - Network List Manager says "disconnected" or both probes fail
   (`msftconnecttest.com/connecttest.txt`, then the Roblox version API) -> `ExitProcess(0)`.
   No window, no message, no files. This is the "silent fail".
3. Single instance mutex `Local\RaveSingleInstance`. A second start focuses the existing window and exits.
4. `%LOCALAPPDATA%\Rave` is created and `settings.json` is loaded.
5. Window, D3D11, ImGui, then `ui::BeforeLoop()` applies the accent, loads fonts and the logo, starts the launcher
   monitor and the news request.
6. While the window is hidden or minimized the loop only handles messages and sleeps 50 ms (no rendering).

## Folders and files Rave creates

- `%LOCALAPPDATA%\Rave` - created on every start.
- `%LOCALAPPDATA%\Rave\fflags.json` - the FFlags from the editor, exactly as typed (a JSON object).
- `%LOCALAPPDATA%\Rave\settings.json` - `{ "startBehavior": "keep_open" | "minimize" | "hide", "accent": "#rrggbb" }`.
  Missing or broken values fall back to keep_open and #6addbb.
- `%LOCALAPPDATA%\Rave\Versions\version-<hash>\` - one Roblox client. A version counts as installed only when
  `AppSettings.xml` and `RobloxPlayerBeta.exe` exist (AppSettings.xml is written last).
- `%LOCALAPPDATA%\Rave\Versions\version-<hash>\ClientSettings\ClientAppSettings.json` - the FFlags for Roblox.
- `%TEMP%\rave-temp` - only created when a crash report or bug report is written:
  - `rave-crash-<date>-<time>.dmp/.txt` from the crash handler, then a message box + the folder opens.
  - `rave-bug-<date>-<time>.txt` when a Roblox version check, install or launch fails (logged on the Launch page).
- No log files are ever written. The Launch page log lives in memory only.

## UI (ui.cpp)

- No animations at all: no intro titles, no arrow button, no page fade, no hover fades, no log fade in.
  Clicking a tab switches the page in the same frame. Widgets draw their hover/active colors directly.
- Header: tabs Home / Launch / State at (20, 40), the logo centered at the top (30 px high, made from
  logo.png: cropped to its visible pixels and scaled with WIC high quality cubic), close icon on the right.
- Every page is one box at (20, 100), size (window width - 40) x 347.
- **Home**: "WHAT'S NEW?" box. `news::fetchAsync()` reads
  `https://api.github.com/repos/elistrrdev/Rave/releases/latest`, the public releases-only repo (the source lives in
  the private `elistrrdev/Rave-SRC`, which the API can not read without a token). A release shows its name, date and body
  (simple markdown is stripped, em/en dashes become `-`, long text wraps and scrolls with the mouse wheel).
  No release (404) or no answer shows "no news" centered in the box.
- **Launch**: log box + buttons.
  - The log keeps the last 8 lines (font 17 px, spacing 7 px, so the last line with g/y/p fits the 190 px box).
  - LAUNCH ROBLOX: check latest version -> install/update if needed -> write FFlags -> start Roblox.
    The button label shows the current step and the install percentage.
  - FFLAGS EDITOR: opens the FFlags editor popup (see below).
  - SETTINGS: opens the Settings popup (see below).
- **State**: three label/value rows (label in Main, value in Lwhite): Rave Version, Windows, Latest Roblox.
  The latest Roblox version is fetched on start and every 10 minutes by the launcher monitor.
- Bottom right: "Rave V0.1" (from version.hpp).

## FFlags editor (ui.cpp namespace `editor` + core/fflags.cpp)

- An ImGui modal popup (520 x 360, centered, rounded). The rest of Rave is dimmed and blocked while it is open.
  It is drawn inside the Rave window, there is no extra OS window and no close button.
- Top left "FFLAGS EDITOR", top right EXPORT FFLAGS and IMPORT FFLAGS, a multiline text box with an example
  hint while empty, a status line, and SAVE at the bottom.
- EXPORT copies the text box to the clipboard. IMPORT replaces the text box with the clipboard text, but only
  if it is a valid JSON object.
- SAVE and ESC both save and close. ESC is checked before the text box runs, because ImGui would otherwise
  revert the text on ESC. Invalid JSON keeps the popup open and shows the error in red. Empty text is allowed.
  If the text is the same as when the popup opened, it just closes (no write, no "FFlags saved" log).
- Saving writes `%LOCALAPPDATA%\Rave\fflags.json` and, if a Roblox version is installed,
  `<version>\ClientSettings\ClientAppSettings.json`. LAUNCH ROBLOX writes that file again before every start,
  so a freshly downloaded Roblox version also gets the FFlags. Roblox reads this file when it starts.

## Settings popup (ui.cpp namespace `prefs` + core/settings.cpp)

- Same popup look as the FFlags editor (520 x 360, no close button, SAVE at the bottom, ESC saves and closes).
- BEHAVIOR ON START: KEEP OPEN / MINIMIZE / HIDE (segmented buttons, the selected one is filled with the accent).
  It is used when Roblox starts from LAUNCH ROBLOX:
  - KEEP OPEN: nothing happens.
  - MINIMIZE: Rave minimizes itself.
  - HIDE: Rave hides its window (also from the taskbar). When Roblox closes, the window comes back exactly as it
    was (same position, same page, same log), because it was only hidden.
  Starting Rave again while it is hidden also shows it again.
- ACCENT COLOR: 8 swatches (teal #6addbb, blue #6aa8dd, purple #9f8ae6, pink #dd6ab4, red #dd6a6a,
  orange #dd9a6a, yellow #ddcc6a, green #8edd6a). Each swatch shows the palette it creates: main color,
  hover color, and the soft (35 percent) color. Clicking one applies it right away (preview), SAVE/ESC keeps it.
- Only writes settings.json and logs "Settings saved" if something changed.

## Roblox watcher (core/launcher.cpp)

- After LAUNCH ROBLOX started Roblox, the monitor thread checks once per second whether `RobloxPlayerBeta.exe` runs.
- The first time it sees Roblox running it applies the start behavior (through `WM_RAVE_WINDOW` posted to the
  Rave window, handled in main.cpp WndProc).
- When Roblox is gone for 2 checks in a row it logs "Roblox was closed" and, if Rave hid itself, shows the window again.
- If Roblox never shows up within 60 seconds the watch stops.

## Icons

- `.exe` file icon (Explorer, file properties, Apps and Features): the first icon group, id 1 = `logo-bg.ico`.
  Same for the installer.
- Rave window and taskbar icon: id 2 = `logo.ico`, set as class icon and with `WM_SETICON`.
- Installer: taskbar (big icon) = `logo.ico`, title bar (small icon) and the header image = `logo-bg.ico`.
- Start Menu / Desktop shortcuts from the installer: `IShellLink::SetIconLocation(Rave.exe, -2)`, which means
  "icon resource id 2" inside Rave.exe, so the shortcuts show `logo.ico`.
- The .ico files are generated from the PNGs (sizes 16, 20, 24, 32, 40, 48, 64, 128, 256, PNG compressed,
  resized with GDI+ high quality bicubic). logo.ico uses a square crop around the visible part of logo.png so the
  R fills the icon. logo-bg.ico uses the whole image. When a PNG changes, the matching .ico has to be generated again.

## Roblox install (core/roblox.cpp)

1. `https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer` -> `clientVersionUpload` (version-hash).
2. `https://setup.rbxcdn.com/<version-hash>-rbxPkgManifest.txt` -> lines `v0`, then name / md5 / packed size / unpacked size.
3. Every `.zip` package is downloaded into memory by up to 4 threads, checked (size + md5, 3 tries),
   and extracted into the folder from `kMappings` (same layout the official installer uses, e.g.
   `content-textures3.zip` -> `PlatformContent\pc\textures`). Unknown zips go into the version root.
   `RobloxPlayerInstaller.exe` from the manifest is skipped. No temp files are used for downloads.
4. `AppSettings.xml` is written, older version folders are removed (locked ones are skipped).
5. Start: `RobloxPlayerBeta.exe --app` with the version folder as working dir, through `sys::launchUnelevated()`.
   Normally Rave runs unelevated and this is a plain CreateProcess. Only if someone starts Rave as administrator
   by hand, Roblox is started through the Explorer shell (`IShellDispatch2::ShellExecute` on the desktop window)
   so it still runs **unelevated** as a normal Roblox would (CreateProcess is the fallback).
6. Progress = half download bytes, half extracted bytes.

## Window corners

- Windows 11 (build 22000+): DWM rounded corners (`DWMWA_WINDOW_CORNER_PREFERENCE = round`).
- Windows 10: DWM can not round windows, so Rave sets a rounded window region with the same 8 px radius
  (`ApplyCornerRegion` in misc.cpp, updated when the window size changes). The blur behind stays the same.

## Colors (custom/colors.hpp, misc.cpp SetStyle)

- `Main` #6addbb (106, 221, 187): accents, active tab, warn logs, row labels.
- `Second` #40d6aa: button hover.
- Gray, Lwhite, Green, Red, Info, Bg (black at 20 percent), itemBg (black at 30 percent).
- `colors::setAccent(rgb)` sets `Main` and computes `Second` (HSV saturation x 1.35, value x 0.97). All widgets read
  `colors::Main` / `colors::Second` while drawing, so a new accent shows up in the next frame everywhere.
- The header logo is tinted to the accent: the decoded logo.png pixels are kept in memory, every pixel keeps its
  alpha and its brightness relative to teal, and the texture is rebuilt at the start of the next frame. With the
  default teal the original pixels are used unchanged.
- FFlags popup: PopupBg (16, 18, 18), border Main at 16 percent, dim behind it black at 55 percent,
  text box white at 5 percent, text selection Main at 35 percent.

## Third party changes

- `__ext\ImGui\imgui_widgets.cpp` includes `main.hpp` and has a custom scrollbar look. Its grab position is
  set directly (no animation). Password inputs use the stock ImGui `*` glyph.
- `__ext\fonts.h` only contains Montserrat Medium and Montserrat SemiBold. The old icon font was removed because its
  license was unknown; the close X is drawn by `items::closeButton`.

## Installer (Installer/installer.cpp)

- Asks for admin (requireAdministrator) because it writes to Program Files, HKLM and the all users shortcuts.
- Wizard pages like the OBS Studio (NSIS) setup: Welcome -> License (Rave terms from `LICENSE`, must accept) ->
  Install Location (default `C:\Program Files\Rave`, or the previous install path) -> Additional Tasks
  (Start Menu shortcut, Desktop shortcut) -> Installing (progress + details) -> Finish (Launch Rave checkbox).
- It is one normal window (500 x 386 at 100 percent, scaled by the system DPI) that draws the Wizard97 layout
  itself: side image on Welcome/Finish, header with title, subtitle and logo-bg on the other pages, the
  progress bar, the "Destination Folder" frame and all texts. Real Win32 controls are only used for the license
  text, the path box, the details list and the buttons. The checkboxes are a small own control (`RaveSetupCheck`,
  teal when checked) so they can be colored in both themes. Tab, Enter (Next) and Esc (Cancel) work through
  IsDialogMessage.
- Light/dark: reads `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`
  (high contrast forces light). Dark uses the dark title bar (DWMWA_USE_IMMERSIVE_DARK_MODE), the
  `DarkMode_Explorer` / `DarkMode_CFD` control themes (enabled through the uxtheme dark mode exports on
  Windows 10 1903+), and dark colors for everything drawn. Changing the Windows setting while Setup is open
  switches it live (WM_SETTINGCHANGE "ImmersiveColorSet").
- The license text box never selects the whole text: its WM_GETDLGCODE drops DLGC_HASSETSEL and the caret is
  put at the start when the page opens. It has no Windows border (Windows 11 draws a blue focus line in that
  border), Setup draws its own 1 px frame around it instead, and the text has 6/4 px inner padding (EM_SETRECTNP).
  The page focuses the accept checkbox; Page Up / Page Down still scroll the license (EM_SCROLL).
- The own checkboxes only draw their dotted focus box after the keyboard was used for navigation
  (Tab or arrow keys set `keyboardCues`), like normal Windows controls. Mouse users never see it.
- Message boxes (cancel question, errors, Rave running, all uninstall questions) are an own window
  (`message()`), not MessageBoxW, so they follow the same light/dark theme, dark title bar and dark buttons.
  It supports OK, OK/Cancel, Yes/No, the question/warning/error/information icons and MB_DEFBUTTON2.
- The side image is drawn at runtime with logo.ico on a dark teal gradient.
- Writes `Rave.exe`, `LICENSE.txt` and `uninstall.exe` (a copy of the installer), shortcuts, `HKLM\SOFTWARE\Rave` and the
  Apps and Features entry `HKLM\...\Uninstall\Rave`.
- If Rave is running it asks to close it first.
- "Launch Rave" on the finish page starts Rave unelevated.
- Uninstall (`uninstall.exe` or `/uninstall`): copies itself to `%TEMP%\Rave-Uninstall-<ticks>.exe` (needed,
  because a running exe can not delete itself), runs from there, removes files, shortcuts and registry keys, asks
  whether `%LOCALAPPDATA%\Rave` should be removed too. At the end it starts a hidden
  `cmd /c ping 127.0.0.1 -n 3 & del` that deletes the temp copy about 2 seconds later. A delete-on-reboot is
  registered as a fallback.

## License and third-party notices

- Rave is proprietary: "Copyright (c) 2026 elistrrdev. All rights reserved." (also in the VERSIONINFO of both exes).
  The GitHub repo is private and only used as a backup.
- `LICENSE` is the Rave End User License Agreement and Terms of Use: definitions, acceptance, ownership, license grant
  (personal, non-commercial), restrictions (no sharing, no reverse engineering, no modified builds, no cheats), administrator rights
  (Rave does not ask for them, only Setup and the uninstaller do), Roblox (not affiliated, official client
  from Roblox servers, Roblox Terms of Use, FFlags at own risk), data and privacy (exact network hosts and local files of
  V0.1), updates, test versions, feedback, support, termination, warranty disclaimer, liability, indemnity, laws,
  consumer rights, governing law, third-party components, general terms, contact.
- Appendix A lists every third-party part: Dear ImGui 1.91.3 (MIT, full text), stb_truetype / stb_rect_pack /
  stb_textedit (public domain or MIT), FreeType 2.12.1 (FreeType License credit line), Montserrat (SIL OFL 1.1, full
  text), and a note about the Windows APIs and the Visual C++ runtime.
- The installer embeds `LICENSE` (IDR_LICENSE), shows it on the License page and writes it as `<install dir>\LICENSE.txt`.
  The uninstaller removes LICENSE.txt again.
- The text uses one line per paragraph so the installer text box can wrap it. Plain ASCII only.
- When third-party material changes, Appendix A has to change in the same commit.

## Admin rights

- Rave.exe never asks for admin. It is `asInvoker` (no shield on the icon) and has no UAC request in code.
  Everything it does (Roblox install in %LOCALAPPDATA%, FFlags, settings, process watch) works with normal rights.
- Only the installer and uninstaller ask for admin (requireAdministrator).
- `sys::isElevated()` is only used for the "Administrator: yes/no" line in crash and bug reports.

## Rules that affect code

- No comments in code (see AI.md). Explanations belong here.
- The version lives only in `Rave/version.hpp`. Do not change it without the user.
