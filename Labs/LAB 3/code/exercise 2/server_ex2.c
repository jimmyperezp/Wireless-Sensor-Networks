/*
 * LAB3 - Exercise 2: SERVER
 *  Created on: Nov 3, 2026
 *  Author: Jaime Pérez
 *  
 *  The server receives periodical data including temperature and humidity from the client.
 *  
 */


#include "contiki.h"
#include <stdio.h> /* For printf() */
#include "leds.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uiplib.h"

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

static struct simple_udp_connection udp_conn; //Identifies the socket that will be later used.


/*---------------------------------------------------------------------------*/
PROCESS(udp_server_process, "Server process");
AUTOSTART_PROCESSES(&udp_server_process);


/*--------------------Callback definitions------------------------------------*/
static void udp_rx_callback(struct simple_udp_connection *c,
                    const uip_ipaddr_t *source_addr,
                    uint16_t source_port,
                    const uip_ipaddr_t *dest_addr,
                    uint16_t dest_port,
                    const uint8_t *data, uint16_t datalen)
{
    // (Timestamp in seconds, as in Lab 2) + origin of the measurement
    printf("\n(Timestamp: %d) Sensor data from ", (int)(clock_time()/1000));
    uiplib_ipaddr_print(source_addr);
    printf(" -> %s\n",(char*)data);

    leds_toggle(LEDS_GREEN); // Green LED = received message.
}
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{

  PROCESS_BEGIN();
  printf("Hello, welcome to LAB 3 (Exercise 2 - Server)\n");

  //Only done in the server as only 1 root device is needed.
  NETSTACK_ROUTING.root_start();

  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, udp_rx_callback);

  // Give the control back to the scheduler. Everything is done in the callback.
  while(1) {
      PROCESS_YIELD();
  }

  PROCESS_END();
}
