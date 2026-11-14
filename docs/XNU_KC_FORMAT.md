# Estado nativo de kernel collections

Se compila pexpert/gen/kcformat.c del XNU fijado. El patch 0036 limita los
vnodes a las composiciones con su proveedor BSD. Se conservan las funciones
de cabecera, base, slide y formato tal como las usa XNU. Los tres arrays ARM64
tienen cuatro slots. Las clases primaria, paginable y auxiliar usan los
índices 0, 1 y 3. El bloque de cada array ocupa 32 bytes.

Con destino válido para el formato, la ruta principal devuelve Fileset si
la cabecera registrada tiene filetype MH_FILESET. Si no hay cabecera o el
tipo difiere, consulta el estado nativo static_kernelcache y devuelve Static
o KCGEN. El getter principal admite un destino nulo y retorna true sin
acceder a memoria. En las colecciones auxiliares, un formato devuelto no
acredita presencia. Esta depende de la cabecera registrada y de su lifetime.
Un puntero registrado requiere un mapeo legible.

El arranque independiente conserva la decisión original de arm_init. Una
sección __thread_starts no vacía activa el rebasing threaded nativo. En
KCGEN, el caminador de kalloc exige __PRELINK_TEXT,__text. El enlace estático
de AtlanticOS no altera el indicador nativo de formato. La imagen viable aún
debe satisfacer el layout exigido por arm_vm_init y los consumidores de
sections de startup y kalloc.

El carril experimental pasó 169 unidades y las ocho comprobaciones de
procedencia, código generado y ABI. El enlace estricto de 195 objetos resuelve
cinco nombres anteriores y conserva 341 pendientes. El carril estándar
conserva 13 objetos base. La prueba no produjo una imagen ejecutable.
[Manifiesto](../evidence/xnu-kcformat-20261113/manifest.json).
