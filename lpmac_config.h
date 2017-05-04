/**
 * @brief The User Config for The LoRa Peer MAC Library
 *
 * @author Craig Hesling <craig@hesling.com>
 * @date Apr 30, 2017
 */

#ifndef LPMAC_LPMAC_CONFIG_H_
#define LPMAC_LPMAC_CONFIG_H_

#define MAX_RETRIES 3
#define LBT_ENABLED
#define ID_FILTER_ENABLED

#define USE_BAND_915
#define USE_MODEM_LORA
//#define USE_MODEM_FSK

#define TX_OUTPUT_POWER                             20        // dBm

#if defined( USE_BAND_433 )

#   define RF_FREQUENCY                                434000000 // Hz

#elif defined( USE_BAND_780 )

#   define RF_FREQUENCY                                780000000 // Hz

#elif defined( USE_BAND_868 )

#   define RF_FREQUENCY                                868000000 // Hz

#elif defined( USE_BAND_915 )

#   define RF_FREQUENCY                                902000000 // Hz
#else
#   error "Please define a frequency band in the compiler options."
#endif

#if defined( USE_MODEM_LORA )

#define LORA_BANDWIDTH                              0         // [0: 125 kHz,
                                                              //  1: 250 kHz,
                                                              //  2: 500 kHz,
                                                              //  3: Reserved]
#define LORA_SPREADING_FACTOR                       7         // [SF7..SF12]
#define LORA_CODINGRATE                             1         // [1: 4/5,
                                                              //  2: 4/6,
                                                              //  3: 4/7,
                                                              //  4: 4/8]
#define LORA_PREAMBLE_LENGTH                        8         // Same for Tx and Rx
//#define LORA_PREAMBLE_LENGTH                        20         // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                         5         // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false

#elif defined( USE_MODEM_FSK )

#define FSK_FDEV                                    25e3      // Hz
#define FSK_DATARATE                                50e3      // bps
#define FSK_BANDWIDTH                               50e3      // Hz
#define FSK_AFC_BANDWIDTH                           83.333e3  // Hz
#define FSK_PREAMBLE_LENGTH                         5         // Same for Tx and Rx
#define FSK_FIX_LENGTH_PAYLOAD_ON                   false

#else
    #error "Please define a modem in the compiler options."
#endif


#endif /* LPMAC_LPMAC_CONFIG_H_ */
