/**
 * @brief The LoRa Peer MAC Library
 *
 * @author Craig Hesling <craig@hesling.com>
 * @date Apr 28, 2017
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h> // strlen in uartputs
#include <stdbool.h>

/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Queue.h>
#include <ti/sysbios/gates/GateMutexPri.h>

/* Board Header files */
#include "Board.h"

#include "board.h" // The LoRaMac-node/src/boards/LoRaBug/board.h file

#ifdef __cplusplus
extern "C" {
#endif

#include "radio.h"

#ifdef __cplusplus
}
#endif

#include "lpmac_errors.h"
#include "lpmac_types.h"
#include "lpmac_config.h"
#include "lpmac.h"

#include <Board.h>
#include "io.h"

// ---- System Config ---- //

#define TASKSTACKSIZE   2048

//#define RX_TIMEOUT_VALUE                            60000
//#define RX_TIMEOUT_VALUE                            5000
//#define RX_TIMEOUT_VALUE                            1000
#define RX_TIMEOUT_VALUE                            0
#define BUFFER_SIZE                                 64 // Define the payload size here

// ---- RUNTIME ---- //

const static struct Radio_s *radios;
static network_event_fn_t network_update_fn;
static rx_fn_t rx_fn;

static Task_Params lpmacTaskParams;
static Task_Struct lpmacTaskStruct;
static Char lpmacTaskStack[TASKSTACKSIZE];

static Event_Struct lpmacEventsStruct;
static Event_Handle lpmacEventsHandle;

static Event_Struct lpmacRequestEventsStruct;
static Event_Handle lpmacRequestEventsHandle;


static GateMutexPri_Struct lpmacMutexStruct;
static GateMutexPri_Handle lpmacMutexHandle;

static uint16_t BufferSize;
static uint8_t Buffer[256];

static Queue_Struct neighborsQueueStruct;
static Queue_Handle neighborsQueueHandle;

// Outgoing Buffers
static struct pkt_hdr *outgoing_hdr;
static uint8_t        *outgoing_buf;
static uint8_t         next_pkt_id = 0;

static int8_t RssiValue = 0;
static int8_t SnrValue = 0;

/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;

/*!
 * \brief Function to be executed on Radio Tx Done event
 */
static void OnTxDone( void )
{
    printf("OnTxDone\n");
    radios->Sleep( );
//    radios->Standby();
    Event_post(lpmacEventsHandle, EVENT_TXDONE);
}

/*!
 * \brief Function to be executed on Radio Rx Done event
 */
static void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    pkt_hdr_t *hdr = (pkt_hdr_t *)payload;
    //    radios->Standby();
//    radios->Sleep();
    dprintf("OnRxDone - RSSI=%d, SNR=%d\n", rssi, snr);
    hexdump(payload, size);
    uarthexdump(payload, size);

    if (size < PKT_HDR_CALC_SIZE(0)) {
        dprintf("Received a packet(%u) that was smaller than a header(%u)\n", size, PKT_HDR_CALC_SIZE(0));
//    	radios->Rx(0);
        return;
    }
    if (PKT_SIZE(hdr) != size) {
        dprintf("Received a packet whose size(%u) disagrees with header size(%u)\n", PKT_SIZE(hdr), size);
//    	radios->Rx(0);
        return;
    }

    BufferSize = size;
//    Buffer = payload; // simply grab the reference to save memory
    memcpy( Buffer, payload, BufferSize );
    RssiValue = rssi;
    SnrValue = snr;
//    radios->Rx(0);
    Event_post(lpmacEventsHandle, EVENT_RXDONE);
}

/*!
 * \brief Function executed on Radio Tx Timeout event
 */
static void OnTxTimeout( void )
{
    dprintf("OnTxTimeout\n");
    radios->Sleep( );
//    radios->Standby();
    Event_post(lpmacEventsHandle, EVENT_TXTIMEOUT);
}

/*!
 * \brief Function executed on Radio Rx Timeout event
 */
static void OnRxTimeout( void )
{
    dprintf("OnRxTimeout\n");
//    radios->Sleep( );
//    radios->Standby();
    Event_post(lpmacEventsHandle, EVENT_RXTIMEOUT);
}

/*!
 * \brief Function executed on Radio Rx Error event
 */
static void OnRxError( void )
{
    dprintf("OnRxError\n");
//    radios->Sleep( );
//    radios->Standby();
    Event_post(lpmacEventsHandle, EVENT_RXERROR);
}

/*!
 * \brief CAD Done callback prototype.
 *
 * \param [IN] channelDetected    Channel Activity detected during the CAD
 */
static void OnCadDone( bool channelActivityDetected )
{
    dprintf("OnCadDone - %s\n", channelActivityDetected ? "Detected" : "NotDetected");
    radios->Sleep();
//    radios->Standby();
    UInt event = channelActivityDetected?EVENT_CADDONE_DETECT:EVENT_CADDONE_NODETECT;
    Event_post(lpmacEventsHandle, event);
}

static inline node_id_t getmyid()
{
    uint64_t id;
    BoardGetUniqueId((uint8_t *) &id);
    return (node_id_t)(id&0xFFFFFFFF);
}

static void clearevents(UInt events)
{
    if (Event_getPostedEvents(lpmacEventsHandle) & events) {
        Event_pend(lpmacEventsHandle, Event_Id_NONE, events, BIOS_WAIT_FOREVER);
    }
}



/**
 * Send using Listen Before Talk with random backoff times.
 * This blocks until the transmission is finished.
 *
 * @param hdr Pointer to a packet header
 * @param data Pointer to data buffer, can be NULL
 * @param data_size Size of data buffer, can be 0
 */
static void send(const struct pkt_hdr *hdr, char *data) {
    UInt events;
    int delay;
    uint8_t *buf = (uint8_t *)malloc(PKT_SIZE(hdr));
    if (buf == NULL) {
        rerror("Failed to allocate send buffer\n");
    }
    memcpy(buf, hdr, PKT_HDR_SIZE(hdr));
    if ((data != NULL) && (hdr->data_size>0)) {
        memcpy(PKT_DATA_PTR((pkt_hdr_t *)buf), data, hdr->data_size);
    }

    delay = 10 * (rand()%100);
    dprintf("delaying %dms\n", delay);
    Task_sleep(TIME_MS * delay);

//    radios->Sleep();
    radios->Standby();

#ifdef LBT_ENABLED
    do {
        dprintf("CAD - Starting\n");
        radios->StartCad();
        dprintf("CAD - Started\n");
        events = Event_pend(lpmacEventsHandle, Event_Id_NONE, EVENT_CADDONE_DETECT | EVENT_CADDONE_NODETECT, BIOS_WAIT_FOREVER);
        if (events & EVENT_CADDONE_DETECT) {
            delay = (rand() % 20) * 100;
            dprintf("CAD - Activity Detected - Backoff %d ms\n", delay);
            Task_sleep(delay * TIME_MS);
        } else {
            dprintf("CAD - Clear\n");
            break;
        }
    } while(1);
#endif

    dprintf("Firing Message\n");
    hexdump(buf, PKT_SIZE(hdr));
    uarthexdump(buf, PKT_SIZE(hdr));
    radios->Send(buf, PKT_SIZE(hdr));
    free(buf);
    events = Event_pend(lpmacEventsHandle, Event_Id_NONE, EVENT_TXDONE | EVENT_TXTIMEOUT, BIOS_WAIT_FOREVER);
    if(events & EVENT_TXTIMEOUT) {
    	dprintf("Received a TXTIMEOUT\n");
//        rerror("Received a TXTIMEOUT\n");
    }
//    radios->Sleep();
    radios->Rx(RX_TIMEOUT_VALUE);
}






static void lpmacTask(UArg arg0, UArg arg1)
{
    dprintf("LPMAC Task Started\n");

    // Target board initialization
    dprintf("Board Initialization\n");
    BoardInitMcu();
    BoardInitPeriph();

    // Radio initialization
    RadioEvents.TxDone    = OnTxDone;
    RadioEvents.RxDone    = OnRxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError   = OnRxError;
    RadioEvents.CadDone   = OnCadDone;

    dprintf("Radio Init\n");
    radios->Init(&RadioEvents);

    dprintf("Set channel to %u\n", RF_FREQUENCY);
    radios->SetChannel(RF_FREQUENCY);

#if defined( USE_MODEM_LORA )

    dprintf("Set TX and RX config\n");
    Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                      LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE,
                      LORA_PREAMBLE_LENGTH,
                      LORA_FIX_LENGTH_PAYLOAD_ON,
                      true,
                      0, 0, LORA_IQ_INVERSION_ON, 3000);

    Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                      LORA_CODINGRATE,
                      0, LORA_PREAMBLE_LENGTH,
                      LORA_SYMBOL_TIMEOUT,
                      LORA_FIX_LENGTH_PAYLOAD_ON, 0, true, 0, 0,
                      LORA_IQ_INVERSION_ON, true);
    dprintf("# Radio set TX and RX config\n");

//    radios->Write(REG_LR_SYNCWORD, LPMAC_SYNCWORD);
//    dprintf("# Radio set syncword to 0x%X\n", LPMAC_SYNCWORD);

    Radio.Write( REG_LR_PAYLOADMAXLENGTH, 0xFF);

    dprintf("Set Large Payload Params\n");
    dprintf("Payload Length = %u\n", Radio.Read( REG_LR_PAYLOADLENGTH ));
    dprintf("Max Payload = %u\n", Radio.Read( REG_LR_PAYLOADMAXLENGTH ));

#elif defined( USE_MODEM_FSK )

    radios->SetTxConfig( MODEM_FSK, TX_OUTPUT_POWER, FSK_FDEV, 0,
            FSK_DATARATE, 0,
            FSK_PREAMBLE_LENGTH, FSK_FIX_LENGTH_PAYLOAD_ON,
            true, 0, 0, 0, 3000 );

    radios->SetRxConfig( MODEM_FSK, FSK_BANDWIDTH, FSK_DATARATE,
            0, FSK_AFC_BANDWIDTH, FSK_PREAMBLE_LENGTH,
            0, FSK_FIX_LENGTH_PAYLOAD_ON, 0, true,
            0, 0,false, true );

#else
#error "Please define a frequency band in the compiler options."
#endif

//    dprintf("Radio.Rx( %u ) - Starting\n", RX_TIMEOUT_VALUE);
//    radios->Rx(RX_TIMEOUT_VALUE);
//    dprintf("Radio.Rx( %u ) - Finished\n", RX_TIMEOUT_VALUE);
    radios->Rx(0);

//    radios->Sleep();


    // Clear posted events from initialization
//    clearevents(EVENT_TXDONE|EVENT_TXTIMEOUT|EVENT_RXDONE|EVENT_RXTIMEOUT|EVENT_CADDONE_DETECT|EVENT_CADDONE_NODETECT);


//    GateMutexPri_enter(lpmacMutexHandle);
//    GateMutexPri_enter(lpmacMutexHandle);

    srand((int)(getmyid()&0x00000000FFFFFFFF));
    dprintf("ID = 0x%X\n", getmyid());

    while (1)
    {
        UInt events;
        pkt_hdr_t *hdr;

        events = Event_pend(lpmacEventsHandle, Event_Id_NONE,
                            EVENT_JOIN | EVENT_SEND | EVENT_RECV | EVENT_RXDONE | EVENT_RXTIMEOUT,
                            BIOS_WAIT_FOREVER);
//        dprintf("events = 0x%X\n", events);
        if (events & EVENT_JOIN) {
            // JOIN
            dprintf("Send JOIN\n");
            char hdr_buf[PKT_HDR_CALC_SIZE(0)];
            hdr = (pkt_hdr_t *)&hdr_buf;

            hdr->src       = getmyid();
            hdr->dst_count = 0; // Broadcast
//            hdr->dst[0] = dst;
            // REQ_ACK - Will send full ACKable packet back to assert presence
            // NO_ACK  - Will simply send non-acked presence packet back
            hdr->pkt_opts  = PKT_OPTIONS_REQ_ACK;
            hdr->pkt_type  = PKT_TYPE_JOIN;
            hdr->data_size = 0;
            hdr->pkt_id    = next_pkt_id++;

            send(hdr, NULL);
            // Allow to go into Rx Mode again
//            radios->Rx(RX_TIMEOUT_VALUE);

//            dprintf("JOIN Not Implemented\n");
            Event_post(lpmacRequestEventsHandle, EVENT_JOINDONE);

        }
        if (events & EVENT_SEND) {
            // SEND
            dprintf("Send Started\n");
            send(outgoing_hdr, outgoing_buf);
            // Allow to go into Rx Mode again
//            radios->Rx(RX_TIMEOUT_VALUE);
            // HACK/TEST
            Event_post(lpmacRequestEventsHandle, EVENT_SENDDONE_OK);
        }
        if (events & EVENT_RXDONE) {
            // RX
            dprintf("RX Packet\n");
            hdr = (pkt_hdr_t *)Buffer;

            switch(hdr->pkt_type) {
            case PKT_TYPE_JOIN:
                network_update_fn(NETWORK_EVENT_NEIGHBOR_ADD, hdr->src);
                break;
            case PKT_TYPE_UNJOIN:
                network_update_fn(NETWORK_EVENT_NEIGHBOR_REM, hdr->src);
                break;
            case PKT_TYPE_ACK:
                // TODO: Implement Ack'ed packets
                break;
            case PKT_TYPE_DATA:
                // Let user know about data recv
#				ifdef ID_FILTER_ENABLED
            	if (hdr->dst[0] == getmyid())
#				endif
                rx_fn(PKT_DATA_PTR(hdr), hdr->data_size, hdr->src, RssiValue);
                break;
            }
            // Allow to go into Rx Mode again
//            radios->Rx(0);
        }
//        radios->Rx(RX_TIMEOUT_VALUE);

        // Allow to go into Rx Mode again
//        radios->Rx(RX_TIMEOUT_VALUE);
//        Radio.Send(Buffer, sizeof(Buffer));
        //Radio.Rx( RX_TIMEOUT_VALUE);
//        Task_sleep(TIME_MS * 1000);
//        Task_yield();
//        dprintf("Sending\n");
//        send(hdr, Buffer, sizeof(Buffer));
//        dprintf("Done\n");
//        Task_sleep(TIME_MS * 1000);
//        Task_yield();
    }
//    free(hdr);
}







void LPMAC_Init(const struct Radio_s *radio,
                network_event_fn_t network_updates_callback,
                rx_fn_t rx_callback) {

    radios = radio;
    network_update_fn = network_updates_callback;
    rx_fn = rx_callback;

    Queue_construct(&neighborsQueueStruct, NULL);
    neighborsQueueHandle = Queue_handle(&neighborsQueueStruct);

    Event_construct(&lpmacEventsStruct, NULL);
    lpmacEventsHandle = Event_handle(&lpmacEventsStruct);

    Event_construct(&lpmacRequestEventsStruct, NULL);
    lpmacRequestEventsHandle = Event_handle(&lpmacRequestEventsStruct);

    GateMutexPri_construct(&lpmacMutexStruct, NULL);
    lpmacMutexHandle = GateMutexPri_handle(&lpmacMutexStruct);

    /* Construct heartBeat Task  thread */
    Task_Params_init(&lpmacTaskParams);
    lpmacTaskParams.stackSize = TASKSTACKSIZE;
    lpmacTaskParams.stack = &lpmacTaskStack;
    Task_construct(&lpmacTaskStruct, (Task_FuncPtr) lpmacTask, &lpmacTaskParams, NULL);
}

bool LPMAC_Send(const uint8_t *buf, size_t len, node_id_t dst) {
    char hdr_buf[PKT_HDR_CALC_SIZE(1)];
    struct pkt_hdr *hdr = (struct pkt_hdr *)&hdr_buf;
    UInt events;

    hdr->src       = getmyid();
    hdr->dst_count = 1;
    hdr->dst[0]    = dst;
    hdr->pkt_opts  = PKT_OPTIONS_REQ_ACK;
    hdr->pkt_type  = PKT_TYPE_DATA;
    hdr->data_size = len;
    hdr->pkt_id    = next_pkt_id++;

    // Setup send parameters
    outgoing_buf       = buf;
    outgoing_hdr       = hdr;

    // Set request to send
    Event_post(lpmacEventsHandle, EVENT_SEND);

    // Wait for system to respond about send process
    events = Event_pend(lpmacRequestEventsHandle, Event_Id_NONE, EVENT_SENDDONE_OK | EVENT_SENDDONE_FAIL, BIOS_WAIT_FOREVER);

    return (events & EVENT_SENDDONE_OK) ? 1 : 0;
}

bool LPMAC_Join() {
    // Set request to join
    Event_post(lpmacEventsHandle, EVENT_JOIN);
    Event_pend(lpmacRequestEventsHandle, Event_Id_NONE, EVENT_JOINDONE, BIOS_WAIT_FOREVER);
    return true;
}
