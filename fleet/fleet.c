/*
 * frank-sdk — lookups over the run-time board table (boards/gen/frank_fleet.c).
 * From frank-test core/board_table.c, unchanged.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include "board_desc.h"

#include <string.h>

const frank_board_desc_t *frank_board_desc(frank_board_id_t id,
                                           frank_role_t role) {
    for (unsigned i = 0; i < frank_board_table_len; i++) {
        if (frank_board_table[i].id == id &&
            frank_board_table[i].role == role)
            return &frank_board_table[i];
    }
    return NULL;
}

unsigned frank_boards_for_mcu(frank_mcu_class_t mcu,
                              const frank_board_desc_t **out, unsigned max) {
    unsigned n = 0;
    for (unsigned i = 0; i < frank_board_table_len && n < max; i++) {
        const frank_board_desc_t *d = &frank_board_table[i];
        if (d->mcu == mcu || d->mcu == FRANK_MCU_ANY)
            out[n++] = d;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

const char *frank_board_name(frank_board_id_t id) {
    for (unsigned i = 0; i < frank_board_table_len; i++)
        if (frank_board_table[i].id == id) return frank_board_table[i].name;
    return "unknown";
}

const char *frank_board_slug(frank_board_id_t id) {
    for (unsigned i = 0; i < frank_board_table_len; i++)
        if (frank_board_table[i].id == id) return frank_board_table[i].slug;
    return "unknown";
}

frank_board_id_t frank_board_from_slug(const char *slug) {
    if (!slug) return FRANK_BOARD_UNKNOWN;
    for (unsigned i = 0; i < frank_board_table_len; i++)
        if (strcmp(frank_board_table[i].slug, slug) == 0)
            return frank_board_table[i].id;
    return FRANK_BOARD_UNKNOWN;
}

const char *frank_mcu_class_name(frank_mcu_class_t c) {
    switch (c) {
        case FRANK_MCU_RP2350A: return "RP2350A";
        case FRANK_MCU_RP2350B: return "RP2350B";
        default:                return "any";
    }
}

const char *frank_role_name(frank_role_t r) {
    switch (r) {
        case FRANK_ROLE_MASTER: return "master";
        case FRANK_ROLE_SLAVE:  return "slave";
        default:                return "single";
    }
}
