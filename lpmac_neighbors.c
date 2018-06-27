/**@file lpmac_neighbors.c
 *
 * @date May 4, 2017
 * @author Craig Hesling <craig@hesling.com>
 */

#include <stdbool.h>
#include <ti/sysbios/gates/GateMutexPri.h>

#include "lpmac.h"
#include "lpmac_neighbors_errors.h"
#include "lpmac_neighbors.h"

#define NEIGHBOR_ID_BLANK ((node_id_t)0x00000000)

typedef struct table_entry {
    node_id_t id;
} table_entry_t;
static table_entry_t table[NEIGHBORS_MAX];

static neighbor_event_fn_t neighbor_update_fn;
static GateMutexPri_Struct tableMutexStruct;

void lpmac_neighbors_init(neighbor_event_fn_t neighbor_updates_callback) {
	neighbor_update_fn = neighbor_updates_callback;
	GateMutexPri_construct(&tableMutexStruct, NULL);
}

static table_entry_t *table_find(node_id_t id) {
    size_t index;
    for (index = 0; index < NEIGHBORS_MAX; index++) {
        if(table[index].id == id) {
            return &table[index];
        }
    }
    return NULL;
}

static bool table_add(table_entry_t *entry) {
    table_entry_t *slot = table_find(NEIGHBOR_ID_BLANK);
    if (slot == NULL) {
        return false;
    }
    *slot = *entry;
    return true;
}

static bool table_rem(node_id_t id) {
    table_entry_t *entry = table_find(id);
    if (entry == NULL) {
        return false;
    }
    entry->id = NEIGHBOR_ID_BLANK;
    return true;
}

static void table_clear() {
    size_t index;
    for (index = 0; index < NEIGHBORS_MAX; index++) {
        table[index].id = NEIGHBOR_ID_BLANK;
    }
}



void lpmac_neighbors_clear() {
	UInt key =  GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
	table_clear();
	GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
}

void lpmac_neighbors_add(node_id_t node_id, link_quality_t link_quality) {
	UInt key = GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
	if(table_find(node_id) == NULL) {
	    table_entry_t entry = { .id = node_id };
	    if(!table_add(&entry)) {
	        dprintf("Neighbor Table Full\n");
	    }
	    neighbor_update_fn(NEIGHBOR_EVENT_ADD, node_id, link_quality);
	}
	GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
}

void lpmac_neighbors_rem(node_id_t node_id) {
	UInt key = GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
	if(table_rem(node_id)) {
	    neighbor_update_fn(NEIGHBOR_EVENT_REM, node_id, 0);
	}
	GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
	return;
}

void lpmac_neighbors_heard(node_id_t node_id, link_quality_t link_quality) {
    dprintf("Overheard pkt from "PRINTF_FMT_NODE_ID"\n", node_id);
    lpmac_neighbors_add(node_id, link_quality);
}

void lpmac_neighbors_failed(node_id_t node_id) {
    lpmac_neighbors_rem(node_id);
    return;
}

void lpmac_neighbors_show() {
    size_t index;
    size_t count = 0;
    UInt key = GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
    for (index = 0; index < NEIGHBORS_MAX; index++)
    {
        if(table[index].id != NEIGHBOR_ID_BLANK) {
            count++;
            dprintf("Neighbor %lu: 0x"PRINTF_FMT_NODE_ID"\n", count, table[index].id);
        }
    }
    dprintf("Neighbor List Complete - Total %lu\n", count);
    GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
}

void lpmac_neighbors_docallbacks() {
	return;
}
