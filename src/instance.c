/**************************************************************************
*  File: instance.c                                        Part of tbaMUD *
*  Usage: Runtime dungeon instances.                                      *
**************************************************************************/

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "handler.h"
#include "interpreter.h"
#include "db.h"
#include "dg_scripts.h"
#include "genolc.h"
#include "genwld.h"
#include "shop.h"
#include "genshp.h"
#include "constants.h"
#include "act.h"
#include "lists.h"
#include "instance.h"

struct instance_obj_ref {
  struct obj_data *obj;
  struct instance_obj_ref *next;
};

struct instance_data {
  int id;
  zone_rnum template_zone;
  zone_rnum zone;
  room_rnum *template_rooms;
  room_rnum *rooms;
  int room_count;
  int shop_start;
  int shop_count;
  room_rnum return_room;
  time_t empty_since;
  struct instance_data *next;
};

room_rnum top_of_runtime_world = 0;
zone_rnum top_of_runtime_zone_table = 0;
int top_of_runtime_shop = -1;

static struct instance_data *instance_list = NULL;
static int next_instance_id = 1;

static struct instance_data *instance_by_id(int id);
static struct instance_data *instance_group_instance(struct char_data *ch, zone_rnum template_zone);
static room_rnum instance_room_for_template(struct instance_data *inst, room_rnum room);
static room_rnum instance_entry_room(struct instance_data *inst);
static void clone_room_to_instance(struct instance_data *inst, int index);
static void clone_instance_shops(struct instance_data *inst);
static void reset_instance(struct instance_data *inst);
static void destroy_instance(struct instance_data *inst);
static void free_instance_shop_slot(int shop);
static int instance_player_count(struct instance_data *inst);
static void queue_instance_room_chars(room_rnum room);
static void track_obj(struct instance_obj_ref **list, struct obj_data *obj);
static struct obj_data *find_tracked_obj(struct instance_obj_ref *list, obj_rnum rnum);
static void free_tracked_objs(struct instance_obj_ref *list);

int valid_room_rnum(room_rnum rnum)
{
  if (rnum == NOWHERE)
    return FALSE;
  if (rnum <= top_of_world)
    return TRUE;
  if (top_of_runtime_world > top_of_world && rnum <= top_of_runtime_world &&
      world[rnum].instance_id > 0)
    return TRUE;
  return FALSE;
}

int instance_shop_top(void)
{
  return MAX(top_shop, top_of_runtime_shop);
}

int instance_room_id(room_rnum room)
{
  if (!valid_room_rnum(room))
    return 0;
  return world[room].instance_id;
}

int instance_zone_is_template(zone_rnum zone)
{
  zone_rnum top = MAX(top_of_zone_table, top_of_runtime_zone_table);

  if (zone == NOWHERE || zone > top)
    return FALSE;
  return ZONE_FLAGGED(zone, ZONE_DUNGEON);
}

int instance_zone_is_runtime(zone_rnum zone)
{
  zone_rnum top = MAX(top_of_zone_table, top_of_runtime_zone_table);

  if (zone == NOWHERE || zone > top)
    return FALSE;
  return ZONE_FLAGGED(zone, ZONE_INSTANCE);
}

int instance_room_is_template(room_rnum room)
{
  if (!valid_room_rnum(room))
    return FALSE;
  return instance_zone_is_template(world[room].zone) && world[room].instance_id == 0;
}

room_rnum instance_safe_return_room(struct char_data *ch)
{
  room_rnum room = NOWHERE;

  if (ch)
    room = ch->instance_return_room;

  if (valid_room_rnum(room) && !instance_room_is_template(room))
    return room;

  if (valid_room_rnum(r_mortal_start_room) && !instance_room_is_template(r_mortal_start_room))
    return r_mortal_start_room;

  return valid_room_rnum(0) ? 0 : NOWHERE;
}

room_vnum instance_safe_load_room_vnum(struct char_data *ch)
{
  room_rnum room = instance_safe_return_room(ch);

  return valid_room_rnum(room) ? GET_ROOM_VNUM(room) : NOWHERE;
}

static struct instance_data *instance_by_id(int id)
{
  struct instance_data *inst;

  for (inst = instance_list; inst; inst = inst->next)
    if (inst->id == id)
      return inst;
  return NULL;
}

static struct instance_data *instance_group_instance(struct char_data *ch, zone_rnum template_zone)
{
  struct char_data *tch;
  struct instance_data *inst, *found = NULL;
  struct iterator_data iter = { NULL, NULL };

  if (!GROUP(ch) || !GROUP(ch)->members || !GROUP(ch)->members->iSize)
    return NULL;

  if ((tch = GROUP_LEADER(GROUP(ch))) != NULL && tch != ch && !IS_NPC(tch) &&
      GET_INSTANCE_ID(tch) > 0) {
    inst = instance_by_id(GET_INSTANCE_ID(tch));
    if (inst && inst->template_zone == template_zone)
      return inst;
  }

  for (tch = (struct char_data *) merge_iterator(&iter, GROUP(ch)->members);
       tch; tch = (struct char_data *) next_in_list(&iter)) {
    if (tch == ch || IS_NPC(tch) || GET_INSTANCE_ID(tch) <= 0)
      continue;

    inst = instance_by_id(GET_INSTANCE_ID(tch));
    if (inst && inst->template_zone == template_zone) {
      found = inst;
      break;
    }
  }

  if (iter.pList)
    remove_iterator(&iter);
  return found;
}

static room_rnum instance_room_for_template(struct instance_data *inst, room_rnum room)
{
  int i;

  for (i = 0; i < inst->room_count; i++)
    if (inst->template_rooms[i] == room)
      return inst->rooms[i];
  return NOWHERE;
}

static room_rnum instance_entry_room(struct instance_data *inst)
{
  if (!inst || inst->room_count <= 0)
    return NOWHERE;

  return valid_room_rnum(inst->rooms[0]) ? inst->rooms[0] : NOWHERE;
}

static void clone_room_to_instance(struct instance_data *inst, int index)
{
  struct room_data *src = &world[inst->template_rooms[index]];
  struct room_data *dst = &world[inst->rooms[index]];
  int dir;

  memset(dst, 0, sizeof(*dst));
  *dst = *src;
  dst->contents = NULL;
  dst->people = NULL;
  dst->events = NULL;
  dst->script = NULL;
  dst->proto_script = NULL;
  dst->light = 0;
  dst->zone = inst->zone;
  dst->instance_id = inst->id;

  for (dir = 0; dir < DIR_COUNT; dir++)
    dst->dir_option[dir] = NULL;
  dst->ex_description = NULL;
  dst->name = NULL;
  dst->description = NULL;

  copy_room_strings(dst, src);
  copy_proto_script(src, dst, WLD_TRIGGER);
  assign_triggers(dst, WLD_TRIGGER);
}

static void remap_instance_exits(struct instance_data *inst)
{
  int i, dir;

  for (i = 0; i < inst->room_count; i++) {
    struct room_data *room = &world[inst->rooms[i]];

    for (dir = 0; dir < DIR_COUNT; dir++) {
      room_rnum mapped;

      if (!room->dir_option[dir] || room->dir_option[dir]->to_room == NOWHERE)
        continue;

      mapped = instance_room_for_template(inst, room->dir_option[dir]->to_room);
      room->dir_option[dir]->to_room = mapped;
    }
  }
}

static int shop_has_room_in_zone(int shop, zone_rnum zone)
{
  int i;

  for (i = 0; SHOP_ROOM(shop, i) != NOWHERE; i++) {
    room_rnum room = real_room(SHOP_ROOM(shop, i));

    if (room != NOWHERE && world[room].zone == zone)
      return TRUE;
  }
  return FALSE;
}

static void clone_instance_shops(struct instance_data *inst)
{
  int shop, new_shop;

  if (top_of_runtime_shop < top_shop)
    top_of_runtime_shop = top_shop;

  inst->shop_start = top_of_runtime_shop + 1;
  inst->shop_count = 0;

  for (shop = 0; shop <= top_shop; shop++) {
    if (!shop_has_room_in_zone(shop, inst->template_zone))
      continue;

    new_shop = ++top_of_runtime_shop;
    RECREATE(shop_index, struct shop_data, top_of_runtime_shop + 1);
    memset(&shop_index[new_shop], 0, sizeof(struct shop_data));
    copy_shop(&shop_index[new_shop], &shop_index[shop], FALSE);
    shop_index[new_shop].instance_id = inst->id;
    shop_index[new_shop].template_shop = shop;
    SHOP_BANK(new_shop) = 0;
    SHOP_SORT(new_shop) = 0;
    inst->shop_count++;
  }
}

static void track_obj(struct instance_obj_ref **list, struct obj_data *obj)
{
  struct instance_obj_ref *ref;

  if (!obj)
    return;

  CREATE(ref, struct instance_obj_ref, 1);
  ref->obj = obj;
  ref->next = *list;
  *list = ref;
}

static struct obj_data *find_tracked_obj(struct instance_obj_ref *list, obj_rnum rnum)
{
  for (; list; list = list->next)
    if (list->obj && GET_OBJ_RNUM(list->obj) == rnum)
      return list->obj;
  return NULL;
}

static void free_tracked_objs(struct instance_obj_ref *list)
{
  struct instance_obj_ref *next;

  while (list) {
    next = list->next;
    free(list);
    list = next;
  }
}

static void reset_instance(struct instance_data *inst)
{
  struct reset_com *cmd = zone_table[inst->template_zone].cmd;
  struct char_data *mob = NULL, *tmob = NULL;
  struct obj_data *obj = NULL, *tobj = NULL, *obj_to = NULL;
  struct instance_obj_ref *objects = NULL;
  int cmd_no, last_cmd = 0;

  if (!cmd)
    return;

  for (cmd_no = 0; cmd[cmd_no].command != 'S'; cmd_no++) {
    room_rnum room;

    if (cmd[cmd_no].if_flag && !last_cmd)
      continue;

    switch (cmd[cmd_no].command) {
    case '*':
      last_cmd = 0;
      break;

    case 'M':
      room = instance_room_for_template(inst, cmd[cmd_no].arg3);
      if (room == NOWHERE) {
        last_cmd = 0;
        break;
      }
      mob = read_mobile(cmd[cmd_no].arg1, REAL);
      if (mob) {
        char_to_room(mob, room);
        load_mtrigger(mob);
        tmob = mob;
        last_cmd = 1;
      } else
        last_cmd = 0;
      tobj = NULL;
      break;

    case 'O':
      obj = read_object(cmd[cmd_no].arg1, REAL);
      if (!obj) {
        last_cmd = 0;
        break;
      }
      track_obj(&objects, obj);
      if (cmd[cmd_no].arg3 != NOWHERE) {
        room = instance_room_for_template(inst, cmd[cmd_no].arg3);
        if (room == NOWHERE) {
          extract_obj(obj);
          last_cmd = 0;
          break;
        }
        obj_to_room(obj, room);
        load_otrigger(obj);
      } else {
        IN_ROOM(obj) = NOWHERE;
      }
      tobj = obj;
      tmob = NULL;
      last_cmd = 1;
      break;

    case 'P':
      obj_to = find_tracked_obj(objects, cmd[cmd_no].arg3);
      if (!obj_to) {
        last_cmd = 0;
        break;
      }
      obj = read_object(cmd[cmd_no].arg1, REAL);
      if (!obj) {
        last_cmd = 0;
        break;
      }
      track_obj(&objects, obj);
      obj_to_obj(obj, obj_to);
      load_otrigger(obj);
      tobj = obj;
      tmob = NULL;
      last_cmd = 1;
      break;

    case 'G':
      if (!mob) {
        last_cmd = 0;
        break;
      }
      obj = read_object(cmd[cmd_no].arg1, REAL);
      if (!obj) {
        last_cmd = 0;
        break;
      }
      track_obj(&objects, obj);
      obj_to_char(obj, mob);
      load_otrigger(obj);
      tobj = obj;
      tmob = NULL;
      last_cmd = 1;
      break;

    case 'E':
      if (!mob || cmd[cmd_no].arg3 < 0 || cmd[cmd_no].arg3 >= NUM_WEARS) {
        last_cmd = 0;
        break;
      }
      obj = read_object(cmd[cmd_no].arg1, REAL);
      if (!obj) {
        last_cmd = 0;
        break;
      }
      track_obj(&objects, obj);
      IN_ROOM(obj) = IN_ROOM(mob);
      load_otrigger(obj);
      if (wear_otrigger(obj, mob, cmd[cmd_no].arg3)) {
        IN_ROOM(obj) = NOWHERE;
        equip_char(mob, obj, cmd[cmd_no].arg3);
      } else
        obj_to_char(obj, mob);
      tobj = obj;
      tmob = NULL;
      last_cmd = 1;
      break;

    case 'R':
      room = instance_room_for_template(inst, cmd[cmd_no].arg1);
      if (room != NOWHERE &&
          (obj = get_obj_in_list_num(cmd[cmd_no].arg2, world[room].contents)) != NULL)
        extract_obj(obj);
      tmob = NULL;
      tobj = NULL;
      last_cmd = 1;
      break;

    case 'D':
      room = instance_room_for_template(inst, cmd[cmd_no].arg1);
      if (room == NOWHERE || cmd[cmd_no].arg2 < 0 || cmd[cmd_no].arg2 >= DIR_COUNT ||
          !world[room].dir_option[cmd[cmd_no].arg2]) {
        last_cmd = 0;
        break;
      }
      switch (cmd[cmd_no].arg3) {
      case 0:
        REMOVE_BIT(world[room].dir_option[cmd[cmd_no].arg2]->exit_info, EX_LOCKED);
        REMOVE_BIT(world[room].dir_option[cmd[cmd_no].arg2]->exit_info, EX_CLOSED);
        break;
      case 1:
        SET_BIT(world[room].dir_option[cmd[cmd_no].arg2]->exit_info, EX_CLOSED);
        REMOVE_BIT(world[room].dir_option[cmd[cmd_no].arg2]->exit_info, EX_LOCKED);
        break;
      case 2:
        SET_BIT(world[room].dir_option[cmd[cmd_no].arg2]->exit_info, EX_LOCKED);
        SET_BIT(world[room].dir_option[cmd[cmd_no].arg2]->exit_info, EX_CLOSED);
        break;
      }
      tmob = NULL;
      tobj = NULL;
      last_cmd = 1;
      break;

    case 'T':
      room = instance_room_for_template(inst, cmd[cmd_no].arg3);
      if (cmd[cmd_no].arg1 == MOB_TRIGGER && tmob) {
        if (!SCRIPT(tmob))
          CREATE(SCRIPT(tmob), struct script_data, 1);
        add_trigger(SCRIPT(tmob), read_trigger(cmd[cmd_no].arg2), -1);
        last_cmd = 1;
      } else if (cmd[cmd_no].arg1 == OBJ_TRIGGER && tobj) {
        if (!SCRIPT(tobj))
          CREATE(SCRIPT(tobj), struct script_data, 1);
        add_trigger(SCRIPT(tobj), read_trigger(cmd[cmd_no].arg2), -1);
        last_cmd = 1;
      } else if (cmd[cmd_no].arg1 == WLD_TRIGGER && room != NOWHERE) {
        if (!world[room].script)
          CREATE(world[room].script, struct script_data, 1);
        add_trigger(world[room].script, read_trigger(cmd[cmd_no].arg2), -1);
        last_cmd = 1;
      } else
        last_cmd = 0;
      break;

    case 'V':
      room = instance_room_for_template(inst, cmd[cmd_no].arg3);
      if (cmd[cmd_no].arg1 == MOB_TRIGGER && tmob && SCRIPT(tmob)) {
        add_var(&(SCRIPT(tmob)->global_vars), cmd[cmd_no].sarg1, cmd[cmd_no].sarg2,
          cmd[cmd_no].arg3);
        last_cmd = 1;
      } else if (cmd[cmd_no].arg1 == OBJ_TRIGGER && tobj && SCRIPT(tobj)) {
        add_var(&(SCRIPT(tobj)->global_vars), cmd[cmd_no].sarg1, cmd[cmd_no].sarg2,
          cmd[cmd_no].arg3);
        last_cmd = 1;
      } else if (cmd[cmd_no].arg1 == WLD_TRIGGER && room != NOWHERE && world[room].script) {
        add_var(&(world[room].script->global_vars), cmd[cmd_no].sarg1, cmd[cmd_no].sarg2,
          cmd[cmd_no].arg2);
        last_cmd = 1;
      } else
        last_cmd = 0;
      break;

    default:
      last_cmd = 0;
      break;
    }
  }

  free_tracked_objs(objects);

  for (cmd_no = 0; cmd_no < inst->room_count; cmd_no++)
    reset_wtrigger(&world[inst->rooms[cmd_no]]);
}

int instance_create(zone_rnum template_zone, room_rnum return_room, room_rnum *entry_room)
{
  struct instance_data *inst;
  int i, count = 0;
  room_rnum room, new_top;
  zone_rnum new_zone;

  if (template_zone == NOWHERE || template_zone > top_of_zone_table)
    return 0;
  if (!ZONE_FLAGGED(template_zone, ZONE_DUNGEON))
    return 0;

  for (room = 0; room <= top_of_world; room++)
    if (world[room].zone == template_zone)
      count++;

  if (count <= 0)
    return 0;

  if (top_of_runtime_world < top_of_world)
    top_of_runtime_world = top_of_world;
  if (top_of_runtime_zone_table < top_of_zone_table)
    top_of_runtime_zone_table = top_of_zone_table;

  CREATE(inst, struct instance_data, 1);
  inst->id = next_instance_id++;
  inst->template_zone = template_zone;
  inst->return_room = return_room;
  inst->room_count = count;
  CREATE(inst->template_rooms, room_rnum, count);
  CREATE(inst->rooms, room_rnum, count);

  new_zone = ++top_of_runtime_zone_table;
  RECREATE(zone_table, struct zone_data, top_of_runtime_zone_table + 1);
  memset(&zone_table[new_zone], 0, sizeof(struct zone_data));
  zone_table[new_zone] = zone_table[template_zone];
  zone_table[new_zone].name = str_udup(zone_table[template_zone].name);
  zone_table[new_zone].builders = str_udup(zone_table[template_zone].builders);
  zone_table[new_zone].cmd = NULL;
  zone_table[new_zone].age = 0;
  zone_table[new_zone].reset_mode = 0;
  zone_table[new_zone].instance_id = inst->id;
  zone_table[new_zone].template_zone = template_zone;
  REMOVE_BIT_AR(zone_table[new_zone].zone_flags, ZONE_DUNGEON);
  SET_BIT_AR(zone_table[new_zone].zone_flags, ZONE_INSTANCE);
  inst->zone = new_zone;

  new_top = top_of_runtime_world + count;
  RECREATE(world, struct room_data, new_top + 1);

  i = 0;
  for (room = 0; room <= top_of_world; room++) {
    if (world[room].zone != template_zone)
      continue;

    inst->template_rooms[i] = room;
    inst->rooms[i] = top_of_runtime_world + i + 1;
    i++;
  }
  top_of_runtime_world = new_top;

  for (i = 0; i < inst->room_count; i++)
    clone_room_to_instance(inst, i);
  remap_instance_exits(inst);
  clone_instance_shops(inst);
  reset_instance(inst);

  inst->next = instance_list;
  instance_list = inst;
  inst->empty_since = 0;

  if (entry_room)
    *entry_room = inst->rooms[0];

  mudlog(CMP, LVL_IMPL, TRUE, "Created dungeon instance %d from zone %d (%s).",
    inst->id, zone_table[template_zone].number, zone_table[template_zone].name);
  return inst->id;
}

int instance_leave(struct char_data *ch)
{
  struct instance_data *inst = instance_by_id(GET_INSTANCE_ID(ch));
  room_rnum target;

  if (!inst)
    return FALSE;

  target = instance_safe_return_room(ch);

  act("$n vanishes through a shimmering tear in the air.", TRUE, ch, 0, 0, TO_ROOM);
  char_from_room(ch);
  char_to_room(ch, target);
  ch->instance_return_room = NOWHERE;
  act("$n steps out of a shimmering tear in the air.", TRUE, ch, 0, 0, TO_ROOM);
  look_at_room(ch, 0);
  return TRUE;
}

int instance_relocate_char(struct char_data *ch)
{
  room_rnum target;

  if (!ch)
    return FALSE;

  target = instance_safe_return_room(ch);
  if (!valid_room_rnum(target))
    return FALSE;

  if (IN_ROOM(ch) != NOWHERE)
    char_from_room(ch);
  char_to_room(ch, target);
  ch->instance_return_room = NOWHERE;

  if (!IS_NPC(ch)) {
    GET_LOADROOM(ch) = GET_ROOM_VNUM(target);
    save_char(ch);
  }

  return TRUE;
}

static int instance_player_count(struct instance_data *inst)
{
  struct descriptor_data *d;
  int count = 0;

  for (d = descriptor_list; d; d = d->next)
    if (IS_PLAYING(d) && d->character && GET_INSTANCE_ID(d->character) == inst->id)
      count++;
  return count;
}

void instance_update(void)
{
  struct instance_data *inst, *next;
  time_t now = time(NULL);

  for (inst = instance_list; inst; inst = next) {
    next = inst->next;

    if (instance_player_count(inst) > 0) {
      inst->empty_since = 0;
      continue;
    }

    if (!inst->empty_since) {
      inst->empty_since = now;
      continue;
    }

    if (now - inst->empty_since >= 300)
      destroy_instance(inst);
  }
}

static void free_instance_shop_slot(int shop)
{
  int i;

  if (shop < 0 || shop > top_of_runtime_shop || !shop_index[shop].instance_id)
    return;

  if (shop_index[shop].no_such_item1)
    free(shop_index[shop].no_such_item1);
  if (shop_index[shop].no_such_item2)
    free(shop_index[shop].no_such_item2);
  if (shop_index[shop].missing_cash1)
    free(shop_index[shop].missing_cash1);
  if (shop_index[shop].missing_cash2)
    free(shop_index[shop].missing_cash2);
  if (shop_index[shop].do_not_buy)
    free(shop_index[shop].do_not_buy);
  if (shop_index[shop].message_buy)
    free(shop_index[shop].message_buy);
  if (shop_index[shop].message_sell)
    free(shop_index[shop].message_sell);
  if (shop_index[shop].in_room)
    free(shop_index[shop].in_room);
  if (shop_index[shop].producing)
    free(shop_index[shop].producing);
  if (shop_index[shop].type) {
    for (i = 0; BUY_TYPE(shop_index[shop].type[i]) != NOTHING; i++)
      if (BUY_WORD(shop_index[shop].type[i]))
        free(BUY_WORD(shop_index[shop].type[i]));
    free(shop_index[shop].type);
  }

  memset(&shop_index[shop], 0, sizeof(struct shop_data));
}

static void queue_instance_room_chars(room_rnum room)
{
  struct char_data *ch, *next_ch;

  for (ch = world[room].people; ch; ch = next_ch) {
    next_ch = ch->next_in_room;

    if (!IS_NPC(ch))
      instance_relocate_char(ch);
    else
      extract_char(ch);
  }
}

static void destroy_instance(struct instance_data *inst)
{
  struct instance_data *cur, *prev = NULL;
  int i;

  for (i = 0; i < inst->room_count; i++)
    queue_instance_room_chars(inst->rooms[i]);
  extract_pending_chars();

  for (i = 0; i < inst->room_count; i++) {
    room_rnum room = inst->rooms[i];

    while (world[room].contents)
      extract_obj(world[room].contents);

    if (SCRIPT(&world[room]))
      extract_script(&world[room], WLD_TRIGGER);
    free_proto_script(&world[room], WLD_TRIGGER);
    free_room_strings(&world[room]);
    memset(&world[room], 0, sizeof(struct room_data));
    world[room].number = NOWHERE;
    world[room].zone = NOWHERE;
  }

  for (i = 0; i < inst->shop_count; i++)
    free_instance_shop_slot(inst->shop_start + i);

  if (inst->zone != NOWHERE && inst->zone <= top_of_runtime_zone_table &&
      zone_table[inst->zone].instance_id == inst->id) {
    if (zone_table[inst->zone].name)
      free(zone_table[inst->zone].name);
    if (zone_table[inst->zone].builders)
      free(zone_table[inst->zone].builders);
    memset(&zone_table[inst->zone], 0, sizeof(struct zone_data));
  }

  for (cur = instance_list; cur; cur = cur->next) {
    if (cur == inst) {
      if (prev)
        prev->next = cur->next;
      else
        instance_list = cur->next;
      break;
    }
    prev = cur;
  }

  mudlog(CMP, LVL_IMPL, TRUE, "Destroyed dungeon instance %d.", inst->id);
  free(inst->template_rooms);
  free(inst->rooms);
  free(inst);
}

void instance_list_to_char(struct char_data *ch)
{
  struct instance_data *inst;

  if (!instance_list) {
    send_to_char(ch, "There are no active dungeon instances.\r\n");
    return;
  }

  send_to_char(ch, "Active dungeon instances:\r\n");
  for (inst = instance_list; inst; inst = inst->next) {
    int players = instance_player_count(inst);

    send_to_char(ch, "  #%d zone %d (%s), rooms: %d, shops: %d, players: %d\r\n",
      inst->id, zone_table[inst->template_zone].number,
      zone_table[inst->template_zone].name, inst->room_count, inst->shop_count, players);
  }
}

ACMD(do_instance)
{
  char arg[MAX_INPUT_LENGTH], subarg[MAX_INPUT_LENGTH];
  struct instance_data *group_inst = NULL;
  zone_rnum zone;
  room_rnum entry = NOWHERE, return_room;
  int id;

  two_arguments(argument, arg, subarg);

  if (!*arg) {
    send_to_char(ch, "Usage: instance <zone vnum>|list|leave\r\n");
    return;
  }

  if (!str_cmp(arg, "list")) {
    instance_list_to_char(ch);
    return;
  }

  if (!str_cmp(arg, "leave")) {
    if (!instance_leave(ch))
      send_to_char(ch, "You are not inside a dungeon instance.\r\n");
    return;
  }

  if (!is_number(arg)) {
    send_to_char(ch, "Usage: instance <zone vnum>|list|leave\r\n");
    return;
  }

  zone = real_zone(atoi(arg));
  if (zone == NOWHERE) {
    send_to_char(ch, "No such zone.\r\n");
    return;
  }
  if (!ZONE_FLAGGED(zone, ZONE_DUNGEON)) {
    send_to_char(ch, "That zone is not flagged DUNGEON.\r\n");
    return;
  }
  if (GET_INSTANCE_ID(ch)) {
    send_to_char(ch, "Leave your current instance first.\r\n");
    return;
  }

  return_room = IN_ROOM(ch);
  group_inst = instance_group_instance(ch, zone);
  if (group_inst) {
    id = group_inst->id;
    entry = instance_entry_room(group_inst);
  } else {
    id = instance_create(zone, return_room, &entry);
  }

  if (!id || entry == NOWHERE) {
    send_to_char(ch, "Unable to create an instance from that zone.\r\n");
    return;
  }

  act("$n opens a shimmering tear in the air and steps through.", TRUE, ch, 0, 0, TO_ROOM);
  char_from_room(ch);
  char_to_room(ch, entry);
  ch->instance_return_room = valid_room_rnum(return_room) ? return_room : r_mortal_start_room;
  act("$n steps out of a shimmering tear in the air.", TRUE, ch, 0, 0, TO_ROOM);
  send_to_char(ch, "%s dungeon instance #%d.\r\n",
    group_inst ? "Joined your group's" : "Created", id);
  look_at_room(ch, 0);
}
