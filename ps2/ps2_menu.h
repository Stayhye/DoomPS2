#ifndef PS2_MENU_H
#define PS2_MENU_H

#include <tamtypes.h>

typedef struct {
    const char *label;
    const char **values;
    int count;
    int cur;
    void (*action)(void);
} ps2_setting_t;

int PS2_SelectMenu(const char *title, char **items, int count);
void PS2_SettingsMenu(const char *title, ps2_setting_t *s, int n);

#endif