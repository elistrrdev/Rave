\# KEEP THIS FILE UP TO DATE

# Rave plan

## V0.1 (current)

Done in code:

- [x] Pages: Home, Launch, State
- [x] Launch page: LAUNCH ROBLOX, FFLAGS EDITOR (in-app popup, export/import via clipboard, save + ESC), SETTINGS
- [x] Settings popup: behavior on start (keep open / minimize / hide, hide comes back when Roblox closes),
      accent color swatches (recolors the whole UI and the header logo), saved in settings.json
- [x] Launch log also shows "Roblox was closed", last log line no longer cut off
- [x] "FFlags saved" only when the FFlags really changed
- [x] State page: Rave version, Windows, latest Roblox version
- [x] Bottom right text "Rave V0.1"
- [x] Main color #6addbb with variants
- [x] New logo: logo.png in the header, logo-bg.ico (rounded corners) as exe icon, logo.ico for taskbar and shortcuts (assets\)
- [x] No animations, no intro arrow, tabs switch instantly
- [x] Home news box reads the latest GitHub release, "no news" while there is none
- [x] Startup: internet check first (silent exit without internet)
- [x] Rave runs without admin rights, no UAC prompt, no shield on the icon (only Setup and the uninstaller ask)
- [x] %LOCALAPPDATA%\Rave created on start, %TEMP%\rave-temp only for crash and bug reports, no log files
- [x] Roblox launcher: latest version download (parallel, md5 checked), install, update, launch
- [x] Roblox always starts unelevated, even if Rave is started as admin by hand
- [x] Windows 11 rounded corners, Windows 10 gets an emulated rounded region
- [x] Portable build `builds\test\Test-Rave.exe`
- [x] Installer `builds\installer\Rave-Installer.exe` (wizard, shortcuts, Apps and Features, uninstaller)
- [x] Installer follows the Windows light/dark app mode (also live), logo-bg in title bar and header,
      license text is not preselected and has no blue focus line
- [x] Installer and uninstaller message boxes follow light/dark too
- [x] Uninstaller deletes its %TEMP% copy right after it finished
- [x] build.bat, .gitignore, CODE.md, PLAN.md
- [x] Pushed to https://github.com/elistrrdev/Rave-SRC (main), repo is private (backup only)
- [x] Public releases-only repo https://github.com/elistrrdev/Rave: Home news reads its latest release,
      Apps and Features links point to it
- [x] Closed source, all rights reserved: `LICENSE` is now the Rave End User License Agreement and Terms of Use with
      Appendix A (third-party notices), shown in the installer and installed as LICENSE.txt
- [x] Icon font with unknown license removed, close X is drawn directly
- [x] Decided: no public release of V0.1, first release will be V0.2
- [x] Decided: no anticheat in Rave (removed from the plan, the license and the installer text)

Waiting for the user:

- [ ] Test the Settings popup (all 3 start behaviors with a real Roblox start and close, every accent, restart Rave
      and check that settings.json was used)
- [ ] Test the "Roblox was closed" log line
- [ ] Test the installer and uninstaller message boxes in light and dark mode, the license page focus line,
      and that %TEMP% has no Rave-Uninstall-*.exe a few seconds after uninstalling
- [ ] Test that Rave starts without any UAC prompt (Start Menu, Desktop, Test-Rave.exe, "Launch Rave" in Setup)
      and that LAUNCH ROBLOX, FFlags, settings and the start behaviors still work like before
- [ ] Test on Windows 10 if possible (corners, blur)
- [ ] Read the new License Agreement in the installer (and maybe have it checked by a lawyer before V0.2 goes public)
- [ ] Fill the public releases repo (README.md, LICENSE with the Rave terms) and publish the first release there

## V0.2

- [ ] Decide the main feature of V0.2

## Ideas for later (not promised)

- Register Rave as the `roblox-player:` protocol handler so joining from the website goes through Rave
- Rave self update from GitHub releases
- FFlags presets (FPS unlock, low graphics) as one click buttons inside the editor
- Code signing for Rave.exe and the installer (SmartScreen)
