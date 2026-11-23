# Nebula native plan: status

Written 2026-11-23 13:03. If this time is more than three hours old, the work has stopped: open the session and send the word continue.

Steps landed: 11 of 138 (8%).

In work now: Nothing started.

## Packages

| Package | Landed | Of |
| --- | --- | --- |
| S0.1 | 5 | 5 |
| S0.2 | 6 | 6 |
| S0.3 | 0 | 2 |
| R1 | 0 | 4 |
| R2 | 0 | 5 |
| G2 | 0 | 9 |
| G6 | 0 | 8 |
| G1 | 0 | 5 |
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

## Decisions (23)

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
- 2026-11-23 S0.2.6: The walk's late depth clear stays (plan_late_depth_clears, the walker's call, the fixtures' plan clears lines); Vulkan's and Metal's waiting depth clear (pending_depth_*, settle_depth_clear, clear_depth_alone) is deleted and defer_scene_depth_clear answers false on all four renderers, as Direct3D 12 and 11 did (because Their wait existed only while held depth probes kept the scene pass open (defer_scene_depth_clear refused without them), so it is dead with the probes; the planner itself is E4's to delete (FM 2.15 landing 2: walk_plan.h's plans deleted in its last part))
- 2026-11-23 S0.2.6: Proof 6 (P-340) is three engine tests, flare-window-phantom and flare-window-stardust at 640x360 and flare-window-eggstar at 160x90: each the tour's own seeded play of the stop (gate_tour.test_for) with the step hash on, expecting the recorded hash of step 599 (tests/replays) (because AGENT.md section 9 lets the agent start the game only through its own commands, so tools/verify/size_probe.py cannot be run; the engine tests prove the same at the same sizes and the landing runs them from now on)
- 2026-11-23 S0.2.6: The Direct3D 11 unit test galaxy_d3d11_probe_count_tests (P-332's) is deleted with its subject, and the walk plan fixtures are converted (probe passes dropped, later kinds one lower, plan probes lines gone) instead of recorded again from a tour (because The probe shader it tested is gone, so there is no new code for it to prove; the fixtures hold pass kinds and indices only, and the conversion keeps every other pass and plan as the tours recorded them)
- 2026-11-23 S0.2.6: Read first (d): no mod call exposes the probe answers (native/petari/src/Game/System/NebulaModCalls.cpp has none; native/include had them only in the host service probe_results), so no stub is written; the diagnostics bit 19 (probe-reads) is left unused (because A stub is asked only for a mod service that exposes the answers, and a bit reused for another diagnostic would read old command lines wrongly)
- 2026-11-23 S0.2.6: Proof 3 (the game thread's share in the old wait near zero) is shown by the search that the wait does not exist (svc_probe_results, screen_probe_results, kProbeWaitNs and StarPointerPeekZ::setDrawSyncToken: no hit in runtime, native) and by the landing's measure/step/pc, not by a new profile (because A profile plays the game outside the agent's commands (AGENT.md section 9) and the a3-prof folder is an old run's, not a tool; with the waiting function deleted its share is zero by construction, a stronger proof than a sampled near-zero)
- 2026-11-23 S0.2.6: Proof 4: the replays are not recorded again; the recorded tour on this step's build gives all 20 recordings' step hashes (step_hash.py check: 20 of 20 the same over 600 steps, D:/NebulaWork/build/Nebula-native/runs/tour) (because The game step stopped reading answers at S0.2.5, whose landing recorded the four changed replays once for every renderer; recording identical files again would change nothing, and a replay recorded without a change is a place a check sees less)
- 2026-11-23 S0.2.6: The diagnostics frame-plans and constants-watch move down to bits 19 and 20, so the bits stay without a gap after probe-reads is deleted; this replaces the part of the Read first (d) decision that left bit 19 unused (because --diagnostics takes names only (launch_args.h diagnostics_from_text), so no command line reads a bit number and a moved bit reads no old line wrongly; galaxy_launch_args_tests holds that every bit up to the last has a name, and the first landing failed it on GitHub's Linux jobs)

## Checks changed during the run (46)

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
- 2026-11-23 AB-linux-fsr3 .github/workflows/release.yml: the Linux job's build line also gives build_linux.sh -s <shader headers>; the file's header comment says where FSR 3's headers come from (allowed by the planner's brief AB-linux-fsr3.md (the owner's question of 2026-11-23 about FSR 3 on Linux))
- 2026-11-23 AB-linux-fsr3 .github/workflows/check.yml: the Linux job's build line also gives build_linux.sh -s <shader headers> (allowed by the planner's brief AB-linux-fsr3.md (the owner's question of 2026-11-23 about FSR 3 on Linux))
- 2026-11-23 AB-linux-fsr3 tools/gate/github_jobs.tsv: the new job FSR 3 shaders (Windows) is a must job (allowed by the planner's brief AB-linux-fsr3.md (the owner's question of 2026-11-23 about FSR 3 on Linux))
- 2026-11-23 AB-linux-fsr3 tools/gate/tests/github_rule_test.py: the count of green jobs is the table's own count (len(MUSTS)) and the jobs still waited for are the must jobs from the seventh on (MUSTS[6:]), not 7 and the seventh (allowed by the planner's brief AB-linux-fsr3.md (the owner's question of 2026-11-23 about FSR 3 on Linux))
- 2026-11-23 AH-plan-tool tools/gate/tests/plan_rerun_test.py: the cases that built on a state commit that stays on the branch after a failed landing: the second land after a failure makes the commit again (one commit, on the same code commit); the foreign commit with the state subject stays and the tool's own comes on top of it; a record's tip is the tip the gate was given; the folder-reads-main cases leave a state commit as a landing cut off leaves it; the helper that makes a failed landing keeps the commit; the start text is the shared REPLAY_WORDS_RULE (allowed by docs/plan/native/AGENT.md section 4.3, the row for a failed landing, and section 9)
- 2026-11-23 AH-plan-tool tools/gate/tests/plan_test.py: the failed landing case now says the state commit is off the branch and the branch's steps.tsv says nothing is landed (it said the commit stays) (allowed by docs/plan/native/AGENT.md section 4.3, the row for a failed landing)
- 2026-11-23 AH-plan-tool tools/gate/tests/plan_mend_test.py: the case that start, ahead and land wait while a mend is open lists start and land (ahead writes the next step meanwhile) and the sentence they say; the case that a new landing takes the earlier state commit off makes the earlier commit as a cut-off landing leaves it (allowed by docs/plan/native/AGENT.md section 9, the mend)
- 2026-11-23 AH-plan-tool tools/gate/tests/plan_ahead_test.py: the helper that makes a failed landing leaves its state commit on the branch (a landing cut off), and the case that makes a second state commit of a mended step does the same (allowed by docs/plan/native/AGENT.md section 4.3, the row for a failed landing)

## Tool faults

0 tool faults recorded.
