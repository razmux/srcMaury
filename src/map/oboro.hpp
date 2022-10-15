#ifndef _OBORO_H_
#define _OBORO_H_

#define MAX_RESTOCK_DB 200
#define MAX_BG_ARRAY 6

// Oboro Restock Starts at -> pc_delitem(); line aprox 5216.
struct RESTOCK 
{
	char item_name[200];
	unsigned int item_id, quantity;
};

extern struct RESTOCK RESTOCK[MAX_RESTOCK_DB];

/// Battleground 2.2.0
const char* GetBGName(int bgid);
void ReOrderBG();
void ShowBGArray(struct map_session_data *sd);

int status_change_start_sub(struct block_list* bl, va_list ap);

void do_init_oboro(void);
int RESTOCK_CONF( int poc, char item_name[200], int item_id, int item_quantity );
void READ_RESTOCK(void);

#endif /* _OBORO_H_ */