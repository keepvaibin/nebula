# Service: ui

## ui 1.1 (j/ui-1.1)

- `alive(mod, screen, &out)`: LUMA_OK and out true while the screen is open; false once close, its scene's end or the
  mod's end ended it (open it again). A handle this run never gave: LUMA_E_HANDLE; a null out: LUMA_E_ARGUMENT. A
  handle given to something else (a pane, another mod's screen) answers false.
- `set_scale(mod, pane, x, y)`: the pane and the panes under it scaled about the pane's origin (1 the layout's own;
  negative mirrors), until set again or an animation that moves its scale plays. Not finite or past 64 either way:
  LUMA_E_ARGUMENT (Service.Argument with x or y). Pane facts gain `scale_x`, `scale_y` in thousandths; set_scale
  writes `ui.scale.<pane>`.
- Nothing more at run time.
- Test 3810-ui-scale-alive (ju-scale, ui.c CASE 9). Capabilities ui.scale, ui.alive.

## ui 1.2 (j/ui-1.2)

- `menu_rows(mod, screen, rows, count)`: the screen's panes a menu (at most LUMA_UI_MENU_ROWS_MOST, 32), each row a
  `LumaUiMenuRow {pane, enabled}` (one array). Up and down move the focus over the rows that are on, wrapping; the
  pointer (touch is the pointer) on a row that is on focuses it, on a row that is off does nothing and blocks A; A
  takes the focused row once a press. Focus drawn a tenth larger, an off row at half its alpha; each pane's own scale
  and alpha given back when the menu changes or the screen ends. A second call replaces the menu and keeps the focus
  on the same row when it is still on. count 0 takes it away. The newest shown screen with a menu takes the controls;
  none while a host menu is open.
- `menu_chosen(mod, screen, &row)`: the row taken at the last reading, -1 none. The controls are read once a frame in
  luma::ui::frame, after the mods' frame handlers; a row taken stays until the next reading, so the next frame's
  handlers, the step's hooks and the mods' tasks each see it once.
- Input: the game's own reading after the bindings (MR::isPressedUp/Down/Jump, MR::isStarPointerPointingPane, the
  game's buttons' test).
- Test 3811-ui-menu (mod ju-menu, ui.c case 10). Used by kit.menu and kit.screen's list.

## ui 1.3

- `set_position(mod, pane, x, y)`: the pane and the panes under it placed with its origin at x across and y up from
  its parent's origin, in layout units in the parent's space. It replaces the layout's place of the pane (not an
  offset from it), so copies of one button stand where the mod puts each. Kept until set again or an animation that
  moves the pane's place plays; that animation places it every frame from then, stopped or ended too, until another
  play on the screen. Not finite or past 4096 either way: LUMA_E_ARGUMENT (Service.Argument with x or y). Pane facts
  gain `x`, `y` (layout units); set_position writes `ui.position.<pane>`.
- Test 3814-ui-position (mod ju-position). Capability ui.position.
- `share_rows(mod, rows, count)`: panes of any of the mod's open screens (`LumaUiMenuRow {pane, enabled}`, at most 32)
  join the game's own menu focus beside its buttons instead of covering it as menu_rows does. Each frame each row
  that is on, shown (with its parents) and drawn is offered at its centre; the pad moves one highlight between the
  game's buttons and the rows by screen position, the pointer highlights a row while it leads, and A (the game's
  decide press) takes the highlighted row. No press is taken from the game, so its buttons keep working. The look is
  the mod's. A second call replaces the rows, count 0 takes them away; a row's screen ending ends the row. Nothing is
  highlighted or taken while menu_rows covers the game's menu or a host menu is open. Share the rows of pause
  screens: a row over the HUD while the game plays takes the pad's directions into the focus.
- `shared_focus(mod, &focused, &chosen)`: the highlighted shared row and the row taken at the last reading (once a
  frame after the mods' frame handlers), 0 for none. Fact `ui.share.<pane>` {screen, pane, focused, chosen} when
  either changes.
- Test 3815-ui-share (mod ju-share). Capability ui.share.
