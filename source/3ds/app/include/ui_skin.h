#ifndef THREEDSRP_UI_SKIN_H
#define THREEDSRP_UI_SKIN_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { UI_SKIN_MENU,UI_SKIN_READY,UI_SKIN_HELP,UI_SKIN_DENSE,UI_SKIN_CONSOLES,UI_SKIN_PRIVACY } UiSkinKind;
/* Presentation-only mirror of console output. Never handles input or settings. */
void ui_skin_begin(UiSkinKind kind,const char *primary);
void ui_skin_at(unsigned row,unsigned column);
void ui_skin_write(const char *text);
void ui_skin_item(unsigned row,bool selected);
void ui_skin_end(uint16_t *bottom);
const uint16_t *ui_skin_brand(void); /* immutable row-major 400x240 RGB565 */
#endif
