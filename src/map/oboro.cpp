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