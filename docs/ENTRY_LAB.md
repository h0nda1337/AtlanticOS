# Preflight ARM64 original — etapa de laboratorio

El código en atlantic-entry/ se ensambla desde fuente propia. Tiene header Image
AArch64, stack propio, captura de x0-x3/EL/SCTLR/DAIF/MPIDR/contador/VBAR/TCR/TTBR0,
vectores de excepción y validación FDT acotada. No enlaza ni ejecuta XNU.

Las direcciones UART/RAM y la salida semihosting viven exclusivamente en
platforms/qemu-virt/early.c. La entrada común acepta EL1/EL2 con MMU OFF; no
desactiva mapas desconocidos. Ahora exige además I/D-cache OFF antes de tocar
stack/BSS: cualquier estado activo se detiene sin modificarlo. El proveedor
original platforms/qemu-virt/el1.S prepara EL1h desde EL2 con ERET y conserva el
stack propio. La entrada directa EL1 recorre una rama que no accede a EL2.
Estos son contratos del laboratorio Cortex-A57, no una inicialización G20.
Capturar estado y construir vectores no equivale a inicializar Mach, GIC o VM.

Reproducir en Linux/WSL, con Clang, LLD, llvm-objcopy y QEMU AArch64:

```sh
python3 tools/test_freestanding.py --sanitize
python3 tools/build_entry.py
python3 tools/emulate_entry.py
python3 tools/build_entry.py --probe el1-trap --output work/entry-qemu-trap
python3 tools/emulate_entry.py --build work/entry-qemu-trap
```

build_entry solo permite el BSP qemu-virt. No genera boot.img para teléfono ni
incluye comandos de dispositivos. El ELF y binario Image derivan del mismo
enlace, con hashes y mapa. El tamaño retenido incluye BSS/stack, no solo archivo;
la cifra no representa memoria residente XNU ni prueba la meta de 100 MiB.

emulate_entry crea una máquina sin red o discos y comprueba desde las trazas que
el EL observado coincide con cada ejecución esperada. Semihosting se usa solo
para finalizar el test, nunca para acceder a archivos del host desde el payload.
Los logs incluyen literalmente XNU_NOT_LINKED y NO_XNU_BOOT_CLAIM.

## Preparación EL1 comprobada el 2026-10-21

La rama EL2 configura HCR.RW para EL1 AArch64, desactiva stage 2 y routing de
interrupciones hacia EL2, permite acceso al contador/timer físico mediante
CNTHCTL=3 y fija CNTVOFF=0. Instala SP_EL1/VBAR_EL1 propios y SCTLR_EL1 con
bits RES1 de Armv8.0; M/C/I y endian quedan cero. SPSR=0x3c5 retorna a EL1h
con DAIF enmascarado. No se toca EL3 ni se llama a servicios OEM.

Ambas ramas preparan registros de traducción EL1 sin habilitar MMU, mantienen
FP y timers desactivados y continúan con registros generales. El código C lee
CurrentEL, SCTLR, DAIF, SP y VBAR reales, verifica stack alineado dentro de su
reserva, vector propio y contador accesible que avanza. El delta mide progreso
del contador virtual de QEMU; no mide frecuencia física ni latencia de IRQ.

La variante el1-trap ejecuta BRK #0xa71 después de esos checks. El vector propio
lee ESR/ELR/FAR de EL1; el runner exige ESR=0xf2000a71 y salida 1 del payload.
Ese 1 es el resultado esperado del trap; una excepción distinta, timeout,
retorno del BRK o estado EL incorrecto falla la prueba. Se ejecutan ambas
variantes desde EL1 y EL2: cuatro recorridos AArch64, sin discos ni red.

Las constantes corresponden al Cortex-A57 del laboratorio. Para el G20 faltan
captura física, estado de caches/coherencia, accesos permitidos por firmware
seguro y configuración comprobada de timers/GIC. No empaquetar esta imagen para
el teléfono. El mapa MMU y entrada XNU todavía no están integrados.

Referencias de arquitectura consultadas: [modelo de excepciones de Arm](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Exception%20model.pdf),
[transiciones AArch64 explicadas por Arm](https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/changing-exception-level-and-security-state-with-an-armv8a-fixed-virtual-platform)
y [definiciones arquitectónicas de Arm TF-A v2.12.0](https://github.com/ARM-software/arm-trusted-firmware/blob/v2.12.0/include/arch/aarch64/arch.h).
Se estudiaron valores de registros; no se importó código de inicialización ni
una dependencia TF-A. Fuente propia y evidencia: [entry-el1-20261021](../evidence/entry-el1-20261021/manifest.json).

FDT: implementación original, sin código libfdt importado. Verifica header v17,
bounds/alineación/solapamientos, tabla memreserve con terminador y overflow,
nombres/propiedades acotados, raíz única, balance de nodos, profundidad máxima,
orden propiedades/hijos y END. El BSP debe probar primero que el buffer entero
es RAM legible; validar un header no hace segura una dirección física arbitraria.
Todavía no se extrae un mapa allocator ni se convierte a SecureDT/boot_args XNU.

Pruebas host C incluyen truncamientos, offsets inválidos, tamaño desbordado,
reservas y overflow, profundidad excesiva y 10 000 mutaciones con ASan/UBSan.
Las ejecuciones ARM64 con DTB QEMU son EMULATED de este componente únicamente.

Referencia del contenedor: [protocolo Image AArch64](https://www.kernel.org/doc/html/latest/arch/arm64/booting.html).
No es una implementación Linux ni reutiliza su kernel, entry o drivers.
## Timer virtual con GICv3 en QEMU

El modo `timer-irq` instala un vector IRQ para EL1h y configura el GICv3 de
`virt` con una CPU y seguridad deshabilitada. Dos plazos del timer virtual
entregan INTID 27 por grupo 1 en cada ruta de entrada. El handler registra el
estado activo, enmascara y reprograma el timer para desactivar la fuente, y
completa la interrupción con `ICC_EOIR1_EL1` en modo EOI 0. El plazo de control
vence con el timer enmascarado, con `ISTATUS=1` y sin una nueva entrega.

```sh
python3 tools/build_entry.py --probe timer-irq --output work/entry-qemu-timer
python3 tools/emulate_entry.py --build work/entry-qemu-timer
```

La prueba registra dos ACK, dos EOI y cero interrupciones pendientes tras
cada disparo. Los registros y las direcciones MMIO corresponden a QEMU. El
BSP físico requiere confirmar la interfaz y el grupo del G20 según T06.
El paso por `sleh_irq`, `rtclock_intr` y el callback Mach queda para el
kernel enlazado. [Capturas de ambos recorridos](../evidence/qemu-timer-irq-20261113/manifest.json).

## Colocación y lectura de un Mach-O de prueba en QEMU

`atl_xnu_image_materialize` revalida el archivo retenido y el plan de memoria.
El llamador debe tener acceso exclusivo y estable a la arena completa y conservar
inmutables los bytes de entrada. Se comprueban solapamientos de punteros y, por
separado, los rangos físicos declarados para archivo y destino. El materializador
no deduce esas direcciones desde el puntero. Valida todos los límites antes
de copiar y pone a cero las colas de cada segmento, hasta su tamaño de mapping.

La prueba usa un único vCPU y EL1h con MMU y caches deshabilitadas. El laboratorio
declara una ventana de 32 MiB en 0x42000000. Verifica byte por byte 49 456 bytes
de archivo, 32 464 bytes de BSS y relleno, y que el prefijo, las páginas de
contexto/árbol/boot_args y un canario al final de la arena permanecen intactos.
La ventana de 64 MiB que incluye el DTB de QEMU se rechaza antes de escribir.
Ambos recorridos comprueban las mismas condiciones. En host, ASan/UBSan verifica
ocho colocaciones, cinco con huecos, y 45 rechazos transaccionales.

```sh
python3 tools/test_image_materialize.py --sanitize --output work/image-materialize-host
python3 tools/build_entry.py --probe materialize --output work/entry-qemu-materialize
python3 tools/emulate_entry.py --build work/entry-qemu-materialize
```

El fixture se genera a partir del ensamblador de pruebas y su entrada no se
invoca. Falta construir el contexto retenido y demostrar las traducciones,
permisos y barreras requeridos por XNU. Los tamaños de arena y segmentos son
geometría del ensayo y no miden memoria residente de AtlanticOS.
[Manifiesto de las pruebas](../evidence/image-materialize-20261113/manifest.json).

## Traducciones EL1 y conservación de ATLC

El modo translation coloca el Mach-O de prueba con cabecera RO y código RX.
Instala ATLC, SecureDT y boot_args, y activa tablas EL1 de 4 KiB y direcciones
virtuales de 39 bits. El código original verifica los registros TTBR, TCR y
SCTLR activos y recorre las páginas de cada segmento. Lee los datos y su
relleno desde las direcciones altas, escribe y restaura una palabra de datos,
y ejecuta una rutina aislada del fixture.

La escritura intencional al código y a la cabecera falla con ESR 0x9600004f.
La ejecución intencional de una página RW/NX falla con ESR 0x8600000f. El
vector reconoce únicamente las direcciones y clases de fallo esperadas. Al
cambiar ambas raíces, las lecturas por las direcciones altas continúan y los
alias bajos de la arena dejan de traducir. La prueba vuelve a localizar ATLC
desde una copia de los argumentos y comprueba el árbol y el trailer completos.

~~~sh
python3 tools/build_entry.py --probe translation --output work/entry-qemu-translation
python3 tools/emulate_entry.py --build work/entry-qemu-translation
~~~

Las doce ejecuciones de los seis modos pasan desde EL1 y EL2. El punto de
entrada del Mach-O permanece sin invocar. Las tablas de este ensayo son del
BSP QEMU y comprueban el contrato de datos y permisos del laboratorio. El
arranque real todavía debe demostrar cómo XNU instala y sustituye sus propias
tablas, incluido el alcance y el costo de las asignaciones iniciales.
[Manifiesto](../evidence/qemu-retained-translation-20261113/manifest.json).
