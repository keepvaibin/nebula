# Nebula native plan: status

Written 2026-11-23 07:14. If this time is more than three hours old, the work has stopped: open the session and send the word continue.

Steps landed: 9 of 138 (7%).

In work now: Nothing started.

## Packages

| Package | Landed | Of |
| --- | --- | --- |
| S0.1 | 5 | 5 |
| S0.2 | 4 | 6 |
| G6 | 0 | 8 |
| S0.3 | 0 | 2 |
| R1 | 0 | 4 |
| R2 | 0 | 5 |
| G1 | 0 | 5 |
| G2 | 0 | 9 |
| R3 | 0 | 13 |
| R4 | 0 | 10 |
| R4v | 0 | 2 |
| R5 | 0 | 8 |
| R6 | 0 | 7 |
| G3 | 0 | 7 |
| G4 | 0 | 5 |
| G5 | 0 | 5 |
| R7 | 0 | 8 |
| R8 | 0 | 6 |
| R9 | 0 | 2 |
| R10 | 0 | 4 |
| G7 | 0 | 5 |
| G8 | 0 | 4 |
| G9 | 0 | 5 |
| Z | 0 | 3 |

## Last 10 landings

None yet.

## Repairs

1 open of kind fail, 1 in all.

| Id | State | Kind | Check | Renderer | Traced to | Seen |
| --- | --- | --- | --- | --- | --- | --- |
| R-001 | open | fail | tour/d3d12 | d3d12 | step S1.1 commit abc | 1 |

## For the owner to check by hand

Nothing is owed.

## Waiting for the owner

Nothing waits for the owner.

## Blocked

Nothing is blocked.

## Tool mends

No tool was mended.

## Decisions (16)

- 2026-11-23 S0.2.1: LoadSlice: 32 items a frame; an item is a phase of GameScene::initAfterScenarioChosen, the player, one placement made whole, one piece of FileSelector::initStep, or one Star Bit of the reserve (because the card asks for a fixed count of work and names no count; the count is a constant in one place, no player sees which frame of the load an item falls in, and the load worker (G3.7) replaces the slices)
- 2026-11-23 S0.2.1: Drawing's random stream (MR::getDrawRandom) is seeded with the same seed as the game's stream, in setRandomSeedFromStageName and setFixedRandomSeed (because a fixed seed keeps the cosmetic numbers the same in every run of a replay and needs no new setting; the draw stream runs through the same sequence as the game's but is advanced only by drawing, so neither changes the other)
- 2026-11-23 S0.2.1: A message's sound tags are started by the talk former's step walk (TalkTextFormer, a silent print of the text box while it is drawn) and never by the draw; text outside a talk former starts no tag sound (because the step makes the decision (P-214); the silent print walks the same characters and alphas the draw did, so a talk sounds as before; the message pak is compressed so other texts with sound tags could not be counted (a risk named in P-214's note))
- 2026-11-23 S0.2.1: The galaxy map's capture is prepared in calcAnim (the step) and recorded by the next draw from what the step stored; at a time rate above 1 the layouts are recorded as the last step of the frame left them (because P-214: the step decides and stores, the draw only records; a capture with several steps before one draw cannot show more than one of them)
- 2026-11-23 S0.2.1: The settings menu's galaxy select names are answered from GameSystem::updateSceneController before the sequence's update, once the system archive is loaded, without a scene (because P-261 offers the names asked at the step's start or served without the scene; the system update runs once per step in every scene, so the menu gets them in the title as in a galaxy)
- 2026-11-23 S0.2.1: Proof 2 (a time rate of 2 against 1) is shown by replays/rates on the 17 stops of tests/replays/rate-stops.txt; PhantomGalaxy 1, StarDustGalaxy 1 and SandClockGalaxy 1 wait for P-294 (S0.2.5) (because their first difference is a StarPieceShooter placed from the picture's depth (MR::getStarPointerWorldPosUsingDepth), answered once a frame: the fault of row P-294, which the card of S0.2.5 closes, not a draw that does step work of the four this card names)
- 2026-11-23 S0.2.2: The scene query with no hit answers the point where the ray meets the camera's far distance (CameraContext::mFarZ); its distance is the view-space depth of the answered point, as StarPointerController::calcViewDistanceZ gives it today (because the card's Read first leaves both open; the drawn picture's cleared depth put a pointer over the sky at the far plane, so the targets' distance check sees what it saw before, and the distance needs no second form)
- 2026-11-23 S0.2.3: StarPointerPeekZ, its members and MR::setStarPointerDrawSyncToken go in this step, with PointerInfo's mDrawReady and mZDepth; TDDraw::invProject stays for TalkDirector until S0.2.4 (because the card's What changes says StarPointerPeekZ goes here and its Old path said S0.2.6; once the pointer asks the scene nothing reads the class, and a dead copy is against the owner's rules (AGENT.md section 8); S0.2.6 deletes the probe path the class used)
- 2026-11-23 S0.2.3: The query's pointer targets are the actors that took a StarPointerTarget, kept per scene in AllLiveActorGroup in that order; a target counts while its actor is alive, not clipped and its model not hidden, and a sphere that holds the camera is not hit (because INV section 3 names the targets' spheres but not which; these are the ones whose model the picture's depth could show, the list dies with the scene that made its actors, and walking only them keeps the step's cost small)
- 2026-11-23 S0.2.3: The pointer asks the scene in the step where the game scene's camera is kept (StarPointerDirector::mIsUpdateTransHolder) and a pointer layout shows its pointer, with the pointer's view and projection matrices (StarPointerTransformHolder); the ray is StarPointerTarget::calcScreenPositionFromView read backwards (because the picture was asked only in the game scene's draw while a pointer was shown, so the point stays as it was elsewhere; the targets' own screen test uses those two matrices, so a target's sphere and its circle on the screen agree)
- 2026-11-23 S0.2.4: TalkPeekZ goes with no scene query in its place, and TDDraw::invProject with it; proof 1 becomes the searches for TalkPeekZ, setTalkDirectorDrawSyncToken and invProject and the recorded plays' step hashes (because nothing in the tree reads TalkPeekZ::mWorldPos or mDepth (the balloon is placed by TalkMessageCtrl::updateBalloonPos from its actor), so a query would compute a value no one reads, a dead copy against the owner's rules (AGENT.md section 8); invProject had no other caller)
- 2026-11-23 S0.2.5: How much of the flare shows: a 1 by 1 target written after the scene by a new written material (flare_sight.hlsl) from the 17 samples, moved by at most 0.05 a frame from the last frame's (two targets used in turn), and the flare's models mixed into the frame by it (flare_blend.hlsl: the frame before and after them); not read by the flare's vertex programs (because the flare's materials are generated programs shared by hash with other models, and every flare material blends its source by its alpha (the glow adds, the ring and line mix), so mixing the frames by the share draws the flare at its alpha times the share on all four renderers through shared code, with no generated program changed; a target pass always clears, so the eased share reads the other target)
- 2026-11-23 S0.2.5: The step places the flare at its bright object's centre now (the first object giving light with a sample on the screen, from the step's camera) and takes its strength from the distance to the screen's centre alone (because the card: the ring, glow and line from the sun and the camera only; how much of the object shows is the sight's, on the graphics card)
- 2026-11-23 S0.2.5: The replay references of the four stops where the flare stands (EggStarGalaxy 1, PhantomGalaxy 1, SandClockGalaxy 1, StarDustGalaxy 1) are recorded again in this step, and again with every renderer at S0.2.6 (because the flare's three actors are in the step hash (position and nerve step), and the card's change places them from the sun and the camera where the picture's answers placed them before; the card did not name the replay)
- 2026-11-23 S0.2.5: Proof 1 is the engine test lens-flare-hidden (EggStarGalaxy 1 at frame 100, the sun behind the house), not the mod test draw/2403-draw-points (because mods and their tests are off in this work (AGENT.md section 8); the engine test takes the same picture without the mod, and the landing judges it from now on)
- 2026-11-23 S0.2.5: PhantomGalaxy 1, SandClockGalaxy 1 and StarDustGalaxy 1 are added to tests/replays/rate-stops.txt in this landing, though no check of this landing plays replays/rates (because P-294's note (11-23, S0.2.1) says the step that closes the row adds them; their difference at a rate of 2 came from the picture's depth answers (the star bit launcher's goal from the pointer, the flare's actors), which no step reads after S0.2.5; the long run's replays/rates judges them)

## Checks changed during the run (38)

- 2026-11-22 S0.1.3 tools/verify/console_census.py: the open group gx_sections counts the four section names GXMT, GXTG, GXTM and GXWP with a ceiling of 31, and the closed group fifo excludes only those four whole words (allowed by docs/engine/FRAME-MODEL.md section 2.9)
- 2026-11-22 U-unattended tools/gate/main_push_guard.py: the guard also refuses a push of a branch named plant/... (allowed by docs/plan/native/TOOL-MEND-PLANTS.md, how a plant is kept from ever landing)
- 2026-11-22 U-unattended tools/gate/strictness.py: new: the strictness check itself (allowed by docs/plan/native/TOOL-MEND.md section 6)
- 2026-11-22 U-unattended tools/gate/tests/plan_live_test.py: the landings list has a seventh column, figures (allowed by docs/plan/native/AGENT.md section 11)
- 2026-11-22 U-unattended tools/gate/tests/plan_page_test.py: the page data has the new parts and the old-page line; heartbeat.txt is in the live folder (allowed by docs/plan/native/AGENT.md section 12)
- 2026-11-22 U-unattended tools/gate/tests/plan_rerun_test.py: a new landing takes the tool's earlier state commit off the branch; a card and a log line permit a test tool in a step; the heartbeat is not a change (allowed by docs/plan/native/AGENT.md section 9)
- 2026-11-22 U-unattended tools/gate/tests/plan_test.py: the repair rules name the mend; the allow-list case names the file in the card and writes its log line (allowed by docs/plan/native/AGENT.md section 9)
- 2026-11-22 U-unattended tools/gate/tests/push_again_test.py: the 4xx case that is not tried again uses 404 (allowed by docs/plan/native/AGENT.md section 5)
- 2026-11-22 U-unattended tools/gate/tests/result_kinds_test.py: the test gate has its own live folder; the third flake gives a repair, not a ledger row to open (allowed by docs/plan/04-GATE-AND-MAIN.md section 9)
- 2026-11-22 U-unattended tools/gate/tests/scope_test.py: the repository checks of a run now include repo/strictness (allowed by docs/plan/04-GATE-AND-MAIN.md section 17)
- 2026-11-22 Y-beta-build .github/workflows/check.yml: the Mac job builds both processors (build_macos.sh -a "x86_64 arm64", libc++ for both under one cache key, the x86_64 Rust target); the Linux start check runs with the Khronos validation layer (--gpu-validation 1) and fails on a [VK-LIVE-ERR] line (allowed by briefs/Y-beta-build.md pieces 1 and 5, and the planner's message of 2026-11-22 21:58 (item 6))
- 2026-11-22 Y-beta-build .github/workflows/release.yml: the version step also gives mod support and the version's label to every package job; the lumakit job runs only with mod support (the jobs that need it go on when it is skipped); a dry run can be told its version; the Mac job builds both processors with the compiler check.yml pins, and its package is kept for the Intel Mac job; the Linux start check runs with the validation layer; the pgo check of a pre-release tells and goes on; every package is checked by tools/check_mod_support_package.py (allowed by briefs/Y-beta-build.md pieces 1 to 3 and the planner's message of 2026-11-22 21:58 (items 4 to 7))
- 2026-11-23 L-linux-window .github/workflows/check.yml: the Linux start check runs on a virtual screen of 1920 x 1080 with a window manager (openbox) and fails unless the log says the window is borderless full screen at that size and the system's pointer is hidden (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 L-linux-window .github/workflows/release.yml: the Linux package start check runs as the check.yml one, and first checks the unpacked folder's top (install.sh, README.txt, files, support) and the first line of README.txt (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 L-linux-window tools/gate/tests/linux_launch_test.py: the test is rewritten to run the start script on every route of its words (terminal, dialog tools, file) and for no install and an error ending; the cases it had stay (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 L-linux-window tools/gate/tests/mod_support_package_test.py: the Linux fixture holds the menu entry and the type file under support/ (the package's new layout), as the check finds them by name (allowed by the planner's message of 2026-11-23 (the owner's Linux faults))
- 2026-11-23 M-004 tools/gate/tests/gate_line_test.py: the table of the --also parts that the test names has replays/rates beside the others (the check name in CHECKS and the sorted list of ALSO_PARTS) (allowed by docs/plan/04-GATE-AND-MAIN.md section 15, the table of a step's or repair's extra checks)
- 2026-11-23 M-004 tools/gate/tests/plan_rerun_test.py: the set of the step tool's extra checks (ALSO_CHECKS) has replays/rates (allowed by docs/plan/04-GATE-AND-MAIN.md section 15, the table of a step's or repair's extra checks)
- 2026-11-23 S0.2.1 tests/replays/001-BattleShipGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/016-CosmosGardenGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/022-EggStarGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/027-FactoryGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/035-HeavenlyBeachGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/042-HellProminenceGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/048-HoneyBeeKingdomGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/054-IceVolcanoGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/058-KoopaBattleVs1Galaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); the same to step 501, then other hashes for steps 502 to 513, then the recorded hash d0a1420de34b747e from step 514 (it was from step 502) (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/066-OceanPhantomCaveGalaxy-4.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/068-OceanRingGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/078-ReverseKingdomGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/085-SandClockGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/090-StarDustGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/095-SurfingLv1Galaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.1 tests/replays/097-TamakoroExLv1Galaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.1 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.1)
- 2026-11-23 S0.2.5 tests/replays/022-EggStarGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.5 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.5)
- 2026-11-23 S0.2.5 tests/replays/073-PhantomGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.5 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.5)
- 2026-11-23 S0.2.5 tests/replays/085-SandClockGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.5 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.5)
- 2026-11-23 S0.2.5 tests/replays/090-StarDustGalaxy-1.tsv: the step hashes recorded again from a Direct3D 12 tour run of step S0.2.5 (step_hash.py record); they differ from step 0 (allowed by the card, docs/plan/native/03-NATIVE-STAGE-0.md, step S0.2.5)

## Tool faults

0 tool faults recorded.
