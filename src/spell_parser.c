/**************************************************************************
 *  File: spell_parser.c                                    Part of tbaMUD *
 *  Usage: Top-level magic routines; outside points of entry to magic sys. *
 *                                                                         *
 *  All rights reserved.  See license for complete information.            *
 *                                                                         *
 *  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "interpreter.h"
#include "spells.h"
#include "handler.h"
#include "comm.h"
#include "db.h"
#include "dg_scripts.h"
#include "mud_event.h"
#include "fight.h"  /* for hit() */

#define SINFO spell_info[spellnum]

/* Global Variables definitions, used elsewhere */
struct spell_info_type spell_info[TOP_SPELL_DEFINE + 1];
char cast_arg2[MAX_INPUT_LENGTH];
const char *unused_spellname = "!UNUSED!"; /* So we can get &unused_spellname */

/* Local (File Scope) Function Prototypes */
static void say_spell(struct char_data *ch, int spellnum, struct char_data *tch,
    struct obj_data *tobj);
static int can_cast_spell_now(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum);
static int cast_spell_internal(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum, int say_words);
static void spello(int spl, const char *name, int max_mana, int min_mana,
    int mana_change, int minpos, int targets, int violent, int routines,
    const char *wearoff);
static void spell_cast_info(int spell, int cast_time, int cast_style);
static int start_delayed_spell(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum, int mana);
static int mag_manacost(struct char_data *ch, int spellnum);

/* Local (File Scope) Variables */
struct syllable {
  const char *org;
  const char *news;
};

#define DELAYED_CAST_NONE 0
#define DELAYED_CAST_CHAR 1
#define DELAYED_CAST_OBJ  2

static struct syllable syls[] = { { " ", " " }, { "ar", "abra" },
    { "ate", "i" }, { "cau", "kada" }, { "blind", "nose" }, { "bur", "mosa" }, {
        "cu", "judi" }, { "de", "oculo" }, { "dis", "mar" },
    { "ect", "kamina" }, { "en", "uns" }, { "gro", "cra" }, { "light", "dies" },
    { "lo", "hi" }, { "magi", "kari" }, { "mon", "bar" }, { "mor", "zak" }, {
        "move", "sido" }, { "ness", "lacri" }, { "ning", "illa" }, { "per",
        "duda" }, { "ra", "gru" }, { "re", "candus" }, { "son", "sabru" }, {
        "tect", "infra" }, { "tri", "cula" }, { "ven", "nofo" }, { "word of",
        "inset" }, { "a", "i" }, { "b", "v" }, { "c", "q" }, { "d", "m" }, {
        "e", "o" }, { "f", "y" }, { "g", "t" }, { "h", "p" }, { "i", "u" }, {
        "j", "y" }, { "k", "t" }, { "l", "r" }, { "m", "w" }, { "n", "b" }, {
        "o", "a" }, { "p", "s" }, { "q", "d" }, { "r", "f" }, { "s", "g" }, {
        "t", "h" }, { "u", "e" }, { "v", "z" }, { "w", "x" }, { "x", "n" }, {
        "y", "l" }, { "z", "k" }, { "", "" } };

static int mag_manacost(struct char_data *ch, int spellnum) {
  int rank = MAX(1, GET_SKILL_RANK(ch, spellnum));

  return MAX(SINFO.mana_max - (SINFO.mana_change * (rank - 1)), SINFO.mana_min);
}

static char *obfuscate_spell(const char *unobfuscated) {
  static char obfuscated[200];
  int maxlen = 200;

  int j, ofs = 0;

  *obfuscated = '\0';

  while (unobfuscated[ofs]) {
    for (j = 0; *(syls[j].org); j++) {
      if (!strncmp(syls[j].org, unobfuscated + ofs, strlen(syls[j].org))) {
        if (strlen(syls[j].news) < maxlen) {
          strncat(obfuscated, syls[j].news, maxlen);
          maxlen -= strlen(syls[j].news);
        } else {
          log("No room in obfuscated version of '%s' (currently obfuscated to '%s') to add syllable '%s'.",
              unobfuscated, obfuscated, syls[j].news);
        }
        ofs += strlen(syls[j].org);
        break;
      }
    }
    /* i.e., we didn't find a match in syls[] */
    if (!*syls[j].org) {
      log("No entry in syllable table for substring of '%s' starting at '%s'.", unobfuscated, unobfuscated + ofs);
      ofs++;
    }
  }
  return obfuscated;
}

static void say_spell(struct char_data *ch, int spellnum, struct char_data *tch,
    struct obj_data *tobj) {
  const char *format, *spell = skill_name(spellnum);
  char act_buf_original[256], act_buf_obfuscated[256], *obfuscated = obfuscate_spell(spell);


  struct char_data *i;

  if (tch != NULL && SAME_ROOM(tch, ch)) {
    if (tch == ch)
      format = "$n closes $s eyes and utters the words, '%s'.";
    else
      format = "$n stares at $N and utters the words, '%s'.";
  } else if (tobj != NULL
      && ((GET_INSTANCE_ID(tobj) == GET_INSTANCE_ID(ch) && IN_ROOM(tobj) == IN_ROOM(ch)) || (tobj->carried_by == ch)))
    format = "$n stares at $p and utters the words, '%s'.";
  else
    format = "$n utters the words, '%s'.";

  snprintf(act_buf_original, sizeof(act_buf_original), format, spell);
  snprintf(act_buf_obfuscated, sizeof(act_buf_obfuscated), format, obfuscated);

  for (i = GET_ROOM(ch)->people; i; i = i->next_in_room) {
    if (i == ch || i == tch || !i->desc || !AWAKE(i))
      continue;
    if (GET_CLASS(ch) == GET_CLASS(i))
      perform_act(act_buf_original, ch, tobj, tch, i);
    else
      perform_act(act_buf_obfuscated, ch, tobj, tch, i);
  }

  if (tch != NULL && tch != ch && SAME_ROOM(tch, ch)) {
    snprintf(act_buf_original, sizeof(act_buf_original), "$n stares at you and utters the words, '%s'.",
    GET_CLASS(ch) == GET_CLASS(tch) ? spell : obfuscated);
    act(act_buf_original, FALSE, ch, NULL, tch, TO_VICT);
  }
}

static int character_still_exists(struct char_data *victim)
{
  struct char_data *i;

  if (victim == NULL)
    return FALSE;

  for (i = character_list; i; i = i->next)
    if (i == victim)
      return TRUE;

  return FALSE;
}

static int object_still_exists(struct obj_data *obj)
{
  struct obj_data *i;

  if (obj == NULL)
    return FALSE;

  for (i = object_list; i; i = i->next)
    if (i == obj)
      return TRUE;

  return FALSE;
}

static const char *spell_casting_word(int cast_style)
{
  return (cast_style == SPELL_CAST_SPIRITUAL) ? "praying" : "chanting";
}

static void send_cast_countdown(struct char_data *ch, int spellnum, int stars)
{
  char starbuf[64] = "";
  int i;

  for (i = 0; i < stars && i < 20; i++)
    strlcat(starbuf, " *", sizeof(starbuf));

  send_to_char(ch, "Casting %s%s\r\n", skill_name(spellnum), starbuf);
}

static void send_cast_start(struct char_data *ch, int spellnum)
{
  if (spell_info[spellnum].cast_style == SPELL_CAST_SPIRITUAL) {
    send_to_char(ch, "You call out to the gods!\r\n");
    act("$n calls out to the gods!", FALSE, ch, 0, 0, TO_ROOM);
  } else {
    send_to_char(ch, "You start chanting!\r\n");
    act("$n starts chanting!", FALSE, ch, 0, 0, TO_ROOM);
  }
}

static void send_cast_finish(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum)
{
  const char *spell = skill_name(spellnum);
  const char *word = spell_casting_word(spell_info[spellnum].cast_style);
  char to_char[256], to_vict[256], to_room[256];

  if (tch != NULL && tch != ch && SAME_ROOM(ch, tch)) {
    snprintf(to_char, sizeof(to_char),
        "You stop %s then look at $N and utter '%s'.", word, spell);
    snprintf(to_vict, sizeof(to_vict),
        "$n stops %s then looks at you and utters '%s'.", word, spell);
    snprintf(to_room, sizeof(to_room),
        "$n stops %s then looks at $N and utters '%s'.", word, spell);
    act(to_char, FALSE, ch, 0, tch, TO_CHAR);
    act(to_vict, FALSE, ch, 0, tch, TO_VICT);
    act(to_room, FALSE, ch, 0, tch, TO_NOTVICT);
  } else if (tobj != NULL) {
    snprintf(to_char, sizeof(to_char),
        "You stop %s then look at $p and utter '%s'.", word, spell);
    snprintf(to_room, sizeof(to_room),
        "$n stops %s then looks at $p and utters '%s'.", word, spell);
    act(to_char, FALSE, ch, tobj, 0, TO_CHAR);
    act(to_room, FALSE, ch, tobj, 0, TO_ROOM);
  } else {
    snprintf(to_char, sizeof(to_char),
        "You stop %s and utter '%s'.", word, spell);
    snprintf(to_room, sizeof(to_room),
        "$n stops %s and utters '%s'.", word, spell);
    act(to_char, FALSE, ch, 0, 0, TO_CHAR);
    act(to_room, FALSE, ch, 0, 0, TO_ROOM);
  }
}

static int delayed_cast_target_type(struct char_data *tch, struct obj_data *tobj)
{
  if (tch != NULL)
    return DELAYED_CAST_CHAR;
  if (tobj != NULL)
    return DELAYED_CAST_OBJ;
  return DELAYED_CAST_NONE;
}

static int delayed_cast_target_valid(struct char_data *ch, int spellnum,
    int target_type, struct char_data **tch, struct obj_data **tobj)
{
  if (target_type == DELAYED_CAST_CHAR) {
    if (!character_still_exists(*tch) || GET_POS(*tch) <= POS_DEAD)
      return FALSE;

    if (IS_SET(SINFO.targets, TAR_CHAR_ROOM | TAR_FIGHT_SELF | TAR_FIGHT_VICT) &&
        !SAME_ROOM(ch, *tch))
      return FALSE;
  } else if (target_type == DELAYED_CAST_OBJ) {
    if (!object_still_exists(*tobj))
      return FALSE;

    if (IS_SET(SINFO.targets, TAR_OBJ_ROOM) &&
        (IN_ROOM(*tobj) != IN_ROOM(ch) ||
        GET_INSTANCE_ID(*tobj) != GET_INSTANCE_ID(ch)))
      return FALSE;
  } else {
    *tch = NULL;
    *tobj = NULL;
  }

  return TRUE;
}

static int parse_delayed_cast_vars(const char *vars, int *spellnum, int *mana,
    int *remaining, int *start_room, int *start_instance, int *target_type,
    struct char_data **tch, struct obj_data **tobj)
{
  void *tch_ptr = NULL, *tobj_ptr = NULL;

  if (vars == NULL)
    return FALSE;

  if (sscanf(vars, "%d %d %d %d %d %d %p %p", spellnum, mana, remaining,
      start_room, start_instance, target_type, &tch_ptr, &tobj_ptr) != 8)
    return FALSE;

  *tch = (struct char_data *) tch_ptr;
  *tobj = (struct obj_data *) tobj_ptr;
  return TRUE;
}

static void update_delayed_cast_vars(struct mud_event_data *pMudEvent,
    int spellnum, int mana, int remaining, int start_room, int start_instance,
    int target_type, struct char_data *tch, struct obj_data *tobj)
{
  char vars[MAX_INPUT_LENGTH];

  snprintf(vars, sizeof(vars), "%d %d %d %d %d %d %p %p", spellnum, mana,
      remaining, start_room, start_instance, target_type, (void *) tch,
      (void *) tobj);

  if (pMudEvent->sVariables != NULL)
    free(pMudEvent->sVariables);
  pMudEvent->sVariables = strdup(vars);
}

EVENTFUNC(event_spell_cast)
{
  struct mud_event_data *pMudEvent = (struct mud_event_data *) event_obj;
  struct char_data *ch, *tch = NULL;
  struct obj_data *tobj = NULL;
  int spellnum = 0, mana = 0, remaining = 0, start_room = NOWHERE;
  int start_instance = 0, target_type = DELAYED_CAST_NONE;

  if (pMudEvent == NULL || pMudEvent->pStruct == NULL)
    return 0;

  ch = (struct char_data *) pMudEvent->pStruct;

  if (!parse_delayed_cast_vars(pMudEvent->sVariables, &spellnum, &mana,
      &remaining, &start_room, &start_instance, &target_type, &tch, &tobj))
    return 0;

  if (spellnum < 1 || spellnum > TOP_SPELL_DEFINE || GET_POS(ch) <= POS_DEAD)
    return 0;

  if (IN_ROOM(ch) != start_room || GET_INSTANCE_ID(ch) != start_instance ||
      GET_POS(ch) < spell_info[spellnum].min_position) {
    send_to_char(ch, "Your concentration breaks.\r\n");
    return 0;
  }

  remaining--;
  if (remaining > 0) {
    send_cast_countdown(ch, spellnum, remaining);
    update_delayed_cast_vars(pMudEvent, spellnum, mana, remaining, start_room,
        start_instance, target_type, tch, tobj);
    return PASSES_PER_SEC;
  }

  if (!delayed_cast_target_valid(ch, spellnum, target_type, &tch, &tobj)) {
    send_to_char(ch, "Your spell fizzles as its target slips away.\r\n");
    return 0;
  }

  send_cast_finish(ch, tch, tobj, spellnum);
  cast_spell_internal(ch, tch, tobj, spellnum, FALSE);
  return 0;
}

/* This function should be used anytime you are not 100% sure that you have
 * a valid spell/skill number.  A typical for() loop would not need to use
 * this because you can guarantee > 0 and <= TOP_SPELL_DEFINE. */
const char *skill_name(int num) {
  if (num > 0 && num <= TOP_SPELL_DEFINE)
    return (spell_info[num].name);
  else if (num == -1)
    return ("UNUSED");
  else
    return ("UNDEFINED");
}

int find_skill_num(char *name) {
  int skindex, ok;
  char *temp, *temp2;
  char first[256], first2[256], tempbuf[256];

  for (skindex = 1; skindex <= TOP_SPELL_DEFINE; skindex++) {
    if (is_abbrev(name, spell_info[skindex].name))
      return (skindex);

    ok = TRUE;
    strlcpy(tempbuf, spell_info[skindex].name, sizeof(tempbuf)); /* strlcpy: OK */
    temp = any_one_arg(tempbuf, first);
    temp2 = any_one_arg(name, first2);
    while (*first && *first2 && ok) {
      if (!is_abbrev(first2, first))
        ok = FALSE;
      temp = any_one_arg(temp, first);
      temp2 = any_one_arg(temp2, first2);
    }

    if (ok && !*first2)
      return (skindex);
  }

  return (-1);
}

/* This function is the very heart of the entire magic system.  All invocations
 * of all types of magic -- objects, spoken and unspoken PC and NPC spells, the
 * works -- all come through this function eventually. This is also the entry
 * point for non-spoken or unrestricted spells. Spellnum 0 is legal but silently
 * ignored here, to make callers simpler. */
int call_magic(struct char_data *caster, struct char_data *cvict,
    struct obj_data *ovict, int spellnum, int level, int casttype) {
  int savetype;

  if (spellnum < 1 || spellnum > TOP_SPELL_DEFINE)
    return (0);

  if (!cast_wtrigger(caster, cvict, ovict, spellnum))
    return 0;
  if (!cast_otrigger(caster, ovict, spellnum))
    return 0;
  if (!cast_mtrigger(caster, cvict, spellnum))
    return 0;

  if (IN_ROOM_FLAGGED(caster, ROOM_NOMAGIC)) {
    send_to_char(caster, "Your magic fizzles out and dies.\r\n");
    act("$n's magic fizzles out and dies.", FALSE, caster, 0, 0, TO_ROOM);
    return (0);
  }
  if (IN_ROOM_FLAGGED(caster, ROOM_PEACEFUL) && (SINFO.violent || IS_SET(SINFO.routines, MAG_DAMAGE))) {
    send_to_char(caster, "A flash of white light fills the room, dispelling your violent magic!\r\n");
    act("White light from no particular source suddenly fills the room, then vanishes.", FALSE, caster, 0, 0, TO_ROOM);
    return (0);
  }
  if (cvict && MOB_FLAGGED(cvict, MOB_NOKILL)) {
    send_to_char(caster, "This mob is protected.\r\n");
    return (0);
  }
  savetype = spell_savetype(spellnum, casttype);

  if (SINFO.violent && cvict) {
    int attacker_roll = 0, defender_roll = 0;

    if (!spell_attack_hits(caster, cvict, savetype, &attacker_roll, &defender_roll)) {
      if (CONFIG_DEBUG_MODE >= NRM)
        send_to_char(caster, "\t1Debug:\r\n   \t2Spell attack roll: \t3%d\r\n"
          "   \t2Spell defense roll: \t3%d\tn\r\n",
          attacker_roll, defender_roll);

      damage(caster, cvict, 0, spellnum);
      return (1);
    }

    if (CONFIG_DEBUG_MODE >= NRM)
      send_to_char(caster, "\t1Debug:\r\n   \t2Spell attack roll: \t3%d\r\n"
        "   \t2Spell defense roll: \t3%d\tn\r\n",
        attacker_roll, defender_roll);
  }

  if (IS_SET(SINFO.routines, MAG_DAMAGE))
    if (mag_damage(level, caster, cvict, spellnum, savetype) == -1)
      return (-1); /* Successful and target died, don't cast again. */

  if (IS_SET(SINFO.routines, MAG_AFFECTS))
    mag_affects(level, caster, cvict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_UNAFFECTS))
    mag_unaffects(level, caster, cvict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_POINTS))
    mag_points(level, caster, cvict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_ALTER_OBJS))
    mag_alter_objs(level, caster, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_GROUPS))
    mag_groups(level, caster, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_MASSES))
    mag_masses(level, caster, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_AREAS))
    mag_areas(level, caster, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_SUMMONS))
    mag_summons(level, caster, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_CREATIONS))
    mag_creations(level, caster, spellnum);

  if (IS_SET(SINFO.routines, MAG_ROOMS))
    mag_rooms(level, caster, spellnum);

  if (IS_SET(SINFO.routines, MAG_MANUAL))
    switch (spellnum) {
    case SPELL_CHARM:
      MANUAL_SPELL(spell_charm)
      ;
      break;
    case SPELL_CREATE_WATER:
      MANUAL_SPELL(spell_create_water)
      ;
      break;
    case SPELL_DETECT_POISON:
      MANUAL_SPELL(spell_detect_poison)
      ;
      break;
    case SPELL_ENCHANT_WEAPON:
      MANUAL_SPELL(spell_enchant_weapon)
      ;
      break;
    case SPELL_IDENTIFY:
      MANUAL_SPELL(spell_identify)
      ;
      break;
    case SPELL_LOCATE_OBJECT:
      MANUAL_SPELL(spell_locate_object)
      ;
      break;
    case SPELL_SUMMON:
      MANUAL_SPELL(spell_summon)
      ;
      break;
    case SPELL_WORD_OF_RECALL:
      MANUAL_SPELL(spell_recall)
      ;
      break;
    case SPELL_TELEPORT:
      MANUAL_SPELL(spell_teleport)
      ;
      break;
    }

  return (1);
}

/* mag_objectmagic: This is the entry-point for all magic items.  This should
 * only be called by the 'quaff', 'use', 'recite', etc. routines.
 * For reference, object values 0-3:
 * staff  - [0]	level	[1] max charges	[2] num charges	[3] spell num
 * wand   - [0]	level	[1] max charges	[2] num charges	[3] spell num
 * scroll - [0]	level	[1] spell num	[2] spell num	[3] spell num
 * potion - [0] level	[1] spell num	[2] spell num	[3] spell num
 * Staves and wands will default to level 14 if the level is not specified; the
 * DikuMUD format did not specify staff and wand levels in the world files */
void mag_objectmagic(struct char_data *ch, struct obj_data *obj, char *argument) {
  char arg[MAX_INPUT_LENGTH];
  int i, k;
  struct char_data *tch = NULL, *next_tch;
  struct obj_data *tobj = NULL;

  one_argument(argument, arg);

  k = generic_find(arg, FIND_CHAR_ROOM | FIND_OBJ_INV | FIND_OBJ_ROOM |
  FIND_OBJ_EQUIP, ch, &tch, &tobj);

  switch (GET_OBJ_TYPE(obj)) {
  case ITEM_STAFF:
    act("You tap $p three times on the ground.", FALSE, ch, obj, 0, TO_CHAR);
    if (obj->action_description)
      act(obj->action_description, FALSE, ch, obj, 0, TO_ROOM);
    else
      act("$n taps $p three times on the ground.", FALSE, ch, obj, 0, TO_ROOM);

    if (GET_OBJ_VAL(obj, 2) <= 0) {
      send_to_char(ch, "It seems powerless.\r\n");
      act("Nothing seems to happen.", FALSE, ch, obj, 0, TO_ROOM);
    } else {
      GET_OBJ_VAL(obj, 2)--;
      WAIT_STATE(ch, PULSE_VIOLENCE);
      /* Level to cast spell at. */
      k = GET_OBJ_VAL(obj, 0) ? GET_OBJ_VAL(obj, 0) : DEFAULT_STAFF_LVL;

      /* Area/mass spells on staves can cause crashes. So we use special cases
       * for those spells spells here. */
      if (HAS_SPELL_ROUTINE(GET_OBJ_VAL(obj, 3), MAG_MASSES | MAG_AREAS)) {
        for (i = 0, tch = GET_ROOM(ch)->people; tch;
            tch = tch->next_in_room)
          i++;
        while (i-- > 0)
          call_magic(ch, NULL, NULL, GET_OBJ_VAL(obj, 3), k, CAST_STAFF);
      } else {
        for (tch = GET_ROOM(ch)->people; tch; tch = next_tch) {
          next_tch = tch->next_in_room;
          if (ch != tch)
            call_magic(ch, tch, NULL, GET_OBJ_VAL(obj, 3), k, CAST_STAFF);
        }
      }
    }
    break;
  case ITEM_WAND:
    if (k == FIND_CHAR_ROOM) {
      if (tch == ch) {
        act("You point $p at yourself.", FALSE, ch, obj, 0, TO_CHAR);
        act("$n points $p at $mself.", FALSE, ch, obj, 0, TO_ROOM);
      } else {
        act("You point $p at $N.", FALSE, ch, obj, tch, TO_CHAR);
        if (obj->action_description)
          act(obj->action_description, FALSE, ch, obj, tch, TO_ROOM);
        else
          act("$n points $p at $N.", TRUE, ch, obj, tch, TO_ROOM);
      }
    } else if (tobj != NULL) {
      act("You point $p at $P.", FALSE, ch, obj, tobj, TO_CHAR);
      if (obj->action_description)
        act(obj->action_description, FALSE, ch, obj, tobj, TO_ROOM);
      else
        act("$n points $p at $P.", TRUE, ch, obj, tobj, TO_ROOM);
    } else if (IS_SET(spell_info[GET_OBJ_VAL(obj, 3)].routines,
        MAG_AREAS | MAG_MASSES)) {
      /* Wands with area spells don't need to be pointed. */
      act("You point $p outward.", FALSE, ch, obj, NULL, TO_CHAR);
      act("$n points $p outward.", TRUE, ch, obj, NULL, TO_ROOM);
    } else {
      act("At what should $p be pointed?", FALSE, ch, obj, NULL, TO_CHAR);
      return;
    }

    if (GET_OBJ_VAL(obj, 2) <= 0) {
      send_to_char(ch, "It seems powerless.\r\n");
      act("Nothing seems to happen.", FALSE, ch, obj, 0, TO_ROOM);
      return;
    }
    GET_OBJ_VAL(obj, 2)--;
    WAIT_STATE(ch, PULSE_VIOLENCE);
    if (GET_OBJ_VAL(obj, 0))
      call_magic(ch, tch, tobj, GET_OBJ_VAL(obj, 3), GET_OBJ_VAL(obj, 0),
          CAST_WAND);
    else
      call_magic(ch, tch, tobj, GET_OBJ_VAL(obj, 3),
      DEFAULT_WAND_LVL, CAST_WAND);
    break;
  case ITEM_SCROLL:
    if (*arg) {
      if (!k) {
        act("There is nothing to here to affect with $p.", FALSE, ch, obj, NULL,
            TO_CHAR);
        return;
      }
    } else
      tch = ch;

    act("You recite $p which dissolves.", TRUE, ch, obj, 0, TO_CHAR);
    if (obj->action_description)
      act(obj->action_description, FALSE, ch, obj, tch, TO_ROOM);
    else
      act("$n recites $p.", FALSE, ch, obj, NULL, TO_ROOM);

    WAIT_STATE(ch, PULSE_VIOLENCE);
    for (i = 1; i <= 3; i++)
      if (call_magic(ch, tch, tobj, GET_OBJ_VAL(obj, i), GET_OBJ_VAL(obj, 0),
          CAST_SCROLL) <= 0)
        break;

    if (obj != NULL)
      extract_obj(obj);
    break;
  case ITEM_POTION:
    tch = ch;

    if (!consume_otrigger(obj, ch, OCMD_QUAFF)) /* check trigger */
      return;

    act("You quaff $p.", FALSE, ch, obj, NULL, TO_CHAR);
    if (obj->action_description)
      act(obj->action_description, FALSE, ch, obj, NULL, TO_ROOM);
    else
      act("$n quaffs $p.", TRUE, ch, obj, NULL, TO_ROOM);

    WAIT_STATE(ch, PULSE_VIOLENCE);
    for (i = 1; i <= 3; i++)
      if (call_magic(ch, ch, NULL, GET_OBJ_VAL(obj, i), GET_OBJ_VAL(obj, 0),
          CAST_POTION) <= 0)
        break;

    if (obj != NULL)
      extract_obj(obj);
    break;
  default:
    log("SYSERR: Unknown object_type %d in mag_objectmagic.",
        GET_OBJ_TYPE(obj));
    break;
  }
}

static int can_cast_spell_now(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum)
{
  (void)tobj;

  if (spellnum < 0 || spellnum > TOP_SPELL_DEFINE) {
    log("SYSERR: cast_spell trying to call spellnum %d/%d.", spellnum,
    TOP_SPELL_DEFINE);
    return (0);
  }

  if (GET_POS(ch) < SINFO.min_position) {
    switch (GET_POS(ch)) {
      case POS_SLEEPING:
        send_to_char(ch, "You dream about great magical powers.\r\n");
        break;
      case POS_RESTING:
        send_to_char(ch, "You cannot concentrate while resting.\r\n");
        break;
      case POS_SITTING:
        send_to_char(ch, "You can't do this sitting!\r\n");
        break;
      case POS_FIGHTING:
        send_to_char(ch, "Impossible!  You can't concentrate enough!\r\n");
        break;
      default:
        send_to_char(ch, "You can't do much of anything like this!\r\n");
        break;
    }
    return (0);
  }
  if (AFF_FLAGGED(ch, AFF_CHARM) && (ch->master == tch)) {
    send_to_char(ch, "You are afraid you might hurt your master!\r\n");
    return (0);
  }
  if ((tch != ch) && IS_SET(SINFO.targets, TAR_SELF_ONLY)) {
    send_to_char(ch, "You can only cast this spell upon yourself!\r\n");
    return (0);
  }
  if ((tch == ch) && IS_SET(SINFO.targets, TAR_NOT_SELF)) {
    send_to_char(ch, "You cannot cast this spell upon yourself!\r\n");
    return (0);
  }
  if (IS_SET(SINFO.routines, MAG_GROUPS) && !GROUP(ch)) {
    send_to_char(ch, "You can't cast this spell if you're not in a group!\r\n");
    return (0);
  }

  return (1);
}

static int cast_spell_internal(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum, int say_words)
{
  if (!can_cast_spell_now(ch, tch, tobj, spellnum))
    return (0);

  if (say_words) {
    send_to_char(ch, "%s", CONFIG_OK);
    say_spell(ch, spellnum, tch, tobj);
  }

  return (call_magic(ch, tch, tobj, spellnum, GET_LEVEL(ch), CAST_SPELL));
}

/* cast_spell is used generically to cast any spoken spell, assuming we already
 * have the target char/obj and spell number.  It checks all restrictions,
 * prints the words, etc. Entry point for NPC casts.  Recommended entry point
 * for spells cast by NPCs via specprocs. */
int cast_spell(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum)
{
  return cast_spell_internal(ch, tch, tobj, spellnum, TRUE);
}

static int start_delayed_spell(struct char_data *ch, struct char_data *tch,
    struct obj_data *tobj, int spellnum, int mana)
{
  char vars[MAX_INPUT_LENGTH];
  int cast_time = spell_info[spellnum].cast_time;
  int target_type = delayed_cast_target_type(tch, tobj);

  if (AFF_FLAGGED(ch, AFF_HASTE))
    cast_time /= 2;

  if (char_has_mud_event(ch, eSPELL_CAST)) {
    send_to_char(ch, "You are already casting a spell.\r\n");
    return FALSE;
  }

  if (!can_cast_spell_now(ch, tch, tobj, spellnum))
    return FALSE;

  if (cast_time <= 0) {
    if (cast_spell(ch, tch, tobj, spellnum)) {
      if (mana > 0)
        GET_MANA(ch) = MAX(0, MIN(GET_MAX_MANA(ch), GET_MANA(ch) - mana));
    }
    return TRUE;
  }

  snprintf(vars, sizeof(vars), "%d %d %d %d %d %d %p %p", spellnum, mana,
      cast_time, IN_ROOM(ch), GET_INSTANCE_ID(ch), target_type, (void *) tch,
      (void *) tobj);

  send_cast_start(ch, spellnum);
  if (mana > 0)
    GET_MANA(ch) = MAX(0, MIN(GET_MAX_MANA(ch), GET_MANA(ch) - mana));

  NEW_EVENT(eSPELL_CAST, ch, vars, PASSES_PER_SEC);
  WAIT_STATE(ch, cast_time * PASSES_PER_SEC);
  return TRUE;
}

/* do_cast is the entry point for PC-casted spells.  It parses the arguments,
 * determines the spell number and finds a target, throws the die to see if
 * the spell can be cast, checks for sufficient mana and subtracts it, and
 * passes control to cast_spell(). */
ACMD(do_cast) {
  struct char_data *tch = NULL;
  struct obj_data *tobj = NULL;
  char *s, *t;
  int number, mana, spellnum, i, target = 0;

  if (IS_NPC(ch))
    return;

  /* get: blank, spell name, target name */
  s = strtok(argument, "'");

  if (s == NULL) {
    send_to_char(ch, "Cast what where?\r\n");
    return;
  }
  s = strtok(NULL, "'");
  if (s == NULL) {
    send_to_char(ch,
        "Spell names must be enclosed in the Holy Magic Symbols: '\r\n");
    return;
  }
  t = strtok(NULL, "\0");

  skip_spaces(&s);

  /* spellnum = search_block(s, spells, 0); */
  spellnum = find_skill_num(s);

  if ((spellnum < 1) || (spellnum > MAX_SPELLS) || !*s) {
    send_to_char(ch, "Cast what?!?\r\n");
    return;
  }
  if (GET_SKILL_RANK(ch, spellnum) == 0) {
    send_to_char(ch, "You are unfamiliar with that spell.\r\n");
    return;
  }
  /* Find the target */
  if (t != NULL) {
    char arg[MAX_INPUT_LENGTH];

    strlcpy(arg, t, sizeof(arg));
    one_argument(arg, t);
    skip_spaces(&t);

    /* Copy target to global cast_arg2, for use in spells like locate object */
    strcpy(cast_arg2, t);
  }
  if (IS_SET(SINFO.targets, TAR_IGNORE)) {
    target = TRUE;
  } else if (t != NULL && *t) {
    number = get_number(&t);
    if (!target && (IS_SET(SINFO.targets, TAR_CHAR_ROOM))) {
      if ((tch = get_char_vis(ch, t, &number, FIND_CHAR_ROOM)) != NULL)
        target = TRUE;
    }
    if (!target && IS_SET(SINFO.targets, TAR_CHAR_WORLD))
      if ((tch = get_char_vis(ch, t, &number, FIND_CHAR_WORLD)) != NULL)
        target = TRUE;

    if (!target && IS_SET(SINFO.targets, TAR_OBJ_INV))
      if ((tobj = get_obj_in_list_vis(ch, t, &number, ch->carrying)) != NULL)
        target = TRUE;

    if (!target && IS_SET(SINFO.targets, TAR_OBJ_EQUIP)) {
      for (i = 0; !target && i < NUM_WEARS; i++)
        if (GET_EQ(ch, i) && isname(t, GET_EQ(ch, i)->name)) {
          tobj = GET_EQ(ch, i);
          target = TRUE;
        }
    }
    if (!target && IS_SET(SINFO.targets, TAR_OBJ_ROOM))
      if ((tobj = get_obj_in_list_vis(ch, t, &number,
          GET_ROOM(ch)->contents)) != NULL)
        target = TRUE;

    if (!target && IS_SET(SINFO.targets, TAR_OBJ_WORLD))
      if ((tobj = get_obj_vis(ch, t, &number)) != NULL)
        target = TRUE;

  } else { /* if target string is empty */
    if (!target && IS_SET(SINFO.targets, TAR_FIGHT_SELF))
      if (FIGHTING(ch) != NULL) {
        tch = ch;
        target = TRUE;
      }
    if (!target && IS_SET(SINFO.targets, TAR_FIGHT_VICT))
      if (FIGHTING(ch) != NULL) {
        tch = FIGHTING(ch);
        target = TRUE;
      }
    /* if no target specified, and the spell isn't violent, default to self */
    if (!target && IS_SET(SINFO.targets, TAR_CHAR_ROOM) && !SINFO.violent) {
      tch = ch;
      target = TRUE;
    }
    if (!target) {
      send_to_char(ch, "Upon %s should the spell be cast?\r\n",
          IS_SET(SINFO.targets, TAR_OBJ_ROOM | TAR_OBJ_INV | TAR_OBJ_WORLD | TAR_OBJ_EQUIP) ?
              "what" : "who");
      return;
    }
  }

  if (target && (tch == ch) && SINFO.violent) {
    send_to_char(ch, "You shouldn't cast that on yourself -- could be bad for your health!\r\n");
    return;
  }
  if (!target) {
    send_to_char(ch, "Cannot find the target of your spell!\r\n");
    return;
  }
  mana = mag_manacost(ch, spellnum);
  if ((mana > 0) && (GET_MANA(ch) < mana) && (GET_LEVEL(ch) < LVL_IMMORT)) {
    send_to_char(ch, "You haven't the energy to cast that spell!\r\n");
    return;
  }

  if (SINFO.cast_time > 0) {
    start_delayed_spell(ch, tch, tobj, spellnum, mana);
    return;
  }

  if (cast_spell(ch, tch, tobj, spellnum)) {
    WAIT_STATE(ch, PULSE_VIOLENCE);
    if (mana > 0)
      GET_MANA(ch) = MAX(0, MIN(GET_MAX_MANA(ch), GET_MANA(ch) - mana));
  }
}

void spell_level(int spell, int chclass, int level) {
  int i;

  if (spell < 0 || spell > TOP_SPELL_DEFINE) {
    log("SYSERR: attempting assign to illegal spellnum %d/%d", spell,
        TOP_SPELL_DEFINE);
    return;
  }

  (void)chclass;

  if (level < 1 || level > LVL_IMPL) {
    log("SYSERR: assigning '%s' to illegal level %d/%d.", skill_name(spell),
        level, LVL_IMPL);
    return;
  }

  for (i = 0; i < NUM_CLASSES; i++)
    spell_info[spell].min_level[i] = MIN(spell_info[spell].min_level[i], level);
}

/* Assign the spells on boot up */
static void spello(int spl, const char *name, int max_mana, int min_mana,
    int mana_change, int minpos, int targets, int violent, int routines,
    const char *wearoff) {
  int i;

  for (i = 0; i < NUM_CLASSES; i++)
    spell_info[spl].min_level[i] = LVL_IMMORT;
  spell_info[spl].mana_max = max_mana;
  spell_info[spl].mana_min = min_mana;
  spell_info[spl].mana_change = mana_change;
  spell_info[spl].min_position = minpos;
  spell_info[spl].targets = targets;
  spell_info[spl].violent = violent;
  spell_info[spl].routines = routines;
  spell_info[spl].cast_time = 0;
  spell_info[spl].cast_style = SPELL_CAST_ARCANE;
  spell_info[spl].name = name;
  spell_info[spl].wear_off_msg = wearoff;
}

static void spell_cast_info(int spell, int cast_time, int cast_style)
{
  if (spell < 1 || spell > TOP_SPELL_DEFINE)
    return;

  spell_info[spell].cast_time = MIN(255, MAX(0, cast_time));
  spell_info[spell].cast_style = cast_style;
}

void unused_spell(int spl) {
  int i;

  for (i = 0; i < NUM_CLASSES; i++)
    spell_info[spl].min_level[i] = LVL_IMPL + 1;
  spell_info[spl].mana_max = 0;
  spell_info[spl].mana_min = 0;
  spell_info[spl].mana_change = 0;
  spell_info[spl].min_position = 0;
  spell_info[spl].targets = 0;
  spell_info[spl].violent = 0;
  spell_info[spl].routines = 0;
  spell_info[spl].cast_time = 0;
  spell_info[spl].cast_style = SPELL_CAST_NONE;
  spell_info[spl].name = unused_spellname;
}

#define skillo(skill, name) spello(skill, name, 0, 0, 0, 0, 0, 0, 0, NULL);
/* Arguments for spello calls:
 * spellnum, maxmana, minmana, manachng, minpos, targets, violent?, routines.
 * spellnum:  Number of the spell.  Usually the symbolic name as defined in
 *  spells.h (such as SPELL_HEAL).
 * spellname: The name of the spell.
 * maxmana :  The maximum mana this spell will take (i.e., the mana it
 *  will take when the player first gets the spell).
 * minmana :  The minimum mana this spell will take, no matter how high
 *  level the caster is.
 * manachng:  The change in mana for the spell from level to level.  This
 *  number should be positive, but represents the reduction in mana cost as
 *  the caster's level increases.
 * minpos  :  Minimum position the caster must be in for the spell to work
 *  (usually fighting or standing). targets :  A "list" of the valid targets
 *  for the spell, joined with bitwise OR ('|').
 * violent :  TRUE or FALSE, depending on if this is considered a violent
 *  spell and should not be cast in PEACEFUL rooms or on yourself.  Should be
 *  set on any spell that inflicts damage, is considered aggressive (i.e.
 *  charm, curse), or is otherwise nasty.
 * routines:  A list of magic routines which are associated with this spell
 *  if the spell uses spell templates.  Also joined with bitwise OR ('|').
 * See the documentation for a more detailed description of these fields. You
 * only need a spello() call to define a new spell; to decide who gets to use
 * a spell or skill, look in class.c.  -JE */
void mag_assign_spells(void) {
  int i;

  /* Do not change the loop below. */
  for (i = 0; i <= TOP_SPELL_DEFINE; i++)
    unused_spell(i);
  /* Do not change the loop above. */

  spello(SPELL_ANIMATE_DEAD, "animate dead", 35, 10, 3, POS_STANDING,
  TAR_OBJ_ROOM, FALSE, MAG_SUMMONS, NULL);

  spello(SPELL_ARMOR, "armor", 30, 15, 3, POS_FIGHTING,
  TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "You feel less protected.");

  spello(SPELL_BLESS, "bless", 35, 5, 3, POS_STANDING,
  TAR_CHAR_ROOM | TAR_OBJ_INV, FALSE, MAG_AFFECTS | MAG_ALTER_OBJS,
      "You feel less righteous.");

  spello(SPELL_BLINDNESS, "blindness", 35, 25, 1, POS_STANDING,
  TAR_CHAR_ROOM | TAR_NOT_SELF, TRUE, MAG_AFFECTS,
      "You feel a cloak of blindness dissolve.");

  spello(SPELL_BURNING_HANDS, "burning hands", 30, 10, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_CALL_LIGHTNING, "call lightning", 40, 25, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_CHARM, "charm person", 75, 50, 2, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_NOT_SELF, TRUE, MAG_MANUAL,
      "You feel more self-confident.");

  spello(SPELL_CHILL_TOUCH, "chill touch", 30, 10, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_AFFECTS,
      "You feel your strength return.");

  spello(SPELL_CLONE, "clone", 80, 65, 5, POS_STANDING,
  TAR_IGNORE, FALSE, MAG_SUMMONS, NULL);

  spello(SPELL_COLOR_SPRAY, "color spray", 30, 15, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_CONTROL_WEATHER, "control weather", 75, 25, 5, POS_STANDING,
  TAR_IGNORE, FALSE, MAG_MANUAL, NULL);

  spello(SPELL_CREATE_FOOD, "create food", 30, 5, 4, POS_STANDING,
  TAR_IGNORE, FALSE, MAG_CREATIONS, NULL);

  spello(SPELL_CREATE_WATER, "create water", 30, 5, 4, POS_STANDING,
  TAR_OBJ_INV | TAR_OBJ_EQUIP, FALSE, MAG_MANUAL, NULL);

  spello(SPELL_CURE_BLIND, "cure blind", 30, 5, 2, POS_STANDING,
  TAR_CHAR_ROOM, FALSE, MAG_UNAFFECTS, NULL);

  spello(SPELL_CURE_CRITIC, "cure critic", 30, 10, 2, POS_FIGHTING,
  TAR_CHAR_ROOM, FALSE, MAG_POINTS, NULL);

  spello(SPELL_CURE_LIGHT, "cure light", 30, 10, 2, POS_FIGHTING,
  TAR_CHAR_ROOM, FALSE, MAG_POINTS, NULL);

  spello(SPELL_CAUSE_LIGHT, "cause light", 30, 10, 2, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_CAUSE_CRITIC, "cause critic", 30, 10, 2, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_CURSE, "curse", 80, 50, 2, POS_STANDING,
  TAR_CHAR_ROOM | TAR_OBJ_INV, TRUE, MAG_AFFECTS | MAG_ALTER_OBJS,
      "You feel more optimistic.");

  spello(SPELL_DARKNESS, "darkness", 30, 5, 4, POS_STANDING,
  TAR_IGNORE, FALSE, MAG_ROOMS, NULL);

  spello(SPELL_DETECT_ALIGN, "detect alignment", 20, 10, 2, POS_STANDING,
  TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS, "You feel less aware.");

  spello(SPELL_DETECT_INVIS, "detect invisibility", 20, 10, 2, POS_STANDING,
  TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
      "Your eyes stop tingling.");

  spello(SPELL_DETECT_MAGIC, "detect magic", 20, 10, 2, POS_STANDING,
  TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
      "The detect magic wears off.");

  spello(SPELL_DETECT_POISON, "detect poison", 15, 5, 1, POS_STANDING,
  TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_MANUAL,
      "The detect poison wears off.");

  spello(SPELL_DISPEL_EVIL, "dispel evil", 40, 25, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_DISPEL_GOOD, "dispel good", 40, 25, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_EARTHQUAKE, "earthquake", 40, 25, 3, POS_FIGHTING,
  TAR_IGNORE, TRUE, MAG_AREAS, NULL);

  spello(SPELL_ENCHANT_WEAPON, "enchant weapon", 150, 100, 10, POS_STANDING,
  TAR_OBJ_INV, FALSE, MAG_MANUAL, NULL);

  spello(SPELL_ENERGY_DRAIN, "energy drain", 40, 25, 1, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_MANUAL, NULL);

  spello(SPELL_GROUP_ARMOR, "group armor", 50, 30, 2, POS_STANDING,
  TAR_IGNORE, FALSE, MAG_GROUPS, NULL);

  spello(SPELL_FIREBALL, "fireball", 40, 30, 2, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_FLY, "fly", 40, 20, 2, POS_FIGHTING,
  TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "You drift slowly to the ground.");

  spello(SPELL_GROUP_HEAL, "group heal", 80, 60, 5, POS_STANDING,
  TAR_IGNORE, FALSE, MAG_GROUPS, NULL);

  spello(SPELL_HARM, "harm", 75, 45, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_HASTE, "haste", 75, 50, 3, POS_STANDING,
  TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "You feel slower.");

  spello(SPELL_HEAL, "heal", 60, 40, 3, POS_FIGHTING,
  TAR_CHAR_ROOM, FALSE, MAG_POINTS | MAG_UNAFFECTS, NULL);

  spello(SPELL_INFRAVISION, "infravision", 25, 10, 1, POS_STANDING,
  TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
      "Your night vision seems to fade.");

  spello(SPELL_INVISIBLE, "invisibility", 35, 25, 1, POS_STANDING,
  TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE,
      MAG_AFFECTS | MAG_ALTER_OBJS, "You feel yourself exposed.");

  spello(SPELL_LIGHTNING_BOLT, "lightning bolt", 30, 15, 1, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_LOCATE_OBJECT, "locate object", 25, 20, 1, POS_STANDING,
  TAR_OBJ_WORLD, FALSE, MAG_MANUAL, NULL);

  spello(SPELL_MAGIC_MISSILE, "magic missile", 25, 10, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_POISON, "poison", 50, 20, 3, POS_STANDING,
  TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_OBJ_INV, TRUE,
  MAG_AFFECTS | MAG_ALTER_OBJS, "You feel less sick.");

  spello(SPELL_PROT_FROM_EVIL, "protection from evil", 40, 10, 3, POS_STANDING,
  TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
      "You feel less protected.");

  spello(SPELL_PROT_FROM_GOOD, "protection from good", 40, 10, 3, POS_STANDING,
  TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
      "You feel less protected.");

  spello(SPELL_REMOVE_CURSE, "remove curse", 45, 25, 5, POS_STANDING,
  TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_EQUIP, FALSE,
  MAG_UNAFFECTS | MAG_ALTER_OBJS, NULL);

  spello(SPELL_REMOVE_POISON, "remove poison", 40, 8, 4, POS_STANDING,
  TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE,
      MAG_UNAFFECTS | MAG_ALTER_OBJS, NULL);

  spello(SPELL_SANCTUARY, "sanctuary", 110, 85, 5, POS_STANDING,
  TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "The white aura around your body fades.");

  spello(SPELL_SENSE_LIFE, "sense life", 20, 10, 2, POS_STANDING,
  TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
      "You feel less aware of your surroundings.");

  spello(SPELL_SHOCKING_GRASP, "shocking grasp", 30, 15, 3, POS_FIGHTING,
  TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL);

  spello(SPELL_SLEEP, "sleep", 40, 25, 5, POS_STANDING,
  TAR_CHAR_ROOM, TRUE, MAG_AFFECTS, "You feel less tired.");

  spello(SPELL_STRENGTH, "strength", 35, 30, 1, POS_STANDING,
  TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "You feel weaker.");

  spello(SPELL_SUMMON, "summon", 75, 50, 3, POS_STANDING,
  TAR_CHAR_WORLD | TAR_NOT_SELF, FALSE, MAG_MANUAL, NULL);

  spello(SPELL_TELEPORT, "teleport", 75, 50, 3, POS_STANDING,
  TAR_CHAR_ROOM, FALSE, MAG_MANUAL, NULL);

  spello(SPELL_WATERWALK, "waterwalk", 40, 20, 2, POS_STANDING,
  TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "Your feet seem less buoyant.");

  spello(SPELL_WORD_OF_RECALL, "word of recall", 20, 10, 2, POS_FIGHTING,
  TAR_CHAR_ROOM, FALSE, MAG_MANUAL, NULL);

  spello(SPELL_IDENTIFY, "identify", 50, 25, 5, POS_STANDING,
  TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_MANUAL, NULL);

  /* NON-castable spells should appear below here. */
  spello(SPELL_IDENTIFY, "identify", 0, 0, 0, 0,
  TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_MANUAL, NULL);

  /* you might want to name this one something more fitting to your theme -Welcor*/
  spello(SPELL_DG_AFFECT, "Script-inflicted", 0, 0, 0, POS_SITTING,
  TAR_IGNORE, TRUE, 0, NULL);

  spell_cast_info(SPELL_ANIMATE_DEAD, 12, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_ARMOR, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_BLESS, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_BLINDNESS, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_BURNING_HANDS, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CALL_LIGHTNING, 6, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CHARM, 8, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CHILL_TOUCH, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CLONE, 15, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_COLOR_SPRAY, 2, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CONTROL_WEATHER, 10, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CREATE_FOOD, 5, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CREATE_WATER, 5, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_CURE_BLIND, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_CURE_CRITIC, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_CURE_LIGHT, 2, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_CAUSE_CRITIC, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_CAUSE_LIGHT, 2, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_CURSE, 6, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_DARKNESS, 5, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_DETECT_ALIGN, 4, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_DETECT_INVIS, 4, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_DETECT_MAGIC, 4, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_DETECT_POISON, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_DISPEL_EVIL, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_DISPEL_GOOD, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_EARTHQUAKE, 6, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_ENCHANT_WEAPON, 12, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_ENERGY_DRAIN, 6, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_GROUP_ARMOR, 6, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_FIREBALL, 4, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_FLY, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_GROUP_HEAL, 8, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_HARM, 6, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_HASTE, 12, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_HEAL, 6, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_INFRAVISION, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_INVISIBLE, 5, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_LIGHTNING_BOLT, 4, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_LOCATE_OBJECT, 7, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_MAGIC_MISSILE, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_POISON, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_PROT_FROM_EVIL, 3, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_PROT_FROM_GOOD, 3, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_REMOVE_CURSE, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_REMOVE_POISON, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_SANCTUARY, 4, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_SENSE_LIFE, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_SHOCKING_GRASP, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_SLEEP, 3, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_STRENGTH, 4, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_SUMMON, 12, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_TELEPORT, 12, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_WATERWALK, 2, SPELL_CAST_ARCANE);
  spell_cast_info(SPELL_WORD_OF_RECALL, 12, SPELL_CAST_SPIRITUAL);
  spell_cast_info(SPELL_IDENTIFY, 4, SPELL_CAST_ARCANE);

  /* Declaration of skills - this actually doesn't do anything except set it up
   * so that immortals can use these skills by default.  The min level to use
   * the skill for other classes is set up in class.c. */
  skillo(SKILL_BACKSTAB, "backstab");
  skillo(SKILL_BASH, "bash");
  skillo(SKILL_HIDE, "hide");
  skillo(SKILL_KICK, "kick");
  skillo(SKILL_PICK_LOCK, "pick lock");
  skillo(SKILL_RESCUE, "rescue");
  skillo(SKILL_SNEAK, "sneak");
  skillo(SKILL_STEAL, "steal");
  skillo(SKILL_TRACK, "track");
  skillo(SKILL_WHIRLWIND, "whirlwind");
  skillo(SKILL_BANDAGE, "bandage");
  skillo(SKILL_DUAL_WIELD, "dual wield");
}
