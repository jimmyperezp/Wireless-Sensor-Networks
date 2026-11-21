"""
rssi_txt_a_csv.py
Alternativa a rssi_logger.py cuando las Cookies solo se pueden leer desde la
maquina virtual (sin Python).

En la maquina virtual se guarda la salida del monitor serie del servidor en un
fichero de texto por distancia (copiar y pegar desde Eclipse, o redirigir la
salida desde una terminal). Despues se llevan esos .txt al PC y este script
los convierte en los CSV que usa rssi_analysis.py.

Nombre de cada .txt: tiene que empezar por la distancia real en metros,
seguida de "m". Ejemplos validos:
    0.5m.txt   1m.txt   1,5m.txt   3m_pasillo.txt   7m_repeticion2.txt

Solo se usan las lineas que contienen "RAW," (las demas se ignoran), asi que
da igual que el texto copiado incluya mensajes de arranque o lineas DIST.

Uso (VS Code, boton Play): deja los .txt en CARPETA_TXT y ejecuta.
Despues ejecuta rssi_analysis.py como siempre.
"""

# ========================= CONFIGURACION =========================
CARPETA_TXT    = "logs_txt"   # carpeta (junto a este script) con los .txt copiados de la VM
CARPETA_DATOS  = "datos"      # carpeta donde se escriben los CSV (la que lee rssi_analysis.py)
# =================================================================

import csv
import glob
import os
import re
import sys

BASE = os.path.dirname(os.path.abspath(__file__))
PATRON_DIST = re.compile(r"^\s*(\d+(?:[.,]\d+)?)\s*m", re.IGNORECASE)
PATRON_RAW = re.compile(r"RAW,(\d+),(-?\d+)(?:,(\d+))?")


def main():
    carpeta_txt = os.path.join(BASE, CARPETA_TXT)
    carpeta_datos = os.path.join(BASE, CARPETA_DATOS)
    os.makedirs(carpeta_txt, exist_ok=True)
    os.makedirs(carpeta_datos, exist_ok=True)

    ficheros = sorted(glob.glob(os.path.join(carpeta_txt, "*.txt")))
    if not ficheros:
        sys.exit(f"No hay ficheros .txt en {carpeta_txt}.\n"
                 "Copia ahi la salida del monitor serie (un .txt por distancia, p.ej. 1m.txt).")

    for ruta in ficheros:
        nombre = os.path.basename(ruta)
        m = PATRON_DIST.match(nombre)
        if not m:
            print(f"  SALTADO {nombre}: el nombre debe empezar por la distancia, p.ej. '1.5m.txt'")
            continue
        distancia = float(m.group(1).replace(",", "."))

        filas = []
        with open(ruta, encoding="utf-8", errors="ignore") as f:
            for linea in f:
                r = PATRON_RAW.search(linea)
                if r:
                    seq, rssi, lqi = r.group(1), r.group(2), r.group(3) or ""
                    filas.append([distancia, int(seq), int(rssi), lqi, ""])

        if not filas:
            print(f"  SALTADO {nombre}: no contiene lineas RAW (¿PRINT_RAW = 1 en el servidor?)")
            continue

        base_csv = os.path.splitext(nombre)[0]
        salida = os.path.join(carpeta_datos, f"rssi_{distancia:.2f}m_{base_csv}.csv")
        with open(salida, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["distancia_m", "seq", "rssi_dbm", "lqi", "t_s"])
            w.writerows(filas)
        con_lqi = "con LQI" if filas[0][3] != "" else "sin LQI"
        print(f"  {nombre:30s} -> {len(filas):5d} muestras a {distancia:g} m ({con_lqi})")

    print(f"\nCSV guardados en {carpeta_datos}. Ahora ejecuta rssi_analysis.py")


if __name__ == "__main__":
    main()
