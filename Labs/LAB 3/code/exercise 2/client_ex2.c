/*
 * LAB3 - Exercise 2: UDP Client code (RPL node)
 *  Created on: Oct 26, 2026
 *  Author: Adrián Riveiro & Jaime Pérez
 *
 *  The client periodically measures temperature and humidity (SI7021) and
 *  sends both values to the server (root).
 */


#include "contiki.h"
#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include <string.h>
#include "leds.h"
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

#define SEND_PERIOD 5 // Seconds between measurements

static struct simple_udp_connection udp_conn; //Identifies the socket that will be later used.


/*---------------------------------------------------------------------------*/
PROCESS(sensors_setup, "Sensors setup");
PROCESS(client_process, "Client process");
AUTOSTART_PROCESSES(&sensors_setup, &client_process);


/*--------------------Callback definitions------------------------------------*/
static void udp_rx_callback(struct simple_udp_connection *c,
        const uip_ipaddr_t *source_addr,
        uint16_t source_port,
        const uip_ipaddr_t *dest_addr,
        uint16_t dest_port,
        const uint8_t *data, uint16_t datalen)
{
    // Not used in this exercise (the server does not answer), but kept for debugging
    printf("Received packet from ");
    uiplib_ipaddr_print(source_addr);
    printf(", Data: %.*s\n", (int)datalen, (char*)data);

    leds_toggle(LEDS_GREEN); //GREEN LED = received message
}
/*---------------------------------------------------------------------------*/


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
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(client_process, ev, data)
{
  static struct etimer timer;
  static uip_ipaddr_t dest_addr;
  static uint32_t hume = 0;
  static int32_t temper = 0;
  static char buf[50]; // Message sent to the server

  PROCESS_BEGIN();

  printf("Hello, welcome to LAB 3 (Exercise 2 - Client)\n");

  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&timer, CLOCK_SECOND * SEND_PERIOD);

  while(1) {

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

      if(NETSTACK_ROUTING.node_is_reachable() &&
         NETSTACK_ROUTING.get_root_ipaddr(&dest_addr)) {

          SI7021_measure(&hume, &temper);

          // %03d so that 25.045 is not shown as 25.45
          sprintf(buf, "T: %d.%03d C, H: %d.%03d %%",
                  (int)(temper/1000), (int)(temper%1000),
                  (int)(hume/1000), (int)(hume%1000));

          printf("\n(Timestamp %d) Sending to server: %s\n", (int)clock_time(), buf);
          simple_udp_sendto(&udp_conn, buf, strlen(buf) + 1, &dest_addr); // +1 to send the '\0'
          leds_toggle(LEDS_RED); //RED = message sent.
      }
      else {
          printf("Server unreachable yet...\n");
      }

      etimer_reset(&timer);
  }

  PROCESS_END();
}
