# Firmware p352 RTAS31.68-66-3 — análisis offline, 2026-10-20

Descarga autorizada por el usuario, conservada en
`G:\AtlanticOS\work\firmware-g20-RTAS31.68-66-3`. No se ejecutaron los
binarios, herramientas de flasheo ni instrucciones del paquete. No hubo escrituras
al teléfono. Los binarios, PAC, XML y DTS completos permanecen privados en work/;
el repositorio publica herramientas originales, hashes y resultados de análisis.

## Procedencia y coincidencias

Se localizó el ZIP mediante una [página de StockRom](https://www.stockrom.net/2023/03/xt2128-1-retbr-11r-rtas31-68-66-3.html)
y se descargó del [archivo público enlazado](https://drive.google.com/file/d/1Iei_uO5gcSJtGcebjSLWyXb-Am-HYtVH/view).
Es un **mirror de terceros**, anunciado como XT2128-1 DS / PANG0000BR / RETBR.
La página oficial [Software Fix de Lenovo](https://support.lenovo.com/us/en/downloads/ds101291-rescue-and-smart-assistant-lmsa)
se consultó, pero no proporcionó aquí una URL directa del paquete exacto.
La búsqueda adicional del mirror lolinet encontró releases RETAIL antiguos;
no se descargaron como sustitutos de esta compilación.

| Artefacto | Bytes | SHA-256 |
|---|---:|---|
| ZIP descargado | 2 443 730 615 | `27b7a4050b3d6821f46934e1dbb13442d2e1ffe7921bc3c5d3677394942d5489` |
| P352CE_PVT_11.0_64B_CFC_68-66-3.01-01.pac | 4 838 913 003 | `aa131a143dd5cb448012bb318d271e6f425df1207b8bd1771455d2a8d4b0eb44` |
| boot.img del PAC | 67 108 864 | `4c9401e83db54ab14f2cf7a548e12b018165f8aeb25074d3e182cb4f64762d0f` |

La extracción completa verificó el CRC32 ZIP del PAC (`ea39d0af`). CRC y SHA
detectan cambios de bytes; **no verifican la firma OEM ni una raíz de confianza**.
El CRC interno PAC y AVB/firmas Unisoc no están implementados en el inspector.

El `prop.default` del ramdisk contiene exactamente
`motorola/java_retail/java:11/RTAS31.68-66-3/66-3:user/release-keys`, igual a la
observación ADB de esta unidad, y parche `2023-03-01`. La cabecera codifica
Android 11.0.0 y mes 2023-03. La coincidencia de versión está respaldada por
contenido, además del nombre del archivo.

Una consulta ADB acotada posterior obtuvo `ro.carrier=amxmx` y
`ro.boot.carrier=amxmx`. El paquete anuncia RETBR y su ramdisk usa
`ro.carrier=unknown`. **No está establecida la equivalencia regional ni binaria
con las particiones de este teléfono**. No se presenta como backup exacto ni
como recuperación lista para instalar. Los permisos de Android y Recovery OEM
no permitieron leer las particiones originales; no se solicitó root ni se
cambiaron permisos. El teléfono volvió a Android con boot_completed=1 y slot _a.

## Contenedor y datos observados

El inspector propio acepta únicamente el dialecto PAC `BP_R2.0.1` estudiado.
La tabla contiene 42 registros. Se infirieron y comprobaron sus campos usando
límites, continuidad de payloads, magics internos y tamaño real del archivo:

| Campo | Offset dentro de cabecera / registro | Interpretación de esta muestra |
|---|---|---|
| Longitud PAC | cabecera 0x2c/0x30 | palabras alta/baja de 64 bits |
| Conteo / tabla | cabecera 0x434/0x438 | 42 / 0x84c |
| Marca | cabecera 0x844 | 0xfffafffa |
| Tamaño de registro | registro 0 | 0xa14 |
| ID / nombre | registro 4 / 0x204 | UTF-16LE acotado; último espacio reutilizado para palabras altas |
| Tamaño payload | registro 0x5fc/0x604 | palabras alta/baja |
| Offset payload | registro 0x600/0x610 | palabras alta/baja |

Las palabras altas son necesarias: `boot.img` está en offset PAC 4 690 134 148.
Truncar a 32 bits produciría un análisis incorrecto. Esta reconstrucción de
formato no es un driver de almacenamiento ni un implementador de PAC universal.

La cabecera `ANDROID!` comienza en offset 0 de boot.img: versión **2**, tamaño
1660 y página **2048**. Los límites se interpretan conforme al
[formato AOSP](https://source.android.com/docs/core/architecture/bootloader/boot-image-header),
contrastado con el [header oficial](https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h)
(blob `67ae349583e66424e5af0a3b7a386b42b6c958e0`).

| Sección | Offset dentro de boot.img | Bytes |
|---|---:|---:|
| kernel OEM | 2 048 | 19 017 744 |
| ramdisk gzip/newc | 19 021 824 | 9 184 020 |
| second | 28 207 104 | 0 |
| recovery_dtbo | 28 207 104 | 0 |
| dtb, contenedor Android DT table | 28 207 104 | 155 425 |

Los campos de carga son kernel=0x8000, ramdisk=0x5400000, tags=0x100 y
dtb=0x1f00000. **Son valores de cabecera**: no se convierten en direcciones
físicas de ejecución sin demostrar cómo los trata U-Boot. El inspector no
genera una imagen AtlanticOS a partir de ellos. Tras las secciones estándar
quedan 38 746 112 bytes; su contenido/padding/firmas requiere análisis adicional.

El XML enumera boot_a/b, vendor_boot_a/b y dtb_a/b, pero el PAC no contiene un
payload vendor_boot ni un dtb.img separado. La presencia de particiones no
permite deducir una cabecera v3 ni que vendor_boot sea utilizado en este release.

## Árboles de dispositivo

La sección dtb contiene una Android DT table con **un FDT base de 155 361 bytes**.
`dtbo.img` contiene otra tabla con **cinco overlays**. Todos los IDs/revisiones
de tabla son cero; por sí solos no resuelven la selección de placa.

El árbol base declara Unisoc UMS512 / `sprd,ums512`, GICv3 en 0x12000000
(tamaño 0x20000), redistribuidores en 0x12040000 (tamaño 0x100000, stride 0x20000),
PSCI 0.2 por SMC y timer ARMv8 con PPIs 13/14/11/10 (INTIDs 29/30/27/26).
Son declaraciones del firmware descargado, no comprobaciones eléctricas.
No se habilitó MMIO G20 en AtlanticOS a partir de ellas.

Overlays 0 y 3 anuncian 1H10, 1 y 2 anuncian 1H10 GO, y 4 anuncia 2H10.
La observación del dispositivo `ums512_1h10` reduce candidatos, pero no distingue
0 de 3 ni confirma panel, memoria o revisión. Los overlays contienen botones
GPIO/PMIC con referencias `__fixups__`; no deben usarse los phandles
0xffffffff de un overlay sin resolverlo contra el árbol base. Identificar el
árbol aplicado y medir los botones sigue pendiente.

El parser C original de AtlanticOS validó estructura/rangos de los seis FDT
bajo Clang 19.1.7 + ASan/UBSan. dtc también decodificó los seis con exit 0,
con advertencias registradas (incluidas referencias todavía sin resolver en
overlays). Ninguna de esas pruebas equivale a aplicar correctamente un overlay
o a comprobar que describe el hardware de esta unidad.

## Ruta de `boot` en el U-Boot RETBR

La revisión del 2026-11-13 identifica el registro de `boot` fuera de la región
de cadenas que se examinó inicialmente. La muestra `u-boot-sign.bin` tiene
905 380 bytes y SHA-256
`48c7f8d0169b9ba23ea16db1bc3a9878e7c7a1a153ae7f8b471684d177dcb07e`.
El desensamblado usa AArch64 y una VMA inferida de la cabecera. Todos los
offsets de la tabla se refieren al archivo completo.

| Evidencia | Offset | Observación |
|---|---:|---|
| Nombre y registro de `boot` | `0x97fec`, `0x25700` | Pasa el nombre y el callback `0x2484c` a la función de registro `0x24284`. |
| Registro de `continue` | `0x25738` | Pasa el mismo callback `0x2484c`. |
| Búsqueda e invocación | `0x25a90`, `0x25a2c` | El dispatcher recorre la lista poblada por esa función de registro y entrega al callback la cola del comando, el puntero y la longitud de descarga. |
| Callback compartido | `0x2484c` | Reemplaza los argumentos de entrada y termina en la ruta fija `0x202b4`, que solicita cargar la partición `boot`. |
| Carga desde almacenamiento | `0x1f7a4`, `0x1f878` | Resuelve la partición y llama a la rutina de lectura. Los diagnósticos de esta función identifican la carga y lectura del kernel. |

En AOSP, `boot` ejecuta el `boot.img` descargado a RAM y `continue` continúa
el arranque normal. En la muestra RETBR ambos nombres llegan a la carga
desde la partición. El registro de `boot` o una respuesta `OKAY` por sí sola
sería insuficiente para acreditar ejecución temporal en esta familia.
[Referencia de comandos Fastboot de AOSP](https://android.googlesource.com/platform/system/core/+/refs/heads/main/fastboot/README.md#command-reference).

La observación corresponde al binario del mirror. El teléfono indica `amxmx`
y falta identificar su U-Boot exacto. Sigue abierta la disponibilidad de una
vía OEM a RAM para esa unidad. El registro del desensamblado y sus límites
quedan en [la evidencia](../evidence/g20-stock-dispatch-20261113/manifest.json).

## Reproducción

Con el ZIP ya descargado, la extracción PAC se hace mediante zipfile de Python
con nombre exacto y límite de tamaño; no se abre el archivo .url del mirror.
Para repetir el análisis usando el PAC privado, elegir una salida nueva:

```powershell
python tools/firmware_inspect.py --input work/firmware-g20-RTAS31.68-66-3/P352CE_PVT_11.0_64B_CFC_68-66-3.01-01.pac --output work/firmware-g20-RTAS31.68-66-3/reproduction
python -m unittest tests.test_firmware_inspect -v
```

El inspector limita tamaños/registros, rechaza solapamientos, nombres no leaf,
versiones desconocidas, gzip expansivo/truncado y newc inválido. Solo extrae una
lista fija de archivos a un directorio nuevo bajo work/. No instala enlaces ni
ejecuta contenido del ramdisk. Publica solo seis claves de identidad permitidas.
El soporte Android de esta herramienta se limita a cabeceras v0-v2 y ramdisk
gzip/newc; otras variantes fallan explícitamente.

Para validar un FDT extraído con el parser usado por el preflight, en Debian/WSL:

```sh
clang -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  tools/fdt_check.c atlantic-core/fdt.c -o work/freestanding-tests/fdt-check
work/freestanding-tests/fdt-check work/firmware-g20-RTAS31.68-66-3/reproduction/boot-dtb-0.dtb
```

Resultados y hashes: [evidence/firmware-analysis-20261020](../evidence/firmware-analysis-20261020/manifest.json).
Los tests sintéticos viajan en Git; los binarios OEM no son necesarios para CI.

## Pruebas pendientes y criterio de interpretación

1. Obtener paquete OEM para canal amxmx o comparar boot/dtbo originales mediante
   una vía de lectura autorizada y documentada. Igualdad SHA-256 confirmaría esos
   bytes; mismo fingerprint solo confirma identidad declarada. Firmas OEM requieren
   validación independiente con una clave de confianza identificada.
2. Resolver selección de overlay y árbol final. Comparar model/compatible,
   revisión, memoria, panel y reservas del árbol aplicado. Un match 1H10 aislado
   no justifica escribir registros ni elegir un framebuffer fijo.
3. Identificar el U-Boot `amxmx` exacto mediante un paquete OEM o una lectura
   autorizada, y verificar si la vía OEM utiliza el búfer de descarga para
   ejecutar una imagen sin escribir particiones. Si el binario coincide con el
   SHA-256 RETBR, el nombre `boot` ya tiene una ruta de almacenamiento
   identificada y no basta como candidato a ejecución temporal.
4. Antes de una prueba de RAM, comprobar salida física independiente con botones
   y Android recuperable. El retorno por fastboot reboot ya observado deja
   pendiente el rescate de un payload bloqueado con Power. Después, capturar
   registros reales de entry, build ID y memoria de un payload con marcador
   único, sólo tras identificar una vía temporal y su comportamiento de
   lectura. Verificar que aparece el marcador y que Android sigue arrancando
   por su ruta independiente. Un `OKAY` o el arranque de Android sin marcador
   es un resultado inconcluso para ejecución en RAM.

XNU y el handoff EDK2 continúan sin ejecución física. Este análisis reduce
incertidumbres de formato y aporta datos al BSP independiente; no concede
compatibilidad ni cumple la meta de arranque por sí mismo.
