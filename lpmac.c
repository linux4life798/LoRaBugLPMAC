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
#include <ti/sysbios/knl/Clock.h>
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
#include "lpmac_neighbors.h"
#include "lpmac.h"

#include <Board.h>
#include "io.h"

// ---- System Config ---- //

#define TASKSTACKSIZE   2048

//#define RX_TIMEOUT_VALUE                            60000
//#define RX_TIMEOUT_VALUE                            5000
//#define RX_TIMEOUT_VALUE                            1000
#define RX_TIMEOUT_VALUE                            0
#define BUFFER_SIZE                                 256 // Define the payload size here

// ---- RUNTIME ---- //

const static struct Radio_s *radios;
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

static Clock_Params timeoutParams;
static Clock_Struct timeoutStruct;

static uint16_t BufferSize = 0;
static uint8_t Buffer[BUFFER_SIZE];

static Queue_Struct neighborsQueueStruct;
static Queue_Handle neighborsQueueHandle;

static uint32_t myid = 0xFFFFFFFF;

// Outgoing Buffers
static uint8_t next_pkt_id = 0;
static pkt_hdr_t *outgoing_hdr;
static uint8_t *outgoing_buf;
static int outgoing_retries;

static uint8_t outgoing_ack_hdr_buf[PKT_HDR_CALC_SIZE(1)];
static pkt_hdr_t *outgoing_ack_hdr = (pkt_hdr_t *) &outgoing_ack_hdr_buf;

static int8_t RssiValue = 0;
static int8_t SnrValue = 0;

/*!
 * Radio events function pointer
 */
static RadioEvents_t RadioEvents;

/*!
 * \brief Function to be executed on Radio Tx Done event
 */
static void OnTxDone(void) {
	printf("OnTxDone\n");
	radios->Sleep();
//    radios->Standby();
	Event_post(lpmacEventsHandle, EVENT_TXDONE);
}

/*!
 * \brief Function to be executed on Radio Rx Done event
 */
static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
	pkt_hdr_t *hdr = (pkt_hdr_t *) payload;
	//    radios->Standby();
//    radios->Sleep();
	dprintf("OnRxDone - RSSI=%d, SNR=%d\n", rssi, snr);
	hexdump(payload, size);
	uarthexdump(payload, size);

	if (size < PKT_HDR_CALC_SIZE(0)) {
		dprintf("Received a packet(%u) that was smaller than a header(%u)\n",
				size, PKT_HDR_CALC_SIZE(0));
//    	radios->Rx(0);
		// Do not process this message
		return;
	}
	if (PKT_SIZE(hdr) != size) {
		dprintf(
				"Received a packet whose size(%u) disagrees with header size(%u)\n",
				PKT_SIZE(hdr), size);
//    	radios->Rx(0);
		// Do not process this message
		return;
	}

#	ifdef ID_FILTER_ENABLED
	{
		uint8_t index;
		for (index = 0; index < hdr->dst_count; index++) {
			if (hdr->dst[index] == myid) {
				break;
			}
		}
		// If not a broadcast and we counldn't find our ID
		if (hdr->dst_count != 0 && index == hdr->dst_count) {
			// Do not process this message
			dprintf("Dropping pkt for dst[0] = 0x%8.8X\n", hdr->dst[0]);
			return;
		}
	}
#	endif

	BufferSize = size;
//    Buffer = payload; // simply grab the reference to save memory
	memcpy(Buffer, payload, BufferSize);
	RssiValue = rssi;
	SnrValue = snr;
//    radios->Rx(0);
	// This heard must be before the following Event_post, since it may remove this neighbor
	lpmac_neighbors_heard(hdr->src, rssi);
	Event_post(lpmacEventsHandle, EVENT_RXDONE);
}

/*!
 * \brief Function executed on Radio Tx Timeout event
 */
static void OnTxTimeout(void) {
	dprintf("OnTxTimeout\n");
	radios->Sleep();
//    radios->Standby();
	Event_post(lpmacEventsHandle, EVENT_TXTIMEOUT);
}

/*!
 * \brief Function executed on Radio Rx Timeout event
 */
static void OnRxTimeout(void) {
	dprintf("OnRxTimeout\n");
//    radios->Sleep( );
//    radios->Standby();
	Event_post(lpmacEventsHandle, EVENT_RXTIMEOUT);
}

/*!
 * \brief Function executed on Radio Rx Error event
 */
static void OnRxError(void) {
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
static void OnCadDone( bool channelActivityDetected) {
	dprintf("OnCadDone - %s\n",
			channelActivityDetected ? "Detected" : "NotDetected");
	radios->Sleep();
//    radios->Standby();
	UInt event =
			channelActivityDetected ?
					EVENT_CADDONE_DETECT : EVENT_CADDONE_NODETECT;
	Event_post(lpmacEventsHandle, event);
}

static inline node_id_t getmyid() {
	uint64_t id;
	BoardGetUniqueId((uint8_t *) &id);
	return (node_id_t) (id & 0xFFFFFFFF);
}

static void clearevents(UInt events) {
	if (Event_getPostedEvents(lpmacEventsHandle) & events) {
		Event_pend(lpmacEventsHandle, Event_Id_NONE, events, BIOS_WAIT_FOREVER);
	}
}

Void timeout_callback(UArg arg) {
	Event_post(lpmacEventsHandle, EVENT_TIMEOUT);
}

static void timeout_init() {
	Clock_Params_init(&timeoutParams);
	timeoutParams.period = 0;
	timeoutParams.startFlag = FALSE;
	Clock_construct(&timeoutStruct, timeout_callback, 0, &timeoutParams);
}

static void timeout_start(uint32_t ms) {
	Clock_setTimeout(Clock_handle(&timeoutStruct), (UInt32) (ms * TIME_MS));
	Clock_start( Clock_handle(&timeoutStruct));
}

static void timeout_stop() {
	Clock_stop( Clock_handle(&timeoutStruct));
}

/**
 * Send using Listen Before Talk with random backoff times.
 * This blocks until the transmission is finished.
 *
 * @param hdr Pointer to a packet header
 * @param data Pointer to data buffer, can be NULL
 * @param data_size Size of data buffer, can be 0
 */
static void send(const pkt_hdr_t *hdr, char *data) {
	UInt events;
	int delay;
	uint8_t *buf = (uint8_t *) malloc(PKT_SIZE(hdr));
	if (buf == NULL) {
		rerror("Failed to allocate send buffer\n");
	}
	memcpy(buf, hdr, PKT_HDR_SIZE(hdr));
	if ((data != NULL) && (hdr->data_size > 0)) {
		memcpy(PKT_DATA_PTR((pkt_hdr_t * )buf), data, hdr->data_size);
	}

	delay = 10 * (rand() % 100);
	dprintf("delaying %dms\n", delay);
	Task_sleep(TIME_MS * delay);

//    radios->Sleep();
	radios->Standby();

#ifdef LBT_ENABLED
	do {
		dprintf("CAD - Starting\n");
		radios->StartCad();
		dprintf("CAD - Started\n");
		events = Event_pend(lpmacEventsHandle, Event_Id_NONE,
				EVENT_CADDONE_DETECT | EVENT_CADDONE_NODETECT,
				BIOS_WAIT_FOREVER);
		if (events & EVENT_CADDONE_DETECT) {
			delay = (rand() % 20) * 100;
			dprintf("CAD - Activity Detected - Backoff %d ms\n", delay);
			Task_sleep(delay * TIME_MS);
		} else {
			dprintf("CAD - Clear\n");
			break;
		}
	} while (1);
#endif

	dprintf("Firing Message\n");
	hexdump(buf, PKT_SIZE(hdr));
	uarthexdump(buf, PKT_SIZE(hdr));
	radios->Send(buf, PKT_SIZE(hdr));
	free(buf);
	events = Event_pend(lpmacEventsHandle, Event_Id_NONE,
			EVENT_TXDONE | EVENT_TXTIMEOUT, BIOS_WAIT_FOREVER);
	if (events & EVENT_TXTIMEOUT) {
		dprintf("Received a TXTIMEOUT\n");
//        rerror("Received a TXTIMEOUT\n");
	}
//    radios->Sleep();
	radios->Rx(RX_TIMEOUT_VALUE);
}

static void lpmacTask(UArg arg0, UArg arg1) {
	dprintf("LPMAC Task Started\n");

	// Target board initialization
	dprintf("Board Initialization\n");
	BoardInitMcu();
	BoardInitPeriph();

	// Radio initialization
	RadioEvents.TxDone = OnTxDone;
	RadioEvents.RxDone = OnRxDone;
	RadioEvents.TxTimeout = OnTxTimeout;
	RadioEvents.RxTimeout = OnRxTimeout;
	RadioEvents.RxError = OnRxError;
	RadioEvents.CadDone = OnCadDone;

	myid = getmyid();
	srand((int) myid);
	dprintf("My ID = 0x%X\n", myid);

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
	true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

	Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
	LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
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

    dprintf("Radio.Rx( %u ) - Starting\n", RX_TIMEOUT_VALUE);
    radios->Rx(RX_TIMEOUT_VALUE);
    dprintf("Radio.Rx( %u ) - Finished\n", RX_TIMEOUT_VALUE);

	// Clear posted events from initialization
//    clearevents(EVENT_TXDONE|EVENT_TXTIMEOUT|EVENT_RXDONE|EVENT_RXTIMEOUT|EVENT_CADDONE_DETECT|EVENT_CADDONE_NODETECT);

	while (1) {
		UInt events;
		pkt_hdr_t *hdr;

		events = Event_pend(lpmacEventsHandle, Event_Id_NONE,
				EVENT_JOIN | EVENT_SEND | EVENT_RECV | EVENT_RXDONE
						| EVENT_RXTIMEOUT | EVENT_TIMEOUT,
				BIOS_WAIT_FOREVER);
//        dprintf("events = 0x%X\n", events);
		if (events & EVENT_JOIN) {
			// JOIN
			dprintf("Send JOIN\n");
			char hdr_buf[PKT_HDR_CALC_SIZE(0)];
			hdr = (pkt_hdr_t *) &hdr_buf;

			lpmac_neighbors_clear();

			hdr->src = getmyid();
			hdr->dst_count = 0; // Broadcast
//            hdr->dst[0] = dst;
			// REQ_ACK - Will send full ACKable packet back to assert presence
			// NO_ACK  - Will simply send non-acked presence packet back
			hdr->pkt_opts = PKT_OPTIONS_REQ_ACK;
			hdr->pkt_type = PKT_TYPE_JOIN;
			hdr->data_size = 0;
			hdr->pkt_id = next_pkt_id++;

			send(hdr, NULL);
			// Allow to go into Rx Mode again
//            radios->Rx(RX_TIMEOUT_VALUE);

			Event_post(lpmacRequestEventsHandle, EVENT_JOINDONE);

		}
		if (events & EVENT_SEND) {
			// SEND
			dprintf("Send Started\n");
			send(outgoing_hdr, outgoing_buf);
			// Allow to go into Rx Mode again
//            radios->Rx(RX_TIMEOUT_VALUE);
			// HACK/TEST
			outgoing_retries = 0;
			timeout_start(RETRIES_TIMEOUT_MS);

		}
		if (events & EVENT_RXDONE) {
			// RX
			dprintf("RX Packet\n");
			hdr = (pkt_hdr_t *) Buffer;

			if (hdr->pkt_opts & PKT_OPTIONS_REQ_ACK) {
				dprintf("Acknowledging packet %d\n", hdr->pkt_id);
				outgoing_ack_hdr->pkt_type = PKT_TYPE_ACK;
				outgoing_ack_hdr->pkt_opts = PKT_OPTIONS_NO_ACK;
				outgoing_ack_hdr->pkt_id = hdr->pkt_id;
				outgoing_ack_hdr->dst_count = 1;
				outgoing_ack_hdr->src = myid;
				outgoing_ack_hdr->dst[0] = hdr->src;
				outgoing_ack_hdr->data_size = 0;

				send(outgoing_ack_hdr, NULL);
			}

			switch (hdr->pkt_type) {
			case PKT_TYPE_JOIN:
				dprintf("Got JOIN with pkt_id=%d\n", hdr->pkt_id)
				;
				lpmac_neighbors_add(hdr->src, RssiValue);
				break;
			case PKT_TYPE_UNJOIN:
				dprintf("Got UNJOIN with pkt_id=%d\n", hdr->pkt_id)
				;
				lpmac_neighbors_rem(hdr->src);
				break;
			case PKT_TYPE_ACK:
				dprintf("Got ACK for pkt_id=%d\n", hdr->pkt_id)
				;
				if (outgoing_hdr && (outgoing_hdr->pkt_id == hdr->pkt_id)) {
					outgoing_hdr = NULL;
					timeout_stop();
					events &= ~EVENT_TIMEOUT;
					clearevents(EVENT_TIMEOUT);
					Event_post(lpmacRequestEventsHandle, EVENT_SENDDONE_OK);
				}
				break;
			case PKT_TYPE_DATA:
				// Let user know about data recv
				dprintf("Got DATA with pkt_id=%d\n", hdr->pkt_id)
				;
				rx_fn(PKT_DATA_PTR(hdr), hdr->data_size, hdr->src, RssiValue);
				break;
			default:
				dprintf("Bad packet type\n")
				;
				continue;
			}
			// Allow to go into Rx Mode again
//            radios->Rx(0);
		}
		if (events & EVENT_TIMEOUT) {
			if (outgoing_retries++ < RETRIES_MAX) {
				// Try to resend
				send(outgoing_hdr, outgoing_buf);
				timeout_start(RETRIES_TIMEOUT_MS);
			} else {
				// Failed to send
				outgoing_hdr = NULL;
				Event_post(lpmacRequestEventsHandle, EVENT_SENDDONE_FAIL);
			}
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
}

void LPMAC_Init(const struct Radio_s *radio,
		neighbor_event_fn_t neighbor_updates_callback, rx_fn_t rx_callback) {

	radios = radio;
	rx_fn = rx_callback;
	lpmac_neighbors_init(neighbor_updates_callback);
	timeout_init();

//    Queue_construct(&neighborsQueueStruct, NULL);
//    neighborsQueueHandle = Queue_handle(&neighborsQueueStruct);

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
	Task_construct(&lpmacTaskStruct, (Task_FuncPtr) lpmacTask, &lpmacTaskParams,
			NULL);
}

bool LPMAC_Send(const uint8_t *buf, size_t len, node_id_t dst) {
	char hdr_buf[PKT_HDR_CALC_SIZE(1)];
	struct pkt_hdr *hdr = (struct pkt_hdr *) &hdr_buf;
	UInt events;

	hdr->src = myid;
	hdr->dst_count = 1;
	hdr->dst[0] = dst;
	hdr->pkt_opts = PKT_OPTIONS_REQ_ACK;
	hdr->pkt_type = PKT_TYPE_DATA;
	hdr->data_size = len;
	hdr->pkt_id = next_pkt_id++;

	// Setup send parameters
	outgoing_buf = buf;
	outgoing_hdr = hdr;

	// Set request to send
	Event_post(lpmacEventsHandle, EVENT_SEND);

	// Wait for system to respond about send process
	events = Event_pend(lpmacRequestEventsHandle, Event_Id_NONE,
			EVENT_SENDDONE_OK | EVENT_SENDDONE_FAIL, BIOS_WAIT_FOREVER);

	return (events & EVENT_SENDDONE_OK) ? 1 : 0;
}

bool LPMAC_Join() {
	// Set request to join
	Event_post(lpmacEventsHandle, EVENT_JOIN);
	Event_pend(lpmacRequestEventsHandle, Event_Id_NONE, EVENT_JOINDONE,
			BIOS_WAIT_FOREVER);
	return true;
}

node_id_t LPMAC_MyId(node_id_t id) {
	if (id == 0) {
		return myid;
	} else {
		return (myid = id);
	}
}
