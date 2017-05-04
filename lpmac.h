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

typedef enum {
    NETWORK_EVENT_NEIGHBOR_ADD,
    NETWORK_EVENT_NEIGHBOR_REM,
} network_event_t;

typedef uint32_t node_id_t;

typedef void (*network_event_fn_t)(network_event_t type, node_id_t id);
typedef void (*rx_fn_t)(uint8_t *buf, size_t buf_size, node_id_t dst, uint8_t rssi);

void
LPMAC_Init(const struct Radio_s *radio,
           network_event_fn_t network_updates_callback,
           rx_fn_t rx_callback);
bool
LPMAC_Send(const uint8_t *buf, size_t len, node_id_t dst);

/**
 * This will send a join packet and rebuild the
 * @return true is at least one neighbor was found, false if no neighbors were found
 */
bool
LPMAC_Join();

#ifdef __cplusplus
}
#endif

#endif /* LPMAC_H_ */
