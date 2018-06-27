/**
 * The LoRa Peer MAC Library
 *
 * @author Craig Hesling <craig@hesling.com>
 * @date Apr 28, 2017
 */

#ifndef LPMAC_H_
#define LPMAC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <radio.h>

#define PRINTF_FMT_NODE_ID "%8.8X"

typedef enum {
    NEIGHBOR_EVENT_ADD,
    NEIGHBOR_EVENT_REM,
	NEIGHBOR_EVENT_UPDATE,
} neighbor_event_t;

typedef uint32_t node_id_t;
typedef uint8_t  link_quality_t;

typedef void (*neighbor_event_fn_t)(neighbor_event_t type, node_id_t id, link_quality_t link_quality);
typedef void (*rx_fn_t)(uint8_t *buf, size_t buf_size, node_id_t dst, link_quality_t link_quality);

void
LPMAC_Init(const struct Radio_s *radio,
           neighbor_event_fn_t neighbor_updates_callback,
           rx_fn_t rx_callback);
bool
LPMAC_Send(const uint8_t *buf, size_t len, node_id_t dst);

/**
 * This will send a join packet and rebuild the
 * @return true is at least one neighbor was found, false if no neighbors were found
 */
bool
LPMAC_Join();

node_id_t
LPMAC_MyId(node_id_t id);

void LPMAC_Announce();
void LPMAC_Neighbors();
void LPMAC_Clear();

#ifdef __cplusplus
}
#endif

#endif /* LPMAC_H_ */
