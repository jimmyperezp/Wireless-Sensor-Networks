/*
 * server.c
 * Recibe las balizas del cliente, lee el RSSI de cada paquete, lo promedia
 * en ventanas de WINDOW_SIZE paquetes y estima la distancia con el modelo
 * log-distancia:
 *
 *      d = D_REF * 10^((RSSI_REF - RSSI_medio) / (10 * n))
 *
 * Salida por el puerto serie (formato CSV, facil de procesar):
 *   RAW,<seq>,<rssi_dBm>                        -> cada paquete (calibracion)
 *   DIST,<ventana>,<rssi_medio>,<std>,<dist_m>,<recibidos>,<perdidos>
 */

#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/ipv6/uip.h"
#include "net/packetbuf.h"
#include "net/netstack.h"
#include "net/routing/routing.h"
#include "net/ipv6/uiplib.h"

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
#define RSSI_REF_DBM     (-67)  /* RSSI medido a la distancia de referencia (calibrar) */
#define D_REF_CM         100    /* distancia de referencia: 1 m */
#define PATH_LOSS_N_X10  18   /* exponente de perdidas n * 10  (25 -> n = 2.5) */
#define WINDOW_SIZE      50     /* paquetes por estimacion */
#define PRINT_RAW        0      /* 1 = imprime cada muestra (util para calibrar) */
/* ================================================================= */

static struct simple_udp_connection udp_conn;

/* Acumuladores de la ventana */
static int32_t  sum_rssi;
static int32_t  sum_rssi_sq;
static uint16_t n_samples;
static uint16_t n_lost;
static uint16_t window_id;
static uint16_t last_seq;
static uint8_t  have_last_seq;
static uint8_t  client_discovered = 0;

PROCESS(rssi_server_process, "RSSI server");
AUTOSTART_PROCESSES(&rssi_server_process);

/*---------------------------------------------------------------------------*/
/* Funciones matematicas sencillas (evitan depender de libm) */
static float
exp_approx(float y)
{
  /* exp(y) = (exp(y/32))^32 ; Taylor de orden 5 para el argumento pequeño */
  float z = y / 32.0f;
  float e = 1.0f + z * (1.0f + z * (0.5f + z * (1.0f / 6.0f +
            z * (1.0f / 24.0f + z * (1.0f / 120.0f)))));
  int i;
  for(i = 0; i < 5; i++) {
    e *= e;
  }
  return e;
}

static float
sqrt_approx(float x)
{
  float r;
  int i;
  if(x <= 0.0f) {
    return 0.0f;
  }
  r = (x > 1.0f) ? x : 1.0f;
  for(i = 0; i < 20; i++) {
    r = 0.5f * (r + x / r);
  }
  return r;
}

/* d = D_REF * 10^((RSSI_REF - rssi) / (10 n)) ; 10*n = PATH_LOSS_N_X10 */
static uint32_t
rssi_to_distance_cm(float rssi_mean)
{
  float expo = ((float)RSSI_REF_DBM - rssi_mean) / (float)PATH_LOSS_N_X10;
  if(expo > 4.0f) {
    expo = 4.0f;          /* limita a D_REF * 10^4 para evitar desbordes */
  }
  return (uint32_t)((float)D_REF_CM * exp_approx(expo * 2.302585f) + 0.5f);
}

/* Imprime un valor *100 como numero con 2 decimales (printf sin %f) */
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
static int16_t
read_rssi(void)
{
  /* RSSI del ultimo paquete recibido (sigue en el packetbuf durante el callback) */
  int16_t rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);

  if(rssi == 0) {
    /* Alternativa si el driver no rellena el atributo */
    radio_value_t v;
    if(NETSTACK_RADIO.get_value(RADIO_PARAM_LAST_RSSI, &v) == RADIO_RESULT_OK) {
      rssi = (int16_t)v;
    }
  }
  return rssi;
}
/*---------------------------------------------------------------------------*/
static void
close_window(const uip_ipaddr_t *client_addr)
{
  float mean = (float)sum_rssi / (float)n_samples;
  float var  = (float)sum_rssi_sq / (float)n_samples - mean * mean;
  float std  = sqrt_approx(var);
  uint32_t dist_cm = rssi_to_distance_cm(mean);

  int32_t mean_x100 = (int32_t)(mean * 100.0f + (mean < 0 ? -0.5f : 0.5f));
  int32_t std_x100  = (int32_t)(std * 100.0f + 0.5f);

  printf("DIST,%u,", window_id);
  print_x100(mean_x100);
  printf(",");
  print_x100(std_x100);
  printf(",");
  print_x100((int32_t)dist_cm);
  printf(",%u,%u\n", n_samples, n_lost);

  /* Devuelve el resultado al cliente */
  result_msg_t res;
  res.type           = MSG_RESULT;
  res.window_id      = window_id;
  res.rssi_mean_x100 = (int16_t)mean_x100;
  res.rssi_std_x100  = (uint16_t)std_x100;
  res.dist_cm        = dist_cm;
  res.n_samples      = (uint8_t)n_samples;
  res.n_lost         = (uint8_t)(n_lost > 255 ? 255 : n_lost);
  simple_udp_sendto(&udp_conn, &res, sizeof(res), client_addr);

  /* Reinicia la ventana */
  window_id++;
  sum_rssi = 0;
  sum_rssi_sq = 0;
  n_samples = 0;
  n_lost = 0;
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
  beacon_msg_t msg;
  int16_t rssi;

  if(datalen < sizeof(msg)) {
    return;
  }
  memcpy(&msg, data, sizeof(msg));   /* memcpy: el buffer puede no estar alineado */
  if(msg.type != MSG_BEACON) {
    return;
  }

  rssi = read_rssi();   /* leer cuanto antes */

  if(!client_discovered) {
    client_discovered = 1;
    printf("Cliente detectado: ");
    uiplib_ipaddr_print(sender_addr);
    printf("\n");
  }

  /* Paquetes perdidos (huecos en la secuencia) */
  if(have_last_seq && (uint16_t)(msg.seq - last_seq) > 1) {
    n_lost += (uint16_t)(msg.seq - last_seq - 1);
  }
  last_seq = msg.seq;
  have_last_seq = 1;

#if PRINT_RAW
  printf("RAW,%u,%d\n", msg.seq, rssi);
#endif

  sum_rssi    += rssi;
  sum_rssi_sq += (int32_t)rssi * rssi;
  n_samples++;

  if(n_samples >= WINDOW_SIZE) {
    close_window(sender_addr);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rssi_server_process, ev, data)
{
  PROCESS_BEGIN();

  if(NETSTACK_RADIO.set_value(RADIO_PARAM_TXPOWER, TX_POWER_DBM) != RADIO_RESULT_OK) {
    printf("AVISO: no se pudo fijar la potencia TX\n");
  }

  /* El servidor es la raiz RPL: forma la red y el cliente obtiene su IP */
  NETSTACK_ROUTING.root_start();

  simple_udp_register(&udp_conn, UDP_PORT_SERVER, NULL,
                      UDP_PORT_CLIENT, udp_rx_callback);

  printf("RSSI server listo. Config: RSSI_ref=%d dBm @ %u cm, n=%d/10, ventana=%u\n",
         RSSI_REF_DBM, D_REF_CM, PATH_LOSS_N_X10, WINDOW_SIZE);
  //printf("# RAW,seq,rssi_dBm\n");
  printf("# DIST,ventana,rssi_medio_dBm,std_dB,dist_m,recibidos,perdidos\n");

  while(1) {
    PROCESS_YIELD();
  }

  PROCESS_END();
}
