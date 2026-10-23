#include "input_3ds.h"

const Input3dsTouchZone input_3ds_touch_zones[INPUT_3DS_TOUCH_ZONE_COUNT] = {
    /* Center lower pair split into local touchpad entry and remote click. */
    {   0,   0, 107,  80, CHIAKI_CONTROLLER_BUTTON_SHARE,   "SHARE"   },
    { 107,   0, 213,  80, CHIAKI_CONTROLLER_BUTTON_PS,      "PS"      },
    { 213,   0, 320,  80, CHIAKI_CONTROLLER_BUTTON_OPTIONS, "OPTIONS" },
    {   0,  80, 107, 240, CHIAKI_CONTROLLER_BUTTON_L3,      "L3"      },
    { 107, 160, 213, 240, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD,"CLICK"},
    { 213,  80, 320, 240, CHIAKI_CONTROLLER_BUTTON_R3,      "R3"      },
    { 107,  80, 213, 160, 0,                              "PAD"     }
};

void input_3ds_reset_capture(Input3dsTouchCapture *capture)
{
    capture->was_down = false;
    capture->button = 0;
    capture->touchpad_mode = false;
    capture->surface = false;
    capture->click_side = 0;
    capture->touch_id = capture->touch_id_next = 0;
}

static int16_t axis_map(int16_t raw, bool invert)
{
    int32_t value = raw;
    if(invert)
        value = -value;
    if(value > INPUT_3DS_AXIS_MAX)
        value = INPUT_3DS_AXIS_MAX;
    else if(value < -INPUT_3DS_AXIS_MAX)
        value = -INPUT_3DS_AXIS_MAX;

    int32_t magnitude = value < 0 ? -value : value;
    if(magnitude <= INPUT_3DS_AXIS_DEADZONE)
        return 0;

    const int32_t active_range = INPUT_3DS_AXIS_MAX - INPUT_3DS_AXIS_DEADZONE;
    const int32_t scaled = ((magnitude - INPUT_3DS_AXIS_DEADZONE) * 32767
                            + active_range / 2) / active_range;
    return (int16_t)(value < 0 ? -scaled : scaled);
}

static const Input3dsTouchZone *touch_zone(const Input3dsSnapshot *snapshot)
{
    for(size_t i = 0; i < INPUT_3DS_TOUCH_ZONE_COUNT; ++i) {
        const Input3dsTouchZone *zone = &input_3ds_touch_zones[i];
        if(snapshot->touch_x >= zone->x0 && snapshot->touch_x < zone->x1
                && snapshot->touch_y >= zone->y0 && snapshot->touch_y < zone->y1)
            return zone;
    }
    return NULL;
}

static uint32_t physical_buttons(uint32_t held)
{
    uint32_t buttons = 0;
    if(held & RP_INPUT_B) buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
    if(held & RP_INPUT_A) buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
    if(held & RP_INPUT_Y) buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
    if(held & RP_INPUT_X) buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
    if(held & RP_INPUT_DPAD_UP) buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
    if(held & RP_INPUT_DPAD_DOWN) buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
    if(held & RP_INPUT_DPAD_LEFT) buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
    if(held & RP_INPUT_DPAD_RIGHT) buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
    if(held & RP_INPUT_L) buttons |= CHIAKI_CONTROLLER_ANALOG_BUTTON_L2;
    if(held & RP_INPUT_R) buttons |= CHIAKI_CONTROLLER_ANALOG_BUTTON_R2;
    if(held & RP_INPUT_ZL) buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
    if(held & RP_INPUT_ZR) buttons |= CHIAKI_CONTROLLER_BUTTON_R1;
    return buttons;
}

void input_3ds_map(const Input3dsSnapshot *snapshot,
                   Input3dsTouchCapture *capture,
                   ChiakiControllerState *state)
{
    chiaki_controller_state_set_idle(state);

    state->buttons = physical_buttons(snapshot->held);
    if(snapshot->held & RP_INPUT_L)
        state->l2_state = 255;
    if(snapshot->held & RP_INPUT_R)
        state->r2_state = 255;

    state->left_x = axis_map(snapshot->circle_x, false);
    state->left_y = axis_map(snapshot->circle_y, true);
    state->right_x = axis_map(snapshot->cstick_x, false);
    state->right_y = axis_map(snapshot->cstick_y, true);

    if(snapshot->touch_down) {
        if(!capture->was_down) {
            if(capture->touchpad_mode) {
                if(snapshot->touch_x < 320 && snapshot->touch_y < 240) {
                    if(snapshot->touch_y < INPUT_3DS_TOUCHPAD_HEIGHT) {
                        capture->surface = true;
                        if(snapshot->touch_x < INPUT_3DS_TOUCHPAD_EDGE_WIDTH)
                            capture->click_side = -1;
                        else if(snapshot->touch_x >= 320u - INPUT_3DS_TOUCHPAD_EDGE_WIDTH)
                            capture->click_side = 1;
                        if(capture->click_side)
                            capture->button = CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
                        capture->touch_id = capture->touch_id_next;
                        capture->touch_id_next = (capture->touch_id_next + 1u) & 0x7fu;
                    } else if(snapshot->touch_x < INPUT_3DS_TOUCHPAD_BACK_X) {
                        capture->button = CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
                    } else {
                        capture->touchpad_mode = false;
                    }
                }
            } else {
                const Input3dsTouchZone *zone = touch_zone(snapshot);
                if(zone) {
                    if(zone->button) capture->button = zone->button;
                    else capture->touchpad_mode = true;
                }
            }
        }
        capture->was_down = true;
    } else {
        capture->was_down = false;
        capture->button = 0;
        capture->surface = false;
        capture->click_side = 0;
    }
    state->buttons |= capture->button;
    if(capture->surface) {
        /* Match upstream's PS5 touchpad range. Clamp captured drags at the
         * surface edge: crossing the strip never clicks or activates Back. */
        unsigned x = snapshot->touch_x < 320 ? snapshot->touch_x : 319;
        unsigned y = snapshot->touch_y < INPUT_3DS_TOUCHPAD_HEIGHT ?
            snapshot->touch_y : INPUT_3DS_TOUCHPAD_HEIGHT - 1;
        state->touches[0].id = (int8_t)capture->touch_id;
        state->touches[0].x = (uint16_t)((x * 1919u + 159u) / 319u);
        state->touches[0].y = (uint16_t)((y * 1079u + 99u) / 199u);
        /* A side click includes a stationary contact in that half of the pad.
         * Capture the side until lift, even when dragged over Back. */
        if(capture->click_side) {
            state->touches[0].x = capture->click_side < 0 ? 480 : 1440;
            state->touches[0].y = 540;
        }
    }
    state->touch_id_next = capture->touch_id_next;
}
