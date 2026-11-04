/*
 * LAB3 - Exercise 3: UDP Server code (RPL root)
 *  Created on: Oct 26, 2026
 *  Author: Adrián Riveiro & Jaime Pérez
 *
 *  The server reads commands from the serial line (same frame format as Lab 2:
 *  LR[N], LG[N], ST[N], SH[N]) and sends them to every known client.
 *  Everything the clients answer (measurements, confirmations, errors) is
 *  shown in the server terminal.
 *
 *  A client becomes "known" as soon as it sends any packet to the server
 *  (clients send a periodic HELLO for this purpose).
 */


#include "contiki.h"
#include <stdio.h> /* For printf() */
#include <string.h>
#include "leds.h"
#include "serial-line.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uiplib.h"

#define UDP_CLIENT_PORT 8765
#define UDP_SERVER_PORT 5678

#define HELLO_MSG   "HELLO!"
#define MAX_CLIENTS 5

static struct simple_udp_connection udp_conn; //Identifies the socket that will be later used.

static int num_clients = 0;


/*---------------------------------------------------------------------------*/
PROCESS(server_process, "Server process");
AUTOSTART_PROCESSES(&server_process);


/*--------------------Callback definitions------------------------------------*/
static void udp_rx_callback(struct simple_udp_connection *c,
                    const uip_ipaddr_t *source_addr,
                    uint16_t source_port,
                    const uip_ipaddr_t *dest_addr,
                    uint16_t dest_port,
                    const uint8_t *data, uint16_t datalen)
{

    //First, I check if the client is known. If it's new, then I show the time taken to discover it.

    leds_toggle(LEDS_GREEN); //GREEN LED = received message
    
    if(strncmp((char*)data, HELLO_MSG, strlen(HELLO_MSG)) == 0){
        printf("(Timestamp: %d) Client detected!\n", (int)clock_time());
        num_clients++;
        
        return; //The hello message ends the reception callback.
    }

    //If the message isn't a hello, then it's printed.
    printf("\n[");
    uiplib_ipaddr_print(source_addr);
    printf("] %.*s\n", (int)datalen, (char*)data);
}
/*---------------------------------------------------------------------------*/


/*---------------------------------------------------------------------------*/
PROCESS_THREAD(server_process, ev, data)
{
  static int i;

  PROCESS_BEGIN();
  printf("Hello, welcome to LAB 3 (Exercise 3 - Server)\n");
  printf("Commands: LR | LG | ST | SH [+ optional period in s]\n");

  //Only done in the server as only 1 root device is needed.
  NETSTACK_ROUTING.root_start();

  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, udp_rx_callback);

  while(1) {

      // The server waits for a command introduced by the user (serial line)
      PROCESS_WAIT_EVENT_UNTIL(ev == serial_line_event_message);
      printf("\nMessage received: [%s]", (char*)data);

      if(num_clients == 0) {
          printf(" not sent. No clients registered yet\n");
          continue;
      }

      for(i = 0; i < num_clients; i++) {
        //The message is sent to the client, who will be in charge of filtering it.  
        simple_udp_sendto(&udp_conn, data, strlen((char*)data) + 1, &clients[i]); // +1 to send the '\0'
      }
      printf(" sent to %d client(s)\n", num_clients);

      leds_toggle(LEDS_RED); // RED LED = sent message.
  }

  PROCESS_END();
}
