# Prison Life skins

Skins that ship inside Rave, one folder per weapon (`m4a1`, `m9`, `remington`, `ak47`, `revolver`, `taser`, `riot-shield`, `c4`).

- `<Skin>.png` is the texture, at most 1024 px per side.
- `<Skin>.mesh` (or one shared `.mesh` for several colors of the same model) is the model, already converted to the Roblox mesh format `version 2.00` (V flipped, small models scaled up to the size of the original weapon).
- `source\` holds the original `.obj` and `.png` files they were made from (local only, not in git).

The current skins are by iitkenny: Dark Purple (M4A1), Desert Eagle (M9), Silver (Remington 870), Silver and Oni Vandal Red/White/Black/Green (AK-47), Black/Gold/Silver (Revolver).

To bundle a new one:

1. Convert the `.obj` to a `.mesh` and scale the `.png` down (see CODE.md, Skins, "Bundled skins").
2. Add RCDATA lines with free ids (247-299) to `Rave/rave.rc`, one for the `.png` and one for the `.mesh`.
3. Add it to `skins` of Prison Life in `games/games.json`:
   `{ "target": "m4a1", "name": "Desert", "res": <png id>, "meshRes": <mesh id>, "source": "..." }`.
   A skin without a model leaves out `meshRes`.

Skins that are not bundled can simply be dropped into `%LOCALAPPDATA%\Rave\Skins\Prison Life\<weapon>\` (the SKINS FOLDER button in Rave opens it). They show up in Rave right away as "Your skin". Those are textures only and have to use the same layout as the original texture of that weapon (see `../textures`).
