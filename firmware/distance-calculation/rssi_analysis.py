
"""
rssi_analysis.py
Compara, con datos reales, distintas formas de estimar la distancia a partir
del RSSI, sin tener que reprogramar las Cookies para cada prueba.
 
Entrada: los CSV que genera rssi_logger.py (uno o varios por distancia).
 
Se combinan dos cosas:
  * ESTIMADOR del RSSI de cada ventana de WINDOW_SIZE muestras
      - media            (lo que hace ahora el server.c)
      - mediana          (robusta frente a valores atipicos)
      - media recortada  (descarta el RECORTE % mas alto y mas bajo)
      - Kalman 1D        (filtro recursivo sobre el RSSI; se toma su valor al
                          final de cada ventana)
  * CALIBRACION que convierte RSSI -> distancia
      - firmware         (los parametros que tiene ahora server.c)
      - 2 puntos         (recalculada con las distancias DOS_PUNTOS)
      - MC validacion    (minimos cuadrados con todas las distancias MENOS la
                          que se evalua: medida honesta del error)
      - tabla validacion (interpolacion entre las distancias medidas, tambien
                          dejando fuera la que se evalua)
 
Para que la comparacion sea honesta, ninguna calibracion se evalua en las
distancias que ha usado para calibrarse:
  - "2 puntos" no se evalua en las dos distancias DOS_PUNTOS.
  - "tabla validacion" no se evalua en la distancia minima ni en la maxima,
    porque sin ellas no hay puntos a ambos lados entre los que interpolar.
  - "firmware" se evalua en todas (no sabemos con que datos se calibro).
 
Salida (carpeta CARPETA_RESULTADOS):
  - resumen_metodos.csv       error de cada combinacion estimador x calibracion
  - detalle_por_distancia.csv error de cada combinacion en cada distancia
  - fig1..fig5 *.png          graficas para la memoria
  - por pantalla: tabla resumen y parametros recomendados para server.c
 
Uso (VS Code, boton Play): ajusta la configuracion y ejecuta.
Requiere: numpy y matplotlib  (pip install numpy matplotlib)
"""
 
# ========================= CONFIGURACION =========================
CARPETA_DATOS       = "datos"        # donde estan los CSV de rssi_logger.py
CARPETA_RESULTADOS  = "resultados"   # donde se guardan tablas y graficas
WINDOW_SIZE         = 50             # muestras por estimacion (igual que en server.c)
 
# Parametros que tiene ahora mismo server.c (para compararlos)
RSSI_REF_FIRMWARE   = -67.0          # dBm a 1 m
N_FIRMWARE          = 1.8
 
DOS_PUNTOS          = (1.0, 3.0)     # distancias para la calibracion de 2 puntos (m)
RECORTE             = 0.10           # fraccion descartada por cada lado en la media recortada
 
KALMAN_Q            = 0.01           # ruido de proceso (dB^2): cuanto puede cambiar el RSSI real entre muestras
KALMAN_R            = None           # ruido de medida (dB^2). None = se estima de los datos
 
DISTANCIA_EJEMPLO_KALMAN = None      # distancia para la grafica del Kalman (None = la mediana de las medidas)
MOSTRAR_GRAFICAS    = True           # True = abre las graficas ademas de guardarlas
# =================================================================
 
import csv
import glob
import os
import sys
 
try:
    import numpy as np
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("Faltan librerias. Instalalas con:  pip install numpy matplotlib")
 
BASE = os.path.dirname(os.path.abspath(__file__))
 
# Colores (orden fijo: el mismo metodo siempre con el mismo color)
COLORES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
TINTA = "#3d3d3a"
TINTA_SUAVE = "#8a8a84"
REJILLA = "#e6e5e0"
 
ESTIMADORES = ["media", "mediana", "media recortada", "Kalman"]
CALIBRACIONES = ["firmware", "2 puntos", "MC validacion", "tabla validacion"]
 
 
# ---------------------------------------------------------------- carga
def cargar_datos():
    """Devuelve {distancia_m: array de RSSI en orden de llegada}."""
    ficheros = sorted(glob.glob(os.path.join(BASE, CARPETA_DATOS, "*.csv")))
    if not ficheros:
        sys.exit(f"No hay CSV en {os.path.join(BASE, CARPETA_DATOS)}. "
                 "Registra primero las medidas con rssi_logger.py")
    datos = {}
    for fichero in ficheros:
        with open(fichero, newline="") as f:
            for fila in csv.DictReader(f):
                d = round(float(fila["distancia_m"]), 3)
                datos.setdefault(d, []).append(float(fila["rssi_dbm"]))
    return {d: np.array(v) for d, v in sorted(datos.items())}
 
 
def ventanas(x, w):
    """Trocea x en ventanas consecutivas de w muestras (descarta la ultima incompleta)."""
    n = len(x) // w
    return [x[i * w:(i + 1) * w] for i in range(n)]
 
 
# ---------------------------------------------------------------- estimadores
def media_recortada(x, frac):
    x = np.sort(x)
    k = int(len(x) * frac)
    return float(np.mean(x[k:len(x) - k])) if len(x) - 2 * k > 0 else float(np.mean(x))
 
 
def kalman_1d(z, q, r):
    """Kalman de 1 estado (el RSSI 'real') con modelo constante + ruido.
       Prediccion: x = x,        P = P + Q
       Correccion: K = P/(P+R),  x = x + K(z - x),  P = (1 - K) P
    """
    x, p = z[0], r
    salida = np.empty_like(z, dtype=float)
    for i, zi in enumerate(z):
        p = p + q
        k = p / (p + r)
        x = x + k * (zi - x)
        p = (1.0 - k) * p
        salida[i] = x
    return salida
 
 
def estimar_ventanas(rssi, estimador, r_kalman):
    """RSSI estimado al final de cada ventana."""
    if estimador == "Kalman":
        k = kalman_1d(rssi, KALMAN_Q, r_kalman)
        n = len(rssi) // WINDOW_SIZE
        return np.array([k[(i + 1) * WINDOW_SIZE - 1] for i in range(n)])
    vs = ventanas(rssi, WINDOW_SIZE)
    if estimador == "media":
        return np.array([np.mean(v) for v in vs])
    if estimador == "mediana":
        return np.array([np.median(v) for v in vs])
    if estimador == "media recortada":
        return np.array([media_recortada(v, RECORTE) for v in vs])
    raise ValueError(estimador)
 
 
# ---------------------------------------------------------------- modelo y calibraciones
def distancia_modelo(rssi, rssi_ref, n):
    """d = 1 m * 10^((RSSI_REF - RSSI) / (10 n))  (misma formula que server.c)."""
    return 10.0 ** ((rssi_ref - rssi) / (10.0 * n))
 
 
def ajuste_mc(dists, rssis):
    """Minimos cuadrados de RSSI = RSSI_REF - n * 10 log10(d).
       Devuelve (RSSI_REF, n, sigma de los residuos)."""
    x = 10.0 * np.log10(np.asarray(dists))
    y = np.asarray(rssis)
    pendiente, ordenada = np.polyfit(x, y, 1)
    residuos = y - (ordenada + pendiente * x)
    sigma = float(np.std(residuos, ddof=2)) if len(x) > 2 else float("nan")
    return float(ordenada), float(-pendiente), sigma
 
 
def construir_calibracion(nombre, medias, d_eval):
    """Devuelve una funcion rssi -> distancia. 'medias' = {d: RSSI medio}.
       Las calibraciones 'validacion' no usan la distancia d_eval."""
    if nombre == "firmware":
        return lambda r: distancia_modelo(r, RSSI_REF_FIRMWARE, N_FIRMWARE)
 
    if nombre == "2 puntos":
        d1, d2 = DOS_PUNTOS
        r1, r2 = medias[d1], medias[d2]
        n = (r1 - r2) / (10.0 * np.log10(d2 / d1))
        ref = r1 + 10.0 * n * np.log10(d1)       # lleva la referencia a 1 m
        return lambda r: distancia_modelo(r, ref, n)
 
    otras = {d: m for d, m in medias.items() if d != d_eval}
    if nombre == "MC validacion":
        ref, n, _ = ajuste_mc(list(otras), list(otras.values()))
        return lambda r: distancia_modelo(r, ref, n)
 
    if nombre == "tabla validacion":
        # Interpola log10(d) en funcion del RSSI entre los puntos medidos.
        # Fuera del rango medido se queda en el extremo (np.interp no extrapola).
        orden = sorted(otras.items(), key=lambda kv: kv[1])
        rs = np.array([m for _, m in orden])
        logd = np.log10([d for d, _ in orden])
        return lambda r: 10.0 ** np.interp(r, rs, logd)
 
    raise ValueError(nombre)
 
 
# ---------------------------------------------------------------- evaluacion
def distancias_evaluables(cal, ds):
    """Distancias en las que tiene sentido evaluar cada calibracion."""
    if cal == "2 puntos":
        return [d for d in ds if d not in DOS_PUNTOS]
    if cal == "tabla validacion":
        return [d for d in ds if d not in (min(ds), max(ds))]
    return list(ds)
 
 
def evaluar(datos, r_kalman):
    medias = {d: float(np.mean(v)) for d, v in datos.items()}
    calibs = [c for c in CALIBRACIONES
              if c != "2 puntos" or all(p in medias for p in DOS_PUNTOS)]
    if len(medias) < 3:
        calibs = [c for c in calibs if "validacion" not in c]
 
    detalle, resumen, estimadas = [], [], {}
    for est in ESTIMADORES:
        r_vent = {d: estimar_ventanas(v, est, r_kalman) for d, v in datos.items()}
        for cal in calibs:
            errores_abs, errores_rel = [], []
            evaluables = distancias_evaluables(cal, list(r_vent))
            for d in evaluables:
                rv = r_vent[d]
                f = construir_calibracion(cal, medias, d)
                d_est = f(rv)
                estimadas[(est, cal, d)] = d_est
                e = d_est - d
                errores_abs.extend(np.abs(e))
                errores_rel.extend(np.abs(e) / d * 100.0)
                detalle.append({
                    "estimador": est, "calibracion": cal, "distancia_real_m": d,
                    "distancia_media_estimada_m": round(float(np.mean(d_est)), 3),
                    "desv_tipica_m": round(float(np.std(d_est)), 3),
                    "sesgo_m": round(float(np.mean(e)), 3),
                    "error_relativo_medio_pct": round(float(np.mean(np.abs(e) / d * 100)), 1),
                    "n_ventanas": len(d_est),
                })
            if not errores_abs:
                continue
            ea, er = np.array(errores_abs), np.array(errores_rel)
            resumen.append({
                "estimador": est, "calibracion": cal,
                "distancias_evaluadas": " ".join(f"{d:g}" for d in evaluables),
                "MAE_m": round(float(np.mean(ea)), 3),
                "RMSE_m": round(float(np.sqrt(np.mean(ea ** 2))), 3),
                "error_relativo_medio_pct": round(float(np.mean(er)), 1),
                "error_relativo_mediana_pct": round(float(np.median(er)), 1),
                "error_relativo_p90_pct": round(float(np.percentile(er, 90)), 1),
            })
    return medias, calibs, detalle, resumen, estimadas
 
 
def guardar_csv(filas, nombre):
    ruta = os.path.join(BASE, CARPETA_RESULTADOS, nombre)
    with open(ruta, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(filas[0]))
        w.writeheader()
        w.writerows(filas)
    return ruta
 
 
# ---------------------------------------------------------------- graficas
def estilo(ax, titulo, xlabel, ylabel):
    ax.set_title(titulo, color=TINTA, fontsize=12, loc="left")
    ax.set_xlabel(xlabel, color=TINTA)
    ax.set_ylabel(ylabel, color=TINTA)
    ax.grid(True, color=REJILLA, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(TINTA_SUAVE)
    ax.tick_params(colors=TINTA)
 
 
def guardar_fig(fig, nombre):
    fig.tight_layout()
    fig.savefig(os.path.join(BASE, CARPETA_RESULTADOS, nombre), dpi=200)
 
 
def fig_modelo(datos, medias):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for d, v in datos.items():
        pv = [np.mean(x) for x in ventanas(v, WINDOW_SIZE)]
        ax.scatter([d] * len(pv), pv, s=18, color=COLORES[0], alpha=0.35,
                   edgecolors="none", label="RSSI medio por ventana" if d == min(datos) else None)
    ds = np.array(list(medias))
    ax.scatter(ds, list(medias.values()), s=60, color=COLORES[0], edgecolors="white",
               linewidths=1.5, zorder=3, label="RSSI medio por distancia")
 
    xs = np.logspace(np.log10(ds.min() * 0.8), np.log10(ds.max() * 1.2), 200)
    ax.plot(xs, RSSI_REF_FIRMWARE - 10 * N_FIRMWARE * np.log10(xs), color=COLORES[1], lw=2,
            label=f"firmware (RSSI_ref={RSSI_REF_FIRMWARE:.1f}, n={N_FIRMWARE:.2f})")
    if len(ds) >= 2:
        ref, n, sigma = ajuste_mc(ds, list(medias.values()))
        ax.plot(xs, ref - 10 * n * np.log10(xs), color=COLORES[2], lw=2,
                label=f"minimos cuadrados (RSSI_ref={ref:.1f}, n={n:.2f})")
    ax.set_xscale("log")
    ax.set_xticks(ds)
    ax.set_xticklabels([f"{d:g}" for d in ds])
    ax.xaxis.set_minor_formatter(plt.NullFormatter())
    estilo(ax, "RSSI frente a distancia y modelos ajustados", "Distancia real (m, escala log)", "RSSI (dBm)")
    ax.legend(frameon=False, fontsize=9, labelcolor=TINTA)
    guardar_fig(fig, "fig1_modelo_rssi_distancia.png")
 
 
def fig_cajas(datos):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ds = list(datos)
    bp = ax.boxplot([datos[d] for d in ds], widths=0.5, patch_artist=True,
                    medianprops=dict(color=TINTA, lw=2),
                    flierprops=dict(marker="o", markersize=3, markerfacecolor=TINTA_SUAVE,
                                    markeredgecolor="none"))
    for caja in bp["boxes"]:
        caja.set(facecolor="#cde2fb", edgecolor=COLORES[0], linewidth=1.5)
    for k in ("whiskers", "caps"):
        for linea in bp[k]:
            linea.set(color=COLORES[0], linewidth=1.2)
    ax.set_xticks(range(1, len(ds) + 1))
    ax.set_xticklabels([f"{d:g}" for d in ds])
    estilo(ax, "Dispersion del RSSI en cada distancia (muestras individuales)",
           "Distancia real (m)", "RSSI (dBm)")
    guardar_fig(fig, "fig2_dispersion_rssi.png")
 
 
def fig_estimada_vs_real(datos, calibs, estimadas):
    fig, ax = plt.subplots(figsize=(7, 5))
    ds = np.array(list(datos))
    lim = ds.max() * 1.25
    ax.plot([0, lim], [0, lim], color=TINTA_SUAVE, lw=1.2, ls="--", label="ideal (estimada = real)")
    desplaz = np.linspace(-0.06, 0.06, len(calibs)) * ds.max() / 3
    for i, cal in enumerate(calibs):
        dc = np.array([d for d in ds if ("media", cal, d) in estimadas])
        if len(dc) == 0:
            continue
        mu = [np.mean(estimadas[("media", cal, d)]) for d in dc]
        sd = [np.std(estimadas[("media", cal, d)]) for d in dc]
        ax.errorbar(dc + desplaz[i], mu, yerr=sd, fmt="o-", ms=6, lw=2, capsize=3,
                    color=COLORES[i], mec="white", mew=1.2, label=cal)
    ax.set_xlim(0, lim)
    ax.set_ylim(0, None)
    estilo(ax, "Distancia estimada frente a real (estimador: media)",
           "Distancia real (m)", "Distancia estimada (m)")
    ax.legend(frameon=False, fontsize=9, labelcolor=TINTA)
    guardar_fig(fig, "fig3_estimada_vs_real.png")
 
 
def fig_comparativa(resumen, calibs):
    fig, ax = plt.subplots(figsize=(8, 4.5))
    ancho = 0.8 / len(ESTIMADORES)
    x = np.arange(len(calibs))
    for i, est in enumerate(ESTIMADORES):
        vals = [next((r["error_relativo_medio_pct"] for r in resumen
                      if r["estimador"] == est and r["calibracion"] == c), 0.0) for c in calibs]
        barras = ax.bar(x + (i - (len(ESTIMADORES) - 1) / 2) * ancho, vals, ancho * 0.92,
                        color=COLORES[i], label=est)
        ax.bar_label(barras, fmt="%.0f", fontsize=7, color=TINTA, padding=2)
    ax.set_xticks(x)
    ax.set_xticklabels(calibs)
    estilo(ax, "Error relativo medio de cada combinacion", "Calibracion", "Error relativo medio (%)")
    ax.set_ylim(0, ax.get_ylim()[1] * 1.08)
    ax.legend(title="Estimador", frameon=False, fontsize=9, title_fontsize=9, labelcolor=TINTA,
              loc="upper left", bbox_to_anchor=(1.0, 1.0))
    guardar_fig(fig, "fig4_comparativa_metodos.png")
 
 
def fig_kalman(datos, r_kalman):
    ds = list(datos)
    d = DISTANCIA_EJEMPLO_KALMAN if DISTANCIA_EJEMPLO_KALMAN in datos else ds[len(ds) // 2]
    z = datos[d]
    k = kalman_1d(z, KALMAN_Q, r_kalman)
    t = np.arange(len(z))
    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.plot(t, z, color=REJILLA, lw=1, label="RSSI de cada paquete")
    ax.plot(t, z, "o", ms=2, color=TINTA_SUAVE, alpha=0.5)
    medias_v = [np.mean(v) for v in ventanas(z, WINDOW_SIZE)]
    for i, m in enumerate(medias_v):
        ax.plot([i * WINDOW_SIZE, (i + 1) * WINDOW_SIZE - 1], [m, m], color=COLORES[0], lw=2,
                label="media por ventana" if i == 0 else None)
    ax.plot(t, k, color=COLORES[1], lw=2, label=f"Kalman (Q={KALMAN_Q}, R={r_kalman:.2f})")
    estilo(ax, f"Media por ventanas frente a Kalman a {d:g} m", "Numero de muestra", "RSSI (dBm)")
    ax.set_title(ax.get_title(loc="left"), color=TINTA, fontsize=12, loc="left", pad=28)
    ax.legend(frameon=False, fontsize=9, labelcolor=TINTA, ncol=3,
              loc="lower left", bbox_to_anchor=(0.0, 1.0))
    guardar_fig(fig, "fig5_kalman_vs_media.png")
 
 
# ---------------------------------------------------------------- main
def main():
    os.makedirs(os.path.join(BASE, CARPETA_RESULTADOS), exist_ok=True)
    datos = cargar_datos()
 
    print("Datos cargados:")
    for d, v in datos.items():
        print(f"  {d:6.2f} m: {len(v):5d} muestras | RSSI medio {np.mean(v):7.2f} dBm | "
              f"desv. tipica {np.std(v):4.2f} dB")
    if len(datos) < 2:
        sys.exit("Hacen falta al menos 2 distancias distintas.")
    faltan = [p for p in DOS_PUNTOS if p not in datos]
    if faltan:
        print(f"\nAVISO: no hay datos a {faltan} m; se omite la calibracion de 2 puntos.")
 
    # Ruido de medida para Kalman: varianza tipica dentro de las ventanas
    if KALMAN_R is None:
        varianzas = [np.var(w) for v in datos.values() for w in ventanas(v, WINDOW_SIZE)]
        r_kalman = max(float(np.median(varianzas)), 0.05)
    else:
        r_kalman = KALMAN_R
 
    medias, calibs, detalle, resumen, estimadas = evaluar(datos, r_kalman)
 
    guardar_csv(resumen, "resumen_metodos.csv")
    guardar_csv(detalle, "detalle_por_distancia.csv")
 
    print("\nError de cada combinacion (ordenado de mejor a peor):")
    print(f"  {'estimador':16s} {'calibracion':18s} {'MAE (m)':>8s} {'RMSE (m)':>9s} "
          f"{'err. rel. medio':>16s} {'p90':>7s}")
    for r in sorted(resumen, key=lambda r: r["error_relativo_medio_pct"]):
        print(f"  {r['estimador']:16s} {r['calibracion']:18s} {r['MAE_m']:8.3f} {r['RMSE_m']:9.3f} "
              f"{r['error_relativo_medio_pct']:15.1f}% {r['error_relativo_p90_pct']:6.1f}%   "
              f"[{r['distancias_evaluadas']} m]")
 
    ref, n, sigma = ajuste_mc(list(medias), list(medias.values()))
    print("\nModelo ajustado por minimos cuadrados con TODAS las distancias:")
    print(f"  RSSI_REF_DBM = {ref:.1f} dBm (a 1 m)   PATH_LOSS_N = {n:.2f}   "
          f"sigma de los residuos = {sigma:.2f} dB")
    print("  -> valores para server.c:")
    print(f"     #define RSSI_REF_DBM     ({ref:.1f}f)")
    print(f"     #define PATH_LOSS_N      {n:.2f}f")
    print(f"\nKalman: Q = {KALMAN_Q}, R = {r_kalman:.3f} dB^2"
          f"{' (estimado de los datos)' if KALMAN_R is None else ''}")
 
    fig_modelo(datos, medias)
    fig_cajas(datos)
    fig_estimada_vs_real(datos, calibs, estimadas)
    fig_comparativa(resumen, calibs)
    fig_kalman(datos, r_kalman)
    print(f"\nTablas y graficas guardadas en {os.path.join(BASE, CARPETA_RESULTADOS)}")
 
    if MOSTRAR_GRAFICAS:
        plt.show()
 
 
if __name__ == "__main__":
    main()
 