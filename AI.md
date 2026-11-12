# AI.md (DON'T edit this file)

! WHEN YOU BUILD, BUILD A RELEASE VERSION !

! GITHUB REPO https://github.com/elistrrdev/Rave-SRC !


!!! RULE 0 - NO AI COMMENTS. THIS OVERRIDES EVERYTHING BELOW !!!


! MOST IMPORTANTLY !

If you commit and push to the GitHub, ALWAYS do with bzpass@gmx.net, user elistrrdev and NOT anyhow make Claude a contributor, NOT ANYHOW!
ALWAYS write the commit titles in lowercase and without them looking like AI slop comments.

## NEVER ADD EXPLANATORY COMMENTS TO CODE. Not to `.cpp`, `.hpp`, `.h`, `.lib`
not to any file in this repo. No file-header banners, no
section dividers, no "why this works" essays, no restating what the next line
already says.


**WHEN YOU OPEN ANY FILE, DELETE ANY COMMENTS YOU FIND IN IT.** Leftovers from
earlier sessions are bugs to fix, not context to preserve.

**THE ONLY EXCEPTIONS:**

- `.bat` scripts - keep their comments.
- User-facing UI strings text - that is content, not comment.
- `.md` docs - prose is the point.

**IF A COMMENT IS GENUINELY UNAVOIDABLE:** one short line, all lowercase, plain
ascii words only. No em dashes, no box drawing, no `====` or `────` rules, no
emoji. Do not bloat a file with them.
Anything worth explaining goes in a CODE.md file instead.

! VERSION !
NEVER change the Rave version on your own, NOWHERE.
The user decides when the version changes and what the next version is. Ask first, every time.

! LICENSE !
Rave is closed source and proprietary: "Copyright (c) 2026 elistrrdev. All rights reserved."
The repo is private and only a backup. NEVER add an open source license (GPL, MIT or any other) to Rave itself.
`LICENSE` holds the Rave End User License Agreement and Terms of Use, which the installer shows and installs as LICENSE.txt.
When you add, update or remove any third-party code, font, image or other asset, update Appendix A (Third-Party Notices) in `LICENSE` in the same change.
Never add third-party material whose license is unknown or copyleft (GPL, LGPL, CC BY-SA, and so on). Ask first.

! ALSO !
Don't launch the latest builds by yourself. I'm the tester, and you shouldn't be the one launching them, just building them!

! NO EM DASHES !
NEVER write an em dash. Not in `.md` files, not in UI strings, not in this file.
It is the single clearest tell that a machine wrote
the text. Use the plain hyphen `-` that sits on a normal Windows keyboard.
Same goes for the en dash and every other fancy unicode dash: only `-` is
allowed. When you open a file that still has one, replace it.

## Build

Visual Studio Build Tools 2022 are fully installed and ready to use!

- Run `build.bat` in the repo root. Nothing else is needed (no DirectX SDK, no NSIS, no CMake).
- It finds MSBuild through vswhere and builds `Rave.sln` as **Release | x64**.
- Outputs:
  - `builds\test\Test-Rave.exe` (portable test build, single exe, static CRT)
  - `builds\installer\Rave-Installer.exe` (setup wizard, embeds Rave.exe and LICENSE)
- Raw compiler output and intermediates go to `out\`. Both `out\` and `builds\` are git ignored.
- The Installer project depends on the Rave project, so Rave always builds first and the installer always embeds the fresh Rave.exe.
- Only build. Never launch the builds, the user tests them.