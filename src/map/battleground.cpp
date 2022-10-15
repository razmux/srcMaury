// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "battleground.hpp"

#include "../common/cbasetypes.hpp"
#include "../common/malloc.hpp"
#include "../common/nullpo.hpp"
#include "../common/showmsg.hpp"
#include "../common/strlib.hpp"
#include "../common/socket.hpp"
#include "../common/timer.hpp"
#include "../common/utils.hpp"

#include "achievement.hpp"
#include "battle.hpp"
#include "clif.hpp"
#include "elemental.hpp"
#include "guild.hpp"
#include "homunculus.hpp"
#include "log.hpp"
#include "mercenary.hpp"
#include "mob.hpp"
#include "npc.hpp"
#include "pc.hpp"
#include "pet.hpp"
#include "quest.hpp"
#include "skill.hpp"
#include "clan.hpp"

static DBMap* bg_team_db; // int bg_id -> struct battleground_data
static DBMap* bg_queue_db;
static unsigned int bg_team_counter = 0; // Next bg_id

struct guild bg_guild[13]; // Temporal fake guild information
const unsigned int bg_colors[13] = { 0x0000FF, 0xFF0000, 0x00FF00, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF };
static unsigned int queue_counter = 0; // Next q_id
 
#define BLUE_SKULL 8965
#define RED_SKULL 8966
#define GREEN_SKULL 8967

int bg_member_removeskulls(struct map_session_data *sd)
{
	int n;
	nullpo_ret(sd);
	if( (n = pc_search_inventory(sd,BLUE_SKULL)) >= 0 )
		pc_delitem(sd,n,sd->inventory.u.items_inventory[n].amount,0,2,LOG_TYPE_OTHER);
	if( (n = pc_search_inventory(sd,RED_SKULL)) >= 0 )
		pc_delitem(sd,n,sd->inventory.u.items_inventory[n].amount,0,2,LOG_TYPE_OTHER);
	if( (n = pc_search_inventory(sd,GREEN_SKULL)) >= 0 )
		pc_delitem(sd,n,sd->inventory.u.items_inventory[n].amount,0,2,LOG_TYPE_OTHER);

	return 1;
}

struct battleground_data* bg_team_search(int bg_id)
{ // Search a BG Team using bg_id
	if( !bg_id )
		return NULL;

	return (struct battleground_data *)idb_get(bg_team_db, bg_id);
}

int bg_countlogin(struct map_session_data *sd, bool check_bat_room)
{
	int c = 0, m = map_mapname2mapid("bat_room");
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	nullpo_ret(sd);

	iter = mapit_getallusers();
	for (pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter))
	{
		if (!(pl_sd->qd || map_getmapflag(pl_sd->bl.m, MF_BATTLEGROUND) || (check_bat_room && pl_sd->bl.m == m)))
			continue;
		if( session[sd->fd]->client_addr == session[pl_sd->fd]->client_addr )
			c++;
	}
	mapit_free(iter);
	return c;
}

int bg_checkskill(struct battleground_data *bg, int id)
{
	int idx = id - GD_SKILLBASE;
	if( idx < 0 || idx >= MAX_GUILDSKILL-1 || !bg->g )
		return 0;
	return bg->g->skill[idx].lv;
}

TIMER_FUNC(bg_block_skill_end)
{
	struct battleground_data *bg;
	char output[128];
	int idx = (int)data - GD_SKILLBASE;

	if( (bg = bg_team_search(id)) == NULL )
		return 1;

	if( idx < 0 || idx >= MAX_GUILDSKILL-1 )
	{
		ShowError("bg_block_skill_end invalid skillnum %d.\n", (int)data);
		return 0;
	}

	if( tid != bg->skill_block_timer[idx] )
	{
		ShowError("bg_block_skill_end %d != %d.\n", bg->skill_block_timer[idx], tid);
		return 0;
	}

	sprintf(output, "%s : Guild Skill %s Ready!!", bg->g->name, skill_get_desc((int)data));
	clif_bg_message(bg, bg->bg_id, bg->g->name, output, strlen(output) + 1);
	bg->skill_block_timer[idx] = INVALID_TIMER;

	return 1;
}

void bg_block_skill_status(struct battleground_data *bg, int skillnum)
{
	const struct TimerData * td;
	char output[128];
	int idx;
	t_tick seconds;

	idx = skillnum - GD_SKILLBASE;
	if( bg == NULL || bg->g == NULL || idx < 0 || idx >= MAX_GUILDSKILL-1 || bg->skill_block_timer[idx] == INVALID_TIMER )
		return;

	if( (td = get_timer(bg->skill_block_timer[idx])) == NULL )
		return;

	seconds = DIFF_TICK(td->tick,gettick())/1000;
	sprintf(output, "%s : Cannot use team skill %s. %I64i seconds remaining................", bg->g->name, skill_get_desc(skillnum), seconds);
	clif_bg_message(bg, bg->bg_id, bg->g->name, output, strlen(output) + 1);
}

void bg_block_skill_start(struct battleground_data *bgd, int skillnum, t_tick time)
{
	int idx = skillnum - GD_SKILLBASE;
	if( bgd == NULL || idx < 0 || idx >= MAX_GUILDSKILL-1 )
		return;

	if( bgd->skill_block_timer[idx] != INVALID_TIMER )
		delete_timer(bgd->skill_block_timer[idx], bg_block_skill_end);

	bgd->skill_block_timer[idx] = add_timer(gettick() + time, bg_block_skill_end, bgd->bg_id, skillnum);
}


struct map_session_data* bg_getavailablesd(struct battleground_data *bg)
{
	int i;

	nullpo_retr(NULL, bg);

	ARR_FIND(0, MAX_BG_MEMBERS, i, bg->members[i].sd != NULL);

	return ( i < MAX_BG_MEMBERS ) ? bg->members[i].sd : NULL;
}

int bg_team_clean(int bg_id, bool remove)
{ // Deletes BG Team from db
 	int i;
	struct map_session_data *sd;
 	struct battleground_data *bgd = bg_team_search(bg_id);
	struct guild *g;
 
	if( bgd == NULL ) return 0;
	for( i = 0; i < MAX_BG_MEMBERS; i++ )
	{
		if( (sd = bgd->members[i].sd) == NULL )
 			continue;
 
 		bg_send_dot_remove(sd);
 		sd->bg_id = 0;
		sd->state.bg_afk = 0;
		sd->bmaster_flag = NULL;
		bg_member_removeskulls(sd);

		if (battle_config.bg_team_color && sd->bg_color)
			pc_changelook(sd,LOOK_CLOTHES_COLOR,sd->bg_color-1);

		// Remove Guild Skill Buffs
		status_change_end(&sd->bl,SC_GUILDAURA,INVALID_TIMER);
		status_change_end(&sd->bl,SC_BATTLEORDERS,INVALID_TIMER);
		status_change_end(&sd->bl,SC_REGENERATION,INVALID_TIMER);

		if( !battle_config.bg_eAmod_mode )
			continue; // No need to touch Guild stuff

		if( sd->status.guild_id && (g = guild_search(sd->status.guild_id)) != NULL )
		{
			clif_guild_belonginfo(sd,g);
			clif_guild_basicinfo(sd);
			clif_guild_allianceinfo(sd);
			clif_guild_memberlist(sd);
			clif_guild_skillinfo(sd);
		}
		else
			clif_bg_leave_single(sd, sd->status.name, "Leaving Battleground...");

		clif_name_area(&sd->bl);
		clif_guild_emblem_area(&sd->bl);
 	}

	for( i = 0; i < MAX_GUILDSKILL-1; i++ )
	{
		if( bgd->skill_block_timer[i] == INVALID_TIMER )
			continue;
		delete_timer(bgd->skill_block_timer[i], bg_block_skill_end);
	}

	if( remove )
		idb_remove(bg_team_db, bg_id);
	else
	{
		bgd->count = 0;
		bgd->leader_char_id = 0;
		bgd->team_score = 0;
		bgd->creation_tick = 0;
		memset(&bgd->members, 0, sizeof(bgd->members));
	}

	return 1;
}

int bg_team_delete(int bg_id)
{ // Deletes BG Team from db
	int i;
	struct battleground_data *bg = bg_team_search(bg_id);

	if( bg == NULL )
		return 0;

	for( i = 0; i < MAX_BG_MEMBERS; i++ ) {
		struct map_session_data *sd;

		if( (sd = bg->members[i].sd) == NULL )
			continue;

		bg_send_dot_remove(sd);
		sd->bg_id = 0;
	}

	idb_remove(bg_team_db, bg_id);

	return 1;
}

int bg_team_warp(int bg_id, unsigned short mapindex, short x, short y)
{ // Warps a Team
	int i;
	struct battleground_data *bg = bg_team_search(bg_id);
	struct map_session_data *sd;
	unsigned short m = mapindex_name2idx("bat_room", nullptr);

	if( bg == NULL )
		return 0;
	if( mapindex == 0 ) {
		mapindex = bg->mapindex;
		x = bg->x;
		y = bg->y;
	}

	for( i = 0; i < MAX_BG_MEMBERS; i++ ) {
		if( (sd = bg->members[i].sd) == NULL )
 			continue;
		if (mapindex == 2000) {
			if (sd->bgentry.mapid)
				pc_setpos(sd, sd->bgentry.mapid, sd->bgentry.x, sd->bgentry.y, CLR_TELEPORT);
			else
				pc_setpos(sd, m, 155, 150, CLR_TELEPORT);
			continue;
		}
		pc_setpos(sd, mapindex, x, y, CLR_TELEPORT);
	}
	return 1;
}

int bg_reveal_pos(struct block_list *bl, va_list ap)
{
	struct map_session_data *pl_sd, *sd = NULL;
	int flag, color;

	pl_sd = (struct map_session_data *)bl;
	sd = va_arg(ap,struct map_session_data *); // Source
	flag = va_arg(ap,int);
	color = va_arg(ap,int);

	if( pl_sd->bg_id == sd->bg_id )
		return 0; // Same Team

	clif_viewpoint(pl_sd,sd->bl.id,flag,sd->bl.x,sd->bl.y,sd->bl.id,color);
	return 0;
}

int bg_team_sub_count(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = (TBL_PC *)bl;

	if (sd->state.autotrade)
		return 0;

	return 1;
}

/// Executes 'func' for each party member on the same map and in range (0:whole map)
int bg_team_foreachsamemap(int (*func)(struct block_list*,va_list),struct map_session_data *sd,int range,...)
{
	struct battleground_data *bg;
	int i;
	int x0,y0,x1,y1;
	struct block_list *list[MAX_BG_MEMBERS];
	int blockcount=0;
	int total = 0; //Return value.

	nullpo_ret(sd);

	if((bg = bg_team_search(sd->bg_id)) == NULL)
		return 0;

	x0 = sd->bl.x-range;
	y0 = sd->bl.y-range;
	x1 = sd->bl.x+range;
	y1 = sd->bl.y+range;

	for(i = 0; i < MAX_BG_MEMBERS; i++) {
		struct map_session_data *pl_sd = bg->members[i].sd;

		if(!pl_sd)
			continue;

		if(pl_sd->bl.m!=sd->bl.m || !pl_sd->bl.prev)
			continue;

		if(range &&
			(pl_sd->bl.x<x0 || pl_sd->bl.y<y0 ||
			 pl_sd->bl.x>x1 || pl_sd->bl.y>y1 ) )
			continue;

		list[blockcount++]=&pl_sd->bl;
	}

	map_freeblock_lock();

	for(i = 0; i < blockcount; i++) {
		va_list ap;
		va_start(ap, range);
		total += func(list[i], ap);
		va_end(ap);
	}

	map_freeblock_unlock();

	return total;
}

int bg_send_dot_remove(struct map_session_data *sd) {
	struct battleground_data *bgd;
	int m;

	if (sd && sd->bg_id && (bgd = bg_team_search(sd->bg_id)) != NULL)
	{
		clif_bg_xy_remove(sd);
		if (bgd->reveal_pos && (m = map_mapindex2mapid(bgd->mapindex)) == sd->bl.m)
			map_foreachinmap(bg_reveal_pos, m, BL_PC, sd, 2, 0xFFFFFF);
	}
		clif_bg_xy_remove(sd);
	return 0;
}

int bg_team_join(int bg_id, struct map_session_data *sd)
{ // Player joins team
	int i;
	struct battleground_data *bgd = bg_team_search(bg_id);
	struct map_session_data *pl_sd;

	if( bgd == NULL || sd == NULL || sd->bg_id )
		return 0;

	ARR_FIND(0, MAX_BG_MEMBERS, i, bgd->members[i].sd == NULL);
	if( i == MAX_BG_MEMBERS )
		return 0; // No free slots

	pc_update_last_action(sd,0,IDLE_WALK); // Start count from here...
 	sd->bg_id = bg_id;
	sd->bg_kills = 0;
	sd->state.bg_afk = 0;
	sd->bgentry.mapid = 0;
	sd->bgentry.x = 0;
	sd->bgentry.y = 0;
	bgd->members[i].sd = sd;
	bgd->members[i].x = sd->bl.x;
	bgd->members[i].y = sd->bl.y;
	bgd->count++;

	guild_send_dot_remove(sd);
	if (bgd->creation_tick == 0) bgd->creation_tick = last_tick; // Creation Tick = First member joined.

	if( bgd->leader_char_id == 0 ) { // First Join = Team Leader
		bgd->leader_char_id = sd->status.char_id;
		sd->bmaster_flag = bgd;
	}
	
	guild_send_dot_remove(sd);

	if( battle_config.bg_eAmod_mode )
	{
		clif_bg_belonginfo(sd);
		clif_name_area(&sd->bl);
		skill_blockpc_clear(sd);
 	}

	if (battle_config.bg_team_color && bgd->g->ccolor) {
		sd->bg_color = sd->status.clothes_color+1;
		pc_changelook(sd,LOOK_CLOTHES_COLOR,bgd->g->ccolor);
	}

	if (battle_config.bg_join_location && !map_getmapflag(sd->bl.m, MF_NOWARP)) {
		sd->bgentry.mapid = sd->mapindex;
		sd->bgentry.x = sd->bl.x;
		sd->bgentry.y = sd->bl.y;
	}


	for (i = 0; i < MAX_BG_MEMBERS; i++)
	{
		if ((pl_sd = bgd->members[i].sd) == NULL)
			continue;

		if (battle_config.bg_eAmod_mode)
		{ // Simulate Guild Information
			clif_guild_basicinfo(pl_sd);
			clif_bg_emblem(pl_sd, bgd->g);
			clif_bg_memberlist(pl_sd);
		}

		if ((pl_sd = bgd->members[i].sd) != NULL && pl_sd != sd)
 			clif_hpmeter_single(sd->fd, pl_sd->bl.id, pl_sd->battle_status.hp, pl_sd->battle_status.max_hp);
 	}

	if (battle_config.bg_eAmod_mode) clif_guild_emblem_area(&sd->bl);
	clif_bg_hp(sd);
	clif_bg_xy(sd);
	if (battle_config.bg_queue_interface)
		clif_bg_queue_entry_init(sd);
	return 1;
}

int bg_team_leave(struct map_session_data *sd, int flag)
{ // Single Player leaves team
	int i;
	struct battleground_data *bgd;
	struct map_session_data *pl_sd;
	struct guild *g;
	char output[128];

	if( sd == NULL || !sd->bg_id )
		return 0;
	
	bgd = bg_team_search(sd->bg_id);
	if (bgd && bgd->logout_event[0] && flag)
		npc_event(sd, bgd->logout_event, 0);

	// Packets
	if (battle_config.bg_eAmod_mode) clif_bg_leave_single(sd, sd->status.name, "Leaving Battle...");

	bg_send_dot_remove(sd);
	sd->bg_id = 0;
	sd->bg_kills = 0;
	sd->state.bg_afk = 0;
	sd->bmaster_flag = NULL;
	bg_member_removeskulls(sd);

	if (battle_config.bg_team_color && sd->bg_color)
		pc_changelook(sd,LOOK_CLOTHES_COLOR,sd->bg_color-1);
	
	// Remove Guild Skill Buffs
	status_change_end(&sd->bl, SC_GUILDAURA, INVALID_TIMER);
	status_change_end(&sd->bl, SC_BATTLEORDERS, INVALID_TIMER);
	status_change_end(&sd->bl, SC_REGENERATION, INVALID_TIMER);
	clan_member_leave( sd, sd->status.clan_id, sd->status.account_id, sd->status.char_id );
	status_change_end(&sd->bl, SC_SWORDCLAN, INVALID_TIMER);
	status_change_end(&sd->bl, SC_ARCWANDCLAN, INVALID_TIMER);
	
	if (battle_config.bg_eAmod_mode)
	{ // Refresh Guild Information
		if (sd->status.guild_id && (g = guild_search(sd->status.guild_id)) != NULL)
		{
			clif_guild_belonginfo(sd,g);
			clif_guild_basicinfo(sd);
			clif_guild_allianceinfo(sd);
			clif_guild_memberlist(sd);
			clif_guild_skillinfo(sd);
			clif_guild_emblem(sd, g);
		}
		clif_name_area(&sd->bl);
		clif_guild_emblem_area(&sd->bl);
	}

	if (!bgd) return 0;

	ARR_FIND(0, MAX_BG_MEMBERS, i, bgd->members[i].sd == sd);
	if( i < MAX_BG_MEMBERS ) // Removes member from BG
		memset(&bgd->members[i], 0, sizeof(bgd->members[0]));
		if (bgd->leader_char_id == sd->status.char_id)
			bgd->leader_char_id = 0;
	for (i = 0; i < MAX_BG_MEMBERS; i++)
	{ // Update other BG members
		if ((pl_sd = bgd->members[i].sd) == NULL)
			continue;
		if (!bgd->leader_char_id)
		{ // Set new Leader first on the list
			bgd->leader_char_id = pl_sd->status.char_id;
			pl_sd->bmaster_flag = bgd;
		}

		if (battle_config.bg_eAmod_mode)
		{
			switch (flag)
			{
			case 3: clif_bg_expulsion_single(pl_sd, sd->status.name, "Kicked by AFK Status..."); break;
			case 2: clif_bg_expulsion_single(pl_sd, sd->status.name, "Kicked by AFK Report..."); break;
			case 1: clif_bg_expulsion_single(pl_sd, sd->status.name, "User has quit the game..."); break;
			case 0: clif_bg_leave_single(pl_sd, sd->status.name, "Leaving Battle..."); break;
			}

			clif_guild_basicinfo(pl_sd);
			clif_bg_emblem(pl_sd, bgd->g);
			clif_bg_memberlist(pl_sd);
		}
 	}

	bgd->count--;

	if( flag )
		sprintf(output, "Server : %s has quit the game...", sd->status.name);
	else
		sprintf(output, "Server : %s is leaving the battlefield...", sd->status.name);

	clif_bg_message(bgd, 0, "Server", output, strlen(output) + 1);

	return bgd->count;
}

int bg_member_respawn(struct map_session_data *sd)
{ // Respawn after killed
	struct battleground_data *bg;

	if( sd == NULL || !pc_isdead(sd) || !sd->bg_id || (bg = bg_team_search(sd->bg_id)) == NULL )
		return 0;

	if( bg->mapindex == 0 )
		return 0; // Respawn not handled by Core

	pc_setpos(sd, bg->mapindex, bg->x, bg->y, CLR_OUTSIGHT);
	status_revive(&sd->bl, 1, 100);

	return 1; // Warped
}

int bg_create(unsigned short mapindex, short rx, short ry, int guild_index, const char *ev, const char *dev)
{
	struct battleground_data *bg;
	int i;
	if (++bg_team_counter <= 0) bg_team_counter = 1;

	CREATE(bg, struct battleground_data, 1);
	bg->bg_id = bg_team_counter;
	bg->creation_tick = 0;
	bg->count = 0;
	bg->g = &bg_guild[guild_index];
	bg->mapindex = mapindex;
	bg->x = rx;
	bg->y = ry;
	bg->color = bg_colors[guild_index];
	bg->pf_id = 0;
	safestrncpy(bg->logout_event, ev, sizeof(bg->logout_event));
	safestrncpy(bg->die_event, dev, sizeof(bg->die_event));
	for( i = 0; i < MAX_GUILDSKILL-1; i++ )
		bg->skill_block_timer[i] = INVALID_TIMER;

	memset(&bg->members, 0, sizeof(bg->members));
	idb_put(bg_team_db, bg_team_counter, bg);

	return bg->bg_id;
}

struct guild* bg_guild_get(int bg_id)
{ // Return Fake Guild for BG Members
	struct battleground_data *bg = bg_team_search(bg_id);
	if( bg == NULL ) return NULL;
	return bg->g;
}

int bg_team_get_id(struct block_list *bl)
{
	if (bl)
	switch( bl->type ) {
		case BL_PC:
			return ((TBL_PC*)bl)->bg_id;
		case BL_PET:
			if( ((TBL_PET*)bl)->master )
				return ((TBL_PET*)bl)->master->bg_id;
			break;
		case BL_MOB: {
			struct map_session_data *msd;
			struct mob_data *md = (TBL_MOB*)bl;

			if( md->special_state.ai && (msd = map_id2sd(md->master_id)) != NULL )
				return msd->bg_id;

			return md->bg_id;
		}
		case BL_HOM:
			if( ((TBL_HOM*)bl)->master )
				return ((TBL_HOM*)bl)->master->bg_id;
			break;
		case BL_MER:
			if( ((TBL_MER*)bl)->master )
				return ((TBL_MER*)bl)->master->bg_id;
			break;
		case BL_ELEM:
			if( ((TBL_ELEM*)bl)->master )
				return ((TBL_ELEM*)bl)->master->bg_id;
			break;
		case BL_NPC:
			return ((TBL_NPC*)bl)->u.scr.bg_id;
			break;
		case BL_SKILL:
			return ((TBL_SKILL*)bl)->group->bg_id;
	}

	return 0;
}

int bg_send_message(struct map_session_data *sd, const char *mes, int len)
{
	struct battleground_data *bg;

	nullpo_ret(sd);

	if( sd->bg_id == 0 || (bg = bg_team_search(sd->bg_id)) == NULL )
		return 0;
	
	clif_bg_message(bg, sd->bl.id, sd->status.name, mes, len);

	return 0;
}

/**
 * @see DBApply
 */
int bg_send_xy_timer_sub(DBKey key, DBData *data, va_list ap)
{
	struct battleground_data *bgd = (struct battleground_data *)db_data2ptr(data);
	struct map_session_data *sd;
	char output[128];
	int i, m, idle_announce = battle_config.bg_idle_announce,
		idle_autokick = battle_config.bg_idle_autokick;

	nullpo_ret(bgd);
	m = map_mapindex2mapid(bgd->mapindex);
	bgd->reveal_flag = !bgd->reveal_flag; // Switch

	for (i = 0; i < MAX_BG_MEMBERS; i++) {
		if ((sd = bgd->members[i].sd) == NULL)
			continue;

		if (idle_autokick && DIFF_TICK(last_tick, sd->idletime) >= idle_autokick
			&& bgd->g && map_getmapflag(sd->bl.m, MF_BATTLEGROUND))
		{
			sprintf(output, "[Battlegrounds] %s has been kicked for being AFK.", sd->status.name);
			clif_broadcast2(&sd->bl, output, (int)strlen(output) + 1, bgd->color, 0x190, 20, 0, 0, BG);

			bg_team_leave(sd, 3);

			clif_displaymessage(sd->fd, "You have been kicked from Battleground because of your AFK status.");
			pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, CLR_TELEPORT);
			continue;

		}
		else if (sd->bl.x != bgd->members[i].x || sd->bl.y != bgd->members[i].y) { // xy update
			bgd->members[i].x = sd->bl.x;
			bgd->members[i].y = sd->bl.y;
			clif_bg_xy(sd);
		}
		if (bgd->reveal_pos && bgd->reveal_flag && sd->bl.m == m)
			map_foreachinmap(bg_reveal_pos, m, BL_PC, sd, 1, bgd->color);

		// Message for AFK Idling
		if (idle_announce && DIFF_TICK(last_tick, sd->idletime) >= idle_announce && !sd->state.bg_afk && bgd->g)
		{ // Set AFK status and announce to the team.
			sd->state.bg_afk = 1;
			sprintf(output, "%s : %s seems to be away. AFK Warning - Can be kicked out with @reportafk.", bgd->g->name, sd->status.name);
			clif_bg_message(bgd, bgd->bg_id, bgd->g->name, output, strlen(output) + 1);
		}
	}

	return 0;
}

TIMER_FUNC(bg_send_xy_timer){
	bg_team_db->foreach(bg_team_db, bg_send_xy_timer_sub, tick);

	return 0;
}

void bg_guild_build_data(void)
{
	int i, j, k, skill;
	memset(&bg_guild, 0, sizeof(bg_guild));
	for( i = 1; i <= 13; i++ )
	{ // Emblem Data - Guild ID's
		FILE* fp = NULL;
		char path[256];

		j = i - 1;
		bg_guild[j].emblem_id = 1; // Emblem Index
		bg_guild[j].guild_id = SHRT_MAX - j;
		bg_guild[j].guild_lv = 1;
		bg_guild[j].max_member = MAX_BG_MEMBERS;

		// Skills
		if( j < 3 )
		{ // Clan Skills
			for( k = 0; k < MAX_GUILDSKILL-1; k++ )
			{
				skill = k + GD_SKILLBASE;
				bg_guild[j].skill[k].id = skill;
				switch( skill )
				{
				case GD_GLORYGUILD:
					bg_guild[j].skill[k].lv = 0;
					break;
				case GD_APPROVAL:
				case GD_KAFRACONTRACT:
				case GD_GUARDRESEARCH:
				case GD_BATTLEORDER:
				case GD_RESTORE:
				case GD_EMERGENCYCALL:
				case GD_DEVELOPMENT:
					bg_guild[j].skill[k].lv = 1;
					break;
				case GD_GUARDUP:
				case GD_REGENERATION:
					bg_guild[j].skill[k].lv = 3;
					break;
				case GD_LEADERSHIP:
				case GD_GLORYWOUNDS:
				case GD_SOULCOLD:
				case GD_HAWKEYES:
					bg_guild[j].skill[k].lv = 5;
					break;
				case GD_EXTENSION:
					bg_guild[j].skill[k].lv = 10;
					break;
				}
			}
		}
		else
		{ // Other Data
			snprintf(bg_guild[j].name, NAME_LENGTH, "Team %d", i - 3); // Team 1, Team 2 ... Team 10
			strncpy(bg_guild[j].master, bg_guild[j].name, NAME_LENGTH);
			snprintf(bg_guild[j].position[0].name, NAME_LENGTH, "%s Leader", bg_guild[j].name);
			strncpy(bg_guild[j].position[1].name, bg_guild[j].name, NAME_LENGTH);
		}

		sprintf(path, "%s/emblems/bg_%d.ebm", db_path, i);
		if( (fp = fopen(path, "rb")) != NULL )
		{
			fseek(fp, 0, SEEK_END);
			bg_guild[j].emblem_len = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			if (fread(&bg_guild[j].emblem_data, 1, bg_guild[j].emblem_len, fp) == bg_guild[j].emblem_len) {
				fclose(fp);
				ShowStatus("Done reading '%s' emblem data file.\n", path);
			} else
				ShowStatus("Failed to read '%s'.\n", path); // Never happen
		}
	}

	// Guild Data - Guillaume
	strncpy(bg_guild[0].name, "Blue Team", NAME_LENGTH);
	strncpy(bg_guild[0].master, "General Guillaume", NAME_LENGTH);
	strncpy(bg_guild[0].position[0].name, "Blue Team Leader", NAME_LENGTH);
	strncpy(bg_guild[0].position[1].name, "Blue Team", NAME_LENGTH);
	bg_guild[0].ccolor = battle_config.bg_team_ccolor_blue;

	// Guild Data - Croix
	strncpy(bg_guild[1].name, "Red Team", NAME_LENGTH);
	strncpy(bg_guild[1].master, "Prince Croix", NAME_LENGTH);
	strncpy(bg_guild[1].position[0].name, "Red Team Leader", NAME_LENGTH);
	strncpy(bg_guild[1].position[1].name, "Red Team", NAME_LENGTH);
	bg_guild[1].ccolor = battle_config.bg_team_ccolor_red;

	// Guild Data - Traitors
	strncpy(bg_guild[2].name, "Green Team", NAME_LENGTH);
	strncpy(bg_guild[2].master, "Mercenary", NAME_LENGTH);
	strncpy(bg_guild[2].position[0].name, "Green Team Leader", NAME_LENGTH);
	strncpy(bg_guild[2].position[1].name, "Green Team", NAME_LENGTH);
	bg_guild[2].ccolor = battle_config.bg_team_ccolor_green;
}

void bg_team_getitem(int bg_id, int nameid, int amount)
{
	struct battleground_data *bg;
	struct map_session_data *sd;
	struct item_data *id;
	struct item it;
	int get_amount, j, flag;

	if (amount < 1 || (bg = bg_team_search(bg_id)) == NULL || (id = itemdb_exists(nameid)) == NULL)
		return;
	//if (nameid != 7828 && nameid != 7829 && nameid != 7773) // Why limit it in only Badges? Puto el que lo lea [Easycore]
	//	return;
	if( battle_config.bg_reward_rates != 100 )
		amount = amount * battle_config.bg_reward_rates / 100;

	memset(&it, 0, sizeof(it));
	it.nameid = nameid;
	it.identify = 1;

	for (j = 0; j < MAX_BG_MEMBERS; j++)
	{
		if ((sd = bg->members[j].sd) == NULL)
			continue;
		
		get_amount = amount;

		if ((flag = pc_additem(sd, &it, get_amount, LOG_TYPE_SCRIPT)))
			clif_additem(sd, 0, 0, flag);
	}
}
void bg_team_get_kafrapoints(int bg_id, int amount)
{
	struct battleground_data *bgd;
	struct map_session_data *sd;
	int i, get_amount;

	if ((bgd = bg_team_search(bg_id)) == NULL)
		return;
	if( battle_config.bg_reward_rates != 100 )
		amount = amount * battle_config.bg_reward_rates / 100;

	for (i = 0; i < MAX_BG_MEMBERS; i++)
	{
		if ((sd = bgd->members[i].sd) == NULL)
			continue;

		get_amount = amount;
		pc_getcash(sd, 0, get_amount, LOG_TYPE_SCRIPT);
	}
}

void bg_team_getitem(int bg_id, int winlost)
{
	struct battleground_data *bgd;
	struct map_session_data *sd;
	struct item it;
	int amount, battle_amount, i, j, flag, value;
	int nameid[3] = {7829, 7828, 7773};
	
	if((bgd = bg_team_search(bg_id)) == NULL)
		return;
	
	//Medallas por defecto
	amount = battle_config.bg_badges;
	battle_amount = battle_config.bg_battle_badges;
	
	//Evento HH
	if(battle_config.bg_event_extra_badges > 0)
	{
		amount += battle_config.bg_event_extra_badges;
		battle_amount += battle_config.bg_event_extra_badges;
	}
	
	//Un poco más de medallas al ganador
	if (winlost == 1)
	{
		amount += battle_config.bg_win_badges;
		battle_amount += battle_config.bg_win_badges;		
	}
	
	if (amount < 1 || battle_amount < 1)
	{
		ShowError("[Oboro]: bg_team_getitem, Negative or 0 value");
		return;
	}
	
	for( j = 0; j < MAX_BG_MEMBERS; j++ )
	{
		if( (sd = bgd->members[j].sd) == NULL )
			continue;
	
		for (i = 0; i < 3; i++)
		{
			memset(&it, 0, sizeof(it));
			it.nameid = nameid[i];
			it.identify = 1;
			if( (flag = pc_additem(sd,&it,(i != 2?amount:battle_amount),LOG_TYPE_SCRIPT)) )
				clif_additem(sd,0,0,flag);
		}
		
		value = pc_readaccountreg(sd,add_str("#BGPOINTS"));
		pc_setaccountreg(sd,add_str("#BGPOINTS"), (value + 1));
	
		//KafraPoints al ganador
		if( battle_config.bg_kafrapoints > 0 )
			pc_getcash(sd, 0, battle_config.bg_kafrapoints, LOG_TYPE_NPC);
	}
	return;
}

int battleground_countlogin(struct map_session_data *sd, bool check_bat_room)
{
	int c = 0, m = map_mapname2mapid("bat_room");
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	nullpo_ret(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if( !(pl_sd->qd || map_getmapflag(pl_sd->bl.m, MF_BATTLEGROUND) || (check_bat_room && pl_sd->bl.m == m)) )
			continue;
		/*if( session[sd->fd]->gepard_info.unique_id == session[pl_sd->fd]->gepard_info.unique_id )
			c++;*/
	}
	mapit_free(iter);
	return c;
}

// ====================================================================
// Battleground Queue System
// ====================================================================

struct queue_data* bg_queue_search(int q_id){ // Search a Queue using q_id
	if (!q_id) return NULL;
	return (struct queue_data *)idb_get(bg_queue_db, q_id);
}

int bg_queue_create(const char* queue_name, const char* join_event, int min_level)
{
	struct queue_data *qd;
	if( ++queue_counter <= 0 ) queue_counter = 1;

	CREATE(qd, struct queue_data, 1);
	qd->q_id = queue_counter;
	safestrncpy(qd->queue_name, queue_name, sizeof(qd->queue_name));
	safestrncpy(qd->join_event, join_event, sizeof(qd->join_event));
	qd->first = qd->last = NULL; // First and Last Queue Members
	qd->users = 0;
	qd->min_level = min_level;

	idb_put(bg_queue_db, queue_counter, qd);

	return qd->q_id;
}

void bg_queue_members_clean(struct queue_data *qd)
{
	struct queue_member *head, *next;
	nullpo_retv(qd);

	head = qd->first;
	while( head != NULL )
	{
		if( head->sd ) head->sd->qd = NULL;
		next = head->next;
		aFree(head);
		head = next;
	}

	qd->first = qd->last = NULL;
	qd->users = 0;
}

int bg_queue_member_add(struct queue_data *qd, struct map_session_data *sd)
{
	struct queue_member *qm;
	nullpo_retr(0, qd);
	nullpo_retr(0, sd);

	CREATE(qm, struct queue_member, 1);
	qd->users++;
	qm->sd = sd;
	qm->position = qd->users;
	qm->next = NULL;
	sd->qd = qd; // Attach user to the Queue too

	if (qd->last == NULL)
		qd->first = qd->last = qm; // Attach to first position
	else
	{ // Attach at the end of the queue
		qd->last->next = qm;
		qd->last = qm;
	}

	return qm->position;
}

struct queue_member* bg_queue_member_get(struct queue_data *qd, int position)
{
	struct queue_member *head;
	if( !qd ) return NULL;

	head = qd->first;
	while( head != NULL )
	{
		if( head->sd && head->position == position )
			return head;

		head = head->next;
	}

	return NULL;
}

int bg_queue_member_remove(struct queue_data *qd, int id)
{
	struct queue_member *head, *previous;
	int i;
	nullpo_retr(0,qd);

	head = qd->first;
	previous = NULL;

	while( head != NULL )
	{
		if( head->sd && head->sd->bl.id == id )
		{
			struct queue_member *next;

			next = head->next;
			i = head->position;
			head->sd->qd = NULL;
			qd->users--;

			// De-attach target from the main queue
			if( previous )
				previous->next = head->next;
			else
				qd->first = head->next; // Deleted is on first position

			if( head->next == NULL ) qd->last = previous; // Deleted is on last position

			while( next != NULL )
			{ // Reduces positions of the next of the queue in -1
				next->position--;
				next = next->next;
			}

			aFree(head);
			return i;
		}

		previous = head;
		head = head->next;
	}

	return 0;
}

int bg_queue_member_search(struct queue_data *qd, int id)
{
	struct queue_member *head;
	nullpo_retr(0,qd);

	head = qd->first;
	while( head != NULL )
	{
		if( head->sd && head->sd->bl.id == id )
			return head->position;

		head = head->next;
	}

	return 0; // Not Found
}

int bg_queue_destroy(int q_id)
{
	struct queue_data *qd;

	if( (qd = bg_queue_search(q_id)) == NULL )
		return 0;

	bg_queue_members_clean(qd);
	idb_remove(bg_queue_db, q_id);
	return 1;
}

int bg_queue_join(struct map_session_data *sd, int q_id)
{
	char output[128];
	struct queue_data *qd;
	int i;

	if (battle_config.bg_queue_onlytowns && !map_getmapflag(sd->bl.m, MF_TOWN))
	{
		clif_displaymessage(sd->fd, "You only can join BG queues from Towns or BG Waiting Room.");
		return 0;
	}

	if (sd->bg_id)
	{
		clif_displaymessage(sd->fd, "You cannot join queues when already playing Battlegrounds.");
		return 0;
	}

	if (sd->sc.data[SC_JAILED])
	{
		clif_displaymessage(sd->fd, "You cannot join queues when jailed.");
		return 0;
	}

	if ((qd = sd->qd) != NULL)
	{ // You cannot join a Queue if you are already on one.
		i = bg_queue_member_search(qd, sd->bl.id);
		sprintf(output, "You are already on %s queue at position %d.", qd->queue_name, i);
		clif_displaymessage(sd->fd, output);
		return 0;
	}

	if ((qd = bg_queue_search(q_id)) == NULL)
		return 0; // Current Queue don't exists

	if (qd->min_level && sd->status.base_level < qd->min_level)
	{
		sprintf(output, "You cannot join %s queue. Required min level is %d.", qd->queue_name, qd->min_level);
		clif_displaymessage(sd->fd, output);
		return 0;
	}
	if( bg_countlogin(sd,false) > 0 && battle_config.bg_double_login)
	{
		sprintf(output,"You cannot join %s queue. Double Login detected.", qd->queue_name);
		clif_displaymessage(sd->fd,output);
		return 0;
	}

	i = bg_queue_member_add(qd, sd);
	sprintf(output, "You have joined %s queue at position %d.", qd->queue_name, i);
	clif_displaymessage(sd->fd, output);

	if (battle_config.bg_queue_interface) {
		clif_bg_queue_apply_result(BG_APPLY_ACCEPT, qd->queue_name, sd);
		clif_bg_queue_apply_notify(qd->queue_name, sd);
	}

	if (qd->join_event[0]) npc_event_do(qd->join_event);
	return 1;
}

int bg_queue_leave(struct map_session_data *sd, int q_id)
{
	char output[128];
	struct queue_data *qd;

	if( (qd = bg_queue_search(q_id)) == NULL )
		return 0;

	if( !bg_queue_member_remove(qd,sd->bl.id) )
	{
		sprintf(output,"You are not at %s queue.", qd->queue_name);
		clif_displaymessage(sd->fd, output);
		return 0;
	}
	sprintf(output,"You have been removed from the %s queue.", qd->queue_name);
	clif_displaymessage(sd->fd, output);
	//if (battle_config.bg_queue_interface)
	//	clif_bg_queue_entry_init(sd);

	return 1;
}

void bg_queue_leaveall(struct map_session_data *sd)
{
	struct queue_data *qd;
	if( sd && (qd = sd->qd) != NULL )
	{
		char output[128];
		bg_queue_member_remove(qd,sd->bl.id);
		sprintf(output,"You have been removed from %s BG Queue.",qd->queue_name);
		clif_displaymessage(sd->fd, output);
		//if (battle_config.bg_queue_interface)
		//	clif_bg_queue_entry_init(sd);
	}
}

// ====================================================================

int bg_team_db_reset( DBKey key, DBData *data, va_list ap)
{
	struct battleground_data *bgd = (struct battleground_data *)data;
	bg_team_clean(bgd->bg_id,false);
	return 0;
}

int queue_db_final( DBKey key, DBData *data, va_list ap)
{
	struct queue_data *qd = (struct queue_data *)data;
	bg_queue_members_clean(qd); // Unlink all queue members
	return 0;
}

void do_init_battleground(void)
{
	bg_team_db = idb_alloc(DB_OPT_RELEASE_DATA);
	bg_queue_db = idb_alloc(DB_OPT_RELEASE_DATA);
	add_timer_func_list(bg_send_xy_timer, "bg_send_xy_timer");
	add_timer_func_list(bg_block_skill_end,"bg_block_skill_end");
	add_timer_interval(gettick() + battle_config.bg_update_interval, bg_send_xy_timer, 0, 0, battle_config.bg_update_interval);
	bg_guild_build_data();
}

void do_final_battleground(void)
{
	bg_team_db->destroy(bg_team_db, NULL);
	bg_queue_db->destroy(bg_queue_db, queue_db_final);
}
