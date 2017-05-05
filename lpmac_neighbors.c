/**@file lpmac_neighbors.c
 *
 * @date May 4, 2017
 * @author Craig Hesling <craig@hesling.com>
 */

#include <ti/sysbios/gates/GateMutexPri.h>

#include "lpmac.h"
#include "lpmac_neighbors.h"

static neighbor_event_fn_t neighbor_update_fn;

static GateMutexPri_Struct tableMutexStruct;

void lpmac_neighbors_init(neighbor_event_fn_t neighbor_updates_callback) {
	neighbor_update_fn = neighbor_updates_callback;
	GateMutexPri_construct(&tableMutexStruct, NULL);
}

void lpmac_neighbors_clear() {
	UInt key = GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
	GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
}

void lpmac_neighbors_add(node_id_t node_id, link_quality_t link_quality) {
	UInt key = GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
	neighbor_update_fn(NEIGHBOR_EVENT_ADD, node_id, link_quality);
	GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
}

void lpmac_neighbors_rem(node_id_t node_id) {
	UInt key = GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
	neighbor_update_fn(NEIGHBOR_EVENT_REM, node_id, 0);
	GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
	return;
}

void lpmac_neighbors_heard(node_id_t node_id, link_quality_t link_quality) {
	UInt key = GateMutexPri_enter(GateMutexPri_handle(&tableMutexStruct));
	neighbor_update_fn(NEIGHBOR_EVENT_ADD, node_id, link_quality);
	GateMutexPri_leave(GateMutexPri_handle(&tableMutexStruct), key);
	// do nothing right now
	return;
}

void lpmac_neighbors_docallbacks() {
	return;
}
