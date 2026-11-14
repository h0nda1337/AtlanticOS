# Consola nativa XNU con salida a RAM

El perfil experimental compila `serial_console.c` y `serial_general.c` con la
selección explícita `CONFIG_ATLANTIC_RAM_CONSOLE=1`. Se conservan el ring de
XNU, su reserva con guardas, sus locks, el manejo de preempción y las rutas de
vaciado. También se conservan los buffers de impresión de XNU y la conversión
de fin de línea que usa la consola.

La fila RAM de la tabla nativa conecta `putc` a `atlantic_console_memory_putc`.
Ese adaptador añade cada byte al log acotado que ya existe. La entrada de la
fila está vacía. Las dos operaciones nativas de lectura devuelven -1 sin
esperar. Durante el arranque en frío, `PE_init_printf(FALSE)` cambia el puntero
`PE_putc` del adaptador temprano a `console_write_char`. Requiere el estado
anterior y rechaza una segunda promoción o una solicitud de vídeo. La
inicialización nativa del ring queda en `kernel_bootstrap`, después del
arranque de VM. `PE_kputc` y el camino real de `kprintf` conservan el
transporte temprano.

El cambio excluye los consumidores de pantalla, teclado y UART que necesitan
esos servicios durante el arranque en frío. En el camino de reanudación
sigue pendiente el proveedor real de `serial_init`. `os_log_with_args` conserva
su referencia pendiente en el formateador. Esas dependencias requieren
integración propia antes de habilitar las rutas correspondientes.

Las dos unidades añadidas elevan el perfil experimental a 168. Junto con 26
RPC seleccionados, los 194 objetos del enlace estricto dejan 346 símbolos sin
proveedor y cero duplicados. Se cierran 19 referencias del perfil anterior,
al incorporar consola y excluir llamadas a periféricos fuera del perfil RAM.
El carril estándar conserva sus 13 unidades. Se contrastaron preprocesado y
relocations ARM64, incluyendo la fase PE, el salto al ring nativo y la rama de
entrada sin transporte. Sigue pendiente observar ejecución con VM inicializada
y recuperar el log desde un BSP físico.
[Manifiesto de evidencia](../evidence/xnu-ram-console-20261113/manifest.json).
