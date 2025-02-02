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


