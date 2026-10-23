#ifndef N3DS_MVD_REFERENCE_SERVICE_H
#define N3DS_MVD_REFERENCE_SERVICE_H

#include <3ds.h>

/*
 * Poll one configured video render using libctru's MVDSTD control command,
 * without resending MVDSTD_Config. Requires successful MVD video initialization
 * and a successful MVDSTD_SetConfig() first. Returns the exact MVD service
 * Result (including MVD_STATUS_BUSY). Does not wait or allocate.
 */
Result mvd_reference_render_poll(void);

#endif
