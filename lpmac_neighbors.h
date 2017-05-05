/**@file lpmac_neighbors.h
 *
 * @date May 4, 2017
 * @author Craig Hesling <craig@hesling.com>
 */

#ifndef LPMAC_LPMAC_NEIGHBORS_H_
#define LPMAC_LPMAC_NEIGHBORS_H_

#include "lpmac.h"

void lpmac_neighbors_init(neighbor_event_fn_t neighbor_updates_callback);
void lpmac_neighbors_clear();
void lpmac_neighbors_add(node_id_t node_id, link_quality_t link_quality);
void lpmac_neighbors_rem(node_id_t node_id);
void lpmac_neighbors_heard(node_id_t node_id, link_quality_t link_quality);
void lpmac_neighbors_docallbacks();

#endif /* LPMAC_LPMAC_NEIGHBORS_H_ */
