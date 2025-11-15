# ui_capture.py
# -*- coding: utf-8 -*-
"""
Interfaz final con captura en background:
 - customtkinter UI
 - threads para no bloquear la UI
 - queue para pasar resultados al hilo principal
 - integración simple para guardar la sesión con las funciones Excel existentes
"""

import time
import threading
from queue import Queue, Empty
import customtkinter as ctk
from tkinter import messagebox
from PIL import Image, ImageTk
import tkinter as tk
import pandas as pd
import re

# importar las utilidades Excel/IO desde tu script principal
# suponemos que tus funciones abrir_o_crear_xlsx, asegurar_inicio_simple, escribir_sesion
# están en excel_utils.py o en el módulo donde tenías el código original.
# Si tu script original se llama `capture_cli.py`, cámbialo a:
# from capture_cli import abrir_o_crear_xlsx, asegurar_inicio_simple, escribir_sesion
from principal import abrir_o_crear_xlsx, asegurar_inicio_simple, escribir_sesion

# y la función de captura no bloqueante la provee este mismo módulo (ver más abajo)
from principal import _extraer_numero  # reutiliza la utilidad regex si la tienes

import serial  # pyserial

# ---------------- Apariencia ----------------
ctk.set_appearance_mode("Light")
ctk.set_default_color_theme("blue")

PALETA = {
    "primary":        "#1565c0",
    "primary_hover":  "#0d47a1",
    "bg":             "#eaeaea",
    "surface":        "#f5f7fb",
    "divider":        "#d7dbe3",
    "footer":         "#eef1f6",
    "text":           "#1f2937",
    "text_muted":     "#374151",
    "text_on_primary":"#ffffff",
}

FUENTES = {
    "titulo":     ("Segoe UI", 26, "bold"),
    "subtitulo":  ("Segoe UI", 18, "bold"),
    "boton":      ("Segoe UI", 18, "bold"),
    "estado":     ("Segoe UI", 12),
    "contenido":  ("Segoe UI", 14),
}

# ---------------- Config serial (ajusta si necesitas) ----------------
SERIAL_PORT = "COM4"
BAUD_RATE = 115200
# Lista de columnas (puedes reutilizar tu COLS del otro script)
COLS = [
    "timestamp_s",
    "ROM Flexión/Extensión_°",
    "EMG(F/E)_mv",
    "ROM Desviación Ulnar/Radial_°",
    "EMG(D)_mv",
    "ROM Pronosupinación_°",
    "EMG(PS)_mv",
    "Fuerza de Prensión_Kg",
    "EMG(FP)_mv"
]

# ---------------- Estado global para la sesión ----------------
result_queue = Queue()       # resultados que vienen de los threads
session_dfs = []             # capturas (DataFrames) acumuladas en la sesión
current_session_patient = None  # cedula (string)
MAIN_DIR = None              # si quieres pasar carpeta (se usa cuando guardes)

# ---------------- UI principal ----------------
app = ctk.CTk()
app.title("UpperSense — Panel de Control")
app.configure(fg_color=PALETA["bg"])

# icono
try:
    app.iconbitmap("loguito.ico")
except Exception:
    try:
        _i = tk.PhotoImage(file="loguito.png")
        app.iconphoto(True, _i)
    except Exception:
        pass

# maximizada
try:
    app.state("zoomed")
except:
    app.attributes("-zoomed", True)

# ---------- header ----------
ALTO_HEADER = 110
header_wrap = ctk.CTkFrame(app, height=ALTO_HEADER, fg_color="transparent", corner_radius=0)
header_wrap.pack(fill="x", side="top")

header_canvas = tk.Canvas(header_wrap, height=ALTO_HEADER, highlightthickness=0, bd=0)
header_canvas.pack(fill="x", side="top")

# logo
_header_logo_tk = None
try:
    logo_pil = Image.open("loguito.png").resize((60, 60), Image.LANCZOS)
    _header_logo_tk = ImageTk.PhotoImage(logo_pil)
except Exception:
    _header_logo_tk = None

def draw_gradient(event=None):
    header_canvas.delete("all")

    # ancho/alto del canvas; garantizamos al menos 1 para evitar div/0
    w = header_canvas.winfo_width() or 1
    h = header_canvas.winfo_height() or ALTO_HEADER

    # si aún demasiado pequeño, posponer un poco y salir (evita trabajo inútil)
    if w <= 1 or h <= 1:
        # reintentamos en breve
        header_canvas.after(10, draw_gradient)
        return

    # colores del degradado
    col_a = (0x00, 0x2b, 0x6f)
    col_b = (0x4e, 0xa1, 0xff)
    r1,g1,b1 = col_a; r2,g2,b2 = col_b

    denom = max(1, w - 1)   # evita división por cero
    # si quieres menos trabajo, usa step=2 o 3; aquí usamos 1 para máxima calidad
    for x in range(0, w):
        t = x / denom
        r = int(r1 + (r2 - r1) * t)
        g = int(g1 + (g2 - g1) * t)
        b = int(b1 + (b2 - b1) * t)
        header_canvas.create_line(x, 0, x, h, fill=f"#{r:02x}{g:02x}{b:02x}")

    # logo a la izquierda (si existe)
    if _header_logo_tk:
        header_canvas.create_image(60, h // 2, image=_header_logo_tk, anchor="center")
        # mantener referencia para evitar recolección
        header_canvas.logo_ref = _header_logo_tk

    # título centrado
    header_canvas.create_text(
        w // 2, h // 2,
        text="Panel de Control — Funcionalidad de muñeca y codo",
        fill=PALETA["text_on_primary"],
        font=FUENTES["titulo"]
    )

# En lugar de `app.bind("<Configure>", draw_gradient)` usa:
header_canvas.bind("<Configure>", draw_gradient)
# (si sigues dejando el app.bind, puedes tener múltiples llamadas redundantes)


app.bind("<Configure>", draw_gradient)
divider_header = ctk.CTkFrame(app, height=3, fg_color=PALETA["divider"], corner_radius=0)
divider_header.pack(fill="x", side="top")

# ---------- zona central (tarjeta sin sombras) ----------
zona = ctk.CTkFrame(app, corner_radius=20, fg_color=PALETA["surface"])
zona.pack(expand=True, padx=80, pady=(40,40))

contenedor = ctk.CTkFrame(zona, fg_color="transparent")
contenedor.pack(padx=40, pady=20, fill="both", expand=True)
contenedor.grid_columnconfigure(0, weight=1)

lbl_subtitulo = ctk.CTkLabel(contenedor, text="Opciones de prueba", font=FUENTES["subtitulo"], text_color=PALETA["text"])
lbl_subtitulo.grid(row=0, column=0, pady=(0,12))

page_container = ctk.CTkFrame(contenedor, fg_color="transparent")
page_container.grid(row=1, column=0, sticky="nsew")

# ---------- footer ----------
sep_footer = ctk.CTkFrame(app, height=2, fg_color=PALETA["divider"], corner_radius=0)
sep_footer.pack(fill="x", side="bottom")

footer = ctk.CTkFrame(app, height=40, corner_radius=0, fg_color=PALETA["footer"])
footer.pack(fill="x", side="bottom")
status_left = ctk.CTkLabel(footer, text="Estado: listo", text_color=PALETA["text_muted"], font=FUENTES["estado"])
status_left.pack(side="left", padx=16, pady=6)
status_right = ctk.CTkLabel(footer, text="", text_color=PALETA["text_muted"], font=FUENTES["estado"])
status_right.pack(side="right", padx=16, pady=6)

def set_status(txt):
    status_left.configure(text=f"Estado: {txt}")

def tick():
    status_right.configure(text=f"{time.strftime('%H:%M:%S')}   ·   F11: Pantalla completa   |   Esc: salir")
    app.after(1000, tick)
tick()

# ---------- estilos botones ----------
BTN_MAIN = dict(width=260, height=60, corner_radius=20, fg_color=PALETA["primary"],
                hover_color=PALETA["primary_hover"], text_color=PALETA["text_on_primary"], font=FUENTES["boton"])
BTN_SECOND = dict(corner_radius=16, fg_color=PALETA["primary"], hover_color=PALETA["primary_hover"],
                  text_color=PALETA["text_on_primary"], font=("Segoe UI",14,"bold"), height=44, width=160)

# ---------- Estado examen ----------
class ExamState:
    NONE=0; WRIST=1; ELBOW=2; FULL=3

current_exam = ExamState.NONE
exam_exercises = []   # lista de (cmd, colname) preparados para ejecutar (sin 4)
current_ex_idx = -1
is_acquiring = False

# widgets de la página de examen (serán creados)
exam_title_lbl = None
exam_status_lbl = None
exam_startstop_btn = None
exam_next_btn = None
duracion_entry = None
patient_entry = None

# ---------- util: filtrar ejercicio 4 ----------
def _filter_no_4(ej_list):
    return [ (c,name) for (c,name) in ej_list if c != "4" ]

# ---------- página menú ----------
def clear_page():
    for w in page_container.winfo_children():
        w.destroy()

def show_menu():
    global current_exam, exam_exercises, current_ex_idx, is_acquiring
    clear_page()
    current_exam = ExamState.NONE
    exam_exercises = []
    current_ex_idx = -1
    is_acquiring = False
    set_status("Menú principal")

    frame = ctk.CTkFrame(page_container, fg_color="transparent")
    frame.pack(expand=True, fill="both", pady=10)

    top = ctk.CTkFrame(frame, fg_color="transparent")
    top.pack(pady=(8,6))
    # paciente (cedula) input
    lblp = ctk.CTkLabel(top, text="Cédula:", font=("Segoe UI",12))
    lblp.pack(side="left", padx=(0,6))
    global patient_entry
    patient_entry = ctk.CTkEntry(top, width=200)
    patient_entry.pack(side="left", padx=(0,10))

    # duración general por ejercicio
    lbld = ctk.CTkLabel(top, text="Duración (s):", font=("Segoe UI",12))
    lbld.pack(side="left", padx=(6,6))
    global duracion_entry
    duracion_entry = ctk.CTkEntry(top, width=80)
    duracion_entry.insert(0,"10")  # default 10s
    duracion_entry.pack(side="left")

    # botón
    def mkbtn(text, cmd):
        return ctk.CTkButton(btn_row, text=text, command=cmd, **BTN_MAIN)

    btn_row = ctk.CTkFrame(frame, fg_color="transparent")
    btn_row.pack(pady=22)

    # definiciones originales (pero filtradas para quitar '4')
    wrist_list = _filter_no_4([("1","ROM Flexión/Extensión_°"),
                              ("2","ROM Desviación Ulnar/Radial_°"),
                              ("4","Fuerza de Prensión_Kg")])
    elbow_list = _filter_no_4([("1","ROM Flexión/Extensión_°"),
                               ("3","ROM Pronosupinación_°"),
                               ("4","Fuerza de Prensión_Kg")])
    full_list = _filter_no_4([("1","ROM Flexión/Extensión_°"),
                              ("2","ROM Desviación Ulnar/Radial_°"),
                              ("3","ROM Pronosupinación_°"),
                              ("4","Fuerza de Prensión_Kg")])

    b_wrist = mkbtn("Examen de muñeca", lambda: start_exam(ExamState.WRIST, wrist_list))
    b_elbow = mkbtn("Examen de codo", lambda: start_exam(ExamState.ELBOW, elbow_list))
    b_full  = mkbtn("Examen completo", lambda: start_exam(ExamState.FULL, full_list))

    b_wrist.grid(row=0, column=0, padx=22, pady=10)
    b_elbow.grid(row=0, column=1, padx=22, pady=10)
    b_full.grid(row=0, column=2, padx=22, pady=10)

# ---------- crear página examen ----------
def create_exam_page():
    global exam_title_lbl, exam_status_lbl, exam_startstop_btn, exam_next_btn
    clear_page()
    frame = ctk.CTkFrame(page_container, fg_color="transparent")
    frame.pack(expand=True, fill="both")

    exam_title_lbl = ctk.CTkLabel(frame, text="Examen — Ejercicio", font=("Segoe UI",16,"bold"), text_color=PALETA["text"])
    exam_title_lbl.pack(pady=(6,2))

    exam_status_lbl = ctk.CTkLabel(frame, text="Esperando", font=("Segoe UI",18,"bold"), text_color=PALETA["text"])
    exam_status_lbl.pack(pady=(20,10), fill="x")

    exam_startstop_btn = ctk.CTkButton(frame, text="Iniciar", width=160, height=46, corner_radius=16,
                                       fg_color=PALETA["primary"], hover_color=PALETA["primary_hover"],
                                       text_color=PALETA["text_on_primary"], font=("Segoe UI",14,"bold"),
                                       command=on_exam_start_stop)
    exam_startstop_btn.pack(pady=(10,6))

    bottom = ctk.CTkFrame(frame, fg_color="transparent")
    bottom.pack(side="bottom", fill="x", pady=(20,0), padx=6)

    exam_next_btn = ctk.CTkButton(bottom, text="Siguiente", width=120, height=36, corner_radius=14,
                                  fg_color=PALETA["primary"], hover_color=PALETA["primary_hover"],
                                  text_color=PALETA["text_on_primary"], font=("Segoe UI",13,"bold"),
                                  command=on_exam_next)
    exam_next_btn.pack(side="left")
    exam_next_btn.configure(state="disabled")

# ---------- actualización UI por ejercicio ----------
def update_exam_ui():
    global exam_title_lbl, exam_status_lbl, exam_startstop_btn, exam_next_btn
    if not exam_title_lbl:
        create_exam_page()
    exam_name = {ExamState.WRIST:"Examen de muñeca",
                 ExamState.ELBOW:"Examen de codo",
                 ExamState.FULL:"Examen completo"}.get(current_exam,"Examen")
    ex_num = exam_exercises[current_ex_idx][0] if 0 <= current_ex_idx < len(exam_exercises) else 0
    exam_title_lbl.configure(text=f"{exam_name} — Ejercicio {ex_num}" if ex_num else exam_name)
    exam_status_lbl.configure(text="Esperando")
    exam_startstop_btn.configure(text="Iniciar", command=on_exam_start_stop, state="normal")
    exam_next_btn.configure(text="Siguiente", state="disabled")

# ---------- background capture worker (usa pyserial) ----------
def capture_from_arduino(cmd, nombre_col, duracion):
    """
    Función que corre en el thread y hace la captura; devuelve DataFrame al queue.
    Mantiene la estructura de DataFrame similar al script original.
    """
    try:
        ser = serial.Serial(port=SERIAL_PORT, baudrate=BAUD_RATE, timeout=1)
    except Exception as e:
        result_queue.put(("error", cmd, nombre_col, f"No se pudo abrir puerto: {e}"))
        return

    try:
        time.sleep(1.2)
        # enviar comando y tara similar al original
        ser.write(str(cmd).encode()); time.sleep(0.2)
        ser.write(b" "); time.sleep(0.2)
        t0 = time.time()
        timestamps, valores = [], []
        while (time.time() - t0) < duracion:
            linea = ser.readline().decode(errors="ignore").strip()
            val = _extraer_numero(linea)
            if val is None:
                continue
            ts = time.time() - t0
            timestamps.append(ts); valores.append(val)
        # finalizar
        ser.write(b"e")
    except Exception as e:
        result_queue.put(("error", cmd, nombre_col, f"Error durante captura: {e}"))
        try:
            ser.close()
        except:
            pass
        return
    finally:
        try:
            ser.close()
        except:
            pass

    # construir DataFrame compatible
    df = pd.DataFrame({c: [1]*len(valores) for c in COLS})
    df["timestamp_s"] = timestamps
    df[nombre_col] = valores
    result_queue.put(("ok", cmd, nombre_col, df))

# ---------- handlers de botones (iniciar/detener/siguiente) ----------
def on_exam_start_stop():
    global is_acquiring, exam_startstop_btn, exam_status_lbl, exam_next_btn
    if current_exam == ExamState.NONE or not exam_exercises or current_ex_idx < 0:
        return

    if not is_acquiring:
        # --------------- parseo seguro de duración ---------------
        dur_default = 10
        dur = None
        try:
            # si duracion_entry no existe por cualquier motivo, usamos default
            if duracion_entry is None:
                print("[DEBUG] duracion_entry is None, usando default:", dur_default)
                dur = dur_default
            else:
                raw = duracion_entry.get()
                print(f"[DEBUG] Valor crudo duracion_entry.get(): '{raw}'")
                if raw is None or str(raw).strip() == "":
                    dur = dur_default
                    print("[DEBUG] campo vacío -> usando default:", dur_default)
                else:
                    s = str(raw).strip()
                    # aceptar "10", "10.0", "10,0" -> coger parte entera
                    s = s.replace(",", ".")
                    # si es float, convertir a int por truncamiento
                    if "." in s:
                        try:
                            f = float(s)
                            dur = int(f)
                        except:
                            dur = int(float(re.sub(r"[^\d\.]+", "", s) or dur_default))
                    else:
                        dur = int(re.sub(r"[^\d]+", "", s) or dur_default)

            if dur <= 0:
                raise ValueError("Duración debe ser > 0")
        except Exception as e:
            # mostrar mensaje al usuario y loggear el error
            messagebox.showerror("Duración inválida", "Ingresa una duración en segundos (entero > 0).\n\nDetalle: " + str(e))
            print("[ERROR] Validación duración:", e)
            return
        # --------------- fin parseo ---------------

        # comenzar adquisición: levantar thread
        is_acquiring = True
        exam_status_lbl.configure(text="Realizando toma de datos")
        exam_startstop_btn.configure(text="Detener")
        exam_next_btn.configure(state="disabled")
        cmd, nombre_col = exam_exercises[current_ex_idx]

        t = threading.Thread(target=capture_from_arduino, args=(cmd, nombre_col, dur), daemon=True)
        t.start()
        # arrancar chequeo de resultados
        app.after(200, check_result_queue)
    else:
        # Si está adquiriendo, informamos (no implementamos stop prematuro)
        messagebox.showinfo("En curso", "La captura está diseñada para durar la duración indicada.\nEspera a que termine.")


def on_exam_next():
    global current_ex_idx, session_dfs
    if current_exam == ExamState.NONE or not exam_exercises:
        return
    # si estamos en el último ejercicio: finalizar sesión y guardar
    is_last = (current_ex_idx == len(exam_exercises)-1)
    if is_last:
        # guardar sesión (usar funciones del módulo excel)
        try:
            save_session_and_notify()
        except Exception as e:
            messagebox.showerror("Error guardando", f"Ocurrió un error guardando: {e}")
            return
        show_menu()
        return

    # avanzar al siguiente ejercicio
    current_ex_idx += 1
    update_exam_ui()

# ---------- revisar la cola de resultados que dejan los threads ----------
def check_result_queue():
    global is_acquiring, exam_status_lbl, exam_startstop_btn, exam_next_btn, session_dfs, current_ex_idx
    try:
        msg = result_queue.get_nowait()
    except Empty:
        app.after(200, check_result_queue)
        return

    status, cmd, nombre_col, payload = msg
    if status == "ok":
        df = payload
        session_dfs.append(df)
        is_acquiring = False
        exam_status_lbl.configure(text="Toma de datos realizada")
        # si es el último ejercicio: habilitar Finalizar
        is_last = (current_ex_idx == len(exam_exercises)-1)
        if is_last:
            exam_startstop_btn.configure(text="Finalizar", command=lambda: on_exam_next())
            exam_next_btn.configure(text="Finalizar", state="normal", command=on_exam_next)
        else:
            exam_startstop_btn.configure(text="Iniciar", command=on_exam_start_stop)
            exam_next_btn.configure(text="Siguiente", state="normal", command=on_exam_next)
    else:
        # error
        exam_status_lbl.configure(text=f"Error: {payload}")
        is_acquiring = False
        exam_startstop_btn.configure(text="Iniciar", state="normal")
    # continuar chequeando (si hay otros resultados)
    app.after(200, check_result_queue)

# ---------- iniciar examen (prepara lista de ejercicios sin '4') ----------
def start_exam(kind, ej_list):
    global current_exam, exam_exercises, current_ex_idx, is_acquiring, session_dfs, current_session_patient
    current_exam = kind
    exam_exercises = ej_list[:]  # lista de (cmd, colname); ya filtrada sin 4
    current_ex_idx = 0
    is_acquiring = False
    session_dfs = []  # limpiar capturas previas

    # recoger cédula si existe
    ced = None
    try:
        ced = patient_entry.get().strip()
        if ced == "":
            raise ValueError("vacío")
    except Exception:
        messagebox.showwarning("Paciente", "Ingresa la cédula en el campo superior.")
        return
    current_session_patient = ced

    create_exam_page()
    update_exam_ui()
    set_status({ExamState.WRIST:"Examen de muñeca",
                ExamState.ELBOW:"Examen de codo",
                ExamState.FULL:"Examen completo"}.get(current_exam,"Examen"))

# ---------- guardar la sesión en Excel (usa tus utilidades) ----------
def save_session_and_notify():
    """
    Concatena session_dfs y crea la hoja en Excel usando tus funciones existentes:
    abrir_o_crear_xlsx, asegurar_inicio_simple, escribir_sesion
    """
    if not session_dfs:
        raise RuntimeError("No hay capturas para guardar.")
    # concatenar
    df_final = pd.concat(session_dfs, ignore_index=True)

    # crear ruta (seguimos tu convención MAIN_DIR/paciente/EXCEL_NAME)
    # si preferiste definir MAIN_DIR y EXCEL_NAME en tu módulo original, mantenlos ahí
    from principal import MAIN_DIR as ORIG_MAIN_DIR, EXCEL_NAME as ORIG_EXCEL_NAME
    ruta_xlsx = ORIG_MAIN_DIR / current_session_patient / ORIG_EXCEL_NAME

    wb = abrir_o_crear_xlsx(ruta_xlsx)
    asegurar_inicio_simple(wb)
    ts = time.localtime()
    hoja_nombre = f"sesion_{time.strftime('%Y-%m-%d_%H-%M-%S', ts)}"[:31]
    table_name = f"TablaDatos_{time.strftime('%H%M%S', ts)}"
    hoja_final = escribir_sesion(wb, hoja_nombre, df_final, table_name)
    wb.save(ruta_xlsx)
    messagebox.showinfo("Guardado", f"Sesión guardada en: {ruta_xlsx}\nHoja: {hoja_final}")

# ---------- atajos pantalla ----------
is_fullscreen = False
def toggle_fullscreen(event=None):
    global is_fullscreen
    is_fullscreen = not is_fullscreen
    app.attributes("-fullscreen", is_fullscreen)
def exit_fullscreen(event=None):
    global is_fullscreen
    is_fullscreen = False
    app.attributes("-fullscreen", False)
    try:
        app.state("zoomed")
    except:
        app.attributes("-zoomed", True)
app.bind("<F11>", toggle_fullscreen)
app.bind("<Escape>", exit_fullscreen)

# ---------- inicio ----------
show_menu()
app.mainloop()
