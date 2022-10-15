// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/showmsg.hpp"
#include "../common/timer.hpp"
#include "../common/nullpo.hpp"
#include "../common/db.hpp"
#include "../common/malloc.hpp"
#include "../common/utils.hpp"

#include "map.hpp"
#include "atcommand.hpp"
#include "battle.hpp"
#include "battleground.hpp"
#include "chat.hpp"
#include "channel.hpp"
#include "clif.hpp"
#include "chrif.hpp"
#include "duel.hpp"
#include "instance.hpp"
#include "intif.hpp"
#include "itemdb.hpp"
#include "log.hpp"
#include "pc.hpp"
#include "status.hpp"
#include "skill.hpp"
#include "mob.hpp"
#include "npc.hpp"
#include "pet.hpp"
#include "homunculus.hpp"
#include "mail.hpp"
#include "mercenary.hpp"
#include "party.hpp"
#include "guild.hpp"
#include "script.hpp"
#include "storage.hpp"
#include "trade.hpp"
#include "unit.hpp"
#include "mapreg.hpp"
#include "quest.hpp"
#include "pc.hpp"
#include "oboro.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


struct RESTOCK RESTOCK[MAX_RESTOCK_DB];

int RESTOCK_CONF( int poc, char item_name[200], int item_id, int item_quantity ) 
{
	if ( itemdb_exists(item_id) == NULL ) 
		return 0;

	strcpy(RESTOCK[poc].item_name, item_name);
	RESTOCK[poc].item_id = item_id;
	RESTOCK[poc].quantity = item_quantity;
	return 1;
}


void READ_RESTOCK(void) 
{
	const char* RestockDATA = "db/restock.txt";
	char line[1024], item_name[200];
	int item_id, item_quantity;
	int poc = 0;
		
	FILE* fp;

	fp = fopen(RestockDATA,"r");
	if (fp == NULL)
		ShowInfo("File not found: %s \n", RestockDATA);
	else
	{
		while(fgets(line, sizeof(line), fp)) 
		{
			if ( ( line[0] == '/' && line[1] == '/' ) || sscanf(line, "%23[^,],%d,%d", item_name, &item_id, &item_quantity) < 1 )
					continue;
			else
			{
				if (RESTOCK_CONF(poc, item_name, item_id, item_quantity) == 0 )
					ShowWarning("Linea: %d invalida en %s \n", poc, RestockDATA);
				poc++;
			}
		}
		fclose(fp);
		ShowInfo("Items de Restock recargados, correctamente. \n");
	}
}

int INIT_EVENT_RESTOCK( struct map_session_data *sd, int item_id, int amount ) 
{
	int poc, iBG = -1, i = -1, iUniversal = -1, quantity = 0, weight;
	struct item_data* id = NULL;

	if ( !sd || !item_id || !sd->state.restock || (id = itemdb_exists(item_id)) == NULL || amount > 1)
		return 0;
	
	ARR_FIND(0, MAX_RESTOCK_DB, poc, id->nameid == RESTOCK[poc].item_id);
	if (poc == MAX_RESTOCK_DB || pc_is90overweight(sd))
		return 0;

	// Find BG / Universal Items
	ARR_FIND(0, MAX_STORAGE, iBG, sd->storage.u.items_storage[iBG].nameid == id->nameid && sd->storage.u.items_storage[iBG].card[0] == CARD0_CREATE && MakeDWord(sd->storage.u.items_storage[iBG].card[2], sd->storage.u.items_storage[iBG].card[3]) == battle_config.bg_reserved_char_id);
	ARR_FIND(0, MAX_STORAGE, iUniversal, sd->storage.u.items_storage[iUniversal].nameid == id->nameid && sd->storage.u.items_storage[iUniversal].card[0] == 0 || (sd->storage.u.items_storage[iUniversal].nameid == id->nameid && sd->storage.u.items_storage[iUniversal].card[0] == CARD0_CREATE && MakeDWord(sd->storage.u.items_storage[iUniversal].card[2], sd->storage.u.items_storage[iUniversal].card[3]) == battle_config.universal_reserved_char_id));

	if (map_bg_items(sd->bl.m) && iBG != MAX_STORAGE)
		i = iBG;
	else if ((map_bg_items(sd->bl.m) && iBG == MAX_STORAGE && iUniversal != MAX_STORAGE) || iUniversal != MAX_STORAGE)
		i = iUniversal;
	else
		return 0; // No item found in storage

	quantity = min(RESTOCK[poc].quantity, sd->storage.u.items_storage[i].amount);
	do
	{
		weight = min(id->weight, 100) * quantity;
		if ((weight + sd->weight) * 10 >= (sd->max_weight * 9))
			quantity -= 1;
	} while ((weight + sd->weight) * 10 >= (sd->max_weight * 9));

	if (quantity <= 0)
		return 0; // player over90weight

	storage_storageget(sd, &sd->storage, i, quantity);
	return 0;
}

int status_change_start_sub(struct block_list* bl, va_list ap) {
       struct block_list* src = va_arg(ap, struct block_list*);
	enum sc_type type = (sc_type)va_arg(ap,int); // gcc: enum args get promoted to int
       int rate = va_arg(ap, int);
       int val1 = va_arg(ap, int);
       int val2 = va_arg(ap, int);
       int val3 = va_arg(ap, int);
       int val4 = va_arg(ap, int);
       int tick = va_arg(ap, int);
       int flag = va_arg(ap, int);

       if (src && bl && src->id != bl->id) {
               status_change_start(src, bl, type, rate, val1, val2, val3, val4, tick, flag);
       }
       return 0;
}

const char* GetBGName(int bgid)
{
	switch(bgid)
	{
		case 1:	return "Conquest";
		case 2:	return "Rush";
		case 3:	return "Flavious TD";
		case 4:	return "Team vs Team";
		case 5:	return "Flavius CTF";
		case 6:	return "Poring Ball";
		default:	return "None";
	}
}


void ReOrderBG()
{
	char BG_Var[12];
	int i, actual_array = 0;
	int tmparr[(MAX_BG_ARRAY - 1)];

	for (i = 1; i < MAX_BG_ARRAY; i++)
	{
		sprintf(BG_Var,"$NEXTBG_%d", i);
		if (mapreg_readreg(add_str(BG_Var)) > 0)
		{
			tmparr[actual_array] = mapreg_readreg(add_str(BG_Var));
			actual_array++;
			mapreg_setreg(add_str(BG_Var), 0);
		}
	}

	for (i = 1; i < MAX_BG_ARRAY; i++)
	{
		sprintf(BG_Var,"$NEXTBG_%d", i);
		if (tmparr[(i - 1)] && tmparr[(i - 1)] > 0)
			mapreg_setreg(add_str(BG_Var), tmparr[(i - 1)]);
	}
}

void ShowBGArray(struct map_session_data *sd)
{
	int i;
	char BG_Var[12], output[CHAT_SIZE_MAX];
	clif_displaymessage(sd->fd, "BG Array");
	clif_displaymessage(sd->fd,"-----------------------------------------------");

	for (i = 1; i <= MAX_BG_ARRAY; i++)
	{
		sprintf(BG_Var,"$NEXTBG_%d", i);
		sprintf(output, "BG %d: %s", i, GetBGName(mapreg_readreg(add_str(BG_Var))));
		clif_displaymessage(sd->fd, output);
	}

	clif_displaymessage(sd->fd,"-----------------------------------------------");
}

void do_init_oboro(void)
{
	READ_RESTOCK();	

	//Let's create the battleground instance!
	if (!mapreg_readreg(add_str("$CURRENTBG")))
		mapreg_setreg(add_str("$CURRENTBG"), 1);
	
	if (!mapreg_readreg(add_str("$CURRENTPOCBG")))
		mapreg_setreg(add_str("$CURRENTPOCBG"), 1);
	
	if (!mapreg_readreg(add_str("$NEXTBG_1")))
		mapreg_setreg(add_str("$NEXTBG_1"), 1);
	
	if (!mapreg_readreg(add_str("$NEXTBG_2")))
		mapreg_setreg(add_str("$NEXTBG_2"), 2);
	
	if (!mapreg_readreg(add_str("$NEXTBG_3")))
		mapreg_setreg(add_str("$NEXTBG_3"), 3);

	if (!mapreg_readreg(add_str("$NEXTBG_4")))
		mapreg_setreg(add_str("$NEXTBG_4"), 4);
	
	if (!mapreg_readreg(add_str("$NEXTBG_5")))
		mapreg_setreg(add_str("$NEXTBG_5"), 5);

	if (!mapreg_readreg(add_str("$NEXTBG_5")))
		mapreg_setreg(add_str("$NEXTBG_5"), 6);
	
	if (!mapreg_readreg(add_str("$MINBGLIMIT")))
		mapreg_setreg(add_str("$MINBGLIMIT"), 6);
	
}