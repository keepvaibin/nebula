# LumaKit: the modules

## How a module is written

- LumaKit is one library mod (id `lumakit`). Each module is one file `source/<module>.lum` (`player.form` is
  `source/player/form.lum`).
- A mod that uses a module needs `lumakit` and writes `use kit.<module>`.
- What the compiler does today:
  - Crossing: numbers, bool, vectors, game enums, views, a class's object, options of these, `text` in, the library's
    structs and enums.
  - Not crossing yet: lists, maps, bytes, closures, handles, `pending`, struct methods. A `pub fn` using them builds,
    but it is not exported (its `.lumi` line says why).
  - The `Kind`/`define`/`find` forms wait for define_class from a script. Until then a kind is a `class X : <game
    enemy> replaces <name>` using the module's helpers, as `actor` does.
  - Write the interface exactly as overview.md section 2 has it. A function that cannot export yet stays in the file, and its
    callers come when the part lands.
- Rule 4, the one pattern. Anything a module makes for a caller is recorded under `caller()` (a `u64`: inside an
  export it is the calling mod; it stays the same while that mod is on). `on mods.stopped(gone) { ... }` in the module
  undoes everything recorded under `gone`, newest first. The kernel calls it through lumakit's `dependent_gone` after
  that mod's `stop` and before its unload. `hud.lum` is the example: an `owners: list<u64>` beside its slots; `take()`
  writes `owners[i] = caller()`; the handler frees each slot whose owner is `gone`. One `on mods.stopped` per module
  file, as with `on frame`. Never keep a closure, a view or a handle of a stopped mod past it.
- Never work around a compiler fault in the kit: it is fixed in the compiler first.

## The table

| # | Module | overview.md | Data (`kit/<module>/`) | Conformance tests (kit-<module>) | Built on (what must exist) |
|---|---|---|---|---|---|
| 1 | `actor` | 2.6 | `<name>.json` (a kind without code) | walks, stomped, spun, drops_coins, sensors_each, hat_on_head, placement_twice, two_mods_one_name | K.derive/hook/call, vtable slots, LUMA_OUT marks, make |
| 2 | `item` | 2.4 | `<id>.json` | placed, spawned, gives_each, bubble, crystal, inventory_reload, mod_off | actor, save (inventory) |
| 3 | `projectile` | 2.5 | none | bounce3_burst, on_hit_each, falls_on_planet, sixty_four, thrower_off | actor, C.fx, C.sound |
| 4 | `hud` | 2.10 | none | each_corner, stacking, remove, follow, hide_lives, popup_1_3x, mod_off_five | svc.ui, draw, C.layout/font/text |
| 5 | `player.form` | 2.1 | `<id>.json` | by_item, by_code, luigi_look, timer_meter, damage_ends, on_spin_once, scale_half_double, game_form, two_mods, mod_off | K.hook setPlayerMode, K.field, C.skin, draw.inst |
| 6 | `player.state` | 2.2 | `<id>.json` | double_jump, glide, dive, ground_pound, tune_end, two_mods, mod_off_mid_move | K.hook player action/physics, MarioConst, C.anim, input |
| 7 | `player.character` | 2.3 | `<id>.json` | from_data, switch_back, as_luigi, downloaded_model, second_player, mod_off | K.derive player, C.skin/anim/sound, assets; `lumalink add character` makes its sources |
| 8 | `camera` | 2.9 | none | take_end, free, first_person, side, mirror, view_region, two_mods | K.hook CameraDirector, draw.view |
| 9 | `gravity` | 2.8 | none | parallel_flip_end, by_frames, shape_priority, two_mods | K.derive PlanetGravity, GravityCreator |
| 10 | `area` | 2.7 | `<name>.json` | three_shapes_order, slow, wind, side_camera, overlap | K.derive AreaObj, AreaObjMgr, camera |
| 11 | `ride` | 2.21 | `<name>.json` | mount, ride_planet, jump, mounted_move, thrown_off, dismount, mod_off | actor, player.state, K.hook player control |
| 12 | `pointer` | 2.22 | none | throw_coins_cost, out_of_coins, grab, gyro_and_refusal, world_point | K.hook StarPieceDirector, input.dev, projectile |
| 13 | `progress` | 2.17 | none | past_120, on_star_instead, comet_added, flags_copy_erase, new_file_rule | svc.save (per file), K.hook GameDataFunction |
| 14 | `net.kit` | 2.18 | none | ordered_20pct_drop, peer_leaves, clock_one_frame, page_served | svc.net (datagrams, listen, WebSocket) |
| 15 | `replay` | 2.19 | none | play_back_exact, decode_other_platform, fifteen_puppets, inputs_same_end, rewind_5s | input inject, time.ctl, K.field/hook, C.skin/anim |
| 16 | `ui.kit` | 2.20 | none | each_widget_pad_mouse_touch, text_field_keyboard, wheel_item, mod_off | svc.ui, draw, input, draw.view |
| 17 | `screen` | 2.11 | none | each_kind_answered_backed, list_updated, mod_stops_open | svc.ui, draw loading point, C.layout/text |
| 18 | `menu` | 2.12 | none | two_mods_order, chosen, disabled, removed, mod_stops | K.hook PauseMenu, svc.ui |
| 19 | `cutscene` | 2.13 | `<id>.json` | each_shot, skipped, run_through, actor_dies, letterbox | camera, talk, time hold, audio |
| 20 | `talk` | 2.14 | `<tree>.json` | say, ask_two_four, tree_loop_flag, voiced, speaker_killed | TalkMessageCtrl calls, C.text/sound, progress |
| 21 | `shop` | 2.15 | `<id>.json` | buy, too_poor, out_of_stock, stock_reload | screen, item, talk, save |
| 22 | `stage` | 2.16 | `<name>.json` | listed_after_20, stars_counted, comet_star, warp, music, built_in_code, downloaded_stage | C.stage, assets.provide, K.hook dome/scenario, progress |

Order: modules 1 to 4 come first (then `cutscene` and `shop`), since the others build on them (`ride` and `pointer`
on `actor` and `projectile`; `shop` on `item`).

## Done

A module is WRITTEN when its file, its `kit/` data schema in `lumakit/schema/<module>.json` (checked at build,
rule 11) and its conformance mod all compile. It is PROVEN when its test ran in the game with matching facts.
