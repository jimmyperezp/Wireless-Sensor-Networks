/*
 * client.c
 * Espera a unirse a la red RPL, obtiene la IP del servidor (raiz RPL) y le
 * envia balizas UDP periodicas. El servidor mide el RSSI y, cada ventana,
 * devuelve su estimacion de distancia.
 *
 * Salida por el puerto serie:
 *   RESULT,<ventana>,<rssi_medio_up>,<std_up>,<dist_m>,<recibidos>,<perdidos>,<rssi_down>
 *   (rssi_down = RSSI con el que el cliente recibe la respuesta del servidor)
 */

#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uip.h"
#include "net/packetbuf.h"
#include "net/netstack.h"
#include "net/routing/routing.h"

#include <stdio.h>
#include <string.h>


/* ========================= CONSTANTES COMUNES ========================= */
/* (deben ser IGUALES en client.c y server.c)                             */
#define UDP_PORT_SERVER   5678
#define UDP_PORT_CLIENT   8765
#define TX_POWER_DBM      0      /* misma potencia en calibracion y medidas */
#define MSG_BEACON        1
#define MSG_RESULT        2

/* Cliente -> servidor */
typedef struct __attribute__((packed)) {
  uint8_t  type;            /* MSG_BEACON */
  uint16_t seq;             /* numero de secuencia */
} beacon_msg_t;

/* Servidor -> cliente (una vez por ventana) */
typedef struct __attribute__((packed)) {
  uint8_t  type;            /* MSG_RESULT */
  uint16_t window_id;
  int16_t  rssi_mean_x100;  /* RSSI medio * 100 (dBm) */
  uint16_t rssi_std_x100;   /* desviacion tipica * 100 (dB) */
  uint32_t dist_cm;         /* distancia estimada (cm) */
  uint8_t  n_samples;
  uint8_t  n_lost;
} result_msg_t;
/* ====================================================================== */

/* ========================= CONFIGURACION ========================= */
#define SEND_INTERVAL   (CLOCK_SECOND / 10)   /* 10 balizas por segundo */
/* ================================================================= */

static struct simple_udp_connection udp_conn;

PROCESS(rssi_client_process, "RSSI client");
AUTOSTART_PROCESSES(&rssi_client_process);

/*---------------------------------------------------------------------------*/
static void
print_x100(int32_t v)
{
  if(v < 0) {
    printf("-");
    v = -v;
  }
  printf("%ld.%02ld", (long)(v / 100), (long)(v % 100));
}
/*---------------------------------------------------------------------------*/
static void
udp_rx_callback(struct simple_udp_connection *c,
                const uip_ipaddr_t *sender_addr,
                uint16_t sender_port,
                const uip_ipaddr_t *receiver_addr,
                uint16_t receiver_port,
                const uint8_t *data,
                uint16_t datalen)
{
  result_msg_t res;
  int16_t rssi_down = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);

  if(datalen < sizeof(res)) {
    return;
  }
  memcpy(&res, data, sizeof(res));
  if(res.type != MSG_RESULT) {
    return;
  }

  printf("RESULT,%u,", res.window_id);
  print_x100(res.rssi_mean_x100);
  printf(",");
  print_x100(res.rssi_std_x100);
  printf(",");
  print_x100((int32_t)res.dist_cm);
  printf(",%u,%u,%d\n", res.n_samples, res.n_lost, rssi_down);
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rssi_client_process, ev, data)
{
  static struct etimer periodic;
  static uip_ipaddr_t server_addr;
  static uint16_t seq = 0;
  static uint8_t not_ready_count = 0;
  beacon_msg_t msg;

  PROCESS_BEGIN();

  if(NETSTACK_RADIO.set_value(RADIO_PARAM_TXPOWER, TX_POWER_DBM) != RADIO_RESULT_OK) {
    printf("AVISO: no se pudo fijar la potencia TX\n");
  }

  simple_udp_register(&udp_conn, UDP_PORT_CLIENT, NULL,
                      UDP_PORT_SERVER, udp_rx_callback);

  printf("RSSI client listo\n");
  printf("# RESULT,ventana,rssi_up_dBm,std_up_dB,dist_m,recibidos,perdidos,rssi_down_dBm\n");

  etimer_set(&periodic, SEND_INTERVAL);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic));
    etimer_reset(&periodic);

    /* Igual que en la practica 3: solo se envia si hay red y se conoce la raiz */
    if(NETSTACK_ROUTING.node_is_reachable() &&
       NETSTACK_ROUTING.get_root_ipaddr(&server_addr)) {
      msg.type = MSG_BEACON;
      msg.seq  = seq++;
      simple_udp_sendto(&udp_conn, &msg, sizeof(msg), &server_addr);
    } else {
      /* Aviso como mucho una vez por segundo */
      if(++not_ready_count >= 10) {
        not_ready_count = 0;
        printf("Servidor aun no alcanzable (esperando a unirse a la red RPL)...\n");
      }
    }
  }

  PROCESS_END();
}
