# SOLO Additions — Feature Overview & Change Summary

This document summarizes the custom systems added to **AwakenSolo** and the surgical patches applied during our review. It’s intended for developers, builders, and staff who need to operate, test, and expand these features.

---

## Contents
- [1. Pickpocket, Hacking, and Heat/Police](#1-pickpocket-hacking-and-heatpolice)
- [2. Dynamic Art System & Furniture Merchants](#2-dynamic-art-system--furniture-merchants)
- [3. Housing — Purchase (Ownership) vs. Lease](#3-housing--purchase-ownership-vs-lease)
- [4. Inner Voice / Entity Companion](#4-inner-voice--entity-companion)
- [5. Commands & Player-Facing UX](#5-commands--player-facing-ux)
- [6. Data Files & Content Layout](#6-data-files--content-layout)
- [7. Integration Points (Hooks)](#7-integration-points-hooks)
- [8. Operational Notes & Tuning](#8-operational-notes--tuning)
- [9. Patch Log (What We Changed)](#9-patch-log-what-we-changed)

---

## 1. Pickpocket, Hacking, and Heat/Police

**Status:** Implemented and wired.

- **Pickpocket system**
  - Awards loot through JSON-backed tables when available; falls back to a small cash skim if tables are empty.
  - Credstick-specific handling: prepared value ranges and flavor when an item is a credstick.
  - Heat anchor room is set once per failed attempt (no duplicate assignments).
  - Player feedback and action messaging cleaned up.

- **Hacking / Lockboxes / Terminal-aware messaging**
  - `do_hackbox` success path distinguishes between **physical lockboxes** and **virtual storage** via presence of a locker terminal in-room.
  - Messaging reflects the context (terminal chirp vs. lockbox panel flicker).
  - Physical lock toggling only occurs when not in virtual-storage scenarios.

- **Heat & Police escalation**
  - Per-second heat/police tick moved to the main loop’s **IRL per-second** block (`comm.cpp`), not nested under the flight pulse.
  - The system periodically spawns police presence based on tiered heat thresholds.

**Files touched (historical):** `src/act.other.cpp`, `src/comm.cpp`

---

## 2. Dynamic Art System & Furniture Merchants

**Status:** Implemented and wired.

- **Dynamic street art (“system art”)**
  - Seeder: `maybe_seed_system_art()` composes themed art (object vnum **#125**, “a work of art”) and persists spawns in `lib/etc/system_art_spawns.txt`.
  - Reseed throttle: ~once per real day via `point_update()` (`limits.cpp`).
  - Art is flagged **NOSELL**/**NORENT** to prevent exploits and rental cleanup loss.
  - Room display coloration for art objects is enabled.

- **Furniture merchants & art items**
  - New furniture/art vnums: **99000–99006** (photo frame, canvas/painting, etc.) in `lib/world/obj/99000_furniture.obj`.
  - Shops that stock them (examples): `lib/world/shp/330.shp`, `lib/world/shp/392.shp`.
  - Player customization: `DECORATE NAMEFRAME` / `DECORATE NAMEPAINTING` restring helpers with fee handling (bank-first).

---

## 3. Housing — Purchase (Ownership) vs. Lease

**Status:** Implemented and wired (ownership pathway).

- **Landlord supports BUY**
  - `BUY <unit>` sets owner, marks **purchased**, clears rent timers, saves lease, issues a key, and gridlogs the transaction.
  - `PAY` politely refuses on owned units (“no rent is due”).

- **Persistence & rules**
  - `lease.json` data now includes: `purchased`, `sale_in_progress`, `sale_complete_time`, `sale_payout` (old files load with safe defaults).
  - `Apartment::can_enter()` allows entry for owners/guests regardless of rent when `purchased == true`.
  - `LIST` shows **Buy:** price (default: `buy_cost = 8 × monthly rent`) for unowned units.

**Files changed:** `src/newhouse.cpp`, `src/newhouse.hpp`

---

## 4. Inner Voice / Entity Companion

**Status:** Implemented and wired.

- **Core behavior**
  - Per-player `VoiceState` with **attitude** (0…1,000,000), content cooldowns, recent contexts, and **entity name** (player-renamable via SAY once attitude is high enough).
  - **Persistence:** names saved/loaded per player in `lib/etc/innervoice/names/<id>.txt`.

- **Hooks (live)**
  - **Movement:** context-aware comments on room change; Tier 1 intel and Tier 2 actions may trigger.
  - **Combat:** low HP warnings; kill events comments; stealth warnings.
  - **Skills:** successes/failures feed attitude and generate commentary.
  - **Speech:** parses SAY to allow renaming at high attitude.

- **Tier thresholds & benefits**
  - **Tier 1** (≥ **500,000** attitude): adjacent-room intel, container peeks, stealth hints (chance scales with attitude).
  - **Tier 2** (≥ **800,000**): utility assists such as auto-sneak, auto-dim, weakening foes, and opportunistic door/chest unlocks.

- **Attitude decay**
  - Decays over time; now invoked from `tick_inner_voice()` approx once per minute.

- **Door/vehicle hooks**
  - Movement TU now includes the real header so these are **not** no-ops; public wrappers forward to `_impl` logic in the module.

**Files changed:** `src/innervoice.cpp`, `src/act.movement.cpp`, `src/inner_voice_shim.hpp`

---

## 5. Commands & Player-Facing UX

- **Theft / Hacking**
  - `PICKPOCKET <target>` (with contextual success/fail messages; heat adjustments).
  - `HACKBOX <object>` (terminal-aware messaging; physical vs. virtual storage distinction).

- **Housing**
  - `BUY <unit>` (at landlord).  
  - Existing lease verbs: `LIST`, `RETRIEVE`, `LEASE`, `LEAVE`, `BREAK`, `PAY`, `STATUS|INFO|CHECK`, `PAYOUT` (where applicable).  
  - Owned units: `PAY` is inapplicable; access allowed without rent ticking.

- **Decorate**
  - `DECORATE NAMEFRAME <keywords> = <name>`
  - `DECORATE NAMEPAINTING <keywords> = <name>`

- **Inner Voice**
  - Speak to rename the entity when your attitude is high enough (parsed via SAY).  
  - Comments arrive ambiently; assists may trigger when Tier 1/2 thresholds are met.

---



- **New QoL / Thief-Friendly Commands**
  - `DO <count> <command>` — repeat a command up to 50 times globally (stops on incapacitation).
  - `.` — repeat **your last full command line** (server-side repeat; ignores a literal dot when saving history).
  - `CASE <target>` — *non-invasive* odds preview for pickpocketing (estimates stealth vs. target awareness and bystander risk).
  - `AUTOSTASH <container>` — move non-equipped inventory into a named **container in your owned/purchased home** (refuses elsewhere).
  - `FENCE ALL` — at a fence NPC, bulk-sell eligible **paydata** items using the standard pricing & market logic.
  - **Heat HUD (diegetic)** — shows a compact red meter `[▉▉▉ m:ss]` in your prompt **only while you have heat** (auto-hides when heat reaches 0).

## 6. Data Files & Content Layout

- **Dynamic Art**
  - Spawns file: `lib/etc/system_art_spawns.txt`
  - Prototype: `lib/world/obj/0.obj` → **#125** (“a work of art”)

- **Furniture & Shops**
  - Furniture/art objects: `lib/world/obj/99000_furniture.obj`
  - Shops stocking them: `lib/world/shp/330.shp`, `lib/world/shp/392.shp`

- **Inner Voice**
  - Content roots: `lib/etc/innervoice/{ambient,rooms,items,skills,combat,vehicles,quests}`
  - Names: `lib/etc/innervoice/names/<playerid>.txt`

---

## 7. Integration Points (Hooks)

- **Per-second loop:** `comm.cpp` calls:
  - `update_thievery_heat_and_police()` (heat/police)
  - `InnerVoice::tick_inner_voice()` (entity comments & timers)

- **Combat:** `fight.cpp` → low-health & enemy-death notifications

- **Skills:** `utils.cpp` → success-test notification

- **Movement & Doors/Vehicles:** `act.movement.cpp` → room move; door & vehicle hooks (with real header include)

- **Speech:** `act.comm.cpp` → inner-voice rename parser

- **Art reseed:** `limits.cpp` → reseed throttle via `point_update()`

---



- **Prompt HUD:** `make_prompt()` conditionally appends a diegetic **Heat HUD** when `theft_heat_level > 0` (auto-removed on 0).

## 8. Operational Notes & Tuning

- **Heat thresholds** and police spawn pacing can be tuned where `update_thievery_heat_and_police()` is defined.
- **Art reseed cadence** is intentionally conservative (once/day) to avoid spam; adjust in `utils.cpp` if desired.
- **Housing buy cost** defaults to `8 × monthly rent` (`Apartment::get_buy_cost()`); tweak per game economy.
- **Inner Voice**
  - Adjust Tier 1/2 thresholds/chances in `innervoice.cpp` if you want earlier or later access to benefits.
  - The decay interval (now ~1/min) can be changed in `tick_inner_voice()`.
  - Ensure builders maintain the `lib/etc/innervoice/...` content to keep commentary fresh.

---

## 9. Patch Log (What We Changed)

**Pickpocket / Hacking / Heat**
- Fixed unterminated string in `do_hackbox` messaging (terminal-aware copy).
- De-duplicated heat anchor assignment in pickpocket fail path.
- Moved heat/police tick to **per-second** block in `comm.cpp`.

**Housing Purchase**
- Added `BUY <unit>` landlord verb; refuses `PAY` on owned units.
- Persisted ownership fields (`purchased`, `sale_*`) in `lease.json`.
- `Apartment::can_enter()` allows entry for owners/guests regardless of rent.
- `LIST` shows **Buy** price; keys issued on purchase.
- Implemented `Apartment::set_purchased(bool)` and used existing `set_owner(idnum_t)`.

**Inner Voice / Entity**
- Ensured per-second tick runs (and invokes attitude decay ~1/min).
- `act.movement.cpp` now includes `innervoice.hpp` so hooks aren’t no-ops.
- Added public wrappers forwarding to internal `_impl` functions.
- Guarded loaders to quietly skip any missing content patterns.

**Dynamic Art / Merchants**
- Confirmed seeding, persistence, object flags, and shop stock; no code changes required here during patching.

---

### Testing Checklist (quick)
- Pickpocket an NPC with/without loot tables; observe heat changes and police escalation over time.
- Hack a **terminal** vs. a **standalone lockbox**; verify distinct messages and lock toggle behavior.
- At landlord: `LIST` shows **Buy** price; use `BUY <unit>`; confirm key issued and `STATUS` says “owned”; `PAY` refuses; zone reload persists ownership.
- Buy a frame/canvas at furniture shops; use `DECORATE NAMEFRAME/NAMEPAINTING` to rename.
- Walk, fight, succeed/fail skill checks; watch Inner Voice comments; raise attitude to trigger Tier 1 then Tier 2; observe assists (auto-sneak, dim, door/chest unlocks).

---

*Prepared for the AwakenSolo codebase — custom systems review & integration summary.*


**Player QoL & Thief Enhancements**
- Added conditional **Heat HUD** in the prompt (appears only when you have heat).
- Implemented global action queue: `DO <count> <command>`.
- Implemented server-side repeat: `.` (replays last full command line).
- Added `CASE <target>` odds preview (no action taken).
- Added `AUTOSTASH <container>` (owned purchased home + container only; skips worn and no-drop items).
- Added `FENCE ALL` bulk-sell at fence NPCs (reuses paydata sell logic).

---

## 10. In-Game Help Entries (recommended)

Create/extend the following HELP topics so players can discover features quickly:

### HELP HEAT
- **Heat HUD:** While you have heat, your prompt shows a red meter like `[▉▉▉ m:ss]`. It auto-hides at 0 heat.
- Heat rises from crimes, then decays over time. Higher heat attracts police presence.
- Tips: Lay low; avoid crowds; stash goods at home.

### HELP DO
- **Syntax:** `DO <count> <command>`
- Repeats a command up to 50 times (global). Stops if you become incapacitated.
- Example: `do 5 pickpocket guard`
- See also: `HELP REPEAT`

### HELP REPEAT
- **Syntax:** `.`
- Repeats your last full command line (server-side). The dot itself is not saved as last command.

### HELP CASE
- **Syntax:** `CASE <target>`
- Gives a *non-invasive* estimate of pickpocket odds (your stealth vs. target awareness) and shows bystander risk.
- No action taken; use `PICKPOCKET <target>` to attempt.

### HELP AUTOSTASH
- **Syntax:** `AUTOSTASH <container>`
- Moves your non-equipped inventory into a **container in your own purchased home**.
- Refuses outside your owned home. Skips worn and non-droppable items.

### HELP FENCE
- **Bulk sell:** While at a fence NPC, `FENCE ALL` sells eligible **paydata** in bulk using standard pricing & market adjustments.
- The regular `SELL` command still works for single items.
- Not all items qualify; the fence will tell you if you have nothing they can use.

# Adventurers — System Overview & Features

This document explains **what Adventurers are**, how they behave, and what features you can expect in-game. It’s written for builders/admins and curious players.

---

## What are Adventurers?

*Adventurers* are NPCs that simulate other runners in the world. They:
- Spawn dynamically when a player moves between zones
- Use real **archetypes** (skills, spells, cyber/bio, gear) from your codebase
- Scale in **difficulty tiers** (Green → Street → Runner → Prime → Apex)
- Carry **class-appropriate gear** (e.g., mages use pistols/light armor; samurai get melee/AR + heavier armor)
- **Respond to socials**, can be **pickpocketed**, and will **fight back**
- **Despawn** automatically from zones that have been empty of players for a while

The goal is to keep the world lively while you play solo.

---

## Feature Highlights

- **Zone-aware spawning:** On player **zone change**, the system rolls a configurable chance (default **5%**). On success, it spawns an Adventurer into a **random eligible room anywhere in that zone**—not necessarily your room.
- **Eligible room filter:** Avoids problematic rooms by default (e.g., **PEACEFUL**, **NOMOB**, **ARENA**, **STORAGE**, **ASTRAL**, radiation, cramped, staff-only, etc.) and supports allow/deny lists in config.
- **Zone inactivity despawn:** If a zone has **no players for 900 seconds** (15 minutes, configurable), all Adventurers in that zone are safely removed.
- **Real archetypes:** Each Adventurer is built from one of your `archetypes.cpp` entries—applying **skills**, **spells**, **adept powers**, **worn gear**, **carried items**, **cyberware**, **bioware**, **foci**, **decks/software**.
- **Tiered difficulty:** After archetype application, a **tier** is chosen (weighted). Tiers apply **skill multipliers** (+/− jitter) and **attribute multipliers** (and optional additive bumps), plus **tougher gear/ware** at higher tiers.
- **Class-aware loadouts:** The system detects the archetype’s **class** (mage/adept/decker/samurai) and prefers **class-appropriate** weapon/armor/ware choices, falling back to tier defaults as needed.
- **Social reactions:** Adventurers react to player **socials**—with targeted and room-ambient responses. Defaults are provided; you can add per-social responses via config.
- **Combat-ready:** Aggressive mobs can target Adventurers like PCs; Adventurers defend and fight using standard combat rules.
- **Economy & pickpocketing:** Adventurers are **pickpocketable** (no `MOB_NOSTEAL`). They carry **loose cash** and (optionally) a **credstick** object for theft/loot, with amounts scaled by tier.
- **Identity & theming:** Random **first/last names**, adjusted **room/long descriptions**, and **keywords** are assigned on spawn.

---

## How it works (pipeline)

1. **You enter a new zone.**  
   The system notes your last zone. If the zone changed, it rolls the chance from config (default **5%**).

2. **Pick an eligible room in that zone.**  
   From all rooms in the zone, the system filters out disallowed ones and picks one at random.

3. **Create a baseline humanoid.**  
   A **template mobile** (configurable VNUM) is cloned as the base body (neutral stats).

4. **Apply an archetype.**  
   A random archetype from `archetypes.cpp` is applied: **skills, spells, powers, worn/carried gear, cyber/bio, foci, deck/software**.

5. **Apply a tier.**  
   The tier adjusts **skills** (multiplier + jitter) and **attributes** (multipliers and/or additive bumps), and adds tier-appropriate **gear/ware**.

6. **Bias loadout by class.**  
   The archetype’s class (mage/adept/decker/samurai) steers the weapon/armor/ware choices toward class-flavor.

7. **Economy & pickpocket.**  
   The NPC gets **loose cash** and (if mapped) a **credstick** item; `MOB_NOSTEAL` is cleared so steal works as expected.

8. **Place the Adventurer.**  
   The NPC is placed into the selected room. Their room description shows a quick class/tier hint, e.g. *(looks samurai Runner)*.

9. **Despawn when idle.**  
   When a zone has no players for the configured time, all Adventurers in that zone are cleaned up.

---

## Difficulty Tiers (example defaults)

| Tier  | Weight | Skill Mult | Attr Multipliers (BOD, QUI, REA, STR, INT, WIL) | Gear/Notes |
|------:|-------:|-----------:|-----------------------------------------------:|:-----------|
| Green | 25     | 0.65       | 1.00, 1.00, 1.00, 1.00, 1.00, 1.00             | Light pistols, lined coats |
| Street| 35     | 0.85       | 1.00, 1.05, 1.05, 1.00, 1.00, 1.00             | Pistols/SMGs, jackets |
| Runner| 25     | 1.00       | 1.05, 1.05, 1.05, 1.05, 1.00, 1.05             | SMG/AR/shotgun, FFBA/security |
| Prime | 12     | 1.20       | 1.10, 1.10, 1.10, 1.05, 1.05, 1.10             | AR/GL/LMG, heavier armor |
| Apex  | 3      | 1.40       | 1.15, 1.20, 1.20, 1.10, 1.10, 1.20             | RC weapons, mil-spec |

> You can adjust the weights and multipliers in `lib/etc/adventurer_difficulty.txt`.

---

## Class-aware Loadouts (examples)

- **Mage**: weapons → `light_pistol, heavy_pistol`; armor → `armored_vest, armored_jacket`; ammo → `ball`; init ware → `none`  
- **Adept**: weapons → `katana, melee_weapon, heavy_pistol`; armor → `armored_jacket, sec_armor`  
- **Decker**: weapons → `light_pistol, smg`; armor → `armored_vest, armored_jacket`  
- **Samurai**: weapons → `katana, assault_rifle, shotgun, lmg_rc`; armor → `sec_armor, sec_armor_ffba, mil_spec_light`; ammo → `ball, apds, exex`

> Customize per class in `lib/etc/adventurer_class_gear.txt`. If an entry is missing, the system falls back to the tier’s gear list.

---

## Social Reactions

Adventurers respond to player **socials** (150+ supported). They can:
- **Say** something back
- **Emote** in the room
- Perform an **action** (e.g., `grin`, `nod`) targeted at you or ambient

You can extend responses with `lib/etc/adventurer_social_replies.txt`:
```
hug|targeted|say|"Careful there, %name%."
wave|room|emote|"gives a curt nod."
```
- `targeted` vs `room` scopes the response
- `%name%` is replaced with the actor’s in-room name

Cooldowns prevent spam; Adventurers must be awake and able to see the actor.

---

## Combat & Aggro

- Adventurers are attacked by **aggressive mobs** under your normal rules.  
- They **defend and fight back** with their tiered skills, attributes, gear, and powers/spells.  
- **MOB_NOSTEAL** is cleared so **Steal** works; they hold cash/credsticks based on tier.

---

## Economy & Loot

- **Loose cash:** Random between **100–1000**, scaled by tier (e.g., Runner/Prime/Apex carry more).  
- **Credstick item:** Randomly chosen from mapped VNUMs; the amount (1000–10000, tier-scaled) is set on the object when supported by your object schema.

> If a particular alias has **no VNUMs** mapped, the system **skips it gracefully** and continues (no crash, no block).

---

## Movement & Behavior Flags

- Adventurers are **not SENTINEL** (they may move if your base AI allows wandering), but are **STAY_ZONE** (won’t cross zones).  
- They are **not WIMPY**.  
- Other behavior (pathing, scripts) can be layered using your usual world-building tools if desired.

---

## Configuration Files (summary)

- `lib/etc/adventurer_spawn.txt`  
  - `template_mobile_vnum=…` — baseline humanoid to clone
  - `zone_entry_spawn_chance_percent=5` — spawn roll on zone change
  - `despawn_zone_inactivity_seconds=900` — zone idle cleanup
  - `spawn_allow_flags=...` / `spawn_disallow_flags=...` — optional filters

- `lib/etc/adventurer_difficulty.txt`  
  - Lines starting `tier|…` define tiers, weights, skill & attribute scaling, and default gear/ware aliases.

- `lib/etc/adventurer_class_gear.txt`  
  - Per-class preferences (weapons, armor, ammo, init ware).

- `lib/etc/adventurer_gear_map.txt`  
  - Alias → **actual object VNUMs** from your repo. Already populated for you.  
  - Safe to leave an alias empty; it will be skipped.

- `lib/etc/adventurer_first.txt`, `lib/etc/adventurer_last.txt` *(optional)*  
  - Name lists for random identity generation; defaults are used if absent.

- `lib/etc/adventurer_social_replies.txt` *(optional)*  
  - Additional per-social responses.

---

## Integration (where to wire it in)

> Full copy/paste steps live in **INTEGRATION_PATCH_NOTES_adventurers_all_in_one.txt** (or the simplified `INTEGRATION_STEPS_SIMPLE.txt`). In short:

1. `handler.cpp` — at the end of `char_to_room()`, call `adventurer_on_pc_enter_room(ch, room)` for PCs.  
2. `comm.cpp` — once per IRL second, call `adventurer_maintain()`.  
3. `act.social.cpp` — after a social executes, call `adventurer_notify_social(ch, vict, cmd)`.  
4. `spec_assign.cpp` — add `SPECIAL(adventurer_spec)` and assign it with `ASSIGNMOB(<template VNUM>, adventurer_spec)`.

That’s it—no room lists or manual VNUM edits required.

---

## Troubleshooting

- **“They’re too strong!”**  
  Lower tier weights or multipliers in `adventurer_difficulty.txt`, or choose a weaker `template_mobile_vnum`.

- **“They don’t spawn here.”**  
  Check room flags and `spawn_disallow_flags` / `spawn_allow_flags`. PEACEFUL/NOMOB/ARENA/ASTRAL/etc. are excluded by default.

- **“Steal failed / no credstick.”**  
  Ensure `credstick|…` has at least one VNUM in `adventurer_gear_map.txt`. Loose cash is independent and should still work.

- **“Mages using rifles?”**  
  Add/adjust class gear in `adventurer_class_gear.txt` (they’ll still fall back to tier defaults if empty).

---

## Quick Test Checklist

- Move between zones → sometimes an Adventurer spawns (5% default).  
- Leave a zone empty for 15+ minutes → Adventurers there despawn.  
- Use socials near them → they respond (targeted and ambient).  
- Aggressive mobs can attack them → they fight back.  
- Try **Steal** → pickpocket succeeds; loot includes cash and (if mapped) a credstick.

Enjoy the company—happy running!
