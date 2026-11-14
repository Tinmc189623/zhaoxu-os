#ifndef ZHAOXU_ADE_PROTOCOL_H
#define ZHAOXU_ADE_PROTOCOL_H

#include <types.h>

#define ADE_PORT_NAME "ade"
#define ADE_TEXT_MAX 32

#define ADE_TEXT_CENTER 1u
#define ADE_TEXT_RIGHT  2u
#define ADE_TEXT_SCALE2 4u

struct ade_message {
    u32 type;
    i32 x, y, w, h;
    u32 color;
    u32 extra;
    char text[ADE_TEXT_MAX];
};

enum ade_message_type {
    ADE_OP_CREATE = 1,
    ADE_OP_CLEAR,
    ADE_OP_FILL,
    ADE_OP_ROUND,
    ADE_OP_TEXT,
    ADE_OP_COMMIT,
    ADE_OP_CURSOR,   /* cursor-zone hint for the window manager */

    ADE_EV_KEY = 0x100,
    ADE_EV_CLICK,
    ADE_EV_FOCUS,
    ADE_EV_CLOSE
};

#endif
