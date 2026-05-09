/**
* @file instance.h
* Runtime dungeon instance support.
*/
#ifndef _INSTANCE_H_
#define _INSTANCE_H_

extern room_rnum top_of_runtime_world;
extern zone_rnum top_of_runtime_zone_table;
extern int top_of_runtime_shop;
extern struct room_data *iworld;
extern struct zone_data *izone;
extern struct shop_data *ishop_index;

room_rnum room_data_rnum(struct room_data *room);
struct room_data *room_by_script_id(long id);
struct room_data *room_by_vnum_instance(room_vnum vnum, int instance_id);
room_rnum room_rnum_by_vnum_instance(room_vnum vnum, int instance_id);
int instance_room_id(room_rnum room);
int instance_room_is_template(room_rnum room);
int instance_zone_is_template(zone_rnum zone);
int instance_zone_is_runtime(zone_rnum zone);
int instance_shop_top(void);
room_rnum instance_safe_return_room(struct char_data *ch);
room_vnum instance_safe_load_room_vnum(struct char_data *ch);
void instance_set_safe_loadroom(struct char_data *ch);
int instance_create(zone_rnum template_zone, room_rnum return_room, long owner_id,
  room_rnum *entry_room);
int instance_create_at_room(zone_rnum template_zone, room_rnum template_room,
  room_rnum return_room, long owner_id, room_rnum *entry_room);
int instance_enter_zone(struct char_data *ch, zone_rnum zone,
  room_rnum template_entry_room, room_rnum return_room, const char *leave_msg,
  const char *enter_msg, int *instance_id, const char **action);
int instance_exit_to_room(struct char_data *ch, room_rnum target);
int instance_teleport_to_room(struct char_data *ch, room_rnum target);
int instance_leave(struct char_data *ch);
int instance_relocate_char(struct char_data *ch);
void instance_update(void);
void instance_list_to_char(struct char_data *ch);

#endif /* _INSTANCE_H_ */
