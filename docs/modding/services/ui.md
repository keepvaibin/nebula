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
