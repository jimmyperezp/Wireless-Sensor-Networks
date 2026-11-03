/*
 * LAB3 - Exercise 3: UDP Client code (RPL node)
 *  Created on: Oct 26, 2026
 *  Author: Adrián Riveiro & Jaime Pérez
 *
 *  Lab 2 (Exercise 2) application, but now the commands arrive from the server
 *  through UDP instead of the serial line. Frame format: LR | LG | ST | SH [+ N].
 *  The results (measurements, LED actions and errors) are sent back to the server.
 */


#include "contiki.h"
#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include <string.h>
#include "leds.h"
#include "etimer.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uiplib.h"
#include "efr32mg12p432f1024gl125.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "si7021.h"
#include "board_cookie.h"
#include "util.h"
#include "imu.h"
#include "icm20648.h"

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define HELLO_MSG    "HELLO"
#define HELLO_PERIOD 10  // Seconds between HELLO messages (so that the server knows this client)
#define MAX_PERIOD   30
#define FRAME_SIZE   16  // Max length of a received frame

process_event_t coordinator_event;
process_event_t udp_frame_event;  // Posted by the UDP callback when a frame arrives

static struct simple_udp_connection udp_conn; //Identifies the socket that will be later used.
static char rx_frame[FRAME_SIZE];             // Copy of the last received frame

struct command {
    char action; // Will contain R, G, H or T.
    int period;  // If the user selects a period to perform said action.
};

/*---------------------------------------------------------------------------*/
PROCESS(sensors_setup, "Sensors setup");
PROCESS(coordinator, "Coordinator process");
PROCESS(toggle_leds, "Toggle LEDs process");
PROCESS(read_sensors, "Sensors reading");
PROCESS(udp_client_process, "Client process");

AUTOSTART_PROCESSES(&sensors_setup, &coordinator, &toggle_leds, &read_sensors, &udp_client_process);


/*--------------------Auxiliary functions--------------------------------------*/
// Prints a message locally and sends it to the server (root)
static void send_to_server(char *msg)
{
    static uip_ipaddr_t server_addr;

    printf("%s\n", msg);

    if(NETSTACK_ROUTING.node_is_reachable() &&
       NETSTACK_ROUTING.get_root_ipaddr(&server_addr)) {
        simple_udp_sendto(&udp_conn, msg, strlen(msg) + 1, &server_addr); // +1 to send the '\0'
    }
    else printf("Server unreachable, message not sent\n");
}


/*--------------------Callback definitions------------------------------------*/
static void udp_rx_callback(struct simple_udp_connection *c,
        const uip_ipaddr_t *source_addr,
        uint16_t source_port,
        const uip_ipaddr_t *dest_addr,
        uint16_t dest_port,
        const uint8_t *data, uint16_t datalen)
{
    // The data buffer is overwritten by the next packet, so the frame is copied
    // into rx_frame (and '\0' terminated) before sending it to the coordinator.
    if(datalen >= FRAME_SIZE) datalen = FRAME_SIZE - 1;
    memcpy(rx_frame, data, datalen);
    rx_frame[datalen] = '\0';

    process_post(&coordinator, udp_frame_event, rx_frame);
}
/*---------------------------------------------------------------------------*/


PROCESS_THREAD(sensors_setup, ev, data)
{
    PROCESS_BEGIN();

    GPIO_PinModeSet(gpioPortF, 9, gpioModePushPull, 1); // T/H, set as out pin (PushPull)
    GPIO_PinOutSet(gpioPortF, 9);                       // T/H, enabled

    I2CSPM_Init_TypeDef i2cspmInit = I2CSPM_INIT_DEFAULT; // I2C configuration
    I2CSPM_Init(&i2cspmInit);                             // I2C configuration
    SI7021_init();                                        // TEMP/HUM Sensor config

    PROCESS_END();
}


PROCESS_THREAD(coordinator, ev, data) {

    static struct command data_to_next_process;
    static char msg[60];
    struct process *next_proc;
    long user_period;
    char *end;
    char *incoming_data;

    PROCESS_BEGIN();

    coordinator_event = process_alloc_event();
    udp_frame_event = process_alloc_event();

    while(1) {

        // Same coordinator as in Lab 2, but now waiting for the UDP frames
        PROCESS_WAIT_EVENT_UNTIL(ev == udp_frame_event);

        incoming_data = (char*)data;

        printf("\nFrame received from server: [%s]\n", incoming_data);

        // First, I check if the message is valid: It must have 2 characters and an optional number:
        if(strlen(incoming_data) < 2) {
            sprintf(msg, "[%s] too short. Expected format: LG|LR|ST|SH[+optional time]", incoming_data);
            send_to_server(msg);
            continue;
        }

        // If the length is right, now I check if the content is valid (LR, LG, ST or SH)
        if(incoming_data[0] == 'L' && (incoming_data[1] == 'R' || incoming_data[1] == 'G')) next_proc = &toggle_leds;

        else if(incoming_data[0] == 'S' && (incoming_data[1] == 'H' || incoming_data[1] == 'T')) next_proc = &read_sensors;

        else {
            sprintf(msg, "[%s] unknown. Valid messages are: LG|LR|ST|SH[+optional time]", incoming_data);
            send_to_server(msg);
            continue; // To stop running this process.
        }

        // The length and content are valid so far. Now I check if the received time is valid:
        user_period = strtol(&incoming_data[2], &end, 10);
        if((*end != '\0') || ((int)user_period < 0) || ((int)user_period > MAX_PERIOD)) {
            sprintf(msg, "[%s] invalid period. Expected a number from 0 to %d", incoming_data, MAX_PERIOD);
            send_to_server(msg);
            continue;
        }

        // If the code reaches here, the message is totally valid. Now I unpack it and send it to the right next process
        data_to_next_process.action = incoming_data[1];
        data_to_next_process.period = (int)user_period;

        process_post(next_proc, coordinator_event, &data_to_next_process);
    }
    PROCESS_END();
}


PROCESS_THREAD(toggle_leds, ev, data) {

    struct command *incoming_data;
    static struct etimer red_timer, green_timer;
    static char msg[50];

    PROCESS_BEGIN();

    while(1) {
        PROCESS_WAIT_EVENT_UNTIL(ev == coordinator_event || ev == PROCESS_EVENT_TIMER);

        if(ev == coordinator_event) {
            // New data received from coordinator. I check the target LED and its periodicity
            incoming_data = (struct command *)data;

            if(incoming_data->action == 'R') {
                leds_toggle(LEDS_RED);
                if(incoming_data->period > 0) etimer_set(&red_timer, (clock_time_t)incoming_data->period * CLOCK_SECOND);
                else etimer_stop(&red_timer);
                sprintf(msg, "Red LED toggled. Period: %d s", incoming_data->period);
                send_to_server(msg);
            }

            else if(incoming_data->action == 'G') {
                leds_toggle(LEDS_GREEN);
                if(incoming_data->period > 0) etimer_set(&green_timer, (clock_time_t)incoming_data->period * CLOCK_SECOND);
                else etimer_stop(&green_timer);
                sprintf(msg, "Green LED toggled. Period: %d s", incoming_data->period);
                send_to_server(msg);
            }
        }

        else if(ev == PROCESS_EVENT_TIMER) {
            if(data == &red_timer) {
                leds_toggle(LEDS_RED);
                etimer_reset(&red_timer);
                sprintf(msg, "(Timestamp: %d) Red LED toggled", (int)(clock_time()/1000));
                send_to_server(msg);
            }
            else if(data == &green_timer) {
                leds_toggle(LEDS_GREEN);
                etimer_reset(&green_timer);
                sprintf(msg, "(Timestamp: %d) Green LED toggled", (int)(clock_time()/1000));
                send_to_server(msg);
            }
        }
    }
    PROCESS_END();
}


PROCESS_THREAD(read_sensors, ev, data) {

    static uint32_t hume = 0;
    static int32_t temper = 0;
    static char msg[50];

    struct command *incoming_data;
    static struct etimer temp_timer, humidity_timer;

    PROCESS_BEGIN();

    while(1) {

        PROCESS_WAIT_EVENT_UNTIL(ev == coordinator_event || ev == PROCESS_EVENT_TIMER);

        if(ev == coordinator_event) {
            incoming_data = (struct command *)data;
            SI7021_measure(&hume, &temper);

            if(incoming_data->action == 'H') {
                sprintf(msg, "Humidity: %d.%03d %%", (int)(hume/1000), (int)(hume%1000));
                send_to_server(msg);
                if(incoming_data->period > 0) etimer_set(&humidity_timer, (clock_time_t)incoming_data->period * CLOCK_SECOND);
                else etimer_stop(&humidity_timer);
            }
            else if(incoming_data->action == 'T') {
                sprintf(msg, "Temperature: %d.%03d C", (int)(temper/1000), (int)(temper%1000));
                send_to_server(msg);
                if(incoming_data->period > 0) etimer_set(&temp_timer, (clock_time_t)incoming_data->period * CLOCK_SECOND);
                else etimer_stop(&temp_timer);
            }
        }

        else if(ev == PROCESS_EVENT_TIMER) {
            SI7021_measure(&hume, &temper);

            if(data == &temp_timer) {
                sprintf(msg, "(Timestamp: %d) Temperature: %d.%03d C", (int)(clock_time()/1000), (int)(temper/1000), (int)(temper%1000));
                send_to_server(msg);
                etimer_reset(&temp_timer);
            }
            else if(data == &humidity_timer) {
                sprintf(msg, "(Timestamp: %d) Humidity: %d.%03d %%", (int)(clock_time()/1000), (int)(hume/1000), (int)(hume%1000));
                send_to_server(msg);
                etimer_reset(&humidity_timer);
            }
        }
    }
    PROCESS_END();
}


// Network process: registers the UDP socket and sends a periodic HELLO,
// so that the server learns the IP address of this client.
PROCESS_THREAD(udp_client_process, ev, data)
{
    static struct etimer hello_timer;
    static uip_ipaddr_t dest_addr;

    PROCESS_BEGIN();

    printf("Hello, welcome to LAB 3 (Exercise 3 - Client)\n");

    simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);

    etimer_set(&hello_timer, CLOCK_SECOND * HELLO_PERIOD);

    while(1) {

        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&hello_timer));

        if(NETSTACK_ROUTING.node_is_reachable() &&
           NETSTACK_ROUTING.get_root_ipaddr(&dest_addr)) {
            simple_udp_sendto(&udp_conn, HELLO_MSG, sizeof(HELLO_MSG), &dest_addr);
        }
        else {
            printf("Node unreachable yet...\n");
        }

        etimer_reset(&hello_timer);
    }

    PROCESS_END();
}
