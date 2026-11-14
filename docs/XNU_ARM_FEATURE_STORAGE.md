# Capacidades ARM del commpage de XNU

El perfil de firmware conserva los probes de CPU de `commpage.c`. En XNU con
BSD, `bsd/kern/kern_mib.c` aporta el almacenamiento y la exposición por sysctl.
El patch 0033 define el almacenamiento dentro de commpage para el perfil
AtlanticOS. Usa las 57 entradas del catálogo `arm_features.inc`, el tipo `int`
y `SECURITY_READ_ONLY_LATE`, como las definiciones que consume el código nativo.
Incluye `gUCNormalMem` con el mismo tipo y reserva. La rama sin firmware
conserva sus declaraciones previas. Se rechaza combinar el perfil de firmware
con MACH_BSD para mantener un solo propietario de esos símbolos.

`commpage_populate` sigue invocando `commpage_init_cpu_capabilities`. Los cuerpos
de detección, las lecturas de registros y sus condiciones son los de la revisión
fijada. Los ceros iniciales son los valores de arranque del catálogo. Los valores
de capacidades del teléfono requieren ejecutar y observar el código en su BSP.

Se selecciona `osfmk/arm64/sme.c` íntegro. El target de laboratorio carece de
`HAS_ARM_FEAT_SME`. En esa configuración, el getter nativo lee `sme_version` y
el perfil mantiene SME deshabilitado. El objeto contiene un ADRP, una carga W y
el retorno, con relocaciones hacia el dato nativo. La medición de capacidades
del T700 es una prueba física posterior.

La auditoría compara todo el código upstream fuera de la declaración nueva,
preprocesa dicha declaración con MACH_BSD activo e inactivo y revisa los datos
fuertes y alineados de los objetos ARM64. Las pruebas de perfil cubren símbolos
y configuración. La inicialización de commpage, su mapeo y la ejecución de
los probes siguen pendientes.

Ambos builds experimentales compilan 162 unidades base y 42 probes RPC. Los
26 RPC seleccionados suman 188 entradas al enlace estricto. La reconstrucción
limpia de LLVM verificó 11 996 archivos y pasó 26 casos C y 32 C++. El candidato
incremental y el compilador limpio cierran los mismos 53 nombres y quedan en
379 indefinidos sin nuevos nombres ni duplicados. La rama estándar compila 13
unidades base. [Informes y objetos](../evidence/xnu-arm-features-20261113/manifest.json).
