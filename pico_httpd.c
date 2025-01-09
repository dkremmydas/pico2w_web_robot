/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "custom.h"

#include <stdio.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/ip4_addr.h"
//#include "lwip/apps/mdns.h"
#include "lwip/init.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"

// Create a virtual file to write the json report
static char json_response[JSON_BUFFER_SIZE + 128]; // Adjust size as needed

void httpd_init(void);

static absolute_time_t wifi_connected_time;
static bool led_on = false;

#if LWIP_MDNS_RESPONDER
static void srv_txt(struct mdns_service *service, void *txt_userdata)
{
  err_t res;
  LWIP_UNUSED_ARG(txt_userdata);

  res = mdns_resp_add_service_txtitem(service, "path=/", 6);
  LWIP_ERROR("mdns add service txt failed\n", (res == ERR_OK), return);
}
#endif

// Return some characters from the ascii representation of the mac address
// e.g. 112233445566
// chr_off is index of character in mac to start
// chr_len is length of result
// chr_off=8 and chr_len=4 would return "5566"
// Return number of characters put into destination
static size_t get_mac_ascii(int idx, size_t chr_off, size_t chr_len, char *dest_in) {
    static const char hexchr[16] = "0123456789ABCDEF";
    uint8_t mac[6];
    char *dest = dest_in;
    assert(chr_off + chr_len <= (2 * sizeof(mac)));
    cyw43_hal_get_mac(idx, mac);
    for (; chr_len && (chr_off >> 1) < sizeof(mac); ++chr_off, --chr_len) {
        *dest++ = hexchr[mac[chr_off >> 1] >> (4 * (1 - (chr_off & 1))) & 0xf];
    }
    return dest - dest_in;
}

//https://lwip-users.nongnu.narkive.com/fNQ0pUUs/proper-way-to-add-extern-fs-support-to-htttpserver-raw

static const char *cgi_control(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    
    static char json_response[JSON_BUFFER_SIZE];

    printf("Control command received. iIndex:%d iNumParams:%d pcParam1:%s pcValue1:%s\n",iIndex, iNumParams, pcParam[0], pcValue[0] );
    
    const char *command = "none"; // Default command if no parameters are passed

    // Parse the received parameters
    if (iNumParams > 0 && pcParam != NULL && pcValue != NULL) {
        for (int i = 0; i < iNumParams; i++) {
            if (strcmp(pcParam[i], "command") == 0) {
                command = pcValue[i];
                break;
            }
        }
    }

    // Dynamically generate the JSON response
    snprintf(json_response, JSON_BUFFER_SIZE,
             "{\"status\":\"success\",\"received_command\":\"%s\"}",
             command);
    
    printf("json Response:%s\n",json_response );

    return "/json_response"; // it will be a custom filename

}

int fs_open_custom(struct fs_file *file, const char *name) {
    if (strcmp(name, "/json_response") == 0) {
        extern char json_response[]; // Reference the global variable
        file->data = json_response;  // Set file data to the virtual file string
        file->len = strlen(json_response); // Set file length
        file->index = 0;           // Start reading from the beginning
        return 1; // Success
    }
    return 0; // File not found
}


int fs_read_custom(struct fs_file *file, char *buffer, int count) {
    // Ensure the file is valid and points to the virtual file
    if (!file || file->data != json_response) {
        printf("Error: Invalid file or not a virtual file\n");
        return -1; // Return error
    }

    // Calculate the remaining bytes to read
    int available = file->len - file->index;
    if (available <= 0) {
        printf("No more data to read.\n");
        return 0; // No data left to read
    }

    // Determine how many bytes to read
    int to_read = (count < available) ? count : available;

    // Copy the data to the provided buffer
    memcpy(buffer, json_response + file->index, to_read);

    // Update the file's read index
    file->index += to_read;

    printf("Read %d bytes from virtual file.\n", to_read);
    return to_read; // Return the number of bytes read
}



void fs_close_custom(struct fs_file *file) {
    if (file && file->data == json_response) {
        // Clear the contents of the virtual_file
        memset(json_response, 0, sizeof(json_response));
        printf("Cleared virtual file contents.\n");
    }

    // Log closure for debugging
    printf("Closed virtual file: %p\n", file);
}





static tCGI cgi_handlers[] = {
    { "/control.cgi", cgi_control}
};



int main() {

    sleep_ms(5000);

    stdio_init_all();

    //gpio_init(MOTOR_FRONT_RIGHT);
    //gpio_set_dir(MOTOR_FRONT_RIGHT, GPIO_OUT);    

    

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    
    char hostname[sizeof(CYW43_HOST_NAME) + 4];
    memcpy(&hostname[0], CYW43_HOST_NAME, sizeof(CYW43_HOST_NAME) - 1);
    get_mac_ascii(CYW43_HAL_MAC_WLAN0, 8, 4, &hostname[sizeof(CYW43_HOST_NAME) - 1]);
    hostname[sizeof(hostname) - 1] = '\0';
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], hostname);

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        exit(1);
    } else {
        printf("Connected.\n");
    }
    printf("\nReady, running httpd at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

    // start http server
    wifi_connected_time = get_absolute_time();

#if LWIP_MDNS_RESPONDER
    // Setup mdns
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    printf("mdns host name %s.local\n", hostname);
#if LWIP_VERSION_MAJOR >= 2 && LWIP_VERSION_MINOR >= 2
    mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_STA], hostname);
    mdns_resp_add_service(&cyw43_state.netif[CYW43_ITF_STA], "pico_httpd", "_http", DNSSD_PROTO_TCP, 80, srv_txt, NULL);
#else
    mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_STA], hostname, 60);
    mdns_resp_add_service(&cyw43_state.netif[CYW43_ITF_STA], "pico_httpd", "_http", DNSSD_PROTO_TCP, 80, 60, srv_txt, NULL);
#endif
    cyw43_arch_lwip_end();
#endif
    // setup http server
    cyw43_arch_lwip_begin();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    httpd_init();
    cyw43_arch_lwip_end();

    while(true) {
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(led_time);
#else
        sleep_ms(1000);
#endif
    }
#if LWIP_MDNS_RESPONDER
    mdns_resp_remove_netif(&cyw43_state.netif[CYW43_ITF_STA]);
#endif
    cyw43_arch_deinit();
}
