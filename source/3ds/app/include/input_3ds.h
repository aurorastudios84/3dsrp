#ifndef N3DS_RP_INPUT_3DS_H
#define N3DS_RP_INPUT_3DS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <chiaki/controller.h>

/* Platform-neutral key bits. The libctru adapter translates KEY_* values into
 * this mask; START and SELECT are reserved for local app actions. */
enum Input3dsKey {
    RP_INPUT_A          = 1u << 0,
    RP_INPUT_B          = 1u << 1,
    RP_INPUT_X          = 1u << 2,
    RP_INPUT_Y          = 1u << 3,
    RP_INPUT_DPAD_UP    = 1u << 4,
    RP_INPUT_DPAD_DOWN  = 1u << 5,
    RP_INPUT_DPAD_LEFT  = 1u << 6,
    RP_INPUT_DPAD_RIGHT = 1u << 7,
    RP_INPUT_L          = 1u << 8,
    RP_INPUT_R          = 1u << 9,
    RP_INPUT_ZL         = 1u << 10,
    RP_INPUT_ZR         = 1u << 11,
    RP_INPUT_START      = 1u << 12,
    RP_INPUT_SELECT     = 1u << 13
};

typedef struct Input3dsSnapshot {
    uint32_t held;
    bool motion; /* Live sample only; synthetic neutral input never forwards motion. */
    int16_t circle_x, circle_y;
    int16_t cstick_x, cstick_y;
    bool touch_down;
    uint16_t touch_x, touch_y;
} Input3dsSnapshot;

/* Captures a button or finger until lift; local mode switches consume the
 * initiating contact. The resistive touchscreen supplies one finger only. */
typedef struct Input3dsTouchCapture {
    bool was_down;
    uint32_t button;
    bool touchpad_mode, surface;
    uint8_t touch_id, touch_id_next;
    int8_t click_side; /* -1 left, +1 right, 0 ordinary gesture */
} Input3dsTouchCapture;

typedef struct Input3dsTouchZone {
    uint16_t x0, y0, x1, y1; /* half-open native bottom-screen pixels */
    uint32_t button;
    const char *label;
} Input3dsTouchZone;

#define INPUT_3DS_TOUCH_ZONE_COUNT 7u
#define INPUT_3DS_TOUCHPAD_EDGE_WIDTH 40u
#define INPUT_3DS_TOUCHPAD_HEIGHT 200u
#define INPUT_3DS_TOUCHPAD_BACK_X 240u
#define INPUT_3DS_AXIS_MAX 156
#define INPUT_3DS_AXIS_DEADZONE 12

/* Shared by mapping, the bottom-screen renderer, and host tests. */
extern const Input3dsTouchZone input_3ds_touch_zones[INPUT_3DS_TOUCH_ZONE_COUNT];

void input_3ds_reset_capture(Input3dsTouchCapture *capture);
void input_3ds_map(const Input3dsSnapshot *snapshot,
                   Input3dsTouchCapture *capture,
                   ChiakiControllerState *state);

#endif
