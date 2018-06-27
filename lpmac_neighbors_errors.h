/**
 * Define how errors are handled in LPMAC Neighbors
 *
 * @author Craig Hesling <craig@hesling.com>
 * @date May 5, 2017
 */

#ifndef LPMAC_LPMAC_NEIGHBORS_ERRORS_H_
#define LPMAC_LPMAC_NEIGHBORS_ERRORS_H_

#include <stdio.h>
#include <xdc/runtime/System.h>
#include <io.h>

/**@def dprintf
 * Print formatted debugging messages
 */
#define dprintf(format, args...) printf("# LPMAC Neighbors: "##format, ##args); uartprintf("# LPMAC Neighbors: "##format, ##args)

/**@def rerror
 * Handle runtime error
 */
#define rerror(msg) uartputs(msg); System_abort(msg)


// Could have pin toggle for debugging here
//#include "io.h"

#endif /* LPMAC_LPMAC_NEIGHBORS_ERRORS_H_ */
