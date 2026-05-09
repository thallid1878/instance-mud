Instance Mud is different generation of mud, a classless, levelless system that can live in flat-files on disk or use an SQL server for holding data. The biggest addition is zone instances a block of 100 room that are dynamicly loaded and unloaded as needed to have instance zones for a single person or large group of people. The combat system has been completely overhauled, no longer are the days of AC and Thac0, instead we are using a stat based attacker roll vs a stat based defender roll to check for hit or miss. Armor is now a damage reduction % with diminishing returns that if stacked to insane amounts will cap damage at 1 though that should not be possible to reach without intent.

Some of the level and older code has been left in place but is being ignored and unhooked from the system, this is currently in place to allow for easy importing of existing tbamud/circlemud 3.x zones into instance-mud. A good example of this on our internal test server is the use of the circlemud 3.x zone infernal pit of kerjim which is still available in the tbamud google docs for download is one of the zones I am currently using as a test dungeon. In the future this code may be removed with a function in place to translate the zone files, but right now I am just ignoring things like level and level restrictions on zones.

In the current state players will buy stats or skills using the exp they earned from killing mobs instead of leveling up. Any player can buy any skill/spell as there are no classes, but just because you have the skill does not mean your stats are going to make good use of it, so make sure you balance them properly.

Instances though most of the work is complete and there have not been any issues yet after extensive testing are only set to allow implementor to access them via command for the moment. This will be changing shortly as I finish the way one enters and exits an instance dungeon.

The next big step that is being worked on once dungeon instances are 100% completed is dynamic mob loading, mobs will use their EXP Point field to randomly buy stats until they can no longer afford any when they load in. This will make it so no 2 mobs of the same type are actually the same, some will be harder to hit than others, or hit you easier than others, or have more or less health. Once this is completed character creation will drop the player into a newbie zone.

The newbie zone is designed for setup up new characters with all their stats set to 10, they will run around and kill things until they get 10,000 exp or die, once that happens they will be transported to the first starter zone and can then spend that exp to buy skills/spells/stats and setup their character however they like. the system keeps track of how much exp you have spent so even if you spend exp in the newbie zone once you have accumulated 10,000 exp you will be removed from the zone.

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
