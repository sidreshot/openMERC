# Dark Places and Xonotic, looked at 2026-11-22

Only mode B is built and maintained. Nothing from Dark Places or Xonotic was imported.

Sources read: DarkPlaces `cl_particles.c` (effectinfo emitters, `lightradius`, `lightcorona`, stain decals, colour ramps) and `cl_main.c` (`R_RTLight_Update`, realtime cubemap lights). Xonotic's effectinfo notes (`cl_particles_quality`, reload of `effectinfo.txt` without a restart).

## Already in this engine

Soldier of Fortune effects stay `.eft` files. Emitters, additive sprites and `CL_SoF_AddLight` are the particle path. The picture already has HDR `Grade`, bloom, motion blur, depth of field and soft particles. Fog strength, tessellation, HDR gamma and shoulder, and snow density now load from `data/base/om_gfx.cfg` (named keys, missing key keeps the current number). That is the same kind of edit as a text effect file: change a number, load a map, no rebuild.

## Not adopted

- effectinfo. It is a different script from `.eft`. It does not read `environ/roadflare`, so it does not make the sib1 signal flare glow. The flare and any other sprite whose graphic contains `flare`, with size at least 4 and no light of its own, now gets a radius of 160 on the existing emitter. A positive radius already stored in an effect file is left unchanged. Smoke and sparks stay dark. No before/after showed effectinfo lighting that sprite better.
- Realtime shadow-casting lights (`R_RTLight_Update`, cubemaps, a shadow pass per light). That replaces the current dynamic-light model. It was not measured faster or clearer on a frame, and it would move the locked fog and lamp picture.
- Their bloom and palette ramps. `Grade` stays gamma 2.2 and shoulder 0.6 unless `om_gfx.cfg` says otherwise. Swapping the curve was not measured better.
- Corona cards on lights. The flare was dark because the effect has no light primitive, not because a corona sprite was missing. A corona card stays off (same family as the lamp disc).

Adopted from these engines: nothing.
