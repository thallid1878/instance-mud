Instance Mud is different generation of mud, a classless, levelless system that can live in flat-files on disk or use an SQL server for holding data. The biggest addition is zone instances a block of 100 room that are dynamicly loaded and unloaded as needed to have instance zones for a single person or large group of people. The combat system has been completely overhauled, no longer are the days of AC and Thac0, instead we are using a stat based attacker roll vs a stat based defender roll to check for hit or miss. Armor is now a damage reduction % with diminishing returns that if stacked to insane amounts will cap damage at 1 though that should not be possible to reach without intent.

Some of the level and older code has been left in place but is being ignored and unhooked from the system, this is currently in place to allow for easy importing of existing tbamud/circlemud 3.x zones into instance-mud. A good example of this on our internal test server is the use of the circlemud 3.x zone infernal pit of kerjim which is still available in the tbamud google docs for download is one of the zones I am currently using as a test dungeon. In the future this code may be removed with a function in place to translate the zone files, but right now I am just ignoring things like level and level restrictions on zones. I know the zone also exists for tbamud and I was just checking that I could import an old zon and make it an instance. The old one is not included with this repo.

All Players will stat with 10 in all their stats, Players can earn exp and then use buystat <stat> to buy stat points for an increasing amount of Exp per point (Stat specific the cost of Str does not affect the cost of dex). Players also currently start with rank 1 of dual wield, and starter equipment for free on first login. The first login puts the character directly into the starter instance with the new gear equipped and skill given via a dg_script.

Instances are fully implemented, you can use a trigger on an object, mob, or in a room to make players enter or exit instances. Zones with the Zone Flag DUNGEON are instance zones, you cannot access them normally, even immortals cannot go directly to an instance zone, so if you want to make edits zedit <rnum> and remove the instance flag. the dg_script commands for instances are %enterinstance% <target> <zone id> [optional <room vnum>] and %exitinstance% <target> <room vnum> so the starter zone is zone 23 in zlist if I have a portal object with my script on it, with the command for touch and the script is %enterinstance% %actor% 23 2301 if I wanted to move the character into the instance and inside room 2301, if I ommit the room it will send them to the first room of the zone, if I wanted have a similar object for leaving the instance the script is %exitinstance% %actor% 3001 where 3001 is the Temple of Midgaard in zone 30.

Mobs now gain stats via their EXP field dynamically they start with base stats 10 like players you can adjust them higher manually, and they try to buy stats at random with their exp at the cost of 10 exp * (n-9) per stat. Since 

The newbie instance zone is designed for setup up new characters with all their stats set to 10, they will run around and kill things until they defeat the boss of the instance or die, once that happens they will be transported to the first starter zone. Players can spend exp to buy skills/spells/stats and setup their character however they like as they progress through the zone or when they leave it.

dg-scripts has a new trigger type for corpse, it still attaches to a mob in olc but when the mob dies it attaches a trigger to the corpse container it creates on death. This allows you to setup traps or other useful scripts for looting corpses, picking corpses up, sacrificing corpses, or even the corpse loading into the room.

This was just a quick look at what I am working towards I am working to create a full roadmap of everything I want to get done for the official baseline code for release and will updated this once I have it.




Derived from Files for tbaMUD.

## Unit Tests

tbaMUD ships with a C unit-test suite located in the `tests/` directory.
The suite uses the [Unity](https://github.com/ThrowTheSwitch/Unity) test
framework (vendored under `tests/vendor/unity/`).

### Quick start

```
./configure
cd tests && make test
```

`make test` builds each test binary, runs it, and writes JUnit XML results to
`tests/test-results/`.  A summary is printed to the terminal:

```
[PASS] test_utils
[PASS] test_random
[PASS] test_interpreter
[PASS] test_class
```

### CI

The GitHub Actions workflow (`.github/workflows/build.yml`) runs `make test`
on every push and pull request against `master` and publishes a formatted
report via the `dorny/test-reporter` action.

See [doc/testing.md](doc/testing.md) for full details on adding new tests and
understanding the test infrastructure.

See [doc/customizing-start-and-death.md](doc/customizing-start-and-death.md)
for changing the new character starter instance and player death room.

See [doc/dg-scripts-guide.md](doc/dg-scripts-guide.md) for current DG script
behavior, including instance commands and mob death trigger warnings.
