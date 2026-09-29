#define LWIP_HTTPD_CGI 1  // Enable CGI (Common Gateway Interface) support
#define LWIP_HTTPD_SSI 0
#define LWIP_HTTPD_SUPPORT_POST 0
#define LWIP_MDNS_RESPONDER 0
#define LWIP_HTTPD_SSI_MULTIPART 1

#define LWIP_HTTPD_SSI_INCLUDE_TAG 0

#define HTTPD_FSDATA_FILE "pico_fsdata.inc"
#define LWIP_HTTPD_DYNAMIC_FILE_READ  1
#define LWIP_HTTPD_DYNAMIC_HEADERS 1
#define LWIP_HTTPD_CUSTOM_FILES 1

#define JSON_BUFFER_SIZE 1024

//LWIP_DBG_OFF LWIP_DBG_OFF
#define LWIP_DEBUG LWIP_DBG_OFF
#define HTTPD_DEBUG LWIP_DBG_OFF
#define LWIP_DBG_MIN_LEVEL LWIP_DBG_LEVEL_SERIOUS
#define LWIP_DBG_TYPES_ON LWIP_DBG_ON

/*
 * 
 * 
 *  Motor controller 1
 *    - Front Left wheel
 *      + ENA = GP6 (9)
 *      + IN1 = GP7 (10)
 *      + IN2 = GP8 (11)
 *    
 *    - Front Right wheel
 *      + ENB = GP2 (4) 
 *      + ENA = GP3 (5)
 *      + ENA = GP4 (6)
 *
 * 
 *  Motor controller 2
 *    - Back Left wheel
 *      + ENA = GP13 (17) 
 *      + IN1 = GP14 (18)
 *      + IN2 = GP15 (19)
 * 
 *    - Back Right wheel
 *      + ENB = GP10 (14) 
 *      + IN1 = GP11 (15)
 *      + IN2 = GP12 (16)
 * 
 * 
 *  Project References
 *   - Explain how to connect the L298N motrol controller (improved version), https://www.youtube.com/watch?v=dyjo_ggEtVU
 * 
 */

#define NUM_OF_WHEELS 4

#define MAX_VEHICLE_SPEED 10  // Maximum speed magnitude

// PWM duty (out of 1000) for the lowest non-zero speed. Below roughly this the
// motors can't overcome friction and just buzz at the PWM frequency, so speed
// 1..MAX_VEHICLE_SPEED is mapped onto MOTOR_MIN_DUTY..1000 instead of 0..1000.
// Raise it if the wheels still hesitate at the start, lower it for gentler
// starts (a higher-voltage motor supply needs less).
#define MOTOR_MIN_DUTY 500

// Kick-start: a wheel starting from rest (or reversing) is driven at full duty
// for this long before settling to its ramp duty. Breaking static friction
// takes far more torque than keeping a wheel turning, so this gets all wheels
// moving together instead of some sitting stalled and buzzing. 0 disables it.
#define MOTOR_KICK_MS 150

// Wiring check: set to 1 to boot into a loop (no WiFi) that raises each
// IN1-IN4 pin on its own for 2 s, printing which one over USB serial, so you
// can watch the matching L1-L4 LED on each L298N. ENA/ENB stay at 0, so the
// motors don't move. Set back to 0 for normal operation.
#define MOTOR_PIN_TEST 0

#define COMMAND_TIMEOUT_MS 1000  // Stop motors if no command received within this long

#define MOTOR_FRONT_RIGHT_ENA 2
#define MOTOR_FRONT_RIGHT_IN1 3
#define MOTOR_FRONT_RIGHT_IN2 4

#define MOTOR_FRONT_LEFT_ENA 6
#define MOTOR_FRONT_LEFT_IN1 7
#define MOTOR_FRONT_LEFT_IN2 8

#define MOTOR_BACK_RIGHT_ENA 10
#define MOTOR_BACK_RIGHT_IN1 11
#define MOTOR_BACK_RIGHT_IN2 12

#define MOTOR_BACK_LEFT_ENA 13
#define MOTOR_BACK_LEFT_IN1 14
#define MOTOR_BACK_LEFT_IN2 15

// Selects which WiFi network to start at boot: reads as HIGH (internal
// pull-up, no switch wired yet) -> host our own access point (WIFI_AP_SSID),
// the default; pulled to GND -> join the home WiFi (WIFI_SSID) instead.
// Reserved now so a future physical toggle switch just wires into this pin
// with no firmware changes.
#define WIFI_MODE_SWITCH_PIN 20

// Access-point subnet (see main()'s AP branch in pico_httpd.c).
#define WIFI_AP_IP_A 192
#define WIFI_AP_IP_B 168
#define WIFI_AP_IP_C 4
#define WIFI_AP_IP_D 1

// How long wifi_ap_ip4_input_filter()'s "controlling client" lock persists
// with no traffic before it's released and a different client may claim it.
#define WIFI_AP_CLIENT_LOCK_TIMEOUT_MS 1000


