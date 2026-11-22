# Gameplay settings

[← Back to the README](../README.md)

The GAMEPLAY tab of the in-game PC OPTIONS page holds settings that change how the game plays.
Open PC OPTIONS (**F6**, **BACK + START** held together on a controller, or Play Career → My
Profile → Options → PC OPTIONS) and press **RB** (the menu's RB key on the keyboard, **E** by
default) until **GAMEPLAY** is shown; **LB** goes back, and clicking a tab's name works too.

## Quick restart

**QUICK RESTART** restarts the race from the track with one button, without the pause menu:
the screen fades to black, the game's loading screen shows while the race reloads, and the race
comes back on a short start sequence (3, 2, 1, GO in about three seconds instead of the full
intro). It has been played in Free Play races, World Tour events and Hot Lap; a pause menu
without a RESTART is closed again and nothing else happens. Off by default; SAVE & APPLY switches it at once, no restart needed.

- **QUICK RESTART BUTTON**: **D-PAD LEFT** or **D-PAD RIGHT**. No controller layout uses
  them while driving, so the button does nothing else. A spare button on your pad (a back
  paddle) mapped to one of them in its own software makes a dedicated restart button.
- **QUICK RESTART TRIGGER**: **HOLD** (the default) restarts once the button has been held
  while the screen fades out; let go during the fade to cancel. **TAP** restarts on one press,
  with no cancel.

It only works while driving: not in menus, replays or photo mode, and not during a start
sequence until GO has left the screen. Races started from the menus, and restarts from the
pause menu, keep the game's full intro. The keyboard has no quick restart key.
## World Tour narrator

**WORLD TOUR NARRATOR** set to **MUTED** silences the voice that introduces each World Tour event
and track in the career menus, in every language. Jeremy Clarkson's Autovista commentary is not
affected. On by default; SAVE & APPLY switches it at once, no restart needed.

It works on the game's narrator voice bank (`Media\audio\VO\<language>\vo.fsb`) without
changing the file: each line is cut to a few milliseconds as the game reads the bank, and the
bank already loaded is changed in memory when you switch it.
## Online menu items

Some menu items need Xbox LIVE or the Forza servers, so here they only say the service is
unavailable: COMMUNITY and MARKETPLACE on the main menu, RIVALS under GO RACE, BUY TUNING SETUPS
under UPGRADE and BUY DESIGNS under PAINT. **ONLINE MENU ITEMS** set to **HIDDEN** leaves them
off: the main menu then shows PLAY CAREER, AUTOVISTA and FREE PLAY. Off (SHOWN) by default. Each
menu shows the change the next time the game builds it: for the main menu, after you've been into
another screen such as PLAY CAREER, or at the next start.

### Auto restart on a dirty lap

**AUTO RESTART ON DIRTY LAP**, under QUICK RESTART's rows, restarts as soon as the game marks
your lap dirty: off the track, into a wall or contact, the moment the warning shows by the lap
time. It restarts as QUICK RESTART does (loading screen, short start sequence), with no button
pressed. Choose where:

| Setting | Restarts |
|---|---|
| OFF (default) | Never |
| HOT LAP | When you are alone on track: Hot Lap |
| RACES | In races against AI cars, in Free Play and the career: the whole race restarts |
| HOT LAP + RACES | Both |

It needs QUICK RESTART on: with QUICK RESTART off the row is greyed out and nothing restarts.
SAVE & APPLY switches it at once.

## Quick Race laps (experimental)

**QUICK RACE LAPS** sets the number of laps of every Quick Race, from 1
to 99, on every track; DEFAULT keeps each track's own count. It is saved to
`config\gameplay.toml` and takes effect the next time track select opens: its LAPS box
shows the new count. Kinect races keep their own count
([#75](https://github.com/i2kiselev/fm4-static-recomp/issues/75)).

Races of up to 55 laps have been played to the finish
([#102](https://github.com/i2kiselev/fm4-static-recomp/issues/102)) with steady frame rate and
memory use; the AI races the whole distance. What to expect on a long race:

- Tyres do not wear and no fuel is used, for you or the AI.
- Cars still on track when you finish keep racing behind the results screen until each one
  completes its laps; with lapped cars this can take many minutes.
- The replay does not cover the whole race: in a 55-lap race it ended at lap 27.
- Rewinding more than about 25 laps back shows a loading screen instead of rewinding on
  screen; it still takes you back.

## Fast post-race

**FAST POST-RACE** plays two of the waits around a World Tour race fast, without skipping
anything: each still plays to its normal end.

- **GLOBE**: the World Tour globe, which spins to the next event and zooms down to its track,
  takes about a second on opening World Tour (instead of about 9) and about two after a
  finished event (instead of about 12).
- **GLOBE + XP BARS**: the post-race XP and car affinity bars also fill in under a second
  (instead of about 6).

Level-up rewards, the championship result, gift cars and every other screen are unchanged.
Off by default. After SAVE & APPLY it works from the next race, with no restart.
## Difficulty

The **DIFFICULTY** rows change how the game itself plays, to make the career harder. Each is off (the
game's own behaviour) by default, and each only ever makes a race harder.

They work on the game's own data as it loads it: with an option on, the game is given an edited
copy of its database (`Media\db\gamedb.slt`) or of its tunables
(`Media\GameTunableSettings\GameTunableSettings.ini`). The files on disk are never changed,
so turning an option off again, or a mod changing those files, works as before. The game reads
them as it starts, so a change takes effect **from the next start**; the row says so until then.

Each change is made to the values the files hold, so on top of a mod's own (PFP4's lower
payouts, for one). A value stays in as many digits as the file gives it: where that is one
decimal (the disc's resale value, 0.5), -25% and -50% come out a little smaller, about -20% and
-40%. The log names each option applied with the values it wrote.

With RACE PAYOUTS on, the World Tour event panel still shows the event's full payout; the
earnings screen after a race pays the reduced amount.

| Setting | Choices | What changes |
|---|---|---|
| **RACE PAYOUTS** | DEFAULT, -25%, -50% | The credits a race pays by distance (`RaceWinnings\CreditsPerMile`) and the car makers' affinity level bonuses (`AffinityLevelDef`) |
| **REPAIR COSTS** | DEFAULT, +100%, +200% | The share of a race's winnings each damaged part costs (`RaceWinnings\DmgPenalty*`), a part at most the whole winnings |
| **RESALE VALUE** | DEFAULT, -25%, -50% | What selling a car pays (`CarValueScales\SellValueScale`, `RestockingValueScale`) |
