# Prison Life skins

Pre-made skins that ship inside Rave go here, one PNG per skin, for example `m4a1/Desert.png`.

To bundle one:

1. Add a line to `Rave/rave.rc` with a free id between 223 and 299, for example
   `223 RCDATA "../games/prison-life/skins/m4a1/Desert.png"`.
2. Add it to `skins` of Prison Life in `games/games.json`:
   `{ "target": "m4a1", "name": "Desert", "res": 223 }`.

Weapon ids: `m4a1`, `m9`, `remington`, `ak47`, `mp5`, `revolver`, `taser`, `riot-shield`, `c4`.

Skins that are not bundled can simply be dropped into `%LOCALAPPDATA%\Rave\Skins\Prison Life\<weapon>\` (the SKINS FOLDER button in Rave opens it). They show up in Rave right away as "Your skin".

A skin has to use the same layout as the original texture of that weapon (see `../textures`).
