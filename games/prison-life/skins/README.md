# Prison Life skins

Skins that ship inside Rave. Every weapon has its own folder (`m4a1`, `m9`, `remington`, `ak47`, `mp5`, `revolver`, `taser`, `c4`) and every skin (or set of colors that share one model) has its own folder inside it, for example `ak47/oni-vandal/`.

A skin folder holds:

- `<Skin>.png`, the texture, at most 1024 px per side. Models with several materials get one combined texture.
- `<Skin>.mesh`, the model in the Roblox mesh format `version 2.00` (several colors of the same model share one `.mesh`).
- `source\` with the original files the skin was made from (`.obj`, `.glb`, `.fbx`, `.png`), local only, not in git.
- `textures\` with extracted texture maps (diffuse, normal and so on), local only, not in git.

The current skins:

- by iitkenny: Dark Purple (M4A1, `m4a1/dark-purple`), Desert Eagle (M9, `m9/desert-eagle`), Silver (Remington 870, `remington/silver`), Silver (AK-47, `ak47/silver`), Oni Vandal Red/White/Black/Green (AK-47, `ak47/oni-vandal`), Black/Gold/Silver (Revolver, `revolver/black-gold-silver`).
- VALORANT skins (Riot Games): Forsaken Vandal White, Gaia Vandal Red, Prelude Vandal Purple, Reaver Vandal Purple (AK-47), Arcane Sheriff, Rogue Bandit (Revolver), Oni Bucky (Remington 870).

`skins/source/` still holds `awp kenny.obj` and `m4 vaporwave kenny.obj`, which came without textures and are not bundled.

To bundle a new one:

1. Make a folder for it under the weapon, put the source files into its `source\`.
2. Convert the model to a `.mesh` and the texture to a `.png` (see CODE.md, Skins, "Bundled skins").
3. Add RCDATA lines with free ids (261-299) to `Rave/rave.rc`, one for the `.png` and one for the `.mesh`.
4. Add it to `skins` of Prison Life in `games/games.json`:
   `{ "target": "m4a1", "name": "Desert", "res": <png id>, "meshRes": <mesh id>, "source": "..." }`.
   A skin without a model leaves out `meshRes`.

Skins that are not bundled can simply be dropped into `%LOCALAPPDATA%\Rave\Skins\Prison Life\<weapon>\` (the SKINS FOLDER button in Rave opens it). They show up in Rave right away as "Your skin". Those are textures only and have to use the same layout as the original texture of that weapon (see `../textures`).
