/*
 * LAB3: UDP Server code
 *  Created on: Oct 26, 2026
 *  Author: Adrián Riveiro & Jaime Pérez
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

#define MY_MESSAGE "Mundial LOL 2026"
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
    printf("Received package from");
    uiplib_ipaddr_print(source_addr);
    printf(", Data: %s\r", (char*)data);

}
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{

  PROCESS_BEGIN();
  printf("Hello, welcome to LAB 3\n");

  //Only done in the server as only 1 root device is needed.
  NETSTACK_ROUTING.root_start();

  //
  simple_udp_register(&udp_conn,UDP_SERVER_PORT,NULL,UDP_CLIENT_PORT,udp_rx_callback);

  while(1){}
  PROCESS_END();
}