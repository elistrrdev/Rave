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
|   |-- version.hpp        single source of the version, name, publisher, public releases repo url
|                          (shared with Installer: the url is the Apps and Features website/help link)
|   |-- main.hpp           shared ImGui helpers, window size, fonts, Tab enum (Home, Accounts, Info)
|   |-- main.cpp           WinMain, startup order, D3D11 device, message loop, WIC image loader (logo, avatars, QR)
|   |-- ui.cpp             pages: Home, Accounts, Info, header (tabs, logo, close), FFlags editor, Settings and
|                          Add Account popups, avatar and QR textures, accent tinting of the header logo
|   |-- custom\            ImGui widgets
|   |   |-- colors.hpp     color palette, setAccent() (Main + derived hover color), color helpers
|   |   |-- custom.hpp     widget declarations, logType enum
|   |   |-- misc.cpp       window begin, style, blur, rounded corners, child boxes, label/value rows
|   |   |-- button.cpp     buttons, choice buttons (segmented options) and accent swatches
|   |   |-- tab.cpp        tab labels in the header
|   |   |-- closebutton.cpp close X in the header (drawn with two lines, no icon font)
|   |   |-- input.cpp      single line text input (not used on a page right now)
|   |   |-- logs.cpp       thread safe log list shown on the Home page (memory only, never written to disk)
|   |-- core\              non UI logic
|   |   |-- net.*          WinHTTP requests (any method, body, headers) into memory with progress, response header
|                          and Set-Cookie lookup, automatic cookies disabled, internet check
|   |   |-- json.*         small JSON parser + string quoting (Roblox APIs, FFlags validation, accounts.dat)
|   |   |-- inflate.*      raw DEFLATE decoder (used by zip)
|   |   |-- zip.*          zip reader/extractor with CRC32 check and path traversal guard
|   |   |-- paths.*        %LOCALAPPDATA%\Rave, Versions folder, %TEMP%\rave-temp, folder helpers
|   |   |-- system.*       admin check (crash report), Windows version, unelevated launch, open url in the default
|                          browser, process lookup, md5
|   |   |-- roblox.*       latest version lookup, package download/verify/extract, launch, running check
|   |   |-- launcher.*     background launch worker (signed in launch), Roblox watcher (start behavior, "closed" log),
|                          latest version state
|   |   |-- accounts.*     account manager: accounts.dat (DPAPI), Quick Login, cookie login, avatars, sign-in tickets
|   |   |-- sysinfo.*      CPU, GPU, RAM, display and the HardwareID for the Info page
|   |   |-- fflags.*       FFlags storage (fflags.json), validation, writing ClientAppSettings.json
|   |   |-- settings.*     settings.json (start behavior, accent color) and the accent palette list
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
5. Window, D3D11, ImGui, then `ui::BeforeLoop()` applies the accent, loads fonts and the logo, reads the system
   specs on a background thread, loads `accounts.dat` (and refreshes names, logins and avatars in the background)
   and starts the launcher monitor.
6. While the window is hidden or minimized the loop only handles messages and sleeps 50 ms (no rendering).

## Folders and files Rave creates

- `%LOCALAPPDATA%\Rave` - created on every start.
- `%LOCALAPPDATA%\Rave\fflags.json` - the FFlags from the editor, exactly as typed (a JSON object).
- `%LOCALAPPDATA%\Rave\settings.json` - `{ "startBehavior": "keep_open" | "minimize" | "hide", "accent": "#rrggbb" }`.
  Missing or broken values fall back to keep_open and #6addbb.
- `%LOCALAPPDATA%\Rave\accounts.dat` - only when an account was added. The added Roblox accounts and the selected one,
  as JSON (`{ "selected": "<id>", "accounts": [ { "id", "name", "displayName", "quick", "expired", "cookie" } ] }`)
  encrypted with DPAPI (CryptProtectData, current Windows user, entropy "Rave accounts v1"). Written to
  `accounts.dat.tmp` first and then moved over the old file. Never readable as plain text on disk.
- `%LOCALAPPDATA%\Rave\Versions\version-<hash>\` - one Roblox client. A version counts as installed only when
  `AppSettings.xml` and `RobloxPlayerBeta.exe` exist (AppSettings.xml is written last).
- `%LOCALAPPDATA%\Rave\Versions\version-<hash>\ClientSettings\ClientAppSettings.json` - the FFlags for Roblox.
- `%TEMP%\rave-temp` - only created when a crash report or bug report is written:
  - `rave-crash-<date>-<time>.dmp/.txt` from the crash handler, then a message box + the folder opens.
  - `rave-bug-<date>-<time>.txt` when a Roblox version check, install or launch fails (logged on the Home page).
    A failed sign-in is only logged, never written to a bug report.
- No log files are ever written. The Home page log lives in memory only. Cookies and tickets are never logged.

## UI (ui.cpp)

- No animations at all: no intro titles, no arrow button, no page fade, no hover fades, no log fade in.
  Clicking a tab switches the page in the same frame. Widgets draw their hover/active colors directly.
- Header: tabs Home / Accounts / Info at (20, 40), the logo centered at the top (30 px high, made from
  logo.png: cropped to its visible pixels and scaled with WIC high quality cubic), close icon on the right.
  Rave opens on Home.
- Every page is one box at (20, 100), size (window width - 40) x 347.
- **Home** (was the Launch page): log box + buttons.
  - The log keeps the last 8 lines (font 17 px, spacing 7 px, so the last line with g/y/p fits the 190 px box).
  - LAUNCH ROBLOX: check latest version -> install/update if needed -> write FFlags -> sign in (if an account is
    selected) -> start Roblox. The button label shows the current step and the install percentage.
  - FFLAGS EDITOR: opens the FFlags editor popup (see below).
  - SETTINGS: opens the Settings popup (see below).
- **Accounts**: the account list (scrolls with the mouse wheel) and two buttons at the bottom.
  - Each row (58 px): round avatar (40 px, the first letter of the name while it loads), display name, `@name`
    (in red with "login expired, add it again" when Roblox no longer accepts the login), USE / IN USE and REMOVE.
    The selected row is tinted with the accent and has an accent border.
  - REMOVE asks once: the button turns into SURE? for 3 seconds, a second click removes the account.
  - Empty list: "No accounts added yet" and a short hint, centered.
  - ADD ACCOUNT opens the Add Account popup. USE ROBLOX'S OWN LOGIN (filled when no account is selected) clears
    the selection, then LAUNCH ROBLOX starts Roblox exactly like before (`--app`, Roblox uses its own saved login).
- **Info**: label/value rows (label in Main, value in Lwhite). On top Rave Version and Latest Roblox, then a thin
  divider line, then the system specs: Windows, CPU, GPU, RAM, Display, HardwareID (always the last row). The latest
  Roblox version is fetched on start and every 10 minutes by the launcher monitor. Hovering the HardwareID shows a
  hand cursor and "click to copy", a click copies it and shows "copied" for 2 seconds.
- Bottom right: "Rave V0.1" (from version.hpp).

## Add Account popup (ui.cpp namespace `addacc` + core/accounts.cpp)

- Same popup look as the others (520 x 360). Title ADD ACCOUNT, two method buttons QUICK LOGIN / COOKIE, CLOSE at
  the bottom. ESC and CLOSE close it. It closes by itself as soon as an account was added or updated
  (`accounts::addedCount()` changed), the new account is then selected and "Account added: ..." is logged.
- QUICK LOGIN (opens by default): Rave asks Roblox for a Quick Log In code and shows the QR code (330 px PNG from
  Roblox, 33 modules, drawn at 132 px with nearest neighbor scaling so it stays sharp, on a white 156 px box) and
  the code. The user enters the code on a signed in device (Roblox Settings > Quick Log In) or scans the QR code,
  then confirms there. The status line shows waiting / "Confirm the login for @name" / signing in / errors.
  Codes run out after about 2 minutes, a new one is fetched automatically (5 times), then NEW CODE appears in the
  QR box.
  When a code first appears, Rave copies it to the clipboard and opens the Quick Log In page
  (`https://www.roblox.com/crossdevicelogin/ConfirmCode`, through `sys::openUrl`) in the default browser once per
  popup. Every later (renewed) code is copied to the clipboard again, the browser is not opened again. The code is
  never put into the URL.
- COOKIE: a masked text box for the `.ROBLOSECURITY` cookie, PASTE (clipboard) and ADD ACCOUNT, a red warning to
  never share the cookie. Quotes, spaces and a leading `.ROBLOSECURITY=` are removed. Only printable ASCII without
  `;` and `,` is accepted (so a cookie can never inject extra HTTP headers).
- The cookie text buffer is wiped with SecureZeroMemory when the popup closes or the method changes.

## Accounts (core/accounts.cpp)

- Everything that talks to Roblox goes through `rbx()`: it adds `Referer: https://www.roblox.com/`, JSON headers,
  the `Cookie: .ROBLOSECURITY=...` header for that one account and the `x-csrf-token`. A 403 with a new
  `x-csrf-token` header is retried once with that token. WinHTTP never stores cookies (net.cpp disables it), so
  accounts can not leak into each other.
- When Roblox sends a new `.ROBLOSECURITY` (cookie rotation) Rave saves it.
- Quick Login (`runQuick`, one worker per attempt, `quickGen` cancels old ones):
  1. `POST https://apis.roblox.com/auth-token-service/v1/login/create` `{}` -> `code`, `privateKey`.
  2. `GET .../v1/login/qr-code-image?key=<privateKey>&code=<code>` -> QR PNG.
  3. Every 2.5 s `POST .../v1/login/status` `{code, privateKey}` -> `Created` / `UserLinked` (`accountName`) /
     `Validated` / `Cancelled`. After 115 s or a 400/404 the code counts as expired.
  4. `POST https://auth.roblox.com/v2/login` `{"ctype":"AuthToken","cvalue":code,"password":privateKey}` -> the
     `Set-Cookie: .ROBLOSECURITY`. A `rblx-challenge-id` header means Roblox wants a captcha or similar check;
     Rave does not try to solve it and tells the user to try later or use COOKIE.
  5. `GET https://users.roblox.com/v1/users/authenticated` -> id, name, displayName. Stored with `quick = true`.
- Cookie login: the same `users/authenticated` check, 401 = "not valid or has expired". Stored with `quick = false`.
- Adding an account that is already in the list updates it (new cookie, names) instead of adding it twice.
  A replaced Quick Login session is signed out at Roblox.
- On start every account is checked once with `users/authenticated` (names updated, `expired` set on 401), then
  the avatars are loaded: `https://thumbnails.roblox.com/v1/users/avatar-headshot?size=150x150&format=Png&userIds=...`,
  the images only from `https://*.rbxcdn.com`. Avatars live in memory only.
- REMOVE: the account leaves accounts.dat. Quick Login sessions were created by Rave, so Rave also calls
  `POST https://auth.roblox.com/v2/logout` for them. Cookie accounts are only removed from Rave (logging out would
  also end the browser session the cookie came from).
- Signed in launch (`launcher.cpp`, when an account is selected):
  `POST https://auth.roblox.com/v1/authentication-ticket` -> header `rbx-authentication-ticket` (one-time ticket),
  then `RobloxPlayerBeta.exe roblox-player:1+launchmode:app+gameinfo:<ticket>+launchtime:<unix ms>+browsertrackerid:<random>+robloxLocale:<xx_yy>+gameLocale:<xx_yy>+channel:+LaunchExp:InApp`
  (the same launch string the Roblox website uses, locale from the Windows user locale). A 401 marks the account as
  expired and the launch stops with "Sign-in failed: ...". The log says "Launching Roblox as Name (@name)".

## Info page data (core/sysinfo.cpp)

- Read once (`std::call_once`), started on a background thread at startup.
- CPU: `ProcessorNameString` from `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0`, cores from
  `GetLogicalProcessorInformationEx(RelationProcessorCore)`, threads from `GetActiveProcessorCount`.
- GPU: DXGI, the hardware adapter with the most dedicated video memory, plus its VRAM.
- RAM: `GetPhysicallyInstalledSystemMemory` (fallback `GlobalMemoryStatusEx`).
- Display: primary monitor resolution and refresh rate (`EnumDisplaySettingsW`), plus the monitor count if more than one.
- HardwareID: SHA-256 of `rave-hwid|<MachineGuid>|<SMBIOS system UUID>|<CPU name>`, first 16 bytes as uppercase hex in
  4 groups (`XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX`). MachineGuid is `HKLM\SOFTWARE\Microsoft\Cryptography`, the UUID comes
  from the SMBIOS type 1 table (`GetSystemFirmwareTable('RSMB')`). Stable across restarts and Rave updates, it changes
  with a Windows reinstall or a new mainboard/CPU. It is only shown, never sent anywhere.

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
  from Roblox servers, Roblox Terms of Use, FFlags at own risk, 7.6 account manager), data and privacy (exact network
  hosts and local files of V0.1, encrypted accounts.dat, crash dumps can hold account data, 8.5 Info page and
  HardwareID stay local), updates, test versions, feedback, support, termination, warranty disclaimer, liability, indemnity, laws,
  consumer rights, governing law, third-party components, general terms, contact.
- Appendix A lists every third-party part: Dear ImGui 1.91.3 (MIT, full text), stb_truetype / stb_rect_pack /
  stb_textedit (public domain or MIT), FreeType 2.12.1 (FreeType License credit line), Montserrat (SIL OFL 1.1, full
  text), and a note about the Windows APIs and the Visual C++ runtime.
- The installer embeds `LICENSE` (IDR_LICENSE), shows it on the License page and writes it as `<install dir>\LICENSE.txt`.
  The uninstaller removes LICENSE.txt again.
- The text uses one line per paragraph so the installer text box can wrap it. Plain ASCII only.
- When third-party material changes, Appendix A has to change in the same commit.
- When Rave talks to a new host or stores new data, sections 8.2 / 8.3 have to change in the same commit.

## Admin rights

- Rave.exe never asks for admin. It is `asInvoker` (no shield on the icon) and has no UAC request in code.
  Everything it does (Roblox install in %LOCALAPPDATA%, FFlags, settings, process watch) works with normal rights.
- Only the installer and uninstaller ask for admin (requireAdministrator).
- `sys::isElevated()` is only used for the "Administrator: yes/no" line in crash and bug reports.

## Rules that affect code

- No comments in code (see AI.md). Explanations belong here.
- The version lives only in `Rave/version.hpp`. Do not change it without the user.
