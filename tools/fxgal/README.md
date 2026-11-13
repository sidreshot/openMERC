# Effect gallery

Item B1 of [docs/EFFECTS.md](../../docs/EFFECTS.md): every group of the effects list played at fixed spots of a few
maps, a shot of the same frozen moment in each look (the new one and the original one) and the GPU time of that frame
in each look. The page it writes (`index.html`) shows the new look; hovering a picture shows the original.

```
python3 tools/fxgal/fxgal.py --exe build-linux-x86_64-Release/release/openMerc --game ~/Games/openMERC \
    --out /tmp/fxgal [--spots jpn1,kos1] [--groups E1,E3] [--no-gpu]
```

`--game` is the folder with `base/pak0.pak`. Each map runs in a fresh user folder (`<out>/run/<map>/user`, its
`config.cfg` holds the settings: 1600x900 in a window, preset High, 60 frames a second, no vsync, no sound); the
console log of the run is `<out>/run/<map>/console.log`. The engine re-bakes a map's lighting at its first load and
uses it from the next one, so a map without baked lighting in the cache (`--lightmaps`, default `<out>/lightmaps`) is
loaded once first to bake it (20 to 60 s); the run prints which lighting the shots have (the re-bake, or the map's own
where the re-bake does not fit it, as for the players). The whole gallery (4 spots, 16 groups) takes about
40 minutes with the GPU times and about half that with `--no-gpu`.

## How a shot is taken

1. The view is set (`omsetpos`; the player flies, unseen and unhurt; the interface is off).
2. The item is started: an effect with `om_fxplay <effect> <dist> <up> <place> [loop]`, a weapon fired (the cheat
   kit that has it, `weaponselect`, `+attack` held), or a mark with `om_decal`.
3. After the item's age in the client's time (`om_after c<seconds>`, the clock of the effects) the game is paused.
   The pause stops the effects, the dynamic lights and the renderer's clock.
4. Each look is set in turn on that frozen frame, shot after a settling second, and its GPU time read from
   `r_om_gputime 1` (the median of the `OMGPU` lines, each the mean of 100 frames) with the frame rate free.

So the two pictures of an item differ by the look only, and the cost of the effect is the GPU time of its frame minus
the GPU time of the same place without it (the `base` row).

The effects are played at 60 frames a second. Their clock steps a full 1/60 s every frame above 60 fps (SPEC-400
Timebase, as in 1.07f), so they age as fast as the frame rate: at 165 fps an explosion's fire dome is mostly gone
after 0.1 s, at 1000 fps after 0.02 s. At 60 fps they last as designed.

A look that changes what is simulated (more particles, other timing, extra pieces made when the effect starts) cannot
be compared on one frozen frame: the frame holds what the first look made. Such an item needs a start of its own per
look; the gallery does not do that yet.

## gallery.json

* `looks`: name -> the cvars of that look. `new` is the High preset, `orig` the original look. When an item of the
  effects list is done, its switch goes into both looks.
* `spots`: `map`, `view` (`omsetpos` arguments: x y z yaw pitch) for effects on the floor or in the air, `wall` (a view
  with a wall about 150-250 units ahead) for effects on a wall, marks and weapons.
* `groups`: the groups of the effects list, each with `items` (an item shows in one row, one picture per spot):
  * `{"fx": <effect>, "place": "floor" | "wall" | "air", "dist": units ahead, "up": units up, "loop": true,
    "age": seconds}`; `loop` starts the effect again and again as a level's effect slot does (fires, smoke, steam,
    sprays: one start of them is a single puff);
  * `{"shoot": <weapon key 1-12>, "alt": true, "age": seconds after the trigger}` (2 9 mm, 3 .44, 4 shotgun,
    5 sniper rifle, 6 machine pistol, 7 assault rifle, 8 machine gun, 9 slug thrower, 10 rocket launcher,
    11 flame gun, 12 MPG; the cheat kits replace the inventory, so each shot takes the kit first: `elbow` for
    1-5 and 7, `bigelbow` for the rest);
  * `{"decal": <sprite>, "size": units}`.

## What the first runs showed

* Explosions (`grenadeexplode`, `rocketexplode`) are visible for about 0.25 s: the fire dome and the shock ring at
  0.03 to 0.2 s, only sparks and the scorch after 0.3 s. The ages of the items are set for that.
* The game time steps by server frames (0.1 s), and the client's clock lags it: an age in game time missed the
  explosions altogether. Hence `om_after c<seconds>`.
* At 1000 fps (the first runs) the explosions were gone before any shot: the effects clock above (SPEC-400).
* An effect the level does not use was never registered by the precache: before `om_fxplay` loaded its sprites,
  models and sounds itself, it played with no picture.
* Not in the gallery: the lava and molten metal surfaces (E16) need spots on the maps that have them; `enemy/breathe`
  (E17) is not in the stock paks.
