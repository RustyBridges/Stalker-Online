# RPG systems: progression and world events

Status: design for the `stalker-online` fork. Decisions below come from the
project owner's answers; numbers are starting values to tune in playtests.

Inspiration: the mechanics of Daggerfall-style RPGs (skills that improve by
use, a background chosen at the start, level derived from skill growth),
fitted to S.T.A.L.K.E.R.: Call of Pripyat. No content from other games.

## Goals

- The player gets better at what they actually do, slowly, over a full
  playthrough.
- A veteran feels noticeably better than a newcomer, but the Zone stays
  deadly: a maxed skill gives roughly +25-40%, never more.
- The Zone changes on its own: emissions have consequences, mutants migrate,
  and jobs are generated from what happens.
- Works the same on Windows and Linux (it is game code, not platform code).

## Part 1: player progression

### Backgrounds (chosen at New Game)

| Background | Head start |
| --- | --- |
| Ex-soldier | Marksmanship, Endurance |
| Smuggler | Barter, Stealth |
| Scientist's assistant | Artefact lore, First aid |
| Drifter | Endurance, Resilience |

A head start is a few levels in those skills; everything else starts at the
base value. After that, everything grows by use.

### Skills (all improve by use)

| Area | Skill | Grows when you... | Effect at max (≈) |
| --- | --- | --- | --- |
| Combat | Marksmanship | hit enemies with firearms | −35% weapon spread and recoil, faster aim |
| Combat | Melee | hit with the knife | +40% knife damage |
| Survival | Endurance | sprint, carry heavy loads | +30% stamina, +15 kg carry weight |
| Survival | First aid | heal real damage with medkits/bandages | +40% healing from medical items |
| Survival | Resilience | survive radiation, anomaly and psi damage | −25% radiation/anomaly damage |
| Social / trade | Barter | buy and sell | prices up to 30% better |
| Zone lore | Artefact lore | find and sell artefacts | +30% artefact sale value, longer detector range |
| Zone lore | Stealth | move crouched near enemies without being noticed | enemies notice you up to 30% later |

Skills range 1-100. Each use gives progress toward the next point; the
amount needed grows with the skill, so early points come quickly and the last
ones take a full playthrough. Gains have cooldowns so they cannot be farmed
(e.g. shooting a wall gives nothing; only hits on living enemies count).

### Character level

Level rises as total skill points rise (every N skill points gained = one
level). Level does not give stats by itself; it **unlocks content**:

- better-paid, harder dynamic jobs;
- better stock at traders;
- faction trust (special tasks, discounts from faction leaders);
- top-tier weapon and suit upgrades at technicians.

Level-ups arrive as PDA news messages.

### UI

A new **Character** tab in the PDA: background, level, the eight skills
with progress bars, and what the next level unlocks.

### Persistence

Skill values, progress and background are stored in the actor's save data,
so they survive save/load like everything else.

### Where the code lives

Most effects belong in the engine (C++), because that is where the numbers
are applied and where save data lives:

| Piece | Location |
| --- | --- |
| Skill data, gains, save/load | new `xrGame/rpg/` module, owned by `CActor` |
| Weapon spread and recoil | `CWeapon::GetFireDispersion`, `CActor` camera recoil |
| Stamina, carry weight | `CActor::MaxCarryWeight`, actor condition |
| Trade prices | `CTrade::GetItemPrice` |
| Healing, damage taken | actor condition / hit processing |
| Gain events | `CActor` hit, use, trade and item callbacks |
| Character tab | `CUIPdaWnd` + a new UI window and XML layout |
| Background choice | dialog at the start of a new game |
| Lua access | exported functions so scripts (jobs, traders) can read level and skills |

Testing helpers: console commands to show the sheet, add skill points and
change background, so effects can be checked without hours of play.

## Part 2: world events

These hook into the game's own scripts (emission manager, simulation board,
trader and dialog scripts), so they need copies of the original script files
from the game archives to modify.

### Emissions with consequences

After each emission:

- **new artefacts** spawn in anomaly fields;
- **anomalies shift**: some anomalies move or appear and disappear, so known
  safe routes change;
- **aftermath events**: dead stalkers to loot, wounded stalkers calling for
  help, mutants moving.

### Mutant migrations

1. A PDA rumour warns that a pack is heading somewhere ("bloodsuckers seen
   moving from the swamps toward Skadovsk").
2. Some time later the pack actually travels there.
3. The player can intercept, avoid or help defend.

### Dynamic jobs

Generated from what happens in the world (an emission left wounded, a
migration threatens a camp, a caravan was ambushed). Rumours arrive as PDA
news; traders and faction leaders offer the actual jobs in dialogue.
Harder, better-paid jobs need a higher character level.

## Milestones

1. **Progression core**: skills, gains, effects, save/load, background
   choice, console helpers. Test with the console sheet before any UI.
2. **Character tab** in the PDA (needs the original `pda.xml`).
3. **Level unlocks**: trader stock, upgrade tiers, faction trust.
4. **Emission consequences**, then **migrations**, then **dynamic jobs**
   (need the original scripts).
