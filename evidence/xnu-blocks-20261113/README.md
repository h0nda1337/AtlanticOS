# Runtime Blocks nativo — 2026-11-13

El candidato incremental de Clang pasa 26 casos C y 32 casos C++. El primer
fallo de especialización de template se conserva en el diagnóstico. La
compilación que sustituye solo el objeto Sema está descrita en
`compiler-incremental-build.json`. La receta pública también permite una
reconstrucción desde el archivo LLVM fijado.

Los informes capturan 161 unidades experimentales, 13 estándar y 42 probes RPC
por carril. El enlace estricto experimental de 187 objetos conserva 432
nombres indefinidos, cero duplicados y ninguna imagen. La comparación de
nombres parte del enlace publicado de 185 objetos. `audit.json` enumera los
cambios. Los objetos nativos Blocks permanecen en la compilación local. Los
objetos de esta carpeta son fixtures propios de la API del compilador.

Las 15 comprobaciones de Blocks incluyen los tipos de los
headers nativos, los imports al allocator y las relocaciones de las firmas en
los dos descriptores `__kalloc_var`. La ejecución del runtime, del kernel y del
handoff EDK2 sigue pendiente.
