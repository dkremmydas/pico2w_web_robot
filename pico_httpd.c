/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *
 */

#include <stdio.h>
#include <string.h>

#include "custom.h"
#include "hardware/pwm.h"
#include "lwip/ip4_addr.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
// #include "lwip/apps/mdns.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/init.h"

typedef enum
{
    CMD_FLT,  // Front Left
    CMD_FRT,  // Front Right
    CMD_FWD,  // Go Front
    CMD_LFT,  // Left
    CMD_RGT,  // Right
    CMD_BLT,  // Back Left
    CMD_BWD,  // Go Back
    CMD_BRT,  // Back Right
    CMD_STOP, // Stop all motors immediately
    CMD_NONE  // No command has been received
} CommandType;

typedef struct
{
    uint en_pin;  // PWM enable pin
    uint in1_pin; // Direction pin 1
    uint in2_pin; // Direction pin 2
    int speed;    // Speed (0 to +10)
    float coef;   // Speed coefficient for this wheel
} Wheel;

Wheel wheels[] = {
    {MOTOR_FRONT_RIGHT_ENA, MOTOR_FRONT_RIGHT_IN1, MOTOR_FRONT_RIGHT_IN2,0,1.0},                                                                   // Front right, index=0
    {MOTOR_FRONT_LEFT_ENA, MOTOR_FRONT_LEFT_IN1, MOTOR_FRONT_LEFT_IN2, 0,1.0}, // Front left, index=1
    {MOTOR_BACK_RIGHT_ENA, MOTOR_BACK_RIGHT_IN1, MOTOR_BACK_RIGHT_IN2, 0,1.0}, // Back right, index=2
    {MOTOR_BACK_LEFT_ENA, MOTOR_BACK_LEFT_IN1, MOTOR_BACK_LEFT_IN2, 0, 1.0}     // Back left, index=3
};

int vehicle_speed = 0;

/* Requires MAX_VEHICLE_SPEED defined in custom.h.
 * increase_vehicle_speed()  -> increase magnitude by 1 (clamped to MAX_VEHICLE_SPEED)
 * decrease_vehicle_speed()  -> decrease magnitude by 1 (clamped to 0)
 * Both preserve the current direction (sign) of vehicle_speed.
 */
static inline void increase_vehicle_speed(void)
{
    if (vehicle_speed < MAX_VEHICLE_SPEED) {
            vehicle_speed++;
    }
}

static inline void decrease_vehicle_speed(void)
{
    if (vehicle_speed > 0)
    {
        vehicle_speed--;
    }
}

// Create a virtual file to write the json report
static char json_response[JSON_BUFFER_SIZE]; // Adjust size as needed

void httpd_init(void);

static absolute_time_t wifi_connected_time;

// store current and previous commands:
// last_commands[0] = current, last_commands[1] = previous
static CommandType last_commands[2] = {CMD_NONE, CMD_NONE};

#define last_command (last_commands[0])
#define prev_command (last_commands[1])

static inline void push_command(CommandType cmd)
{
    last_commands[1] = last_commands[0];
    last_commands[0] = cmd;
}

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
static size_t get_mac_ascii(int idx, size_t chr_off, size_t chr_len, char *dest_in)
{
    static const char hexchr[16] = "0123456789ABCDEF";
    uint8_t mac[6];
    char *dest = dest_in;
    assert(chr_off + chr_len <= (2 * sizeof(mac)));
    cyw43_hal_get_mac(idx, mac);
    for (; chr_len && (chr_off >> 1) < sizeof(mac); ++chr_off, --chr_len)
    {
        *dest++ = hexchr[mac[chr_off >> 1] >> (4 * (1 - (chr_off & 1))) & 0xf];
    }
    return dest - dest_in;
}

CommandType get_command_enum(const char *command)
{
    if (strcmp(command, "FLT") == 0)
        return CMD_FLT;
    if (strcmp(command, "FRT") == 0)
        return CMD_FRT;
    if (strcmp(command, "FWD") == 0)
        return CMD_FWD;
    if (strcmp(command, "LFT") == 0)
        return CMD_LFT;
    if (strcmp(command, "RGT") == 0)
        return CMD_RGT;
    if (strcmp(command, "BLT") == 0)
        return CMD_BLT;
    if (strcmp(command, "BWD") == 0)
        return CMD_BWD;
    if (strcmp(command, "BRT") == 0)
        return CMD_BRT;
    if (strcmp(command, "STP") == 0)
        return CMD_STOP;
    return CMD_NONE; // Default if no match
}

// Setup pwms
void setup_pwms()
{
    for (int i = 0; i < NUM_OF_WHEELS; i++)
    {
        // Set PWM function for ENA pins (NO NEED for gpio_init or set_dir)
        gpio_set_function(wheels[i].en_pin, GPIO_FUNC_PWM);
        uint slice = pwm_gpio_to_slice_num(wheels[i].en_pin);
        pwm_set_wrap(slice, 1000);
        pwm_set_enabled(slice, true);

        // Initialize IN1 and IN2 pins as OUTPUT (needed for direction control)
        gpio_init(wheels[i].in1_pin);
        gpio_set_dir(wheels[i].in1_pin, GPIO_OUT);
        gpio_put(wheels[i].in1_pin, 0); // Set to LOW initially

        gpio_init(wheels[i].in2_pin);
        gpio_set_dir(wheels[i].in2_pin, GPIO_OUT);
        gpio_put(wheels[i].in2_pin, 0); // Set to LOW initially
    }
}

// Helpers to classify commands
static inline int is_forward_cmd(CommandType c)
{
    return (c == CMD_FWD || c == CMD_FLT || c == CMD_FRT);
}

static inline int is_backward_cmd(CommandType c)
{
    return (c == CMD_BWD || c == CMD_BLT || c == CMD_BRT);
}

// Update the vehicle direction and speed according to command history
// Implements:
// - If the current command equals the previous command -> increase speed magnitude by 1 (cap 10)
// - If the current command is NONE and previous was a movement -> decrease speed magnitude by 1 toward 0
// - If a new non-NONE command arrives -> start at magnitude 1 in the appropriate direction
// The function also maps commands to individual wheel speeds.
void update_vehicle()
{
    CommandType cmd = last_command;
    CommandType prev = prev_command;

    
    // Handle immediate STOP
    if (cmd == CMD_STOP)
    {
        vehicle_speed = 0;
    }
    else if (cmd == CMD_NONE)
    {
        // Ramp down toward 0 if coming from a previous command
        if (prev != CMD_NONE)
        {
            // do nothing - keep the speed the same
        }
        else
        {
            decrease_vehicle_speed(); // ramp down if no previous command
        }
    }
    else
    {
        // Non-NONE command incoming
        if (cmd == prev)
        {
            // Repeat of the same command -> accelerate in current direction
            increase_vehicle_speed();
        }
        else
        {
            vehicle_speed = 1; // start at magnitude 1 for new command

            // update the forward/backward direction of each pin
            switch (cmd)
            {
            case CMD_FWD:
                wheels[0].coef = 1.0;  // front right
                wheels[2].coef = 1.0;  // back right
                wheels[1].coef = 1.0; // front left
                wheels[3].coef = 1.0; // back left
                break;
            case CMD_BWD:
                wheels[0].coef = -1.0;  // front right
                wheels[2].coef = -1.0;  // back right
                wheels[1].coef = -1.0; // front left
                wheels[3].coef = -1.0; // back left
                break;
            case CMD_FLT:
                wheels[0].coef = 0.5;  // front right
                wheels[2].coef = 0.5;  // back right
                wheels[1].coef = 1; // front left
                wheels[3].coef = 1; // back left
                break;
            case CMD_FRT:
                wheels[0].coef = 1.0;  // front right
                wheels[2].coef = 1.0;  // back right
                wheels[1].coef = 0.5; // front left
                wheels[3].coef = 0.5; // back left
                break;
            case CMD_BLT:
                wheels[0].coef = -0.5;  // front right
                wheels[2].coef = -0.5;  // back right
                wheels[1].coef = -1.0; // front left
                wheels[3].coef = -1.0; // back left
                break;
            case CMD_BRT:
                wheels[0].coef = -1.0;  // front right
                wheels[2].coef = -1.0;  // back right
                wheels[1].coef = -0.5; // front left
                wheels[3].coef = -0.5; // back left
                break;
            case CMD_LFT:
                wheels[0].coef = -1.0;  // front right
                wheels[2].coef = -1.0;  // back right
                wheels[1].coef = 1.0; // front left
                wheels[3].coef = 1.0; // back left
                break;
            case CMD_RGT:
                wheels[0].coef = 1.0;  // front right
                wheels[2].coef = 1.0;  // back right
                wheels[1].coef = -1.0; // front left
                wheels[3].coef = -1.0; // back left
                break;
            default:
                wheels[0].coef = 1.0;  // front right
                wheels[2].coef = 1.0;  // back right
                wheels[1].coef = 1.0; // front left
                wheels[3].coef = 1.0; // back left
                break;
            }
        }
    }



    // Apply to hardware: direction pins and PWM
    for (int i = 0; i < NUM_OF_WHEELS; i++)
    {
        
        if (wheels[i].coef > 0)
        { // Forward
            gpio_put(wheels[i].in1_pin, 1);
            gpio_put(wheels[i].in2_pin, 0);
        }
        else if (wheels[i].coef < 0)
        { // Backward
            gpio_put(wheels[i].in1_pin, 0);
            gpio_put(wheels[i].in2_pin, 1);
        }

        wheels[i].speed = (int)(vehicle_speed * abs(wheels[i].coef));

        int duty = (wheels[i].speed * 1000) / 10; // negative values handled by sign of speed
        // pwm_set_gpio_level expects unsigned duty; ensure sign handled by direction pins
        if (duty < 0)
            duty = -duty;
        pwm_set_gpio_level(wheels[i].en_pin, duty);
    }
}

static const char *cgi_control(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{

    // Fail-safe parsing: default to CMD_NONE and only set if a valid, non-empty value is found.
    CommandType command = CMD_NONE;

    if (iNumParams > 0 && pcParam != NULL && pcValue != NULL)
    {
        for (int i = 0; i < iNumParams; i++)
        {
            const char *param = pcParam[i];
            const char *value = pcValue[i];
            if (param == NULL || value == NULL)
                continue;
            if (strcmp(param, "command") == 0 && value[0] != '\0')
            {
                CommandType parsed = get_command_enum(value);
                if (parsed != CMD_NONE)
                {
                    command = parsed; // only accept known commands
                }
                break;
            }
        }
    }

    push_command(command);
    update_vehicle();

    // Update JSON response
    snprintf(json_response, JSON_BUFFER_SIZE,
             "{\"status\":1, \"command\":\"%d\", \"vehicle_speed\":\"%d\"}", command,
             vehicle_speed);

    // Log received parameters for debugging
    printf("Command received. iIndex:%d iNumParams:%d pcParam1:%s pcValue1:%s\n", iIndex,
           iNumParams, pcParam[0], pcValue[0]);
    printf("Vehicle new status. Speed: %d, last_command: %d\n", vehicle_speed, last_command);

    return "/json_response";
}

int fs_open_custom(struct fs_file *file, const char *name)
{
    if (strcmp(name, "/json_response") == 0)
    {
        printf("Read json_response file\n");
        extern char json_response[];       // Reference the global variable
        file->data = json_response;        // Set file data to the virtual file string
        file->len = strlen(json_response); // Set file length
        file->index = 0;                   // Start reading from the beginning
        return 1;                          // Success
    }
    return 0; // File not found
}

int fs_read_custom(struct fs_file *file, char *buffer, int count)
{
    // Ensure the file is valid and points to the virtual file
    if (!file || file->data != json_response)
    {
        printf("Error: Invalid file or not a virtual file\n");
        return -1; // Return error
    }

    // Calculate the remaining bytes to read
    int available = file->len - file->index;
    if (available <= 0)
    {
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

void fs_close_custom(struct fs_file *file)
{
    if (file && file->data == json_response)
    {
        // Clear the contents of the virtual_file
        memset(json_response, 0, sizeof(json_response));
        printf("Cleared virtual file contents.\n");
    }

    // Log closure for debugging
    printf("Closed virtual file: %p\n", file);
}

static tCGI cgi_handlers[] = {{"/control.cgi", cgi_control}};

int main()
{
    stdio_init_all();

    // Initialize all wheels
    setup_pwms();

    // Wait some seconds before starting the web server
    sleep_ms(5000);

    if (cyw43_arch_init())
    {
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
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK,
                                           30000))
    {
        printf("failed to connect.\n");
        exit(1);
    }
    else
    {
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
    mdns_resp_add_service(&cyw43_state.netif[CYW43_ITF_STA], "pico_httpd", "_http", DNSSD_PROTO_TCP,
                          80, srv_txt, NULL);
#else
    mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_STA], hostname, 60);
    mdns_resp_add_service(&cyw43_state.netif[CYW43_ITF_STA], "pico_httpd", "_http", DNSSD_PROTO_TCP,
                          80, 60, srv_txt, NULL);
#endif
    cyw43_arch_lwip_end();
#endif
    // setup http server
    cyw43_arch_lwip_begin();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    httpd_init();
    cyw43_arch_lwip_end();

    while (true)
    {
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
