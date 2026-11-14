# Instalación de datos retenidos para el arranque XNU

La biblioteca común instala tres cargas en la arena que ya contiene la imagen
materializada. Usa la geometría del boot_args nativo para localizar el árbol
SecureDT y las páginas de argumentos. El trailer ATLC ocupa los últimos 256
bytes antes del árbol. Los intervalos de contexto deben empezar después de los
segmentos de la imagen y terminar en el árbol. El lector comprueba geometría,
versiones y longitud, y devuelve los campos en memoria del llamador.

| Offset | Tamaño | Contenido |
|---:|---:|---|
| 0 | 16 | ATLC, versión mayor/menor 1/0, bytes de trailer y bytes de árbol |
| 16 | 32 | PA y tamaño de arena, base virtual y topOfKernelData |
| 48 | 40 | PA/tamaño de imagen, PA de entrada, PA/tamaño de contexto |
| 88 | 24 | Afinidad MPIDR, frecuencia del contador, EL inicial, cantidad de CPU |
| 112 | 84 | SHA-256 de imagen, commit upstream y hash del patchset |
| 196 | 60 | Reservado en cero |

Los campos numéricos se escriben little endian. El BSP debe aportar los hashes
de los bytes que realmente verifica y conservar el árbol generado para el
inventario y la CPU observados. La composición no otorga confianza criptográfica
ni descubre alias físicos a partir de punteros. El escritor usa una arena
estable y exclusiva. Todos los rechazos ocurren antes de tocar la arena.
Se pone a cero el espacio asignado a contexto, árbol y argumentos, conservando
prefijo, imagen y cola.

Las pruebas con ASan y UBSan comparan el trailer byte por byte, comprueban la
ubicación y los ceros de los tres espacios, y ejercitan 31 rechazos sin escritura
parcial. El objeto ARM64 importa solo el localizador de boot_args. El árbol del
perfil ahora ocupa 560 bytes y contiene un memory-map sin cargas opcionales.
El productor de boot_args coincide en los 1 152 bytes con el inicializador
nativo, y las cinco pruebas QEMU existentes pasan desde EL1 y EL2.

La prueba de traducciones de QEMU recupera el trailer después del reemplazo
de las tablas. Queda por unir esta validación al Platform Expert y verificar
su conservación cuando la imagen real entre en arm_init y arm_vm_init. Los campos propuestos para framebuffer, fallback y
handoff pertenecen al contexto completo de firmware. El trailer de arranque
v1 contiene únicamente los datos de la tabla.
[Manifiesto de evidencia](../evidence/boot-aux-and-native-tree-20261113/manifest.json).
