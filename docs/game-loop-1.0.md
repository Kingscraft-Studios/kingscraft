# Kingscraft 1.0 Game Loop

**Status:** Living design contract — pre-Alpha
**Last updated:** 2026-09-17
**Scope:** Defines the full 1.0 game loop. Alpha and Beta implement slices of this
document; 1.0 implements it end to end. Anything marked *Open* is intentionally
unpinned and must be settled before the milestone that builds it.

This is the authoritative reference for *what the game is*. When an
implementation detail conflicts with this document, this document wins. When
reality forces a change, change this document first, then the code.

---

## 1. Thesis

Kingscraft is a **connected sandbox 3D RPG**. Reality and Magic are two faces of
one world, and every system feeds every other system. The player is not a
lone survivor farming a boss arena; the player is a newcomer in a living region
whose reputation, settlements, machines, spells and dimensions all remember what
they did.

The genre is a sandbox: the player may grind, explore, build, trade, fight, or
chase the story. The game never forces a single path. But unlike a bare sandbox,
the world has a **narrative spine** — a corruption spreading from a false god —
and unlike a lobby RPG, that spine grows out of the sandbox itself.

### 1.1 Design laws

1. **Everything connects.** A feature ships only if it is fed by, and feeds,
   at least two other systems (see the connectivity matrix in section 10).
2. **No instant crafting.** Production is a *place* and a *relationship*, not a
   menu. Planks come from a lumberjack's workstation, not from thin air.
3. **Progression is social and world-driven.** Power is earned through
   reputation, knowledge, and discovery — not only through loot drops.
4. **The world remembers.** Quests, reputation, cleared corruption, built
   machines, and slain bosses persist. Actions have consequences that outlive
   the session.
5. **Original by construction.** Every name, texture, model, creature and
   system is ours. The well-known conventions of the genre are a starting
   vocabulary, never a template to copy.

---

## 2. The Core Loop

The moment-to-moment loop is **Discover, Learn, Empower, Affect**, repeated.

```text
        ┌────────────┐
        │  DISCOVER  │  explore the region; find settlements, ruins,
        └─────┬──────┘  ley-touched ground, glitched areas
              │
              ▼
        ┌────────────┐
        │   LEARN    │  accept quests, meet professions, observe the
        └─────┬──────┘  world's systems (why did the glitch appear?)
              │
              ▼
        ┌────────────┐
        │  EMPOWER   │  gain reputation, recipes, tools, machines,
        └─────┬──────┘  spells, essence; grow the character and the base
              │
              ▼
        ┌────────────┐
        │   AFFECT   │  reshape terrain, grow a settlement, clear
        └─────┬──────┘  corruption, seal the gate, dethrone the false god
              │
              └──────────────► (new discoveries open)
```

### 2.1 Loop timescales

| Timescale | Loop | Example |
|---|---|---|
| Seconds | Build / mine / fight | Place a block, land a hit, dodge a hazard |
| Minutes | Gather → produce | Chop logs, carry them to the workstation, make planks |
| Hours | Quest → reputation → unlock | Finish a lumberjack contract, gain standing, unlock a craft |
| Sessions | Region arc | Clear the glitched areas sapping a settlement |
| Whole game | The corruption arc | Reach the false god and end his pretence |

---

## 3. Settlements, Professions, Quests, Reputation

This is the heart of 1.0 and the clearest break from the genre's status quo.

### 3.1 Settlements

A settlement is a curated, hand-authored (later procedurally placed) structure
with a small population of **profession NPCs**. Working name: *Hearthstead*.

Each settlement tracks:

- its own **standing** with the player (town-wide reputation), and
- the **personal reputations** of the professions within it.

Settlements are hubs for quests, workstations, trade, and shelter. They are
also the primary source of the knowledge that unlocks production.

*(Open: how settlements are placed and how many exist per region in 1.0.)*

### 3.2 Professions

Each profession is an NPC role that owns a **workstation** and a **quest deck**.
Production chains are gated behind the profession's goodwill.

| Profession | Workstation | Produces | Gives access to |
|---|---|---|---|
| Lumberjack | Sawyer's Bench | planks, sticks, shafts | timber construction |
| Armorer | Forge / Anvil | armor plates, repairs | protection |
| Toolsmith | Tool Bench | pickaxes, axes, tools | better mining/gathering |
| *(more in 1.0)* | | | |

*(Open: full 1.0 profession roster — masonry, cooking, alchemy, enchanting, etc.
The roster is part of the 1.0 contract and will be finalized during Beta.)*

### 3.3 Quests

- Every profession offers a **deck of five quests**.
- Quests are **timed**. Complete before the deadline or the profession's
  reputation with the player drops.
- Reputation changes **spread**: one offended profession tells the others, and
  a low town standing makes the remaining quests harder (higher requirements,
  shorter deadlines, poorer rewards).
- **Soft floor:** reputation loss is tiered, never bottomless-blackball. A
  standing can be repaired with a **forgiveness quest** (see 3.4). This keeps
  pressure without a death spiral.

*(Open: exact timer durations, difficulty tiers, and reward tables.)*

### 3.4 Reputation rules (baseline)

1. Success raises reputation with the quest giver; failure lowers it.
2. Below a threshold, other professions in the settlement raise their quest
   requirements by one tier.
3. Reputation never drops below "Wary"; a profession that is Wary offers one
   **forgiveness quest** whose completion restores "Neutral".
4. Town standing is an aggregate of its professions and gates settlement-level
   perks (housing, trade discounts, safe zones).

---

## 4. Workstations and Production

**No instant crafting.** A recipe is executed at a workstation owned by a
profession the player has the standing to borrow.

- The player brings **inputs** (logs, ore, scrap, essence) to the station.
- The station processes them into **outputs** (planks, sticks, plates, parts).
- Higher standing unlocks more of the station's recipe list.
- Stations are physical: they exist in the world, occupy a structure, and can
  be visited, not conjured from a menu.

The classic "log → planks → sticks" chain still exists, but the *table itself*
belongs to the world, not the player's pocket. A player who wants full
independence must eventually earn the standing to build their **own** station,
which is a late-game freedom, not a starting one.

*(Open: whether the player can ever place personal workstations in 1.0, and at
what reputation/technology tier.)*

---

## 5. Progression Systems

Four currencies of progress, each feeding the others:

1. **Reputation** — social capital with settlements and professions. Unlocks
   workstations, quests, housing and trade.
2. **Knowledge** — a discovery log of the world's systems (first glitch,
   first ley node, first machine). Unlocks experimental recipes and lore.
3. **Essence** — magical material drawn from ley-touched ground, spirits and
   corrupted zones. The fuel for magic and the binding agent for advanced
   technology.
4. **Material wealth** — blocks, ore, salvage, parts. The sandbox baseline.

Knowledge + Reputation gate *what you can make*. Essence + Materials gate *what
you can make it with*. Technology and Magic are two expressions of the same
resource economy — this is the in-fiction meaning of "everything connects".

---

## 6. The Four Dimensions

1. **OverWorld** — the living reality sandbox: settlements, quests, survival,
   glitched areas, the player's home region.
2. **Tech Wasteland** — ruins and wreckage; a source of **machines and the
   ingredients to build them**. Technology's frontier.
3. **The Magic Expanse** *(working name)* — where **Magic content lives**:
   spells, magic items, unexplored powers. It also contains the **Warden**, the
   gatekeeper to the fourth dimension.
4. **The Boss Domain** *(working name)* — **locked**. A boss-only arena and its
   loot. It is not an explorable sandbox; it is a single confrontation.

### 6.1 Dimension rules

- The Warden guards the way from the Magic Expanse into the Boss Domain.
- Entering the Boss Domain is **one-time**. The player must **please the
  Warden** to be allowed through, and may only pass once.
- **After the boss is slain, the Boss Domain seals forever and the Warden
  disappears.** Dimensions 1–3 remain open — the world after the boss is a
  tech-and-magic power sandbox, not a dead end.
- *(Open: the exact terms that please the Warden — a mechanic the design owner
  intends to define. Until defined, the doorway is a locked quest target.)*
- *(Open: what happens if the player enters and dies in the Boss Domain —
  whether the one-time gate is consumed on entry or on victory.)*

### 6.2 Implications

Because the Boss Domain seals on victory, **everything it offers must be
obtainable in that single encounter** (boss drop plus an arena cache). There is
no farming a sealed arena. This is deliberate: the boss is a milestone, not a
loot treadmill.

---

## 7. The Corruption Arc

### 7.1 The false god

The **Corrupted Guardian** (working name) slowly abused his power until he
presented himself as the god of the game's universe. His corruption leaks into
reality as **Glitched Areas**.

### 7.2 Glitched Areas

- **Randomly spawning** corrupted regions in the OverWorld and Tech Wasteland.
- They warp the surrounding world and are hostile to habitation.
- Clearing one yields **essence and corrupted material**, and shifts the
  region's stability.
- They are the connective tissue of the whole loop: exploration finds them,
  quests react to them, machines and magic are needed to clear them, and the
  cleared material powers the journey toward the false god.

### 7.3 The arc

```text
discover settlements & professions
        -> earn reputation & production
        -> glitched areas appear; settlements need help
        -> clear glitches; gather essence & corrupted material
        -> reach the Magic Expanse; learn magic; build technology
        -> please the Warden; pass once into the Boss Domain
        -> dethrone the false god
        -> the gate seals; the world lies open to build, explore and power
```

---

## 8. Post-1.0 Endgame and Live Content

Slaying the false god does **not** end the game. It unlocks the sandbox in its
fullest form:

- Explore, build, and power-fantasy freely with Technology and Magic.
- Claim, grow and fortify settlements.
- Pursue the professions' long-tail goals.

After 1.0, **each content update adds more**: new places, new loot, new
professions, new spells, new glitch types. This is why every system in 1.0
must be **data-driven** (blocks, recipes, quests, mobs, dimensions described in
data, not code) — updates should be content drops, not engine rewrites.

---

## 9. Release Ladder

| Milestone | Goal | Contains |
|---|---|---|
| **Alpha** (Dec 2026) | Playable slice of the loop | OverWorld; one settlement; Lumberjack deck + reputation; his workstation (log→plank→stick); one Glitched Area; reach the Magic Expanse (locked). Free sandbox building available. Experimental features welcome. |
| **Beta** (early 2027) | Breadth and polish | Add Armorer/Toolsmith; first Tech Wasteland zone + machines; more glitches; audio; stability and balancing. |
| **1.0** | The full contract | All four dimensions; the Warden's terms; the boss arc; post-slay endgame; full profession roster; complete progression economy. |
| **Post-1.0** | Live content | New regions, loot, professions, spells, glitch types — shipped as data updates. |

**Cut order when behind schedule:** polish first (audio, particles, post-FX,
secondary content). Gameplay systems are protected last.

---

## 10. Connectivity Matrix

Each system must touch at least two others. Current intended wiring:

| System | Feeds | Is fed by |
|---|---|---|
| Settlements & reputation | quests, workstations, housing, trade | player actions, glitched areas, knowledge |
| Workstations & production | building, tools, machines | reputation, materials, knowledge |
| Glitched Areas | essence, corrupted material, quests | world time, corruption arc |
| Magic (Dim 3) | combat, clearing, construction | essence, knowledge, reputation |
| Technology (Dim 2) | machines, automation, travel | salvage, materials, knowledge |
| Knowledge log | recipes, lore, unlocks | discovery, quests, exploration |
| The corruption arc | stakes, dimension access, endgame | everything above |

If a proposed feature cannot fill two cells in this table, it is not ready.

---

## 11. Systems Map (engine)

Where the 1.0 systems live or will live in the codebase.

| System | Status | Location / target |
|---|---|---|
| Items, stacks, inventory | Planned (M0) | `include/Core/Items/` |
| Block gameplay properties (data-driven) | Planned (M0) | `Core/Blocks/`, `Core/Resources/ModelParser` |
| Player health / damage / death causes | Partial (void only) | `Core/World/PlayerController`, `Event/Events/PlayerDeathEvent` |
| Config / settings persistence | Planned (M0) | `IO/` |
| Settlements & professions (NPCs) | Planned | new `Core/World/Settlement/`, entity framework |
| Quests & reputation | Planned | new `Core/Gameplay/Quests/` |
| Workstations & recipes | Planned | new `Core/Gameplay/Production/` |
| Dimensions & portals | Scaffold (M0) | new `Core/World/Dimension/` |
| Glitched Areas | Planned (M2) | new `Core/Gameplay/Corruption/` |
| Magic & essence | Planned | new `Core/Gameplay/Magic/` |
| Boss & Warden | Planned (1.0) | new `Core/Gameplay/Boss/` |
| Persistence | Partial (text regions) | `IO/Templates/`, `docs/world-format.md` |

Event-driven integration follows the existing pattern: systems communicate
through `Event/Events/` and listeners registered in `Bootstrapper`, with
cross-thread delivery handled by the dispatcher's thread-affine routing.

---

## 12. Open Questions

To be settled before the milestone that builds them:

1. The exact terms that **please the Warden**, and whether the one-time gate is
   consumed on entry or on victory.
2. Full **profession roster** for 1.0.
3. Quest **timer, difficulty-tier and reward** numbers.
4. Whether players can place **personal workstations**, and at what tier.
5. Settlement **placement/count** per region in 1.0.
6. **Multiplayer / PvP** scope: Alpha is single-player; whether Beta or 1.0
   introduce shared worlds, and how PvP coexists with settlement reputation.
7. Whether **hunger / survival pressure** belongs in 1.0 or is optional.

---

## 13. Non-Goals and IP Safety

- No copied names, textures, models, creature designs, achievement lists, or
  interface layouts from any existing game.
- Genre conventions are a shared vocabulary, not a template: we implement the
  *concepts* (mining, building, survival) with our own identity.
- The settlement/profession/reputation loop and the corruption arc are
  Kingscraft originals and are the intended public identity of the game.
- Working title "Kingscraft" is provisional pending an availability check.
