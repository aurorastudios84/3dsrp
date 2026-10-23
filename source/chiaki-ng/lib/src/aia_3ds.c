// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/*
 * 3DS stub for chiaki's AIA (Authority Information Access) cert-chain fetcher.
 *
 * Upstream lib/src/aia.c walks the public internet through libcurl to complete
 * a PSN TLS certificate chain. The 3DS build has no curl on purpose: devkitPro's
 * 3DS portlib is built without websocket support, which upstream's FindCURL
 * requires, and nothing on the *local* Remote Play path needs AIA at all -- it
 * only matters when a PSN account id has to be looked up over the network, and
 * on this port that value is supplied by the user (the same --psn-account-id
 * route the host tools already use).
 *
 * lib/CMakeLists.txt compiles this file instead of src/aia.c when CHIAKI_IS_3DS
 * is set. Only chiaki_aia_init() is actually called by the rest of the library
 * (chiaki_log_init in common.c); the full API is not provided, so a future
 * caller fails loudly at link time rather than silently at runtime.
 */
#include "aia.h"

ChiakiErrorCode chiaki_aia_init(void)
{
	return CHIAKI_ERR_SUCCESS;
}

void chiaki_aia_fini(void)
{
}
