/**
 * Define how errors are handled in LPMAC
 *
 * @author Craig Hesling <craig@hesling.com>
 * @date Apr 28, 2017
 */

#ifndef LPMAC_LPMAC_ERRORS_H_
#define LPMAC_LPMAC_ERRORS_H_

#include <xdc/runtime/System.h>
#include <io.h>

/**@def dprintf
 * Print formatted debugging messages
 */
#define dprintf(format, args...) printf("# LPMAC: "##format, ##args); uartprintf("# LPMAC: "##format, ##args)

/**@def rerror
 * Handle runtime error
 */
#define rerror(msg) uartputs(msg); System_abort(msg)


// Could have pin toggle for debugging here
//#include "io.h"

#endif /* LPMAC_LPMAC_ERRORS_H_ */
