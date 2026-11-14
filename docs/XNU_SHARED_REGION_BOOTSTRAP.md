# Mapeo nativo del commpage y regiones compartidas

`vm_shared_region.c` y `vm_shared_region_pager.c` se compilan completos desde
XNU fijado. El primero aporta `vm_commpage_init`, `vm_commpage_text_init`,
`vm_commpage_enter` y el estado de la región compartida usado por el ciclo de
vida de tareas. El segundo aporta el pager que construye los objetos de región
y aplica los slides. Los dos conservan sus referencias a VM y pmap.

El patch 0034 retiene `upl_page_present` y `upl_phys_page` en el perfil de
firmware. Sus cuerpos son los de `vm_pageout.c` y siguen consumiendo los macros
UPL originales. El resto del bloque de accessors sigue bajo MACH_BSD.

El accessor de generación RSR que usa la liberación de regiones compartidas
procede de `bsd/kern/kern_exec.c`. El perfil de firmware toma el mismo estado
local `uint32_t`, la lectura relajada y el incremento atómico en una unidad
separada. La unidad rechaza cualquier definición de MACH_BSD. Las llamadas, el
criterio de destrucción y la implementación upstream de BSD exec se preservan.
La selección permite verificar las operaciones de estado de VM sin exigir el
camino BSD completo de exec.

La generación nativa de versión de XNU usa `config/version.c.template` y
`config/newvers.pl`. Se emplea el mismo valor de laboratorio `25.0.0` del header
generado y una identidad fija de builder `AtlanticOS`. El número configura la
ABI de versión en este laboratorio. El identificador de la fuente sigue siendo
el commit upstream y el patchset registrado. `osversion` conserva los 256 bytes
escribibles de zero-fill de la plantilla. Los consumidores activos toman su
dirección. Los escritores BSD sysctl y alternate debugger pertenecen a sus
perfiles nativos.

Las pruebas verifican cuerpos, bindings, datos y dependencias con los objetos
reales. La ejecución de `vm_commpage_init` requiere una imagen enlazada y RAM
propia, VM/pmap inicializados y el BSP del target. En ese entorno debe capturarse
la secuencia de mapeo, comprobar lectura/ejecución de los segmentos apropiados,
verificar las escrituras del catálogo y comprobar el uso de la página desde
el contexto nativo. El pager y las pruebas de ciclo de vida necesitan muestras
propias con referencias y liberación comprobadas.

Las 166 unidades experimentales y 13 estándar compilaron con los dos perfiles.
Se compilaron 42 probes RPC por carril y se seleccionaron 26. En el diagnóstico
estricto de 192 objetos se resolvieron 14 referencias previas, con 365 pendientes,
cero nuevas y cero duplicadas. El diagnóstico no produjo un Mach-O. Las pruebas
nuevas cubren 17 casos de VM/UPL/RSR y cinco de metadata de versión.
[Manifiesto de evidencia](../evidence/xnu-shared-region-20261113/manifest.json).
