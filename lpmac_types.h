/**
 * Define how errors are handled in LPMAC
 *
 * @author Craig Hesling <craig@hesling.com>
 * @date Apr 28, 2017
 */


#ifndef LPMAC_LPMAC_TYPES_H_
#define LPMAC_LPMAC_TYPES_H_

#include <ti/sysbios/knl/Event.h>

#include "lpmac.h"

/* Radio Events */
#define EVENT_TXDONE           Event_Id_00
#define EVENT_RXDONE           Event_Id_01
#define EVENT_TXTIMEOUT        Event_Id_02
#define EVENT_RXTIMEOUT        Event_Id_03
#define EVENT_RXERROR          Event_Id_04
#define EVENT_CADDONE_DETECT   Event_Id_05
#define EVENT_CADDONE_NODETECT Event_Id_06
#define EVENT_TIMEOUT          Event_Id_07

/* High Level Events */
#define EVENT_JOIN             Event_Id_10
#define EVENT_JOINDONE         Event_Id_11
#define EVENT_RETURN           Event_Id_12
#define EVENT_SEND             Event_Id_13
#define EVENT_SENDDONE_OK      Event_Id_14
#define EVENT_SENDDONE_FAIL    Event_Id_15
#define EVENT_RECV             Event_Id_16

#define LPMAC_SYNCWORD       0xD0

enum pkt_type {
    PKT_TYPE_ACK    = 1,
    PKT_TYPE_JOIN   = 2,
    PKT_TYPE_UNJOIN = 3,
    PKT_TYPE_DATA   = 4
};

enum trans_state {
    TRANS_STATE_NONE = 0,
    TRANS_STATE_SENT,
    TRANS_STATE_WAIT_ACK,
    TRANS_STATE_ACKED
};

//enum node_state {
//    NODE_STATE_JOIN,
//    NODE_STATE_BLAH
//};

#define PKT_OPTIONS_NO_ACK 0
#define PKT_OPTIONS_REQ_ACK 1

/**
 * This is the states for a transaction with one
 */
struct trans {
    node_id_t        dst;
    unsigned         retries;
    enum trans_state state;
};

struct pkt_hdr {
    enum pkt_type pkt_type  : 8;
    uint8_t       pkt_opts  : 8;
    uint8_t       pkt_id    : 8;
    uint8_t       dst_count : 8;
    size_t        data_size : 8;
    node_id_t     src       : 32;
    node_id_t     dst[];
} __attribute__((__packed__));
typedef struct pkt_hdr pkt_hdr_t;

#define PKT_HDR_CALC_SIZE(dst_count) (sizeof(struct pkt_hdr) + (sizeof(node_id_t)*(dst_count)))
#define PKT_HDR_SIZE(pkt_hdr_ptr) (sizeof(struct pkt_hdr) + (sizeof(node_id_t)*((size_t)((pkt_hdr_ptr)->dst_count))))
#define PKT_DATA_PTR(pkt_hdr_ptr) ( ((uint8_t *)(pkt_hdr_ptr)) + PKT_HDR_SIZE(pkt_hdr_ptr) )
#define PKT_SIZE(pkt_hdr_ptr) ( PKT_HDR_SIZE(pkt_hdr_ptr) + (pkt_hdr_ptr)->data_size )
#define PKT_PAYLOAD_MAX_SIZE(dst_count) ( 256 - (sizeof(struct pkt_hdr) + (sizeof(node_id_t)*(dst_count))) )

#endif /* LPMAC_LPMAC_TYPES_H_ */
