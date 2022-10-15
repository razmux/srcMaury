#ifndef _OBORO_H_
#define _OBORO_H_

#define MAX_BG_ARRAY 6

/// Battleground 2.2.0
const char* GetBGName(int bgid);
void ReOrderBG();
void ShowBGArray(struct map_session_data *sd);

void do_init_oboro(void);


#endif /* _OBORO_H_ */