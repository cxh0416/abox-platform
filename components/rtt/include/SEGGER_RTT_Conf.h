#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

/* A small, non-blocking debug terminal. Override consistently for all users
 * of the abox_rtt target if a product needs different buffer sizes. */
#ifndef SEGGER_RTT_MAX_NUM_UP_BUFFERS
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS 1
#endif
#ifndef SEGGER_RTT_MAX_NUM_DOWN_BUFFERS
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS 1
#endif
#ifndef BUFFER_SIZE_UP
#define BUFFER_SIZE_UP 512
#endif
#ifndef BUFFER_SIZE_DOWN
#define BUFFER_SIZE_DOWN 16
#endif
#ifndef SEGGER_RTT_MODE_DEFAULT
#define SEGGER_RTT_MODE_DEFAULT SEGGER_RTT_MODE_NO_BLOCK_SKIP
#endif

#endif
