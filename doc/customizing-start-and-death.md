# Customizing New Character and Death Locations

Instance-MUD currently has two source-level defaults that most world builders
will want to change for their own worlds:

| Purpose | Current default | Source location |
|---|---:|---|
| New character starter instance zone | `23` | `src/interpreter.c` |
| New character starter instance entry room | `2300` | `src/interpreter.c` |
| Player death room | `59900` | `src/fight.c` |

These are deliberately simple constants for now. Change them in source, rebuild,
and make sure the matching world data exists in either flat files or SQL.

## New Character Starter Instance

New characters are first placed at the normal mortal start room long enough for
login setup, then after the MOTD flow they are moved into an instance of the
starter zone.

Edit these constants near the top of `src/interpreter.c`:

```c
#define STARTER_INSTANCE_ZONE 23
#define STARTER_INSTANCE_ROOM 2300
```

`STARTER_INSTANCE_ZONE` is the zone vnum to instance for new players.
`STARTER_INSTANCE_ROOM` is the room vnum inside that zone where the new player
should appear.

The starter zone must be configured as a dungeon/instance zone. If the zone is
not flagged correctly, or if the entry room does not exist, the game logs the
failure and leaves the player at the normal mortal start room instead.

Recommended starter-zone checklist:

- The zone exists in `lib/world/zon/<zone>.zon` or in the SQL `zon` table.
- The zone is marked with the dungeon/instance flag used by your world data.
- The entry room exists in `lib/world/wld/<zone>.wld` or in the SQL `wld` table.
- Any starter equipment or setup scripts are attached to the instance entry
  room or login flow.
- The zone has a scripted or room-based exit that eventually moves the player to
  your real-world start area.

## Player Death Room

When a player dies, Instance-MUD no longer logs them out. Instead, it moves them
to a real-world death room. The default is room `59900`.

Edit this constant near the top of `src/fight.c`:

```c
#define PLAYER_DEATH_ROOM 59900
```

The death room must be a normal real-world room, not an instanced room. If the
room is missing, the game logs an error and falls back to the configured mortal
start room.

Recommended death-room checklist:

- The room exists in `lib/world/wld/<zone>.wld` or in the SQL `wld` table.
- The zone containing it exists and is indexed.
- Room enter triggers are safe to run immediately after death.
- Any resurrection timer or teleport script eventually returns the player to a
  normal play area.

## Flat Files

For flat-file worlds, add or update the relevant files under `lib/world/`:

```text
lib/world/zon/<zone>.zon
lib/world/wld/<zone>.wld
lib/world/mob/<zone>.mob
lib/world/obj/<zone>.obj
lib/world/shp/<zone>.shp
lib/world/trg/<zone>.trg
lib/world/qst/<zone>.qst
```

Then add the filenames to each matching `index` and `index.mini` file. Keep all
world and index files using Unix LF line endings.

## SQL Worlds

For SQL-backed worlds, the source constants still use room and zone vnums. The
matching records must exist in SQL.

If you created or edited the flat files and want to import them into SQL while
the MUD is running, use the implementor-only import command:

```text
sqlimport zon <zone>.zon
sqlimport wld <zone>.wld
sqlimport mob <zone>.mob
sqlimport obj <zone>.obj
sqlimport shp <zone>.shp
sqlimport trg <zone>.trg
sqlimport qst <zone>.qst
```

Only import the file types you actually changed. If the SQL tables are empty at
startup, Instance-MUD can populate them from the flat files automatically.

## Rebuild

After changing the source constants, rebuild from the project root:

```sh
./configure && touch src/.accepted
cd src
make
```

Restart the MUD with the rebuilt binary. For SQL mode, keep using your normal
startup flags, such as `--usesql`.
