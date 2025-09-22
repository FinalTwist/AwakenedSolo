
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
