/*
 * p3-udp-client.c
 *
 *  Created on: Oct 26, 2026
 *      Author: user
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

#define MY_MESSAGE "Test UDP Client"
static struct simple_udp_connection udp_conn; //Identifies the socket that will be later used.


/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "Server process");
AUTOSTART_PROCESSES(&udp_client_process);


/*--------------------Callback definitions------------------------------------*/
void udp_rx_callback(struct simple_udp_connection *c,
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
PROCESS_THREAD(udp_client_process, ev, data)
{

  PROCESS_BEGIN();

  printf("Hello, welcome to LAB 3\n");
  static struct etimer timer;
  uip_ipaddr_t dest_addr;

  etimer_set(&timer,CLOCK_SECOND * 2);

  simple_udp_register(&udp_conn,UDP_CLIENT_PORT,NULL,UDP_SERVER_PORT,udp_rx_callback);


  while(1) {

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

      if(      (NETSTACK_ROUTING.node_is_reachable) &&
              (NETSTACK_ROUTING.get_root_ipaddr(&dest_addr))){
          simple_udp_sendto(&udp_conn,MY_MESSAGE,sizeof(MY_MESSAGE),&dest_addr);
      }
      else{
          printf("Node unreachable yet...\n");
      }

    leds_toggle(LEDS_RED);

    etimer_reset(&timer);
  }

  PROCESS_END();
}