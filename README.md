<p align="right">
  <strong>English</strong> · <a href="README.zh_CN.md">简体中文</a>
</p>

# Coopa Fight (库巴格斗)

A three-button family fighting game: pick a family member and fight face to face. Land a ● jab and press ▼ ● right
away to link into a super — black-screen close-up, the move's name shouted out, a third of the health bar gone, and
the other side is dizzy until they mash free. It's not about fast fingers, it's about guessing what the other side
does next.

- **Vs computer**: play alone against the other three family members (easy → normal → hard). Beat all three and that
  character gets a crown on the pick screen.
- **Two players**: each holds a Coopa card, face to face.

A round lasts 60 s. Empty the other side's health to win it; when time runs out, more health wins (a tie replays the
round). First to 2 rounds wins.

## Move list

| Input | Move |
|---|---|
| ● | **Jab**: fast (lands in 0.18–0.24 s), small damage |
| ▲ | **Heavy**: 0.55–0.65 s wind-up, big damage, breaks a guard (the guard breaks and they are dizzy for 1 s) |
| ▼ | **Guard**: shield up for 0.6 s (Grandma 0.8 s), blocks jabs and specials; hold ▼ to keep it up to 1.2 s |
| ▼ ▲ | **Special**: comes out from behind the shield, costs no energy; 1 s before the next one |
| ● hits, then ● | Another jab, up to three in a row |
| ● hits, then ▲ | **Linked heavy**: no wind-up, always lands |
| ● hits, then ▼ ● | **Super**: costs 1 energy cell |
| Dizzy | **Mash** any button to break free |

- **Rock, paper, scissors**: a jab interrupts a heavy early in its wind-up (the first 0.3 s); a heavy breaks a guard;
  a guard blocks a jab and the jabber is stuck an extra 0.5 s, so the blocker can punish.
- **Combos**: a landed jab stuns the other side for 0.6 s, and the next button pressed in that time links. You can
  press ahead; buttons pressed ahead come out whether the jab landed or not — if it missed, ▼ ● is just a guard and a
  jab, and you can be punished.
- **Tap ▼ ● quickly, "da-da-da"**: ● does nothing while ▼ is held, so let go of ▼ before pressing ●. ▲ / ● within
  0.35 s after ▼ counts as a special / super.
- **Energy**: 3 cells. Landing, getting hit and blocking all fill it, getting hit the most; the side behind on health
  fills a bit more when hit. Energy carries over to the next round.
- **When a super lands**: both cards play a 1.5 s close-up (the clock stops and buttons don't count), 330 damage (a
  third of a full bar), then the other side is dizzy for 2 s. Mash to wake up sooner: 0.16 s per press, at most 6
  presses a second count. Getting hit by one combo while dizzy wakes you up; for 0.3 s after waking, buttons are
  ignored and you can't be hit.
- **No dizzy loops**: from being knocked dizzy until you can move again, you can't be made dizzy again or take a super
  (● ▼ ● against you only jabs). Someone stuck in hit-stun can only be hit by the next move of the same combo, not by
  a fresh attack.

## Characters

| Character | Feel | ▼ ▲ Special | ● hits, then ▼ ● Super |
|---|---|---|---|
| Coopa | Fastest jab, a little less health (950) | Slingshot: a marble flies over and stuns, link a jab after it | Shadow-clone barrage: several Coopas take turns hitting |
| Dad | Fastest, hardest heavy; slower jab | Shoulder charge: rushes in and breaks a guard (stun only, no dizzy) | Iron cage hammer: locks the other side in a cage and brings the hammer down; they mash inside the cage |
| Grandma | Holds the guard longer | Flying pot lid: the lid flies out and back, hitting twice | Nagging storm: a screen full of nagging rains down, then the rolling pin |
| Grandpa | Slowest moves, hardest jabs | Tai chi push: a 1 s stance; an attack that comes in is caught and pushed back (they're stunned, link after it) | Tai chi: Mountain push: one palm pushes the other side off the screen and they bounce back |

- Grandpa's tai chi is a counter: if nobody attacks, the stance ends and leaves an opening.
- Disconnects are fine: the round restarts, scores and energy are kept.

## Development

Needs shell API 2 (`kit_api = 2`, `kit_link.h`) and SDK 4.

- `make -C tests`: host tests for the rules, moves, link protocol, computer player, saves and art; `test_balance`
  plays thousands of matches to check the numbers (character match-ups, kid vs adult, one-trick strategies, match
  length). With `COOPA_SHELL=<shell dir>` it also decodes the voice lines.
- `tools/gen_lines.py`: regenerate the four characters' voice lines (needs edge-tts and ffmpeg).
- `tools/gen_art.py`: regenerate the constant images (`src/fight_art.c`) from the `art/*.txt` pixel art; LVGL draws
  them straight from the game block, they take no heap.
- `tests/sim_cpu.py`, `tests/sim_duo.py`: play the ladder (including overlapping button presses) / a two-card match
  with a disconnect in the desktop simulator.
- `coopa run`: play in the browser; open the printed `duo.html` for two cards on one page.
- `coopa check`: the same checks the store runs.
- All the numbers live in the move table in `src/fight_moves.c` and the macros in `src/fight_sim.h`.

MIT License (see `LICENSE`).
