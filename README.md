# Agradecimientos

Un agradecimiento especial a **Sparkadium** y al proyecto original **[Cheap-Yellow-MP3-Player](https://github.com/Sparkadium/Cheap-Yellow-MP3-Player)** — me ayudó a retomar el trabajo en este proyecto.

# CYD Album Player

Reproductor de música ESP32 "CYD" que:
- Escanea la tarjeta SD por álbumes (carpetas) y reproduce pistas `.mp3` / `.wav`.
- Muestra una **pantalla de reproductor estilo DAP** (tema oscuro, barra de estado, **visualizador de espectro** — 16 barras de frecuencia impulsadas por el audio, barra de progreso, marcas de tiempo, línea técnica en cian) más controles de volumen **+/−** y controles de transporte táctiles.
- Usa el **LED RGB integrado** en la parte trasera como indicador de Bluetooth / reproducción.
- Actúa como **fuente Bluetooth A2DP** (envía el audio a un altavoz/auricular Bluetooth).
- **Configuración Bluetooth:** al arrancar, **escanea dispositivos de audio cercanos** y permite **seleccionar un altavoz/auricular desde una lista táctil** (sin nombre de dispositivo fijo en el código).
- **Energía de pantalla:** tras **30 segundos** sin toque, la **retroiluminación se apaga**; **el toque es ignorado** mientras está apagada; el botón **BOOT** (**GPIO 0**) **activa/desactiva** la retroiluminación (Bluetooth y reproducción siguen funcionando).

## Capturas de pantalla

Coloca estos JPEG en la **raíz del repositorio** (mismo directorio que `README.md`) al hacer push — el Markdown de abajo los referencia **por nombre de archivo**.

| Archivo | Qué muestra |
|---------|-------------|
| **`AlbumPlaylist.jpeg`** | **Explorador de álbumes:** lista de carpetas, botón **Player** en la cabecera (volver a la reproducción cuando existen pistas), botón **play** en la fila de ruta (misma acción), paginación **PREV/NEXT**, estado BT en la cabecera. |
| **`Execution_screen.jpeg`** | **Pantalla de reproducción:** barras de espectro ("SPECTRUM"), título de pista, progreso y tiempos, línea de álbum, línea técnica, volumen **−/+%/+**, controles de transporte, icono de lista (arriba a la derecha). |

![Explorador de álbumes — archivo: AlbumPlaylist.jpeg](AlbumPlaylist.jpeg)

![Pantalla de reproducción — archivo: Execution_screen.jpeg](Execution_screen.jpeg)

## LED RGB de estado (parte trasera del CYD)

En las placas **ESP32-2432S028R** típicas, el LED RGB trasero usa tres GPIOs y es **activo en bajo** (LOW = LED encendido):

| Canal  | GPIO |
|--------|------|
| Rojo   | 4    |
| Verde  | 16   |
| Azul   | 17   |

**Comportamiento en este sketch**

| Estado | Patrón del LED |
|--------|----------------|
| Bluetooth **no** conectado (emparejando / buscando) | Parpadeo alternado **rojo** y **azul**. |
| Bluetooth conectado y pista **reproduciéndose** | Alternancia **verde** y **azul** (un color a la vez, ~450 ms). |
| Bluetooth conectado pero **pausado** / **detenido** | LED apagado. |

Al arrancar, mientras Bluetooth escanea o empareja, el sketch ejecuta la misma rutina de actualización del LED para que este anime hasta que se conecte un auricular/altavoz.

Los clones pueden usar pines o polaridad distintos; ajusta `RGB_LED_RED` / `RGB_LED_GREEN` / `RGB_LED_BLUE` en `CYDAlbumPlayer.ino` si es necesario.

**Nota (frente "R21" / cúpula transparente):** En muchas placas CYD, la serigrafía **R21** es el designador de una **resistencia**, no un LED controlado por software. Un componente transparente en el frente suele ser el **LDR** (sensor de luz en GPIO 34) — se lee como entrada analógica, no se activa como el LED RGB.

## Interfaz del reproductor (pantalla principal de reproducción)

La vista de reproducción está diseñada para un panel portrait de **240×320** y está inspirada en reproductores de audio digital compactos (alto contraste, mínimo decorado).

- **Barra superior:** icono de nota, **índice de pista / total**, **nombre de la carpeta del álbum** (truncado), distintivo **BT**, **icono de lista** (arriba a la derecha) para abrir la **lista de álbumes** sin detener la reproducción.
- **Línea de título:** nombre de la pista actual (nombre de archivo sin extensión), centrado sobre el visualizador.
- **Panel de espectro ("SPECTRUM"):** **16 barras verticales** que responden a la música. El sketch captura **muestras mono** (L+R tras ganancia de volumen) del camino de audio, ejecuta un **bloque con ventana Hamming** (256 muestras) y filtros **Goertzel** a frecuencias fijas (~80 Hz–18 kHz). **AGC por banda** y realce de agudos mantienen los altos visibles; MP3 asume una tasa de muestreo de **44,1 kHz** para el mapeo de bins (WAV usa la tasa analizada). El área de barras se refresca ~20 veces/s mientras se muestra la pantalla del reproductor; las barras decaen al pausar/detener.
- **Fila de volumen:** botones táctiles **− / porcentaje / +** (ver `PL_VOLUME_Y`).
- **Bloque de información (actualizado ~cada 450 ms durante reproducción o pausa):**
  - **Barra de progreso** delgada (relleno rojo cuando se conoce la duración).
  - Tiempos **transcurrido** y **total** como `HH:MM:SS`; el total muestra `--:--:--` cuando la duración es desconocida.
  - Línea de carpeta (texto tenue).
  - Línea técnica en **cian**: **`WAV / frecuencia-muestreo Hz / PCM`** cuando se analiza desde la cabecera del archivo, o **`MP3 / ~128 kbps (aprox.)`** para MP3.

**Temporización**

- El tiempo transcurrido respeta **pausa / reanudación** (reloj de pared con duración de pausa acumulada).
- La duración y tasa de muestreo de **WAV** provienen del análisis de los bloques `fmt` / `data` en la tarjeta SD.
- La duración total y tasa de bits de **MP3** se **estiman** a partir del tamaño del archivo (asume ~128 kbps CBR); los archivos VBR o inusuales pueden diferir — la UI etiqueta el MP3 como estimado.

**Zonas táctiles**

- **Icono de lista** (arriba a la derecha, `PL_BACK_BTN_*`) cambia al explorador de álbumes; **la reproducción continúa** (estado de pausa/play sin cambios).
- **Player** (cabecera del explorador, cuando existen pistas) o el botón **play** en la fila de ruta: regresa a la pantalla de reproducción **sin** reiniciar la pista.
- Tocar un **álbum distinto** en la lista **detiene brevemente la decodificación actual** antes de escanear la nueva carpeta en la tarjeta SD (evita contención SPI/SD con la transmisión MP3, que antes causaba tartamudeo en Bluetooth). Luego la reproducción comienza desde la pista 1 del nuevo álbum. Mientras se navega (lista abierta), el sketch también **alimenta el decodificador de audio** durante los redibujos TFT y las esperas táctiles para mantener el buffer más lleno.
- **Prev / Play–Pausa / Next** están en la barra de transporte inferior (ver `PL_TRANSPORT_Y` en el sketch).

## Hardware / Pinout usado por este sketch

El mapeo de pines de este proyecto está definido en `CYDAlbumPlayer.ino` y coincide con el archivo de configuración TFT incluido (`Setup_User.h`).

- Tarjeta SD (SPI):
  - `SD_CS = 5` (`#define SD_CS 5`)
- Retroiluminación TFT:
  - `TFT_BL = 21` (`#define TFT_BL 21`) — `HIGH` enciende la retroiluminación en este sketch; `LOW` la apaga.
- Botón **BOOT** (pulsador de placa usado en firmware):
  - `BOOT_BUTTON_PIN = 0` — leído con `INPUT_PULLUP`; **pulsado** = LOW. Se usa como **conmutador con antirrebote** para la retroiluminación (ver **Energía de pantalla y toque** más abajo). **GPIO 0 es un pin de strapping del ESP32:** si BOOT se mantiene en bajo mientras el chip **se reinicia**, el módulo puede entrar en **modo de descarga / flash** en lugar de ejecutar el sketch; suelta BOOT y reinicia para arrancar normalmente.
- Controlador táctil (XPT2046 en su propio bus HSPI):
  - `TOUCH_CLK = 25`
  - `TOUCH_MISO = 39`
  - `TOUCH_MOSI = 32`
  - `TOUCH_CS = 33`
  - `TOUCH_IRQ = 36`

Los pines de dibujo TFT y SPI TFT (TFT_MISO/TFT_MOSI/TFT_SCLK/TFT_CS/TFT_DC/…) son configurados por TFT_eSPI usando `Setup_User.h`.

## Configuración TFT (debe coincidir con tu variante exacta de pantalla CYD)

Este repositorio incluye una copia de referencia del archivo de configuración de TFT_eSPI como `Setup_User.h`.

### 1) Instalar/usar esta configuración en tu biblioteca TFT_eSPI

TFT_eSPI no lee automáticamente `Setup_User.h` desde la raíz del proyecto. Debes aplicarlo a tu instalación de TFT_eSPI.

1. Abre `Setup_User.h` (en este proyecto).
2. Copia su contenido (o reemplaza el archivo) en la carpeta de tu biblioteca TFT_eSPI:
   - `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`

### 2) Seleccionar el controlador de pantalla correcto (crítico)

Las placas CYD vienen en múltiples variantes de controlador de pantalla. En `Setup_User.h`, elige exactamente UN controlador:

- `ILI9341_DRIVER` (v1 original, 1× Micro-USB)
- `ILI9341_2_DRIVER` (v1 controlador alternativo, 1× Micro-USB)
- `ST7789_2_DRIVER` (v2/v3 más reciente, USB-C + Micro)

Si la pantalla aparece en blanco/blanca:
- Prueba primero `ILI9341_DRIVER` (luego cambia a `ILI9341_2_DRIVER` si es necesario).
- Si tu placa tiene 2 puertos USB (USB-C + Micro), usa `ST7789_2_DRIVER`.

### 3) Corrección de orden de colores / inversión (si los colores se ven incorrectos)

En `Setup_User.h`:
- Si los colores tienen rojo/azul invertidos, cambia `TFT_RGB_ORDER` (entre `TFT_RGB` y `TFT_BGR`).
- Si usas `ST7789_2_DRIVER` y los colores se ven lavados/invertidos, descomenta `TFT_INVERSION_ON`.

### 4) Ajuste de gamma en el sketch

`CYDAlbumPlayer.ino` aplica un pequeño ajuste de gamma pensado para el controlador `ILI9341_2`:
- `tft.writecommand(0x26); ...`

Si cambias tu controlador por otro (p. ej., ST7789) y los colores se ven mal, considera comentar ese bloque de gamma o ajustarlo.

## Selección de dispositivo Bluetooth (nombre fijo eliminado)

**Actualización:** El emparejamiento Bluetooth ya no usa un nombre de altavoz codificado en el sketch. En cada arranque, el reproductor **escanea dispositivos de audio cercanos** (clase A2DP sink), **los lista en la pantalla táctil** (con potencia de señal), y **tocas el auricular o altavoz** que quieras. La interfaz pasa al explorador de música SD solo **después** de que A2DP se conecta.

Notas de implementación:

- El arranque usa `BluetoothA2DPSource` con un **callback de SSID / inquiry** para recopilar dispositivos descubiertos y aceptar la **dirección** del que tocaste.
- **La reconexión automática está desactivada** al arrancar y el **último par guardado se borra** para que el dispositivo siempre pase por el selector (no estás limitado a un nombre fijo como el antiguo ejemplo `"E6"`).
- Solo aparecen en la lista los dispositivos que reportan una **Clase de Dispositivo** compatible (audio/renderizado, filtrado por ESP32-A2DP).

Tras la pantalla de bienvenida **WELCOME**, puede aparecer brevemente **"Preparando Bluetooth…"**, luego la pantalla del selector (**"Bluetooth — elegir altavoz"**, **"Escaneando…"**, **"Toca un dispositivo:"**). Puede haber un breve retardo antes de los primeros resultados mientras la pila se inicializa.

## Energía de pantalla y toque (tiempo de espera de retroiluminación, conmutador BOOT)

El sketch trata **"pantalla apagada"** como **retroiluminación apagada** en **`TFT_BL` (GPIO 21)**. El controlador TFT mantiene su última imagen en memoria; solo se conmuta la retroiluminación, por lo que la reproducción y Bluetooth **no** se detienen.

| Comportamiento | Detalle |
|----------------|---------|
| **Tiempo de espera de inactividad** | Si no hay **interacción táctil válida** durante **`DISPLAY_IDLE_OFF_MS`** (por defecto **30 segundos**, en `CYDAlbumPlayer.ino`), la retroiluminación se pone en **LOW** y la pantalla parece apagada. |
| **Toque mientras "apagada"** | **`handleTouch()`** y el manejador del **selector Bluetooth** **retornan inmediatamente** cuando la retroiluminación está apagada: el código **no** lee el controlador táctil para acciones de UI, por lo que los golpes en el panel no cambian pista, volumen ni el estado del explorador. |
| **Botón BOOT** | Una **pulsación corta** en el interruptor **BOOT** (**GPIO 0**, con antirrebote por software) **conmuta** la retroiluminación: **encendido → apagado** o **apagado → encendido**. Al volver a encenderse, el **explorador de álbumes o la pantalla del reproductor se redibuja** una vez para que la UI coincida con el estado actual. |
| **Atenuación automática vs manual** | El mismo conmutador **BOOT** funciona tanto si la retroiluminación fue apagada por el **temporizador de inactividad** como si fue apagada **presionando BOOT** mientras la pantalla estaba encendida. |
| **Mientras la retroiluminación está apagada** | La lógica del **LED RGB** sigue ejecutándose. **A2DP** y la **decodificación de audio** siguen funcionando. Las actualizaciones de la **barra de progreso** y el **espectro** se **omiten** (menos tráfico SPI mientras no se puede ver el panel). |
| **Selector Bluetooth** | Durante el emparejamiento inicial, el **tiempo de espera de inactividad** y **BOOT** siguen aplicándose; **el toque se ignora** si la retroiluminación está apagada, así que usa **BOOT** para encender el panel de nuevo si es necesario. |

**Ajuste:** cambia **`DISPLAY_IDLE_OFF_MS`** cerca de la parte superior de `CYDAlbumPlayer.ino` si quieres un tiempo de espera mayor o menor.

**Nota de clon / hardware:** Algunas revisiones de CYD cabléan la retroiluminación de forma diferente (siempre encendida, o lógica invertida). Si **encendido/apagado** parece invertido, intercambia los niveles **`HIGH`** / **`LOW`** usados para **`TFT_BL`** en el sketch.

## Estructura de la tarjeta SD esperada por este proyecto

El código espera:
- Álbum = una carpeta bajo la raíz de la tarjeta SD (`/`)
- Archivos de pistas dentro de cada carpeta de álbum
  - `.mp3`
  - `.wav`

El proyecto ignora una carpeta conocida de Windows:
- `System Volume Information`

**Orden de pistas dentro de un álbum:** los archivos se ordenan **alfabéticamente por ruta completa** (p. ej., `/Album/track01.mp3` antes que `/Album/track02.mp3`). No se lee ningún metadato ID3 para ordenar.

## Pantalla de bienvenida al arranque (logo de alta calidad opcional)

Al arrancar, después de montar la tarjeta SD, el sketch muestra una pantalla de **BIENVENIDA** y, a continuación:

1. **Tu propio logo** desde la tarjeta SD, o  
2. Un dibujo **procedural integrado** estilo GUARA CREW (respaldo).

### Archivo de logo personalizado (recomendado para una coincidencia perfecta)

Coloca un archivo **raw RGB565** en la raíz de la tarjeta SD:

- **Ruta:** `/guara565.raw` (nombre exacto)
- **Tamaño:** exactamente **200 × 218** píxeles × 2 bytes = **87 200 bytes**
- **Formato:** orden de filas, **RGB565 de 16 bits**, **little-endian** por píxel (estándar para ESP/TFT_eSPI `pushImage`)

Si el archivo no existe o el tamaño es incorrecto, se usa el logo procedural.

Puedes generar el archivo con un pequeño script de Python (redimensiona tu PNG primero):

```python
from PIL import Image

W, H = 200, 218
img = Image.open("logo.png").convert("RGB").resize((W, H))
out = bytearray()
for y in range(H):
    for x in range(W):
        r, g, b = img.getpixel((x, y))
        c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += bytes((c & 0xFF, c >> 8))  # little-endian
open("guara565.raw", "wb").write(out)
```

Copia `guara565.raw` a la raíz de la tarjeta SD.

## Calibración táctil (opcional)

Si los puntos táctiles no coinciden con los botones/áreas de menú, ajusta las constantes de calibración en `CYDAlbumPlayer.ino`:
- `TS_MINX`, `TS_MAXX`, `TS_MINY`, `TS_MAXY`

## Bibliotecas utilizadas (típicas)

Necesitas estas dependencias disponibles en Arduino IDE:
- `TFT_eSPI`
- `XPT2046_Touchscreen`
- `ESP32-A2DP` (Phil Schatzmann)
- `ESP8266Audio` (proporciona `AudioFileSourceSD`, `AudioGeneratorMP3`, `AudioGeneratorWAV`, `AudioOutput`)

## Compilar y cargar

1. Selecciona tu placa ESP32 en Arduino IDE.
2. Asegúrate de que TFT_eSPI esté configurado usando la referencia `Setup_User.h`.
3. Compila y carga `CYDAlbumPlayer.ino`. En la primera ejecución tras la carga, usa la lista en pantalla para elegir tu altavoz o auricular Bluetooth.
