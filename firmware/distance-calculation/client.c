
/*
 * client.c
 * Espera a unirse a la red RPL, obtiene la IP del servidor (raiz RPL) y le
 * envia balizas UDP periodicas. El servidor mide el RSSI de cada baliza y
 * calcula la distancia.
 *
 * NOTA: el servidor NO responde al cliente.
 * La medida del RSSI y el calculo de la distancia se hacen y se muestran
 * unicamente en el servidor (lineas DIST del monitor serie). El cliente solo
 * envia balizas.
 * Si en el futuro el cliente (el coche) necesita conocer su distancia o su
 * posicion, habra que anadir un mensaje de respuesta servidor -> cliente.
 */
 
#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uip.h"
#include "net/netstack.h"
#include "net/routing/routing.h"
 
#include <stdio.h>
 
 
/* ========================= CONSTANTES COMUNES ========================= */
/* (deben ser IGUALES en client.c y server.c)                             */
#define UDP_PORT_SERVER   5678
#define UDP_PORT_CLIENT   8765
#define MSG_BEACON        1
 
/* Mensaje que el cliente envia al servidor (baliza): tipo "beacon_msg_t".
   Contiene 2 campos (3 bytes en total):
     - type (1 byte): tipo de mensaje. Siempre vale MSG_BEACON (definido antes)
     - seq  (2 bytes): numero de secuencia. Aumenta en 1 con cada baliza, y asi
                       el servidor detecta si se ha perdido algun paquete.
   "packed" impide que el compilador anada bytes de relleno, para que emisor y
   receptor interpreten los bytes exactamente igual.
*/
typedef struct __attribute__((packed)) {
  uint8_t  type;            /* MSG_BEACON */
  uint16_t seq;             /* numero de secuencia */
} beacon_msg_t;
/* ====================================================================== */
 
/* ========================= CONFIGURACION ========================= */
/* Periodo entre balizas: CLOCK_SECOND / 10 = 100 ms (10 balizas por segundo).*/
#define SEND_INTERVAL   (CLOCK_SECOND / 10)
/* ================================================================= */
 
/* Socket UDP: guarda los puertos y se configura con simple_udp_register */
static struct simple_udp_connection udp_conn;
 
PROCESS(rssi_client_process, "RSSI client");
AUTOSTART_PROCESSES(&rssi_client_process);
 
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rssi_client_process, ev, data)
{
  /* static: conservan su valor entre esperas del protothread */
  static struct etimer periodic;      /* marca el ritmo de envio (SEND_INTERVAL) */
  static uip_ipaddr_t server_addr;    /* IP del servidor (raiz RPL) */
  static uint16_t seq = 0;            /* numero de secuencia de la siguiente baliza */
  beacon_msg_t msg;                   /* se rellena y se envia sin esperas entre medias */
 
  PROCESS_BEGIN();
 
  /* Puerto local 8765, acepta cualquier remitente (NULL), puerto remoto 5678.
     Callback NULL: el cliente no recibe mensajes del servidor. */
  simple_udp_register(&udp_conn, UDP_PORT_CLIENT, NULL, UDP_PORT_SERVER, NULL);
 
  printf("RSSI client listo\n");
 
  etimer_set(&periodic, SEND_INTERVAL);
  while(1) {
    /* El proceso duerme hasta que vence el temporizador (sin bloquear la red) */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
    etimer_reset(&periodic);
 
    /* Igual que en la practica 3: solo se envia si hay red y se conoce la raiz */
    if(NETSTACK_ROUTING.node_is_reachable() &&
       NETSTACK_ROUTING.get_root_ipaddr(&server_addr)) {
      msg.type = MSG_BEACON;
      msg.seq  = seq++;
      simple_udp_sendto(&udp_conn, &msg, sizeof(msg), &server_addr);
    }
  }
 
  PROCESS_END();
}
 