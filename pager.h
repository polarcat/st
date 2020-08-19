#include <stdlib.h>
#include <stdint.h>

#define PAGER_MOD ShiftMask
#define PAGER_KSYM XK_Page_Up
#define PAGER "less +G -e -R"
#define HISTORY_LINES 1000
#define HISTORY_PATH "/tmp/st"

uint8_t pager_show(uint32_t win, uint32_t button);
void pager_reset(void);
void pager_write(int y);
void pager_init(const char *);
