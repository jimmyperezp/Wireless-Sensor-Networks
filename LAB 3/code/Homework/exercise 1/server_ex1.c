/*
 * LAB3 - Exercise 1: UDP Server code (RPL root)
 *  Created on: Oct 26, 2026
 *  Author: Adrián Riveiro & Jaime Pérez
 *
 *  Whenever a packet is received from a client, the server prints it and
 *  sends a reply packet back to that same client.
 */


#include "contiki.h"
#include <stdio.h> /* For printf() */
#include <string.h>
#include "leds.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uiplib.h"

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define MY_MESSAGE "Mundial LOL 2026" // Reply sent back to the clients
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
    printf("\nReceived packet from ");
    uiplib_ipaddr_print(source_addr);
    printf(", Data: %.*s\n", (int)datalen, (char*)data); // Only datalen characters are printed

    // Reply packet back to the client that has just sent the message (source_addr)
    simple_udp_sendto(c, MY_MESSAGE, sizeof(MY_MESSAGE), source_addr);
    printf("Reply sent: %s\n", MY_MESSAGE);

    leds_toggle(LEDS_GREEN); // Visual feedback of each reception
}
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{

  PROCESS_BEGIN();
  printf("Hello, welcome to LAB 3 (Exercise 1 - Server)\n");

  //Only done in the server as only 1 root device is needed.
  NETSTACK_ROUTING.root_start();

  // Local port: server. Remote port: client.
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, udp_rx_callback);

  // The process must give the control back to the scheduler (while(1){} blocks the whole OS).
  // Everything is done inside the callback.
  while(1) {
      PROCESS_YIELD();
  }

  PROCESS_END();
}
