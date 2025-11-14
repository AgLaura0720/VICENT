# -*- coding: utf-8 -*-
import tkinter as tk
from tkinter import messagebox
import customtkinter as ctk
import time
from PIL import Image, ImageTk

# ================= APARIENCIA GLOBAL =================
ctk.set_appearance_mode("Light")
ctk.set_default_color_theme("blue")

# ================= PALETA (CLARA) =================
PALETA = {
    "primary":        "#1565c0",
    "primary_hover":  "#0d47a1",
    "primary_muted":  "#90caf9",

    "bg":             "#eaeaea",
    "surface":        "#f5f7fb",
    "surface_shadow": "#d0d4dc",
    "divider":        "#d7dbe3",
    "footer":         "#eef1f6",

    "text":           "#1f2937",
    "text_muted":     "#374151",
    "text_on_primary":"#ffffff",
}

# ================= TIPOGRAFÍA =================
FUENTES = {
    "titulo":     ("Segoe UI", 26, "bold"),
    "subtitulo":  ("Segoe UI", 18, "bold"),
    "boton":      ("Segoe UI", 18, "bold"),
    "estado":     ("Segoe UI", 12),
    "contenido":  ("Segoe UI", 14),
}

# ================= VENTANA PRINCIPAL =================
app = ctk.CTk()
app.title("UpperSense — Panel de Control")
app.configure(fg_color=PALETA["bg"])

# Icono de ventana / barra de tareas (ICO real)
try:
    app.iconbitmap("loguito.ico")
except Exception as e:
    print("No se pudo cargar loguito.ico:", e)

# Arrancar maximizada
try:
    app.state("zoomed")
except:
    app.attributes("-zoomed", True)

is_fullscreen = False


def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))


# ================= HEADER (DEGRADADO + LOGO + TÍTULO) =================
ALTO_HEADER = 110

header_wrap = ctk.CTkFrame(app, height=ALTO_HEADER, fg_color="transparent", corner_radius=0)
header_wrap.pack(fill="x", side="top")

header_canvas = tk.Canvas(header_wrap, height=ALTO_HEADER, highlightthickness=0, bd=0)
header_canvas.pack(fill="x", side="top")

# Cargar logo del encabezado desde loguito.png (transparente)
header_logo_tk = None
try:
    logo_pil = Image.open("loguito.png").resize((60, 60), Image.LANCZOS)
    header_logo_tk = ImageTk.PhotoImage(logo_pil)
except Exception as e:
    print("No se pudo cargar loguito.png para el encabezado:", e)


def dibujar_degradado(event=None):
    header_canvas.delete("all")
    w = header_canvas.winfo_width()
    h = header_canvas.winfo_height()

    # Degradado más fuerte
    col_a = hex_to_rgb("#002b6f")   # azul marino
    col_b = hex_to_rgb("#4ea1ff")   # azul brillante
    r1, g1, b1 = col_a
    r2, g2, b2 = col_b

    for x in range(max(1, w)):
        t = x / max(1, w - 1)
        r = int(r1 + (r2 - r1) * t)
        g = int(g1 + (g2 - g1) * t)
        b = int(b1 + (b2 - b1) * t)
        header_canvas.create_line(x, 0, x, h, fill=f"#{r:02x}{g:02x}{b:02x}")

    # Logo a la izquierda
    if header_logo_tk is not None:
        header_canvas.create_image(
            60, h // 2,
            image=header_logo_tk,
            anchor="center"
        )
        # guardar referencia para que no se libere
        header_canvas.logo_ref = header_logo_tk

    # Título centrado
    header_canvas.create_text(
        w // 2, h // 2,
        text="Panel de Control — Funcionalidad de muñeca y codo",
        fill="#ffffff",
        font=FUENTES["titulo"]
    )


app.bind("<Configure>", dibujar_degradado)

divider_header = ctk.CTkFrame(app, height=3, fg_color=PALETA["divider"], corner_radius=0)
divider_header.pack(fill="x", side="top")

# ================= ZONA CENTRAL (TARJETA) =================
zona = ctk.CTkFrame(app, corner_radius=20, fg_color=PALETA["surface"])
zona.pack(expand=True, padx=80, pady=(40, 40))

sombra = ctk.CTkFrame(app, corner_radius=20, fg_color=PALETA["surface_shadow"])
sombra.place(in_=zona, x=6, y=6, relwidth=1, relheight=1)
zona.lift()

contenedor = ctk.CTkFrame(zona, fg_color="transparent")
contenedor.pack(padx=50, pady=40, fill="both", expand=True)

contenedor.grid_columnconfigure(0, weight=1)
contenedor.grid_columnconfigure(1, weight=1)
contenedor.grid_rowconfigure(1, weight=1)

lbl_subtitulo = ctk.CTkLabel(
    contenedor,
    text="Opciones de prueba",
    font=FUENTES["subtitulo"],
    text_color=PALETA["text"]
)
lbl_subtitulo.grid(row=0, column=0, columnspan=2, pady=(0, 20))

# Contenedor donde irán las diferentes "páginas"
page_container = ctk.CTkFrame(contenedor, fg_color="transparent")
page_container.grid(row=1, column=0, columnspan=2, sticky="nsew")

# ================= FOOTER (ESTADO + RELOJ) =================
sep_footer = ctk.CTkFrame(app, height=2, fg_color=PALETA["divider"], corner_radius=0)
sep_footer.pack(fill="x", side="bottom")

footer = ctk.CTkFrame(app, height=40, corner_radius=0, fg_color=PALETA["footer"])
footer.pack(fill="x", side="bottom")

status_left = ctk.CTkLabel(
    footer,
    text="Estado: listo",
    text_color=PALETA["text_muted"],
    font=FUENTES["estado"]
)
status_left.pack(side="left", padx=16, pady=6)

status_right = ctk.CTkLabel(
    footer,
    text="",
    text_color=PALETA["text_muted"],
    font=FUENTES["estado"],
)
status_right.pack(side="right", padx=16, pady=6)


def set_status(txt: str):
    status_left.configure(text=f"Estado: {txt}")


def tick():
    hora = time.strftime("%H:%M:%S")
    status_right.configure(
        text=f"{hora}   ·   F11: Pantalla completa   |   Esc: salir"
    )
    app.after(1000, tick)


tick()

# ================= BOTONES / ESTILOS =================
BTN_MAIN = {
    "width": 220,
    "height": 60,
    "corner_radius": 18,
    "fg_color": PALETA["primary"],
    "hover_color": PALETA["primary_hover"],
    "text_color": "#ffffff",
    "font": FUENTES["boton"],
}

BTN_SECONDARY = {
    "corner_radius": 16,
    "fg_color": PALETA["primary"],
    "hover_color": PALETA["primary_hover"],
    "text_color": "#ffffff",
    "font": ("Segoe UI", 14, "bold"),
    "height": 40,
    "width": 160,
}

# ================= PÁGINAS =================
def limpiar_page():
    for w in page_container.winfo_children():
        w.destroy()


def mostrar_menu():
    limpiar_page()
    lbl_subtitulo.configure(text="Opciones de prueba")

    fila = ctk.CTkFrame(page_container, fg_color="transparent")
    fila.pack(pady=20)

    btn_rango = ctk.CTkButton(
        fila, text="Rango de Movimiento",
        command=mostrar_pagina_rango,
        **BTN_MAIN
    )
    btn_rango.grid(row=0, column=0, padx=30, pady=10)

    btn_fuerza = ctk.CTkButton(
        fila, text="Fuerza de Prensión",
        command=mostrar_pagina_fuerza,
        **BTN_MAIN
    )
    btn_fuerza.grid(row=0, column=1, padx=30, pady=10)

    set_status("Menú principal")


def mostrar_pagina_rango():
    limpiar_page()
    lbl_subtitulo.configure(text="Prueba: Rango de Movimiento")

    top_bar = ctk.CTkFrame(page_container, fg_color="transparent")
    top_bar.pack(fill="x", pady=(0, 10))

    btn_volver = ctk.CTkButton(
        top_bar,
        text="← Volver",
        width=100,
        height=32,
        corner_radius=10,
        fg_color=PALETA["primary_muted"],
        hover_color=PALETA["primary"],
        text_color="#ffffff",
        font=("Segoe UI", 12, "bold"),
        command=mostrar_menu
    )
    btn_volver.pack(side="left", padx=5)

    contenido = ctk.CTkFrame(page_container, fg_color="transparent")
    contenido.pack(pady=10, fill="both", expand=True)

    lbl_tipo = ctk.CTkLabel(
        contenido,
        text="Seleccione el tipo de examen:",
        font=("Segoe UI", 14, "bold"),
        text_color=PALETA["text"]
    )
    lbl_tipo.pack(pady=(10, 5))

    opciones = ["Examen de muñeca", "Examen de codo"]
    combo_examen = ctk.CTkComboBox(
        contenido,
        values=opciones,
        font=("Segoe UI", 13),
        width=260,
        height=32,
        border_width=1,
        fg_color="white",
        text_color="black",
        button_color=PALETA["primary"],
        button_hover_color=PALETA["primary_hover"]
    )
    combo_examen.set(opciones[0])
    combo_examen.pack(pady=10)

    frame_botones = ctk.CTkFrame(contenido, fg_color="transparent")
    frame_botones.pack(pady=20)

    def iniciar_prueba():
        seleccion = combo_examen.get()
        set_status(f"Iniciando prueba de ROM: {seleccion}")
        messagebox.showinfo(
            "ROM",
            f"Se iniciará la prueba de rango de movimiento:\n{seleccion}"
        )

    btn_iniciar = ctk.CTkButton(
        frame_botones, text="Iniciar prueba",
        command=iniciar_prueba,
        **BTN_SECONDARY
    )
    btn_iniciar.grid(row=0, column=0, padx=10)

    btn_cancelar = ctk.CTkButton(
        frame_botones, text="Cancelar",
        command=mostrar_menu,
        **BTN_SECONDARY
    )
    btn_cancelar.grid(row=0, column=1, padx=10)

    set_status("Página: Rango de Movimiento")


def mostrar_pagina_fuerza():
    limpiar_page()
    lbl_subtitulo.configure(text="Prueba: Fuerza de Prensión")

    top_bar = ctk.CTkFrame(page_container, fg_color="transparent")
    top_bar.pack(fill="x", pady=(0, 10))

    btn_volver = ctk.CTkButton(
        top_bar,
        text="← Volver",
        width=100,
        height=32,
        corner_radius=10,
        fg_color=PALETA["primary_muted"],
        hover_color=PALETA["primary"],
        text_color="#ffffff",
        font=("Segoe UI", 12, "bold"),
        command=mostrar_menu
    )
    btn_volver.pack(side="left", padx=5)

    contenido = ctk.CTkFrame(page_container, fg_color="transparent")
    contenido.pack(pady=30, fill="both", expand=True)

    lbl = ctk.CTkLabel(
        contenido,
        text="Prueba de fuerza de prensión\n(Módulo en desarrollo)",
        font=("Segoe UI", 16, "bold"),
        text_color=PALETA["text"]
        

    )
    lbl.pack(pady=20)

    set_status("Página: Fuerza de Prensión")


# ================= PANTALLA COMPLETA =================
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

# ================= INICIO =================
mostrar_menu()
app.mainloop()
