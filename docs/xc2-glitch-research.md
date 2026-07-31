# XC2 collision and movement glitch research

This report records static findings from the retail XC2 `main.elf` in the
existing Ghidra project. Addresses use image base `0x00100000`.

The code primitives below are confirmed by decompilation. The proposed input
sequences are hypotheses until reproduced in-game.

## Highest-value candidates

### 1. Fast-skip can suppress ground-collision restoration

Confidence: high-value, needs live confirmation of manager ordering.

- `event::CharaUtil::setGravity` at `0x001e24c4` performs no gravity or
  collision write while `event::SkipManager::isSkipFast()` is true.
- When it does run, it forwards its boolean to
  `gf::GfObjAcc::setCollisionGround` at `0x00768538`.
- `event::CharaObj::initModelStream` at `0x001f09f8` and
  `event::CharaLinerPos::update` at `0x001f8e60` call
  `setGravity(handle, false)`.
- `event::CharaWalkPos::term` at `0x001f8764` and several completion paths in
  `CharaWalkPos::update` at `0x001f7c0c` call
  `setGravity(handle, true)`.

This creates an asymmetric interruption window:

1. Let an event command run normally long enough to clear ground collision.
2. Activate fast skip after the clear.
3. If command teardown occurs while fast skip is still active, the matching
   `setGravity(true)` becomes a no-op.
4. Control may return with collision state 0 still off.

`SkipManager::isSkipFast` at `0x001d1ca0` requires the active-skip bit and
fast-skip-enable bit, plus two global event conditions. `SkipManager::skip` at
`0x0021c4a8` sets the active bit; `update`/`tail` clear it only after the target
frame is reached. The remaining uncertainty is whether the affected event
command's `term` runs before that clear for a given cutscene.

`SeqManager::setupScene` at `0x0025f810` sets the fast-skip-enable bit from the
event BDAT `category` field. The numeric categories are `None=0`, `FEV=1`,
`TEV=2`, `CS=3`, `SEV=4`, and `MOV=5`. It enables fast skip for every category
at least 3, except for a hard-coded comparison against `bf08030100`.

Across the retail BDAT tables published by the Xenoblade 2 Data Tables project,
this produces 959 enabled events:

- `EVT_listBf`: 743 of 744 (`bf08030100` is the exception);
- `EVT_listQst01`: 104;
- `EVT_listBl`: 61;
- `EVT_listDeb01`: 51;
- `EVT_listFev01` and `EVT_listTlk01`: none.

This is eligibility only. An event must also execute a relevant character walk
or linear-position command—and the skip must overlap its teardown—to be a
ground-collision desync candidate.

Test cutscenes containing player walk or linear-position commands. Frame-step
the skip input across the last few command frames. After control returns, look
for `ground Off, character On`, then carry downward velocity into a thin floor,
slope seam, ledge, or water boundary.

### 2. Interrupt wall-state destruction to retain full collision-off mode

Confidence: strong code path; exact trigger pairing needs testing.

`gf::pc::StateUtilField::setWallMoveMode` at `0x00734b5c` does all of the
following when entering wall mode:

- sets the movement mode at property `+0x120` to `2`;
- clears collision state 0 (ground);
- clears collision state 1 (character);
- sets the wall-mode property flag.

It does this only to the first `fw::ColiObject` in the collision component.
`gf::pc::StateFieldWall::leave` at `0x0073d79c` is the paired restoration path:
it calls `setWallMoveMode(false)`, which sets both collision states again.

Normal HFSM changes are safe: `ai::Hfsm::setStateImpl` at `0x00b587d4` invokes
the old state's leave callback. Two exceptional paths are interesting:

- `gf::GfComBehaviorPc::onWarpNormal` at `0x0074a230` changes HFSM state only
  if `GfComAsm::setState` succeeds. On failure it skips the HFSM transition,
  but still flushes the player property and resets movement.
- `gf::GfComBehaviorPc::procMsgObjectNotifyDead` at `0x00750214` skips the
  HFSM death event when property flag `0x800` is set and the death reason is
  not 4. `onWarpFallDead` later clears that same `0x800` flag.

`GfComPropertyPc::flush` at `0x00754660` does not restore either collision
state. Therefore an animation-state failure or suppressed death event while
the player is in wall mode can bypass the only obvious restoration call.

Best tests:

- Trigger the death code on the wall-adsorption entry frame.
- Combine wall entry with fall-death, skip travel, or a cutscene start.
- Try control/model switches at the wall-entry and wall-leave boundaries.
- Prefer frames where the animation state is changing or invalid, because an
  ordinary warp successfully changes HFSM state and restores collision.

A success should show both `ground Off` and `character Off` after normal
control returns. That is the closest code-supported route to true noclip.

### 3. Event scripts have an explicit character-collision switch

Confidence: confirmed primitive; event list not yet mapped.

`gf::SCOM_COLI_CHAR_VALID::exec` at `0x007abf4c` directly calls
`GfObjAcc::setCollisionChar(handle, bool)`. The wrapper applies state 1 to every
collision object attached to the target.

Any skippable/abortable event that executes the false command and relies on a
later true command is a candidate for persistent character-collision loss.
Watch for `character Off` during cutscenes when wall mode is not active, then
skip or interrupt on successive frames. This is separate from the
fast-skip/ground-state bug above, so an event containing both mechanisms could
potentially strand both states off.

### 4. Object streaming and control swaps can desynchronize character collision

Confidence: medium.

`gf::GfGameObj::setupLeaveGameObj` at `0x0048c590` clears character collision
before putting an object into the out-object system.
`gf::GfGameObj::enableGameObj` at `0x0048b2a0` restores character collision
when the object is enabled again.

Control/model changes near the leave/re-enable boundary are worth testing:
Dromarch transitions, party leader changes, forced deaths, and party formation
changes. The target outcome is gaining control of an object after the leave
clear but before (or without) the enable restoration.

### 5. Follower recovery warps accept weakly validated positions

Confidence: medium; likely useful for position skew even without noclip.

- `gf::pc::TaskFieldCheckWarpFallDead::update` at `0x00729db8` copies the
  follower transform directly, selects warp mode 3, and sets pending-warp flag
  `0x04000000`, without a collision query.
- `TaskFieldCheckWarpBattleWaterDead::update` at `0x00729b94` performs a
  vertical query, but its failure/fallback path still copies the formation
  position and sets the same pending-warp flag.
- `TaskFieldCheckWarp::update` at `0x00729708` similarly writes a target
  transform and pending-warp flag after distance/height tests.

Test a dead or recovering follower while the formation target is across a
thin wall, below a ledge, in water, or over invalid ground. Switch control or
leader on the frame the pending warp is consumed. This is a plausible source
of the known Dromarch/location-skew family.

## High-speed tunnelling: revised conclusion

Pure speed is not the best primary route.

`GfComBehaviorPc::integrateMoveNormal` at `0x0074bbb8` builds a per-frame
displacement and sends it to `fw::ColiCharactorProxy::integrate` at
`0x0028a924`. The proxy sets a `0.4` base step and enables accuracy.
`idcoli::IDColiMove::calcCheckParams` at `0x00adddc8` divides movement by that
step and, with accuracy enabled, multiplies the check count by 3 (or by 8
after a collision retry). Normal movement is therefore sampled at about
`0.133` world units or finer.

Consequences:

- Increasing speed usually increases the number of collision checks rather
  than skipping the whole wall.
- Thin geometry, seams, acute corners, and disconnected floor meshes under
  roughly 0.133 units remain credible.
- Zombie Hover's accumulated downward/wall velocity is still useful at floor
  seams, but the likely failure is geometry/numerical adjacency, not a simple
  one-frame discrete teleport.
- Extremely large values could overflow the signed subdivision count, but a
  legitimate setup would need implausibly large displacement and may crash or
  hang before producing a useful clip.

When collision is disabled, `idcoli::IDColiMove::moveColi` at `0x00adf164`
takes a bypass branch and returns the requested displacement without running
`moveDefColi`. State desynchronization is therefore much more valuable than
raw velocity.

## Live test instrumentation

The Player Telemetry overlay now reads `fw::ColiObject::testState` for:

- state 0: ground collision;
- state 1: character collision.

Use these as the primary success oracle. Position motion alone can be
misleading during events, warps, and Zombie Hover.

Suggested test matrix:

| Setup | Interrupt | Expected useful residue |
|---|---|---|
| Event actor initialized normally | Fast skip during walk/liner teardown | Ground Off, Character On |
| Wall adsorption entry | Death code / fall-death | Ground Off, Character Off |
| Wall adsorption entry | Skip travel or cutscene start | Both Off if ASM/HFSM transition is skipped |
| Event with `COLI_CHAR_VALID false` | Skip/abort before paired true | Ground On, Character Off |
| Party object begins leave/fade | Leader or Dromarch control switch | Character Off on controlled object |
| Dead follower near invalid terrain | Recovery warp + leader switch | Position inside/beyond collision |

## Ghidra artifacts

- `tools/ghidra/ExportGlitchNeighborhood.py`: reusable read-only headless
  exporter with function decompilation and caller/callee neighborhoods.
- `build/ghidra-glitch-final.txt`: 160-function movement/state export.
- `build/ghidra-idcolimove-all.txt`: complete 26-function low-level movement
  solver export.
- `build/ghidra-skipmanager.txt`: SkipManager state handling.
- `build/ghidra-framemanager.txt`: frame shifting and fast-skip call path.
