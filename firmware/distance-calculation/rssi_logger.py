
"""
rssi_logger.py
Registra las lineas RAW que imprime el servidor (server.c con PRINT_RAW = 1)
y las guarda en un CSV, una medida por fichero.
 
Uso (VS Code, boton Play):
  1. Coloca las Cookies a la distancia real que quieras medir.
  2. Ajusta DISTANCIA_M (y el resto de la configuracion si hace falta).
  3. Ejecuta el script. Para solo al llegar a N_MUESTRAS o a DURACION_MAX_S.
  4. Repite para cada distancia.
 
IMPORTANTE: cierra cualquier otro monitor serie del servidor (make login,
PuTTY, monitor de VS Code...) antes de ejecutarlo: el puerto solo puede
tenerlo abierto un programa a la vez.
 
Requiere pyserial:  pip install pyserial
"""
 
# ========================= CONFIGURACION =========================
PUERTO          = "COM5"          # Windows: "COM5" | Linux: "/dev/ttyUSB0" | Mac: "/dev/tty.usbserial-XXXX"
BAUDIOS         = 115200          # el mismo que usais en el monitor serie
DISTANCIA_M     = 1.0             # distancia REAL entre las Cookies en esta medida (m)
N_MUESTRAS      = 1200            # muestras a guardar (1200 = 2 min a 10 balizas/s)
DURACION_MAX_S  = 300             # limite de seguridad por si llegan menos paquetes
CARPETA_DATOS   = "datos"         # carpeta (junto a este script) donde se guardan los CSV
ETIQUETA        = ""              # opcional: texto extra en el nombre, p.ej. "parking"
# =================================================================
 
import csv
import os
import sys
import time
from datetime import datetime
 
try:
    import serial
except ImportError:
    sys.exit("Falta pyserial. Instalalo con:  pip install pyserial")
 
 
def main():
    carpeta = os.path.join(os.path.dirname(os.path.abspath(__file__)), CARPETA_DATOS)
    os.makedirs(carpeta, exist_ok=True)
 
    marca = datetime.now().strftime("%Y%m%d_%H%M%S")
    extra = f"_{ETIQUETA}" if ETIQUETA else ""
    nombre = f"rssi_{DISTANCIA_M:.2f}m{extra}_{marca}.csv"
    ruta = os.path.join(carpeta, nombre)
 
    print(f"Abriendo {PUERTO} a {BAUDIOS} baudios...")
    try:
        puerto = serial.Serial(PUERTO, BAUDIOS, timeout=1)
    except serial.SerialException as e:
        sys.exit(f"No se pudo abrir {PUERTO}: {e}\n"
                 "Comprueba el nombre del puerto y que no haya otro monitor serie abierto.")
 
    print(f"Guardando en {ruta}")
    print(f"Distancia real: {DISTANCIA_M} m | objetivo: {N_MUESTRAS} muestras\n")
 
    n = 0
    inicio = time.time()
    with puerto, open(ruta, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["distancia_m", "seq", "rssi_dbm", "t_s"])
 
        while n < N_MUESTRAS and (time.time() - inicio) < DURACION_MAX_S:
            linea = puerto.readline().decode(errors="ignore").strip()
            if not linea.startswith("RAW,"):
                if linea.startswith("DIST,"):
                    print(f"   (servidor) {linea}")
                continue
            partes = linea.split(",")
            if len(partes) != 3:
                continue
            try:
                seq, rssi = int(partes[1]), int(partes[2])
            except ValueError:
                continue
            w.writerow([DISTANCIA_M, seq, rssi, round(time.time() - inicio, 3)])
            n += 1
            if n % 100 == 0:
                print(f"  {n}/{N_MUESTRAS} muestras (ultimo RSSI: {rssi} dBm)")
 
    if n == 0:
        print("\nNo se ha recibido ninguna linea RAW. Comprueba que el servidor tiene "
              "PRINT_RAW = 1 y que el cliente esta encendido.")
        os.remove(ruta)
    else:
        print(f"\nListo: {n} muestras guardadas en {ruta}")
 
 
if __name__ == "__main__":
    main()
 