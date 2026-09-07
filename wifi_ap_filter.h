#ifndef WIFI_AP_FILTER_H
#define WIFI_AP_FILTER_H

#include "lwip/pbuf.h"

struct netif;

// Defined in pico_httpd.c. In access-point mode, enforces a "first client
// wins" lock: the first client seen becomes the sole allowed source IP until
// it goes quiet for WIFI_AP_CLIENT_LOCK_TIMEOUT_MS (custom.h), at which point
// a different client may claim the lock. A no-op in station mode. Wired in
// via LWIP_HOOK_FILENAME (lwipopts.h), lwIP's official extension point for
// this - included straight into ip4_input() and several other core files.
int wifi_ap_ip4_input_filter(struct pbuf *p, struct netif *inp);

#define LWIP_HOOK_IP4_INPUT(p, inp) wifi_ap_ip4_input_filter(p, inp)

#endif // WIFI_AP_FILTER_H
