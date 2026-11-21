/*
 * server.c
 * Es la raiz de la red RPL. Recibe las balizas del cliente, lee el RSSI de
 * cada paquete, lo promedia en ventanas de WINDOW_SIZE paquetes y estima la
 * distancia con el modelo log-distancia:
 *
 *      d = D_REF * 10^((RSSI_REF - RSSI_medio) / (10 * n))
 *
 * Salida por el puerto serie (formato CSV, facil de procesar):
 *   RAW,<seq>,<rssi_dBm>,<lqi>                  -> cada paquete (si PRINT_RAW = 1)
 *   DIST,<ventana>,<rssi_medio>,<std>,<dist_m>,<recibidos>,<perdidos>
 *
 * NOTA: el servidor NO responde al cliente.
 * La medida del RSSI y el calculo de la distancia se hacen y se muestran
 * unicamente aqui, en el servidor. El cliente solo envia balizas.
 * Si en el futuro el cliente (el coche) necesita conocer su distancia o su
 * posicion, habra que anadir un mensaje de respuesta servidor -> cliente.
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
/* Parametros del modelo (obtenidos en la calibracion), con decimales:
     - RSSI_REF_DBM: RSSI medido a la distancia de referencia D_REF_CM
     - PATH_LOSS_N: exponente de perdidas n
   WINDOW_SIZE: balizas que se promedian para cada distancia. Con el cliente
   enviando cada 100 ms, sale una distancia cada 50 x 100 ms = 5 s. */
#define RSSI_REF_DBM     (-67.0f) /* RSSI medido a la distancia de referencia (calibrar) */
#define D_REF_CM         100    /* distancia de referencia: 1 m */
#define PATH_LOSS_N      1.8f     /* exponente de perdidas n (calibrar) */
#define WINDOW_SIZE      50     /* paquetes por estimacion */
#define PRINT_RAW        0      /* 1 = imprime cada muestra (util para calibrar) */
/* ================================================================= */

/* Socket UDP: guarda los puertos y la funcion de recepcion */
static struct simple_udp_connection udp_conn;

/* Acumuladores de la ventana (static: conservan su valor entre paquetes) */
static int32_t  sum_rssi;                /* suma de los RSSI de la ventana */
static int32_t  sum_rssi_sq;             /* suma de los RSSI al cuadrado */
static uint16_t n_samples;               /* paquetes recibidos en la ventana */
static uint16_t n_lost;                  /* paquetes perdidos en la ventana */
static uint16_t window_id;               /* numero de ventana actual */
static uint16_t last_seq;                /* ultimo numero de secuencia recibido */
static uint8_t  have_last_seq;           /* 1 cuando ya se ha recibido alguna baliza */
static uint8_t  client_discovered = 0;   /* 1 cuando se ha detectado al cliente */

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

/* d = D_REF * 10^((RSSI_REF - rssi) / (10 n)) */
static uint32_t
rssi_to_distance_cm(float rssi_mean)
{
  float expo = (RSSI_REF_DBM - rssi_mean) / (10.0f * PATH_LOSS_N);
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

#if PRINT_RAW
/* LQI (Link Quality Indicator) del ultimo paquete recibido: indica la
   calidad con la que ha llegado la senal (mayor = mejor). Solo se registra
   en las lineas RAW para analizarlo; no interviene en el calculo de la
   distancia. Por eso solo se compila si PRINT_RAW = 1 (si no, el
   compilador avisa de que no se usa y el build falla). */
static uint16_t
read_lqi(void)
{
  uint16_t lqi = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);

  if(lqi == 0) {
    /* Alternativa si el driver no rellena el atributo */
    radio_value_t v;
    if(NETSTACK_RADIO.get_value(RADIO_PARAM_LAST_LINK_QUALITY, &v) == RADIO_RESULT_OK) {
      lqi = (uint16_t)v;
    }
  }
  return lqi;
}
#endif /* PRINT_RAW */
/*---------------------------------------------------------------------------*/
/* Se llama cuando la ventana tiene WINDOW_SIZE muestras: calcula la media,
   la desviacion tipica y la distancia, las imprime y reinicia la ventana. */
static void
close_window(void)
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

  /* Reinicia la ventana */
  window_id++;
  sum_rssi = 0;
  sum_rssi_sq = 0;
  n_samples = 0;
  n_lost = 0;
}
/*---------------------------------------------------------------------------*/
/* Se ejecuta cada vez que llega una baliza del cliente */
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
#if PRINT_RAW
  uint16_t lqi;
#endif

  if(datalen < sizeof(msg)) {
    return;
  }
  memcpy(&msg, data, sizeof(msg));   /* memcpy: el buffer puede no estar alineado */
  if(msg.type != MSG_BEACON) {
    return;
  }

  rssi = read_rssi();   /* leer cuanto antes (el packetbuf se sobrescribe) */
#if PRINT_RAW
  lqi  = read_lqi();
#endif

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
  printf("RAW,%u,%d,%u\n", msg.seq, rssi, lqi);
#endif

  sum_rssi    += rssi;
  sum_rssi_sq += (int32_t)rssi * rssi;
  n_samples++;

  if(n_samples >= WINDOW_SIZE) {
    close_window();
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(rssi_server_process, ev, data)
{
  PROCESS_BEGIN();

  /* El servidor es la raiz RPL: forma la red y el cliente obtiene su IP */
  NETSTACK_ROUTING.root_start();

  /* Puerto local 5678, acepta cualquier remitente (NULL), puerto remoto 8765.
     udp_rx_callback se ejecuta con cada baliza recibida. */
  simple_udp_register(&udp_conn, UDP_PORT_SERVER, NULL,
                      UDP_PORT_CLIENT, udp_rx_callback);

  /* Los float se imprimen con print_x100 (el printf no soporta %f) */
  printf("RSSI server listo. Config: RSSI_ref=");
  print_x100((int32_t)(RSSI_REF_DBM * 100.0f + (RSSI_REF_DBM < 0 ? -0.5f : 0.5f)));
  printf(" dBm @ %u cm, n=", D_REF_CM);
  print_x100((int32_t)(PATH_LOSS_N * 100.0f + 0.5f));
  printf(", ventana=%u\n", WINDOW_SIZE);
#if PRINT_RAW
  printf("# RAW,seq,rssi_dBm,lqi\n");
#endif
  printf("# DIST,ventana,rssi_medio_dBm,std_dB,dist_m,recibidos,perdidos\n");

  /* Todo el trabajo lo hace el callback: el proceso solo espera */
  while(1) {
    PROCESS_YIELD();
  }

  PROCESS_END();
}
