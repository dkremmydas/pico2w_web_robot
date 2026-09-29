/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom.h"
#include "dhcpserver.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "lwip/ip4_addr.h"
#include "lwip/prot/ip4.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
// #include "lwip/apps/mdns.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/init.h"
#include "wifi_ap_filter.h"

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
    int duty;     // Ramp PWM duty (0-1000), applied once any kick-start ends
    int dir;      // Direction currently applied: 1 forward, -1 backward, 0 stopped
} Wheel;

Wheel wheels[] = {
    {MOTOR_FRONT_RIGHT_ENA, MOTOR_FRONT_RIGHT_IN1, MOTOR_FRONT_RIGHT_IN2,0,1.0},                                                                   // Front right, index=0
    {MOTOR_FRONT_LEFT_ENA, MOTOR_FRONT_LEFT_IN1, MOTOR_FRONT_LEFT_IN2, 0,1.0}, // Front left, index=1
    {MOTOR_BACK_RIGHT_ENA, MOTOR_BACK_RIGHT_IN1, MOTOR_BACK_RIGHT_IN2, 0,1.0}, // Back right, index=2
    {MOTOR_BACK_LEFT_ENA, MOTOR_BACK_LEFT_IN1, MOTOR_BACK_LEFT_IN2, 0, 1.0}     // Back left, index=3
};

int vehicle_speed = 0;

// Motor tuning, adjustable at runtime from the web page's settings panel via
// /config.cgi. Starts from the custom.h defaults and resets to them on reboot.
typedef struct
{
    int max_speed;      // Number of ramp steps to full power (MAX_VEHICLE_SPEED)
    int min_duty;       // Duty at the first ramp step (MOTOR_MIN_DUTY)
    int pivot_min_duty; // Same, for LFT/RGT pivot turns (MOTOR_PIVOT_MIN_DUTY)
    int kick_ms;        // Kick-start length, 0 = off (MOTOR_KICK_MS)
    int kick_duty;      // Kick-start duty (MOTOR_KICK_DUTY)
} MotorConfig;

static MotorConfig config = {MAX_VEHICLE_SPEED, MOTOR_MIN_DUTY, MOTOR_PIVOT_MIN_DUTY, MOTOR_KICK_MS,
                             MOTOR_KICK_DUTY};

/* increase_vehicle_speed()  -> increase magnitude by 1 (clamped to config.max_speed)
 * decrease_vehicle_speed()  -> decrease magnitude by 1 (clamped to 0)
 * Both preserve the current direction (sign) of vehicle_speed.
 */
static inline void increase_vehicle_speed(void)
{
    if (vehicle_speed < config.max_speed) {
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

// Separate virtual file for /config.cgi, so a settings update can't clobber a
// control.cgi reply that's still being sent (they poll every 300 ms).
static char config_response[JSON_BUFFER_SIZE];

void httpd_init(void);

static absolute_time_t wifi_connected_time;

// Timestamp of the last received /control.cgi request (including idle "NON"
// polling); used by main()'s watchdog to force-stop the motors if the client
// disappears (WiFi drop, closed tab, crash) while still commanding movement.
static absolute_time_t last_command_time;

// Hands out DHCP leases to clients when running as our own access point.
static dhcp_server_t dhcp_server;

// Set once at boot; read by wifi_ap_ip4_input_filter() to only filter
// traffic while actually running as our own access point.
static bool ap_mode_active = false;

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
        // 1 MHz counter / 1000 steps = 1 kHz PWM. Without a divider the counter
        // runs at the full system clock (~150 kHz PWM), far faster than the
        // L298N can switch, so the motors got almost no power.
        pwm_set_clkdiv(slice, clock_get_hz(clk_sys) / 1000000.0f);
        pwm_set_wrap(slice, 999);
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

// Kick-start state (see MOTOR_KICK_MS in custom.h): while active, kicked
// wheels run at the kick duty and end_motor_kick() later drops every wheel to
// its ramp duty.
static volatile bool kick_active = false;

// Duty for the lowest non-zero speed of the current motion: config.min_duty,
// or config.pivot_min_duty while pivoting in place.
static int min_duty = MOTOR_MIN_DUTY;
static alarm_id_t kick_alarm;

static int64_t end_motor_kick(alarm_id_t id, void *user_data)
{
    (void)id;
    (void)user_data;
    kick_active = false;
    for (int i = 0; i < NUM_OF_WHEELS; i++)
    {
        pwm_set_gpio_level(wheels[i].en_pin, wheels[i].duty);
    }
    return 0; // one-shot
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
            min_duty = (cmd == CMD_LFT || cmd == CMD_RGT) ? config.pivot_min_duty : config.min_duty;

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
                // Turn front-left: left side slower than right side (pivots toward the left).
                wheels[0].coef = 1.0;  // front right
                wheels[2].coef = 1.0;  // back right
                wheels[1].coef = 0.5; // front left
                wheels[3].coef = 0.5; // back left
                break;
            case CMD_FRT:
                // Turn front-right: right side slower than left side (pivots toward the right).
                wheels[0].coef = 0.5;  // front right
                wheels[2].coef = 0.5;  // back right
                wheels[1].coef = 1; // front left
                wheels[3].coef = 1; // back left
                break;
            case CMD_BLT:
                // Turn back-left (reversing): left side slower than right side.
                wheels[0].coef = -1.0;  // front right
                wheels[2].coef = -1.0;  // back right
                wheels[1].coef = -0.5; // front left
                wheels[3].coef = -0.5; // back left
                break;
            case CMD_BRT:
                // Turn back-right (reversing): right side slower than left side.
                wheels[0].coef = -0.5;  // front right
                wheels[2].coef = -0.5;  // back right
                wheels[1].coef = -1.0; // front left
                wheels[3].coef = -1.0; // back left
                break;
            case CMD_LFT:
                // Pivot left in place: left side backward, right side forward.
                wheels[0].coef = 1.0;  // front right
                wheels[2].coef = 1.0;  // back right
                wheels[1].coef = -1.0; // front left
                wheels[3].coef = -1.0; // back left
                break;
            case CMD_RGT:
                // Pivot right in place: right side backward, left side forward.
                wheels[0].coef = -1.0;  // front right
                wheels[2].coef = -1.0;  // back right
                wheels[1].coef = 1.0; // front left
                wheels[3].coef = 1.0; // back left
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
    bool start_kick = false;
    for (int i = 0; i < NUM_OF_WHEELS; i++)
    {
        int dir = vehicle_speed == 0 ? 0 : (wheels[i].coef > 0 ? 1 : -1);

        if (vehicle_speed == 0)
        { // Stopped: release both direction pins so the board's L1-L4 LEDs go
          // dark (the motor coasts either way, since ENA/ENB is at 0 too)
            gpio_put(wheels[i].in1_pin, 0);
            gpio_put(wheels[i].in2_pin, 0);
        }
        else if (wheels[i].coef > 0)
        { // Forward
            gpio_put(wheels[i].in1_pin, 1);
            gpio_put(wheels[i].in2_pin, 0);
        }
        else if (wheels[i].coef < 0)
        { // Backward
            gpio_put(wheels[i].in1_pin, 0);
            gpio_put(wheels[i].in2_pin, 1);
        }

        wheels[i].speed = (int)(vehicle_speed * fabsf(wheels[i].coef)); // fabsf: abs() would truncate 0.5 to 0

        // Fraction of full speed for this wheel (direction is set by IN1/IN2 above).
        // Computed from the float coef so a 0.5 inner wheel at speed 1 still moves.
        float level = fabsf(vehicle_speed * wheels[i].coef) / config.max_speed;
        int duty = 0;
        if (level > 0.0f)
        {
            if (level > 1.0f)
                level = 1.0f;
            // Start at min_duty so the motor turns instead of buzzing
            duty = min_duty + (int)((1000 - min_duty) * level);
        }

        // Kick a wheel that's starting from rest or reversing
        bool kick = config.kick_ms > 0 && duty > 0 && dir != wheels[i].dir;
        wheels[i].duty = duty;
        wheels[i].dir = dir;
        if (kick)
        {
            pwm_set_gpio_level(wheels[i].en_pin, duty > config.kick_duty ? duty : config.kick_duty);
            start_kick = true;
        }
        else if (!kick_active || duty == 0)
        {
            pwm_set_gpio_level(wheels[i].en_pin, duty);
        }
        // else: a kick is running, end_motor_kick() applies this duty
    }

    if (start_kick)
    {
        if (kick_active)
        {
            cancel_alarm(kick_alarm); // restart the window for the newly kicked wheels
        }
        kick_active = true;
        kick_alarm = add_alarm_in_ms(config.kick_ms, end_motor_kick, NULL, true);
    }
}

static const char *cgi_control(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    last_command_time = get_absolute_time();

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
    if (iNumParams > 0 && pcParam != NULL && pcValue != NULL)
    {
        printf("Command received. iIndex:%d iNumParams:%d pcParam1:%s pcValue1:%s\n", iIndex,
               iNumParams, pcParam[0], pcValue[0]);
    }
    else
    {
        printf("Command received. iIndex:%d iNumParams:%d (no params)\n", iIndex, iNumParams);
    }
    printf("Vehicle new status. Speed: %d, last_command: %d\n", vehicle_speed, last_command);

    return "/json_response";
}

// Parses a non-empty, all-digit CGI value into *out if it's within
// [min, max]; returns false (leaving *out untouched) otherwise.
static bool parse_config_value(const char *value, int min, int max, int *out)
{
    char *end;
    long v = strtol(value, &end, 10);
    if (value[0] == '\0' || *end != '\0' || v < min || v > max)
    {
        return false;
    }
    *out = (int)v;
    return true;
}

// /config.cgi?max_speed=15&min_duty=300&... : sets any of the motor tuning
// values given (out-of-range or malformed ones are ignored and reported), and
// replies with the full current config. With no parameters it just reads it.
static const char *cgi_config(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    (void)iIndex;
    static const struct
    {
        const char *name;
        int min, max;
        size_t offset;
    } fields[] = {
        {"max_speed", 1, 100, offsetof(MotorConfig, max_speed)},
        {"min_duty", 0, 1000, offsetof(MotorConfig, min_duty)},
        {"pivot_min_duty", 0, 1000, offsetof(MotorConfig, pivot_min_duty)},
        {"kick_ms", 0, 1000, offsetof(MotorConfig, kick_ms)},
        {"kick_duty", 0, 1000, offsetof(MotorConfig, kick_duty)},
    };

    int rejected = 0;
    for (int i = 0; i < iNumParams; i++)
    {
        if (pcParam[i] == NULL || pcValue[i] == NULL)
            continue;
        for (size_t f = 0; f < LWIP_ARRAYSIZE(fields); f++)
        {
            if (strcmp(pcParam[i], fields[f].name) == 0)
            {
                int *target = (int *)((char *)&config + fields[f].offset);
                if (!parse_config_value(pcValue[i], fields[f].min, fields[f].max, target))
                {
                    printf("Config: rejected %s=%s (allowed %d-%d)\n", fields[f].name, pcValue[i],
                           fields[f].min, fields[f].max);
                    rejected++;
                }
                break;
            }
        }
    }

    if (vehicle_speed > config.max_speed)
    {
        vehicle_speed = config.max_speed; // new ramp is shorter than the current speed
    }

    snprintf(config_response, JSON_BUFFER_SIZE,
             "{\"status\":%d, \"max_speed\":%d, \"min_duty\":%d, \"pivot_min_duty\":%d, "
             "\"kick_ms\":%d, \"kick_duty\":%d}",
             rejected == 0 ? 1 : 0, config.max_speed, config.min_duty, config.pivot_min_duty,
             config.kick_ms, config.kick_duty);
    printf("Config: %s\n", config_response);

    return "/config_response";
}

// Maps a virtual file name to its backing buffer, or NULL if it isn't one.
static char *virtual_file_buffer(const char *name)
{
    if (strcmp(name, "/json_response") == 0)
        return json_response;
    if (strcmp(name, "/config_response") == 0)
        return config_response;
    return NULL;
}

int fs_open_custom(struct fs_file *file, const char *name)
{
    char *buffer = virtual_file_buffer(name);
    if (buffer == NULL)
    {
        // httpd asks here first for every request, so this logs each file served.
        printf("HTTP request: %s\n", name);
        return 0; // Not a virtual file
    }

    printf("Read %s file\n", name);
    file->data = buffer;        // Set file data to the virtual file string
    file->len = strlen(buffer); // Set file length
    file->index = 0;            // Start reading from the beginning
    return 1;                   // Success
}

int fs_read_custom(struct fs_file *file, char *buffer, int count)
{
    // Ensure the file is valid and points to a virtual file
    if (!file || (file->data != json_response && file->data != config_response))
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
    memcpy(buffer, file->data + file->index, to_read);

    // Update the file's read index
    file->index += to_read;
    printf("Read %d bytes from virtual file.\n", to_read);

    return to_read; // Return the number of bytes read
}

void fs_close_custom(struct fs_file *file)
{
    if (file && (file->data == json_response || file->data == config_response))
    {
        // Clear the contents of the virtual_file
        memset((char *)file->data, 0, JSON_BUFFER_SIZE);
        printf("Cleared virtual file contents.\n");
    }

    // Log closure for debugging
    printf("Closed virtual file: %p\n", file);
}

static tCGI cgi_handlers[] = {{"/control.cgi", cgi_control}, {"/config.cgi", cgi_config}};

// Reads WIFI_MODE_SWITCH_PIN to decide which network to start at boot.
// Floating/high (internal pull-up, no switch wired yet) -> access point
// mode (the default); pulled to GND -> station mode (join the home WiFi).
static bool select_ap_mode(void)
{
    gpio_init(WIFI_MODE_SWITCH_PIN);
    gpio_set_dir(WIFI_MODE_SWITCH_PIN, GPIO_IN);
    gpio_pull_up(WIFI_MODE_SWITCH_PIN);
    sleep_ms(10); // let the pull-up settle before sampling
    return gpio_get(WIFI_MODE_SWITCH_PIN);
}

// The client currently holding the access-point "control lock" (see
// wifi_ap_ip4_input_filter below). ip4_addr_isany_val(active_client_ip)
// means no one currently holds it.
static ip4_addr_t active_client_ip;
static absolute_time_t active_client_last_seen;

// Wired in via LWIP_HOOK_IP4_INPUT (see wifi_ap_filter.h). In access-point
// mode, enforces a "first client wins" lock: whichever client's traffic we
// see first is remembered as active_client_ip, and any *different* source
// IP is dropped before it reaches DHCP/TCP/UDP processing - e.g. a second
// device that joined and set itself a static IP. The lock is released after
// WIFI_AP_CLIENT_LOCK_TIMEOUT_MS of silence from the active client, letting
// a different one take over (phone locked, tab closed, etc). A no-op in
// station mode, and DHCP's own initial (0.0.0.0-sourced) broadcasts are
// always let through so a client can actually get a lease.
int wifi_ap_ip4_input_filter(struct pbuf *p, struct netif *inp)
{
    (void)inp;
    if (!ap_mode_active || p->len < sizeof(struct ip_hdr))
    {
        return 0;
    }

    const struct ip_hdr *iphdr = (const struct ip_hdr *)p->payload;
    ip4_addr_t src;
    ip4_addr_copy(src, iphdr->src);

    if (ip4_addr_isany_val(src))
    {
        return 0; // allow an unconfigured client's DHCP broadcast through
    }

    absolute_time_t now = get_absolute_time();
    bool lock_held = !ip4_addr_isany_val(active_client_ip);
    bool lock_expired = lock_held && absolute_time_diff_us(active_client_last_seen, now) >
                                          WIFI_AP_CLIENT_LOCK_TIMEOUT_MS * 1000;

    if (lock_held && !lock_expired && !ip4_addr_cmp(&src, &active_client_ip))
    {
        // Log at most once every 2 s, a blocked client retries constantly
        static absolute_time_t last_drop_log;
        if (absolute_time_diff_us(last_drop_log, now) > 2000 * 1000)
        {
            last_drop_log = now;
            char held[IP4ADDR_STRLEN_MAX];
            ip4addr_ntoa_r(&active_client_ip, held, sizeof(held));
            printf("AP lock: ignoring %s, control held by %s\n", ip4addr_ntoa(&src), held);
        }
        pbuf_free(p);
        return 1; // a different client already holds the lock: dropped
    }

    // No one holds the lock, the previous holder timed out, or this is the
    // current holder checking back in - (re)claim it for this source IP.
    if (!ip4_addr_cmp(&src, &active_client_ip))
    {
        printf("AP lock: control now held by %s\n", ip4addr_ntoa(&src));
    }
    ip4_addr_copy(active_client_ip, src);
    active_client_last_seen = now;
    return 0;
}

#if MOTOR_PIN_TEST
// See MOTOR_PIN_TEST in custom.h. Wheel index -> board/LED: wheels 0/1 are the
// front board (MOTORA/MOTORB), 2/3 the back board, matching the wheels[] table.
static void run_motor_pin_test(void)
{
    static const char *board[] = {"front", "front", "back", "back"};
    static const char *led_in1[] = {"L1 (IN1)", "L3 (IN3)", "L1 (IN1)", "L3 (IN3)"};
    static const char *led_in2[] = {"L2 (IN2)", "L4 (IN4)", "L2 (IN2)", "L4 (IN4)"};

    for (int i = 0; i < NUM_OF_WHEELS; i++)
    {
        pwm_set_gpio_level(wheels[i].en_pin, 0);
        gpio_put(wheels[i].in1_pin, 0);
        gpio_put(wheels[i].in2_pin, 0);
    }

    while (true)
    {
        printf("\n=== PIN TEST: all IN pins LOW - every L1-L4 LED should be OFF ===\n");
        sleep_ms(3000);
        for (int i = 0; i < NUM_OF_WHEELS; i++)
        {
            for (int n = 0; n < 2; n++)
            {
                uint pin = n == 0 ? wheels[i].in1_pin : wheels[i].in2_pin;
                const char *led = n == 0 ? led_in1[i] : led_in2[i];
                printf("PIN TEST: GP%u HIGH -> only %s on the %s board should be ON\n", pin, led, board[i]);
                gpio_put(pin, 1);
                sleep_ms(2000);
                gpio_put(pin, 0);
            }
        }
    }
}
#endif

int main()
{
    stdio_init_all();

    // Initialize all wheels
    setup_pwms();

    // Wait some seconds before starting the web server
    sleep_ms(5000);

#if MOTOR_PIN_TEST
    run_motor_pin_test(); // never returns
#endif

    if (cyw43_arch_init())
    {
        printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    if (select_ap_mode())
    {
        ap_mode_active = true;
        printf("WIFI_MODE_SWITCH_PIN not grounded (default), starting our own access point '%s'\n",
               WIFI_AP_SSID);
        cyw43_arch_enable_ap_mode(WIFI_AP_SSID, WIFI_AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

        ip4_addr_t ap_ip, ap_mask;
        IP4_ADDR(&ap_ip, WIFI_AP_IP_A, WIFI_AP_IP_B, WIFI_AP_IP_C, WIFI_AP_IP_D);
        IP4_ADDR(&ap_mask, 255, 255, 255, 0);
        dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &ap_ip, &ap_mask);

        printf("\nReady, running httpd. Connect to '%s' and browse to %s\n", WIFI_AP_SSID,
               ip4addr_ntoa(&ap_ip));
    }
    else
    {
        printf("WIFI_MODE_SWITCH_PIN grounded, joining home WiFi instead\n");
        cyw43_arch_enable_sta_mode();

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
    }

    // start http server
    wifi_connected_time = get_absolute_time();

    // setup http server
    cyw43_arch_lwip_begin();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    httpd_init();
    cyw43_arch_lwip_end();

    last_command_time = get_absolute_time();

    while (true)
    {
        // Safety watchdog: if we're still commanded to move but haven't heard
        // from a client in a while (WiFi drop, closed tab, crash), stop.
        if (vehicle_speed != 0 &&
            absolute_time_diff_us(last_command_time, get_absolute_time()) > COMMAND_TIMEOUT_MS * 1000)
        {
            printf("No command received for %d ms, stopping motors.\n", COMMAND_TIMEOUT_MS);
            push_command(CMD_STOP);
            update_vehicle();
        }

#if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(led_time);
#else
        sleep_ms(200);
#endif
    }
#if LWIP_MDNS_RESPONDER
    mdns_resp_remove_netif(&cyw43_state.netif[CYW43_ITF_STA]);
#endif
    cyw43_arch_deinit();
}
