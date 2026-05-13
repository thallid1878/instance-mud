# Instance-MUD DG Scripts Guide

DG scripts are the builder-facing scripting system used by mobs, objects, and
rooms. They are still based on the old Death Gate/TBA trigger system, but
Instance-MUD has added instance-aware behavior and some old assumptions are no
longer safe.

This guide focuses on how scripts behave in the current Instance-MUD codebase.

## What Can Own A Trigger

Triggers can be attached to three kinds of game objects:

| Attach type | Owner | File/menu meaning |
| --- | --- | --- |
| `0` | Mob | A mobile/NPC owns the trigger. |
| `1` | Object | An object owns the trigger. |
| `2` | Room | A room owns the trigger. |
| `3` | Corpse | Authored on a mob, then copied to that mob's corpse after death. |

The trigger file format is:

```text
#<trigger vnum>
<trigger name>~
<attach type> <trigger type bitvector> <numeric argument>
<argument list>~
<script commands>
~
```

Use `trigedit <vnum>` in-game to edit triggers. Use the script attachment menu
inside `medit`, `oedit`, or `redit` to attach triggers permanently to mobs,
objects, or rooms. Use `attach` and `detach` for temporary runtime testing.

Corpse triggers are edited as trigger intended type `3: Corpses`, but they are
attached to the mob in `medit`. They do not run while the mob is alive. When the
mob dies, Instance-MUD copies those corpse triggers onto the generated corpse
object and runs them as object-style triggers.

Useful builder commands:

| Command | Use |
| --- | --- |
| `trigedit <vnum>` | Create or edit a trigger. |
| `tstat <vnum>` | Inspect a trigger. |
| `tlist <zone>` | List triggers in a zone. |
| `attach mob <trig> <mob>` | Temporarily attach a trigger to a mob. |
| `attach obj <trig> <obj>` | Temporarily attach a trigger to an object. |
| `attach room <trig> <room>` | Temporarily attach a trigger to a room. |
| `detach ...` | Remove a temporary trigger attachment. |

Always use `/f` in the string editor before saving a trigger. It catches many
missing `end` and `done` problems before they become runtime problems.

## Trigger Types

The numeric argument means different things depending on the trigger type. For
many trigger types it is a percent chance to fire, but some use it as a hit
point threshold, command location mask, game hour, or bribe amount.

### Mob Trigger Types

| Type | Fires when |
| --- | --- |
| `GLOBAL` | The trigger can run even if the zone is otherwise empty. |
| `RANDOM` | Periodically, based on the numeric chance. |
| `COMMAND` | A character enters a matching command. |
| `SPEECH` | A character says matching words or phrases. |
| `ACT` | The mob receives a matching act message. |
| `DEATH` | The mob dies. See the death warning below. |
| `GREET` | A visible character enters the room. |
| `GREET_ALL` | Any character enters the room. |
| `ENTRY` | The mob enters a room. |
| `RECEIVE` | The mob receives an object. |
| `FIGHT` | The mob is fighting during a combat pulse. |
| `HITPRCNT` | The mob is fighting and below the numeric HP percent. |
| `BRIBE` | The mob receives at least the numeric amount of coins. |
| `LOAD` | The mob is loaded. |
| `MEMORY` | The mob sees a remembered character. |
| `CAST` | The mob is targeted by a spell. |
| `LEAVE` | A visible character leaves the room. |
| `DOOR` | A door in the room is manipulated. |
| `TIME` | The game reaches the numeric hour. |
| `DAMAGE` | The mob takes damage. |

### Object Trigger Types

| Type | Fires when |
| --- | --- |
| `GLOBAL` | Reserved/rarely useful for objects. |
| `RANDOM` | Periodically, based on the numeric chance. |
| `COMMAND` | A character enters a matching command near the object. |
| `TIMER` | The object's timer expires. |
| `GET` | A character tries to pick up the object. |
| `DROP` | A character tries to drop the object. |
| `GIVE` | A character tries to give the object. |
| `WEAR` | A character tries to wear the object. |
| `REMOVE` | A character tries to remove the object. |
| `LOAD` | The object is loaded. |
| `CAST` | The object is targeted by a spell. |
| `LEAVE` | Someone leaves the room. |
| `CONSUME` | A character eats, drinks, or quaffs the object. |
| `TIME` | The game reaches the numeric hour. |
| `SACRIFICE` | A character sacrifices the object. |

For object command triggers, the numeric argument is a location mask:

| Flag | Meaning |
| --- | --- |
| `1` | Object must be equipped. |
| `2` | Object must be in inventory. |
| `4` | Object must be in the room. |

### Corpse Trigger Types

Corpse triggers reuse object trigger behavior after the corpse is created, but
only a focused subset is intended for builders:

| Type | Fires when |
| --- | --- |
| `GLOBAL` | Reserved for parity with other trigger lists. |
| `COMMAND` | A character enters a matching command near the corpse. |
| `LOAD` | The corpse is created. |
| `GET` | The corpse is picked up, or an item is taken from the corpse. |
| `GIVE` | The corpse object is given to someone. |
| `TIMER` | The corpse timer reaches zero. |
| `TIME` | The game reaches the numeric hour. |
| `SACRIFICE` | A character sacrifices the corpse. |

For corpse `COMMAND` triggers, use the same object command numeric argument
mask: `1` equipped, `2` inventory, `4` room. Most corpse command triggers should
use `4` because corpses normally sit in the room.

When `GET` fires because a player takes an item from the corpse, `%self%` is the
corpse and `%object%` is the item being looted.

When `SACRIFICE` fires, returning `0` blocks the normal sacrifice. Returning `1`
allows the normal sacrifice behavior to continue.

When `TIMER` fires, the corpse is about to decay. A trigger can use `otimer` to
set a new corpse timer; if the timer is greater than zero after the trigger
runs, normal corpse decay is delayed.

### Room Trigger Types

| Type | Fires when |
| --- | --- |
| `GLOBAL` | The trigger can run even if the zone is otherwise empty. |
| `RANDOM` | Periodically, based on the numeric chance. |
| `COMMAND` | A character enters a matching command in the room. |
| `SPEECH` | A character says matching words or phrases in the room. |
| `RESET` | The zone resets. |
| `ENTER` | A character enters the room. |
| `DROP` | Something is dropped in the room. |
| `CAST` | A spell is cast in the room. |
| `LEAVE` | A character leaves the room. |
| `DOOR` | A door in the room is manipulated. |
| `LOGIN` | A character logs into the room. |
| `TIME` | The game reaches the numeric hour. |

Room triggers are usually the safest place for dungeon flow control because the
room continues to exist after mobs die and objects are purged.

## Script Language Basics

Lines beginning with `*` are comments.

Common flow commands:

```text
if <condition>
elseif <condition>
else
end

while <condition>
done

switch <value>
case <value>
default
done

break
halt
return <number>
wait <time>
```

Common variable commands:

| Command | Use |
| --- | --- |
| `set <name> <value>` | Store a local trigger variable. |
| `eval <name> <expression>` | Evaluate an expression and store the result. |
| `unset <name>` | Remove a variable. |
| `global <name>` | Move a local variable onto the owner script's global list. |
| `context <id>` | Change which variable context this trigger is using. |
| `remote <name> <uid>` | Copy a variable to another script owner. |
| `rdelete <name> <uid>` | Delete a variable from another script owner. |
| `makeuid <name> <target>` | Store a target UID in a variable. |
| `attach <trigger vnum> <uid>` | Attach a trigger to a runtime target. |
| `detach <trigger vnum|all> <uid>` | Detach a trigger from a runtime target. |

Conditions support common operators:

```text
||  &&  ==  !=  <=  >=  <  >  /=  !  +  -  *  /
```

`/=` means "contains", which is useful for checking command arguments:

```text
if %arg% /= portal
  %send% %actor% The portal answers your touch.
end
```

## Variables And Command Shortcuts

Scripts use percent-wrapped substitutions:

```text
%actor%
%actor.name%
%actor.id%
%actor.room%
%self%
%self.id%
%object%
%cmd%
%arg%
```

Available variables depend on the trigger. For example, command triggers
usually provide `%actor%`, `%cmd%`, and `%arg%`; receive/drop/give triggers
usually provide an object variable; cast triggers usually provide spell target
variables.

Instance-MUD also supports context-aware command shortcuts. These expand to the
right mob, object, or room command depending on where the trigger is attached:

| Shortcut | Mob trigger expands to | Object trigger expands to | Room trigger expands to |
| --- | --- | --- | --- |
| `%send%` | `msend` | `osend` | `wsend` |
| `%echo%` | `mecho` | `oecho` | `wecho` |
| `%echoaround%` | `mechoaround` | `oechoaround` | `wechoaround` |
| `%load%` | `mload` | `oload` | `wload` |
| `%purge%` | `mpurge` | `opurge` | `wpurge` |
| `%teleport%` | `mteleport` | `oteleport` | `wteleport` |
| `%damage%` | `mdamage` | `odamage` | `wdamage` |
| `%force%` | `mforce` | `oforce` | `wforce` |
| `%door%` | `mdoor` | `odoor` | `wdoor` |
| `%zoneecho%` | `mzoneecho` | `ozoneecho` | `wzoneecho` |
| `%asound%` | `masound` | `oasound` | `wasound` |
| `%at%` | `mat` | `oat` | `wat` |
| `%recho%` | `mrecho` | `orecho` | `wrecho` |
| `%log%` | `mlog` | `olog` | `wlog` |
| `%cleaninstance%` | `mcleaninstance` | `ocleaninstance` | `wcleaninstance` |
| `%enterinstance%` | `menterinstance` | `oenterinstance` | `wenterinstance` |
| `%exitinstance%` | `mexitinstance` | `oexitinstance` | `wexitinstance` |

Prefer the shortcuts in shared examples. A trigger can then be moved from a mob
to a room or object with fewer edits.

## Wait Is Not A Timer System

`wait` pauses the current trigger and resumes it later from the same owner.

Supported forms include:

```text
wait 10
wait 10 s
wait 1 t
wait until 14:30
```

`wait 10` means 10 game pulses, not 10 seconds. Use `wait 10 s` when you want
real seconds.

Important: the owner must still exist when the wait resumes. If the owner was a
mob that died, an object that was purged, or a room instance that was destroyed,
the resumed trigger cannot safely continue.

Safe places to use `wait`:

- Room triggers in normal rooms.
- Room triggers in active instance rooms, as long as the instance will still
  exist when the wait finishes.
- Object triggers on persistent objects that will not be purged or looted away.
- Mob triggers on mobs that are not about to die, extract, transform, or purge.

Risky places to use `wait`:

- Mob death triggers.
- Object get/drop/consume triggers if the object may be moved, consumed, or
  extracted before the wait finishes.
- Any trigger that purges or transforms its own owner before later commands.

## Mob Death Trigger Warning

Mob death triggers fire while the mob is dying. After the trigger returns, the
combat code continues, creates the corpse, and extracts the NPC.

That means this is unsafe:

```text
* Bad: the mob will be gone when the trigger tries to resume.
wait 1 s
%exitinstance% %actor% 3001
```

The script may appear to stop at the `wait`. That is expected: after the wait,
the trigger engine tries to resume a trigger whose mob owner no longer exists.

Do everything that must happen from the dying mob before any wait:

```text
* Safe: no delayed work depends on the dying mob.
%echo% Kerjim collapses as the dungeon begins to shake.
%send% %actor% You feel the way out pulling at you.
%exitinstance% %actor% 3001
```

If players need time to loot before leaving a dungeon, hand the delayed work to
something that survives the mob. A corpse trigger is now the preferred way to do
that:

```text
* Corpse sacrifice trigger.
%send% %actor% The dungeon releases you as the corpse fades.
%exitinstance% %actor% 3001
return 1
```

Another safe option is to create a portal:

```text
* Mob death trigger: create a no-take portal instead of waiting.
%echo% A pale portal opens as the corpse hits the ground.
%load% obj 22199
```

Then put an object command trigger on object `22199`:

```text
* Object command trigger, arg list: enter portal
if %cmd.mudcommand% == enter && %arg% /= portal
  %send% %actor% You step through the portal.
  %exitinstance% %actor% 3001
  return 1
end
return 0
```

This lets the corpse remain lootable while the exit object controls when the
player leaves. The same pattern works with a room command trigger such as
`leave dungeon`, `exit dungeon`, or `return`.

For this pattern, make the portal object non-takeable. Mob `%load%` places
takeable objects in the mob inventory by default, and a dying mob's inventory is
about to become corpse contents.

## Instance Commands

Instance-MUD adds DG commands for entering and leaving dungeon instances:

```text
%enterinstance% <target> <zone vnum> [entry room vnum]
%exitinstance% <target> <real room vnum>
%cleaninstance% <target> <zone vnum>
```

Examples:

```text
%enterinstance% %actor% 23
%enterinstance% %actor% 23 2307
%exitinstance% %actor% 3001
%cleaninstance% %actor% 23
```

`%enterinstance%` creates or rejoins the actor's active instance of the dungeon
zone. If an entry room vnum is provided, it must be a real room in that dungeon
zone, and the target is moved to the matching copied room in the instance. If it
is omitted, the target enters the first room in the zone.

`%exitinstance%` removes the target from instance membership and moves them to a
real-world room. The destination must not be an instanced/template room.

`%cleaninstance%` immediately destroys the target's active or most recently
exited instance for that dungeon zone if no other players are still inside it.
If another player is still in the instance, such as a group member, the command
does nothing and the normal five-minute empty cleanup can still handle it later.

The movement commands make the target look after moving. They also fire room
enter triggers in the destination room.

Use `%exitinstance%` instead of plain `%teleport%` when a script is intentionally
ending a dungeon run. Plain teleport is still useful for movement inside the
same world context, but `%exitinstance%` documents the intent and clears the
instance state at the same time.

## Common Patterns

### Portal Into A Dungeon

Attach this as an object command trigger to a portal object in the real world:

```text
* Arg list: enter portal
if %cmd.mudcommand% == enter && %arg% /= portal
  %send% %actor% The portal folds around you.
  %enterinstance% %actor% 23
  return 1
end
return 0
```

### Command Exit From A Dungeon Room

Attach this as a room command trigger to an exit room inside the instance:

```text
* Arg list: leave dungeon return exit
if %cmd.mudcommand% == leave || %cmd.mudcommand% == return
  %send% %actor% You leave the dungeon behind.
  %exitinstance% %actor% 3001
  %cleaninstance% %actor% 23
  return 1
end
return 0
```

### Give New Characters Starter Equipment

For a starter instance, prefer a room `LOGIN` trigger and/or `ENTER` trigger on
the first room. New characters are moved into the starter instance after the
MOTD flow, and Instance-MUD fires room enter/login triggers there.

```text
* Room login trigger on the starter instance entry room.
%load% obj 2300 %actor% light
%load% obj 2301 %actor% finger
%load% obj 2312 %actor% wield
%send% %actor% You gather your starter gear.
```

The exact wear-position names are handled by the load command's equipment
position parser. If an item cannot be worn in that slot, it will be placed in
inventory instead.

### Use A Room As The Durable State Owner

When a mob should unlock a later room action, put the durable state on the room:

```text
* Mob death trigger.
set boss_dead 1
remote boss_dead %actor.room%
%echo% A stone seal cracks open.
```

Then use a room command trigger:

```text
* Room command trigger.
if %cmd.mudcommand% == enter && %arg% /= portal
  if %boss_dead% == 1
    %exitinstance% %actor% 3001
    %cleaninstance% %actor% 23
    return 1
  else
    %send% %actor% The portal is still sealed.
    return 1
  end
end
return 0
```

This avoids depending on a dead mob for later logic.

## Debugging Checklist

When a script does not fire:

1. Confirm the trigger is attached to the runtime mob, object, or room with
   `stat`, `tstat`, or the OLC script menu.
2. Confirm the trigger type matches the event. A room `ENTER` trigger is not the
   same thing as a room `LOGIN` trigger.
3. Confirm the numeric argument allows it to fire. For most triggers, use `100`
   while testing.
4. Confirm the argument list matches. For command triggers, try a small focused
   arg list first.
5. Check whether the builder has flags such as `NOHASSLE` that can prevent some
   trigger targeting behavior.
6. Watch `log/trigger` and the main syslog for DG script errors.
7. Use `%log%` inside the trigger to prove which line was reached.
8. Avoid `wait` until the trigger works without it.
9. In instances, prefer room triggers for long-running dungeon state because the
   room is more durable than a dying mob or lootable object.

Example debug lines:

```text
%log% starter login trigger fired for %actor.name%
%send% %actor% Debug: starter login trigger fired.
```

Remove noisy debug output before shipping a zone.

## Practical Rules

- Do not use `wait` in mob death triggers for anything that must happen later.
- Do not purge the trigger owner until the end of the script.
- Prefer room triggers for dungeon state, completion checks, and delayed flow.
- Prefer `%enterinstance%` and `%exitinstance%` for instance movement.
- Use `return 1` from command triggers when the script should consume the
  player's command.
- Use `return 0` from command triggers when normal command handling should
  continue.
- Keep command trigger argument lists narrow. A catch-all command trigger can
  accidentally swallow real player commands.
- Test with `100` percent numeric arguments first, then lower the chance after
  the behavior is correct.
