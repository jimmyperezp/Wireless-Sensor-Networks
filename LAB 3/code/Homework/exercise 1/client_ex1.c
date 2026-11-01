/*
 * LAB3 - Exercise 1: UDP Client code (RPL node)
 *  Created on: Oct 26, 2026
 *  Author: Adrián Riveiro & Jaime Pérez
 *
 *  The client periodically sends a message to the server (root) and shows in
 *  its terminal the content of the reply received from the server.
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

#define SEND_PERIOD 2 // Seconds between messages

#define MY_MESSAGE "Test UDP Client"
static struct simple_udp_connection udp_conn; //Identifies the socket that will be later used.


/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "Client process");
AUTOSTART_PROCESSES(&udp_client_process);


/*--------------------Callback definitions------------------------------------*/
static void udp_rx_callback(struct simple_udp_connection *c,
        const uip_ipaddr_t *source_addr,
        uint16_t source_port,
        const uip_ipaddr_t *dest_addr,
        uint16_t dest_port,
        const uint8_t *data, uint16_t datalen)
{
    // Reply from the server
    printf("Reply received from ");
    uiplib_ipaddr_print(source_addr);
    printf(", Data: %.*s\n", (int)datalen, (char*)data); // Only datalen characters are printed

    leds_toggle(LEDS_GREEN);
}
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer timer;
  static uip_ipaddr_t dest_addr;

  PROCESS_BEGIN();

  printf("Hello, welcome to LAB 3 (Exercise 1 - Client)\n");

  // Local port: client. Remote port: server.
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&timer, CLOCK_SECOND * SEND_PERIOD);

  while(1) {

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

      // node_is_reachable() must be CALLED (with parentheses). Without them, the
      // function pointer is evaluated, which is always true.
      if(NETSTACK_ROUTING.node_is_reachable() &&
         NETSTACK_ROUTING.get_root_ipaddr(&dest_addr)) {

          printf("\nSending to server ");
          uiplib_ipaddr_print(&dest_addr);
          printf(": %s\n", MY_MESSAGE);

          simple_udp_sendto(&udp_conn, MY_MESSAGE, sizeof(MY_MESSAGE), &dest_addr);
          leds_toggle(LEDS_RED);
      }
      else {
          printf("Node unreachable yet...\n");
      }

      etimer_reset(&timer);
  }

  PROCESS_END();
}
