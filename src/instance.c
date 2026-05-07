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

#define INSTANCE_BLOCK_SIZE 100
#define INSTANCE_MAX_ROOM_SLOT ((IDXTYPE_MAX - (INSTANCE_BLOCK_SIZE - 1)) / INSTANCE_BLOCK_SIZE)

struct instance_obj_ref {
  struct obj_data *obj;
  struct instance_obj_ref *next;
};

struct instance_data {
  int id;
  int slot;
  long owner_id;
  zone_rnum template_zone;
  zone_rnum zone;
  room_rnum *template_rooms;
  room_rnum *rooms;
  room_rnum room_start;
  int room_count;
  int shop_start;
  int shop_count;
  room_rnum return_room;
  time_t empty_since;
  struct instance_data *next;
};

struct room_data *iworld = NULL;
struct zone_data *izone = NULL;
struct shop_data *ishop_index = NULL;
room_rnum top_of_runtime_world = NOWHERE;
zone_rnum top_of_runtime_zone_table = NOWHERE;
int top_of_runtime_shop = -1;

static struct instance_data *instance_list = NULL;
static int next_instance_id = 1;
static zone_rnum izone_base = NOWHERE;
static int ishop_base = -1;

static struct instance_data *instance_by_id(int id);
static int allocate_instance_id(void);
static int allocate_instance_slot(void);
static int ensure_instance_blocks(int slot);
static int instance_room_offset(zone_rnum zone, room_rnum room);
static struct instance_data *instance_owned_by_char(struct char_data *ch, zone_rnum template_zone);
static struct instance_data *instance_group_instance(struct char_data *ch, zone_rnum template_zone);
static struct room_data *instance_room_slot(struct instance_data *inst, room_rnum room);
static struct room_data *instance_room_at(struct instance_data *inst, room_rnum room);
static room_rnum instance_room_for_template(struct instance_data *inst, room_rnum room);
static room_rnum instance_entry_room(struct instance_data *inst);
static void clone_room_to_instance(struct instance_data *inst, int index);
static int count_instance_shops(zone_rnum zone);
static void clone_instance_shops(struct instance_data *inst);
static void reset_instance(struct instance_data *inst);
static void destroy_instance(struct instance_data *inst);
static void free_instance_shop_slot(int shop);
static int instance_player_count(struct instance_data *inst);
static void queue_instance_room_chars(struct instance_data *inst, room_rnum room);
static void track_obj(struct instance_obj_ref **list, struct obj_data *obj);
static struct obj_data *find_tracked_obj(struct instance_obj_ref *list, obj_rnum rnum);
static void free_tracked_objs(struct instance_obj_ref *list);

static int izone_index(zone_rnum rnum)
{
  if (izone_base == NOWHERE || rnum < izone_base || rnum > top_of_runtime_zone_table)
    return -1;
  return rnum - izone_base;
}

static int ishop_index_num(int shop)
{
  if (ishop_base < 0 || shop < ishop_base || shop > top_of_runtime_shop)
    return -1;
  return shop - ishop_base;
}

static struct zone_data *izone_slot(zone_rnum rnum)
{
  int idx = izone_index(rnum);

  return (idx >= 0 && izone) ? &izone[idx] : NULL;
}

static struct shop_data *ishop_slot(int shop)
{
  int idx = ishop_index_num(shop);

  return (idx >= 0 && ishop_index) ? &ishop_index[idx] : NULL;
}

struct room_data *room_by_rnum(room_rnum rnum)
{
  if (rnum == NOWHERE)
    return NULL;
  if (rnum >= 0 && rnum <= top_of_world)
    return &world[rnum];
  return NULL;
}

struct room_data *room_by_rnum_instance(room_rnum rnum, int instance_id)
{
  struct instance_data *inst;
  struct room_data *room = NULL;

  if (rnum == NOWHERE)
    return NULL;

  if (instance_id > 0) {
    inst = instance_by_id(instance_id);
    room = instance_room_at(inst, rnum);
    if (room)
      return room;
  }

  return room_by_rnum(rnum);
}

struct zone_data *zone_by_rnum(zone_rnum rnum)
{
  int idx;

  if (rnum == NOWHERE)
    return NULL;
  if (rnum >= 0 && rnum <= top_of_zone_table)
    return &zone_table[rnum];
  if ((idx = izone_index(rnum)) >= 0 && izone && izone[idx].instance_id > 0)
    return &izone[idx];
  return NULL;
}

struct shop_data *shop_by_rnum(shop_rnum rnum)
{
  int idx;

  if (rnum < 0)
    return NULL;
  if (rnum <= top_shop)
    return &shop_index[rnum];
  if ((idx = ishop_index_num(rnum)) >= 0 && ishop_index && ishop_index[idx].instance_id > 0)
    return &ishop_index[idx];
  return NULL;
}

int valid_room_rnum(room_rnum rnum)
{
  return room_by_rnum(rnum) != NULL;
}

int valid_room_rnum_instance(room_rnum rnum, int instance_id)
{
  return room_by_rnum_instance(rnum, instance_id) != NULL;
}

int instance_shop_top(void)
{
  return MAX(top_shop, top_of_runtime_shop);
}

int instance_room_id(room_rnum room)
{
  struct room_data *r = room_by_rnum(room);

  if (!r)
    return 0;
  return r->instance_id;
}

int instance_zone_is_template(zone_rnum zone)
{
  struct zone_data *z = zone_by_rnum(zone);

  if (!z)
    return FALSE;
  return IS_SET_AR(z->zone_flags, ZONE_DUNGEON);
}

int instance_zone_is_runtime(zone_rnum zone)
{
  struct zone_data *z = zone_by_rnum(zone);

  if (!z)
    return FALSE;
  return IS_SET_AR(z->zone_flags, ZONE_INSTANCE);
}

int instance_room_is_template(room_rnum room)
{
  struct room_data *r = room_by_rnum(room);

  if (!r)
    return FALSE;
  return instance_zone_is_template(r->zone) && r->instance_id == 0;
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

static int allocate_instance_id(void)
{
  int start, id;

  if (next_instance_id <= 0)
    next_instance_id = 1;

  start = next_instance_id;
  do {
    id = next_instance_id;
    next_instance_id = (next_instance_id == INT_MAX) ? 1 : next_instance_id + 1;

    if (id > 0 && !instance_by_id(id))
      return id;
  } while (next_instance_id != start);

  return 0;
}

static int allocate_instance_slot(void)
{
  struct instance_data *inst;
  int slot, in_use;

  for (slot = 0; slot <= INSTANCE_MAX_ROOM_SLOT; slot++) {
    in_use = FALSE;
    for (inst = instance_list; inst; inst = inst->next) {
      if (inst->slot == slot) {
        in_use = TRUE;
        break;
      }
    }

    if (!in_use)
      return slot;
  }

  return -1;
}

static int ensure_iworld_block(int slot)
{
  room_rnum new_top = (slot * INSTANCE_BLOCK_SIZE) + INSTANCE_BLOCK_SIZE - 1;
  size_t old_count = (top_of_runtime_world == NOWHERE) ? 0 : top_of_runtime_world + 1;
  size_t new_count = new_top + 1;

  if (slot < 0 || slot > INSTANCE_MAX_ROOM_SLOT)
    return FALSE;

  if (top_of_runtime_world != NOWHERE && new_top <= top_of_runtime_world)
    return TRUE;

  RECREATE(iworld, struct room_data, new_count);
  if (new_count > old_count)
    memset(iworld + old_count, 0, (new_count - old_count) * sizeof(struct room_data));
  top_of_runtime_world = new_top;
  return TRUE;
}

static int ensure_izone_block(int slot)
{
  zone_rnum new_top;
  size_t old_count, new_count;

  if (izone_base == NOWHERE) {
    if (top_of_zone_table >= IDXTYPE_MAX - 1)
      return FALSE;
    izone_base = top_of_zone_table + 1;
    top_of_runtime_zone_table = izone_base - 1;
  }

  if ((int)izone_base + slot >= IDXTYPE_MAX)
    return FALSE;

  new_top = izone_base + slot;
  old_count = (top_of_runtime_zone_table == NOWHERE ||
      top_of_runtime_zone_table < izone_base) ? 0 :
      top_of_runtime_zone_table - izone_base + 1;
  new_count = slot + 1;

  if (top_of_runtime_zone_table != NOWHERE && new_top <= top_of_runtime_zone_table)
    return TRUE;

  RECREATE(izone, struct zone_data, new_count);
  if (new_count > old_count)
    memset(izone + old_count, 0, (new_count - old_count) * sizeof(struct zone_data));
  top_of_runtime_zone_table = new_top;
  return TRUE;
}

static int ensure_ishop_block(int slot)
{
  int new_top;
  size_t old_count, new_count;

  if (ishop_base < 0) {
    if (top_shop >= IDXTYPE_MAX - 1)
      return FALSE;
    ishop_base = top_shop + 1;
    top_of_runtime_shop = ishop_base - 1;
  }

  if (ishop_base + (slot * INSTANCE_BLOCK_SIZE) + INSTANCE_BLOCK_SIZE - 1 >= IDXTYPE_MAX)
    return FALSE;

  new_top = ishop_base + (slot * INSTANCE_BLOCK_SIZE) + INSTANCE_BLOCK_SIZE - 1;
  old_count = (top_of_runtime_shop < ishop_base) ? 0 : top_of_runtime_shop - ishop_base + 1;
  new_count = (slot + 1) * INSTANCE_BLOCK_SIZE;

  if (new_top <= top_of_runtime_shop)
    return TRUE;

  RECREATE(ishop_index, struct shop_data, new_count);
  if (new_count > old_count)
    memset(ishop_index + old_count, 0, (new_count - old_count) * sizeof(struct shop_data));
  top_of_runtime_shop = new_top;
  return TRUE;
}

static int ensure_instance_blocks(int slot)
{
  return ensure_iworld_block(slot) && ensure_izone_block(slot) && ensure_ishop_block(slot);
}

static int instance_room_offset(zone_rnum zone, room_rnum room)
{
  struct zone_data *z = ZONE_AT(zone);
  struct room_data *r = ROOM_AT(room);
  int offset;

  if (!z || !r)
    return -1;

  offset = r->number - z->bot;
  return (offset >= 0 && offset < INSTANCE_BLOCK_SIZE) ? offset : -1;
}

static struct room_data *instance_room_slot(struct instance_data *inst, room_rnum room)
{
  if (!inst || !iworld || room == NOWHERE || room >= INSTANCE_BLOCK_SIZE)
    return NULL;

  return &iworld[inst->room_start + room];
}

static struct room_data *instance_room_at(struct instance_data *inst, room_rnum room)
{
  struct room_data *r = instance_room_slot(inst, room);

  if (!r || r->instance_id != inst->id)
    return NULL;

  return r;
}

static struct instance_data *instance_owned_by_char(struct char_data *ch, zone_rnum template_zone)
{
  struct instance_data *inst;
  long owner_id;

  if (!ch || IS_NPC(ch) || GET_IDNUM(ch) <= 0)
    return NULL;

  owner_id = GET_IDNUM(ch);
  for (inst = instance_list; inst; inst = inst->next)
    if (inst->owner_id == owner_id && inst->template_zone == template_zone)
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

  return instance_room_at(inst, inst->rooms[0]) ? inst->rooms[0] : NOWHERE;
}

static void clone_room_to_instance(struct instance_data *inst, int index)
{
  struct room_data *src = ROOM_AT(inst->template_rooms[index]);
  struct room_data *dst = instance_room_slot(inst, inst->rooms[index]);
  int dir;

  if (!src || !dst)
    return;

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
    struct room_data *room = instance_room_at(inst, inst->rooms[i]);

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

    if (room != NOWHERE && GET_ROOM_ZONE(room) == zone)
      return TRUE;
  }
  return FALSE;
}

static int count_instance_shops(zone_rnum zone)
{
  int shop, count = 0;

  for (shop = 0; shop <= top_shop; shop++)
    if (shop_has_room_in_zone(shop, zone))
      count++;

  return count;
}

static void clone_instance_shops(struct instance_data *inst)
{
  int shop, new_shop, offset = 0;
  struct shop_data *dst;

  inst->shop_start = ishop_base + (inst->slot * INSTANCE_BLOCK_SIZE);
  inst->shop_count = 0;

  for (shop = 0; shop <= top_shop; shop++) {
    if (!shop_has_room_in_zone(shop, inst->template_zone))
      continue;

    if (offset >= INSTANCE_BLOCK_SIZE) {
      log("SYSERR: Instance %d has too many shops for slot block %d.", inst->id,
        INSTANCE_BLOCK_SIZE);
      break;
    }

    new_shop = inst->shop_start + offset++;
    dst = ishop_slot(new_shop);
    if (!dst)
      continue;
    memset(dst, 0, sizeof(struct shop_data));
    copy_shop(dst, SHOP_AT(shop), FALSE);
    dst->instance_id = inst->id;
    dst->template_shop = shop;
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
  struct reset_com *cmd = ZONE_AT(inst->template_zone)->cmd;
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
        mob->instance_id = inst->id;
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
        obj->instance_id = inst->id;
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
      obj->instance_id = GET_INSTANCE_ID(mob);
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
      {
        struct room_data *r = instance_room_at(inst, room);

      if (room != NOWHERE &&
            r && (obj = get_obj_in_list_num(cmd[cmd_no].arg2, r->contents)) != NULL)
        extract_obj(obj);
      }
      tmob = NULL;
      tobj = NULL;
      last_cmd = 1;
      break;

    case 'D':
      room = instance_room_for_template(inst, cmd[cmd_no].arg1);
      {
        struct room_data *r = instance_room_at(inst, room);

      if (room == NOWHERE || cmd[cmd_no].arg2 < 0 || cmd[cmd_no].arg2 >= DIR_COUNT ||
            !r || !r->dir_option[cmd[cmd_no].arg2]) {
        last_cmd = 0;
        break;
      }
      switch (cmd[cmd_no].arg3) {
      case 0:
        REMOVE_BIT(r->dir_option[cmd[cmd_no].arg2]->exit_info, EX_LOCKED);
        REMOVE_BIT(r->dir_option[cmd[cmd_no].arg2]->exit_info, EX_CLOSED);
        break;
      case 1:
        SET_BIT(r->dir_option[cmd[cmd_no].arg2]->exit_info, EX_CLOSED);
        REMOVE_BIT(r->dir_option[cmd[cmd_no].arg2]->exit_info, EX_LOCKED);
        break;
      case 2:
        SET_BIT(r->dir_option[cmd[cmd_no].arg2]->exit_info, EX_LOCKED);
        SET_BIT(r->dir_option[cmd[cmd_no].arg2]->exit_info, EX_CLOSED);
        break;
      }
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
        struct room_data *r = instance_room_at(inst, room);

        if (r) {
          if (!r->script)
            CREATE(r->script, struct script_data, 1);
          add_trigger(r->script, read_trigger(cmd[cmd_no].arg2), -1);
          last_cmd = 1;
        } else
          last_cmd = 0;
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
      } else if (cmd[cmd_no].arg1 == WLD_TRIGGER && room != NOWHERE) {
        struct room_data *r = instance_room_at(inst, room);

        if (r && r->script) {
          add_var(&(r->script->global_vars), cmd[cmd_no].sarg1, cmd[cmd_no].sarg2,
            cmd[cmd_no].arg2);
          last_cmd = 1;
        } else
          last_cmd = 0;
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
    reset_wtrigger(instance_room_at(inst, inst->rooms[cmd_no]));
}

int instance_create(zone_rnum template_zone, room_rnum return_room, long owner_id,
  room_rnum *entry_room)
{
  struct instance_data *inst;
  struct zone_data *src_zone, *dst_zone;
  int i, count = 0, shop_count, slot, id;
  int used_offsets[INSTANCE_BLOCK_SIZE] = { 0 };
  room_rnum room;
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

  if (count > INSTANCE_BLOCK_SIZE) {
    mudlog(BRF, LVL_IMPL, TRUE,
      "Unable to instance zone %d: %d rooms exceeds block size %d.",
      ZONE_AT(template_zone)->number, count, INSTANCE_BLOCK_SIZE);
    return 0;
  }

  shop_count = count_instance_shops(template_zone);
  if (shop_count > INSTANCE_BLOCK_SIZE) {
    mudlog(BRF, LVL_IMPL, TRUE,
      "Unable to instance zone %d: %d shops exceeds block size %d.",
      ZONE_AT(template_zone)->number, shop_count, INSTANCE_BLOCK_SIZE);
    return 0;
  }

  slot = allocate_instance_slot();
  if (slot < 0)
    return 0;

  id = allocate_instance_id();
  if (!id)
    return 0;

  if (!ensure_instance_blocks(slot))
    return 0;

  CREATE(inst, struct instance_data, 1);
  inst->id = id;
  inst->slot = slot;
  inst->owner_id = owner_id;
  inst->template_zone = template_zone;
  inst->return_room = return_room;
  inst->room_count = count;
  inst->room_start = slot * INSTANCE_BLOCK_SIZE;
  CREATE(inst->template_rooms, room_rnum, count);
  CREATE(inst->rooms, room_rnum, count);

  i = 0;
  for (room = 0; room <= top_of_world; room++) {
    int offset;

    if (world[room].zone != template_zone)
      continue;

    offset = instance_room_offset(template_zone, room);
    if (offset < 0 || used_offsets[offset]) {
      mudlog(BRF, LVL_IMPL, TRUE,
        "Unable to instance zone %d: room %d is outside or duplicates the %d-room block.",
        ZONE_AT(template_zone)->number, world[room].number, INSTANCE_BLOCK_SIZE);
      free(inst->template_rooms);
      free(inst->rooms);
      free(inst);
      return 0;
    }

    used_offsets[offset] = TRUE;
    inst->template_rooms[i] = room;
    inst->rooms[i] = offset;
    i++;
  }

  new_zone = izone_base + slot;
  dst_zone = izone_slot(new_zone);
  src_zone = ZONE_AT(template_zone);
  memset(dst_zone, 0, sizeof(struct zone_data));
  *dst_zone = *src_zone;
  dst_zone->name = str_udup(src_zone->name);
  dst_zone->builders = str_udup(src_zone->builders);
  dst_zone->cmd = NULL;
  dst_zone->age = 0;
  dst_zone->reset_mode = 0;
  dst_zone->instance_id = inst->id;
  dst_zone->template_zone = template_zone;
  REMOVE_BIT_AR(dst_zone->zone_flags, ZONE_DUNGEON);
  SET_BIT_AR(dst_zone->zone_flags, ZONE_INSTANCE);
  inst->zone = new_zone;

  for (i = 0; i < inst->room_count; i++)
    clone_room_to_instance(inst, i);
  remap_instance_exits(inst);
  clone_instance_shops(inst);

  inst->next = instance_list;
  instance_list = inst;
  inst->empty_since = 0;

  reset_instance(inst);

  if (entry_room)
    *entry_room = inst->rooms[0];

  mudlog(CMP, LVL_IMPL, TRUE, "Created dungeon instance %d from zone %d (%s) in slot %d.",
    inst->id, src_zone->number, src_zone->name, inst->slot);
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
  ch->instance_id = 0;
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
  ch->instance_id = 0;
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
  struct shop_data *s = SHOP_AT(shop);
  int i;

  if (!s || !s->instance_id)
    return;

  if (s->no_such_item1)
    free(s->no_such_item1);
  if (s->no_such_item2)
    free(s->no_such_item2);
  if (s->missing_cash1)
    free(s->missing_cash1);
  if (s->missing_cash2)
    free(s->missing_cash2);
  if (s->do_not_buy)
    free(s->do_not_buy);
  if (s->message_buy)
    free(s->message_buy);
  if (s->message_sell)
    free(s->message_sell);
  if (s->in_room)
    free(s->in_room);
  if (s->producing)
    free(s->producing);
  if (s->type) {
    for (i = 0; BUY_TYPE(s->type[i]) != NOTHING; i++)
      if (BUY_WORD(s->type[i]))
        free(BUY_WORD(s->type[i]));
    free(s->type);
  }

  memset(s, 0, sizeof(struct shop_data));
}

static void queue_instance_room_chars(struct instance_data *inst, room_rnum room)
{
  struct char_data *ch, *next_ch;
  struct room_data *r = instance_room_at(inst, room);

  if (!r)
    return;

  for (ch = r->people; ch; ch = next_ch) {
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
  struct zone_data *z;
  int i;

  for (i = 0; i < INSTANCE_BLOCK_SIZE; i++)
    queue_instance_room_chars(inst, i);
  extract_pending_chars();

  for (i = 0; i < INSTANCE_BLOCK_SIZE; i++) {
    struct room_data *r = instance_room_slot(inst, i);

    if (!r || r->instance_id != inst->id)
      continue;

    while (r->contents)
      extract_obj(r->contents);

    if (SCRIPT(r))
      extract_script(r, WLD_TRIGGER);
    free_proto_script(r, WLD_TRIGGER);
    free_room_strings(r);
    memset(r, 0, sizeof(struct room_data));
    r->number = NOWHERE;
    r->zone = NOWHERE;
  }

  for (i = 0; i < INSTANCE_BLOCK_SIZE; i++)
    free_instance_shop_slot(inst->shop_start + i);

  z = ZONE_AT(inst->zone);
  if (z && z->instance_id == inst->id) {
    if (z->name)
      free(z->name);
    if (z->builders)
      free(z->builders);
    memset(z, 0, sizeof(struct zone_data));
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

  mudlog(CMP, LVL_IMPL, TRUE, "Destroyed dungeon instance %d from slot %d.",
    inst->id, inst->slot);
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
    struct zone_data *z = ZONE_AT(inst->template_zone);

    send_to_char(ch, "  #%d slot %d zone %d (%s), rooms: %d, shops: %d, players: %d\r\n",
      inst->id, inst->slot, z ? z->number : NOWHERE,
      z ? z->name : "unknown", inst->room_count, inst->shop_count, players);
  }
}

ACMD(do_instance)
{
  char arg[MAX_INPUT_LENGTH], subarg[MAX_INPUT_LENGTH];
  struct instance_data *group_inst = NULL, *owned_inst = NULL, *target_inst = NULL;
  const char *action = "Created";
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
  owned_inst = instance_owned_by_char(ch, zone);
  group_inst = instance_group_instance(ch, zone);
  if (owned_inst) {
    target_inst = owned_inst;
    action = "Rejoined your";
  } else if (group_inst) {
    target_inst = group_inst;
    action = "Joined your group's";
  }

  if (target_inst) {
    id = target_inst->id;
    entry = instance_entry_room(target_inst);
    target_inst->empty_since = 0;
  } else {
    id = instance_create(zone, return_room, GET_IDNUM(ch), &entry);
  }

  if (!id || entry == NOWHERE) {
    send_to_char(ch, "Unable to create an instance from that zone.\r\n");
    return;
  }

  act("$n opens a shimmering tear in the air and steps through.", TRUE, ch, 0, 0, TO_ROOM);
  char_from_room(ch);
  ch->instance_id = id;
  char_to_room(ch, entry);
  ch->instance_return_room = valid_room_rnum(return_room) ? return_room : r_mortal_start_room;
  act("$n steps out of a shimmering tear in the air.", TRUE, ch, 0, 0, TO_ROOM);
  send_to_char(ch, "%s dungeon instance #%d.\r\n", action, id);
  look_at_room(ch, 0);
}
