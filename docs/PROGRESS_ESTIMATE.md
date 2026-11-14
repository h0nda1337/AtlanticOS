# Estimación de avance — 2026-11-13

Estimación de ingeniería: **35 % hacia el primer arranque físico instrumentado
de XNU en el G20**, con rango **25–45 %**. Para **XNU físico → EDK2**, **~28 %
(cerca del 30 %)**, con rango **20–35 %**. Los rangos expresan la incertidumbre
sobre el trabajo restante, en especial la carga temporal y el BSP físico.

La revisión del 2026-11-02 daba **~30 %** y **~25 %**, respectivamente. Partía
del commit 1073f78e9fa3b9ca1f36177eb51050e44c10483d y de 436 símbolos pendientes
en enlace estricto. La tabla conserva los pesos de esa revisión y explica
los cinco puntos de crédito adicional. [Registro de la revisión anterior](../evidence/progress-review-20261102/review.json).

| Bloque hasta el primer XNU físico | Peso | Crédito anterior → actual | Evidencia para el crédito |
|---|---:|---:|---|
| Infraestructura inicial | 10 | 10 → 10 | Clang reproducible desde fuentes, MIG oficial y CI. C y C++ tienen pruebas propias en el compilador |
| Integración nativa y enlace | 25 | 10 → 13 | Metadata C++ del allocator verificada y runtime Blocks nativo de XNU, catálogo ARM, regiones compartidas con commpage, metadata nativa de versión y consola nativa con salida a RAM. 168 unidades base y 26 RPC en enlace estricto, con 346 símbolos pendientes y cero duplicados. Sigue pendiente el Mach-O |
| Entry, datos, carga y MMU | 20 | 7 → 9 | Entry propio, productor de boot_args y dos entregas del timer virtual con ACK/EOI por GICv3 para cada recorrido QEMU. Se verificó la colocación del fixture Mach-O. Faltan contexto retenido, propiedad de RAM y traducciones para XNU |
| Bootstrap Mach operativo | 15 | 0 → 0 | Pendiente ejecutar XNU con VM/allocator y un contexto Mach instrumentado |
| BSP e instrumentación física G20 | 20 | 0 → 0 | Los dos overlays del mirror coinciden en GIC, timer, CPU y PSCI. Falta ejecutar e instrumentar el BSP original en el T700 |
| Carga temporal y recuperación independiente | 10 | 3 → 3 | Identidad y bootloader desbloqueado observados. La carga temporal y la recuperación independiente tras un fallo del payload siguen pendientes. El U-Boot RETBR estudiado deriva `boot` a la partición. Falta acreditar el comportamiento del amxmx de la unidad |
| **Total estimado** | **100** | **30 → 35** | **Primera ejecución física pendiente** |

Los tres puntos de integración reflejan la incorporación de subsistemas
nativos y la eliminación de un requisito de compilación de C++. Los dos puntos
de entry reflejan el ciclo de IRQ observado. Desde la revisión anterior, el
enlace pasó de 436 a 346 nombres pendientes. La cifra de símbolos sirve como
diagnóstico y el crédito se asigna al trabajo técnico de la tabla.

Para llegar desde XNU hasta EDK2 se conserva el modelo provisional de 80 %
para el primer arranque y 20 % para quiesce, handoff y verificación, todavía
pendientes. Sale 0,8 × 35 = 28 %. AtlanticBIOS completo, VNC, Wi-Fi y otras
placas conservan su alcance posterior.

[Evidencia de compilación y enlace](../evidence/xnu-ram-console-20261113/manifest.json) ·
[Timer e IRQ en QEMU](../evidence/qemu-timer-irq-20261113/manifest.json) ·
[Colocación del fixture](../evidence/image-materialize-20261113/manifest.json) ·
[Mirror RETBR y pruebas físicas pendientes](STOCK_FIRMWARE_ANALYSIS.md).

El próximo hito sustantivo es un **ejecutable estático XNU real enlazado**, con
proveedores explícitos y sin imports ignorados o funciones ficticias. Después:
bootstrap nativo en laboratorio con contexto Mach válido, BSP original G20,
carga temporal/recuperación independientes y evidencia física. El handoff
necesita evidencia propia desde ese XNU operativo.

El número de objetos o símbolos no mide linealmente el esfuerzo: añadir un
proveedor puede descubrir dependencias nuevas. La RAM residente sigue sin medir.

Para evitar que otro porcentaje oculte lo pendiente, cada informe de avance debe
indicar también estas puertas. Actualmente están **0 de 4** superadas hacia el
primer XNU físico; el handoff añade una quinta. Son criterios de aceptación,
no tareas de igual tamaño: 0/4 no significa cero trabajo realizado, y 3/4 no
significaría el 75 % del esfuerzo.

| Puerta | Evidencia necesaria para superarla | Estado en la revisión |
|---|---|---|
| P1: imagen XNU estática | Enlace estricto completo, proveedores reales, cero imports pendientes y validación del Mach-O exacto | PENDIENTE: enlace exit 1, sin imagen |
| P2: XNU operativo en laboratorio | Ejecutar la imagen con VM/allocator y contexto válidos; un hilo Mach y timer o transición de contexto instrumentados | PENDIENTE: QEMU ejecuta entry, productor, materializador y probe de timer originales. XNU sigue sin imagen |
| P3: vía física G20 | Recuperación independiente por botones, carga temporal comprobada e instrumentación de entry/BSP propios, sin escritura de particiones | PENDIENTE: se observó Recovery OEM; no se probó el retorno independiente tras un payload ni la vía temporal |
| P4: primer XNU físico | Captura física de la imagen exacta alcanzando bootstrap y servicio Mach sustantivo con BSP original; retorno stock | PENDIENTE: ninguna ejecución AtlanticOS en G20 |
| P5: XNU → EDK2 | Desde ese XNU operativo: PREPARE/QUIESCE/COMMIT, DMA/memoria controlados y entrada EDK2 física identificable | PENDIENTE: sin handoff |

Se actualizará el crédito al completar trabajo sustantivo identificado o al
descubrir un cambio de alcance, explicando la razón. Una puerta solo cambia con
su evidencia de aceptación. Más objetos, más tests o menos símbolos indefinidos
se registran como avances de integración; no convierten automáticamente esas
cifras en puntos porcentuales. Una estimación sin cambio debe ir acompañada del
hito que sigue abierto y del avance concreto desde el informe anterior.

Base de la revisión: [meta](ACTIVE_GOAL.md), [estado](PHASE_STATUS.md),
[pruebas del G20](DEVICE_TEST_PLAN.md), [entry](ENTRY_LAB.md),
[plan de imagen](STATIC_IMAGE_PLAN.md), [boot_args](BOOT_PREPARATION.md),
[runtime Blocks](XNU_BLOCKS_RUNTIME.md) y
[regiones compartidas y commpage](XNU_SHARED_REGION_BOOTSTRAP.md).
