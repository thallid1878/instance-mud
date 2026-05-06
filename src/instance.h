/**
* @file instance.h
* Runtime dungeon instance support.
*/
#ifndef _INSTANCE_H_
#define _INSTANCE_H_

extern room_rnum top_of_runtime_world;
extern zone_rnum top_of_runtime_zone_table;
extern int top_of_runtime_shop;

int instance_room_id(room_rnum room);
int instance_room_is_template(room_rnum room);
int instance_zone_is_template(zone_rnum zone);
int instance_zone_is_runtime(zone_rnum zone);
int instance_shop_top(void);
room_rnum instance_safe_return_room(struct char_data *ch);
room_vnum instance_safe_load_room_vnum(struct char_data *ch);
int instance_create(zone_rnum template_zone, room_rnum return_room, room_rnum *entry_room);
int instance_leave(struct char_data *ch);
int instance_relocate_char(struct char_data *ch);
void instance_update(void);
void instance_list_to_char(struct char_data *ch);

#endif /* _INSTANCE_H_ */
