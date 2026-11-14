# Runtime Blocks nativo y metadata C++ — 2026-11-13

Las 161 unidades experimentales compilan para ARM64, incluidas las dos fuentes
nativas de Blocks. El candidato incremental de Clang pasa los 26 casos C y los
32 casos C++. La primera compilación C++ encontró que los argumentos template
no dependientes podían requerir instanciación. La API pide ahora a Sema
completar esos tipos antes de leer sus tamaños y offsets. La reconstrucción
limpia desde LLVM fijado tiene su propia validación.

El enlace estricto de 187 objetos, incluidos 26 RPC seleccionados, pasa de
436 a 432 símbolos indefinidos sin duplicados ni dependencias nuevas. Los
cuatro cierres son `_Block_object_assign`, `_Block_object_dispose` y las
clases de stack y global en la ABI Mach-O. Sigue pendiente vincular y ejecutar
la imagen completa. [Evidencia reproducible](../evidence/xnu-blocks-20261113/manifest.json).

`libkern/libclosure/runtime.cpp` y `libclosuredata.c` se seleccionan íntegros del
XNU oficial fijado, sin cambiar sus cuerpos. La primera unidad implementa copy,
release, byref y los helpers emitidos por Clang. La segunda contiene los seis
arrays nativos usados para identificar categorías de Blocks. No se importa un
port de kernel o firmware ajeno.

El runtime crea dos descriptores `KALLOC_TYPE_VAR_DEFINE`, para `Block_layout`
y `Block_byref`, con payload de `void *`. La API C v2 los rechazaba en C++.
No se cambia el runtime para evitar su allocator o vaciar esos descriptores:
el patch 0032 selecciona una API C++ independiente y exige su versión probada.
En el contexto XNU privado las llamadas inline de asignación/liberación llegan
a `kalloc_ext` y `kfree_ext` genuinos, que conservan sus dependencias nativas.

La receta C++ usa GNU C++2b, sized deallocation, fapple-kext,
LIBKERN_KERNEL_PRIVATE y OSALLOCDEBUG según `makedefs/MakeInc.def` y
`libkern/conf/Makefile.template`. El contexto MACH_KERNEL_PRIVATE de osfmk
colisiona con tipos BSD de los headers de libkern y queda desactivado en este
componente.

## Contrato de metadata

El macro `__ATLANTIC_XNU_LAYOUT__=2` y sus operaciones C conservan su contrato,
incluido el rechazo de C++. Las nuevas operaciones son:

| Operación original | Resultado |
|---|---|
| `__atlantic_cpp_layout_signature(T)` | Literal C++ `const char[N]`, un dígito por granulo de ocho bytes |
| `__atlantic_cpp_layout_summary(T)` | Bits de presencia de padding/puntero/datos/mezcla |
| `__atlantic_cpp_layout_compatible(A,B)` | Misma secuencia de encodings; void solo coincide con void |

`__ATLANTIC_XNU_CPP_LAYOUT__=1` identifica este contrato separado. Usa tamaños y
offsets del AST real de AArch64 little endian LP64 sin arm64e/PAC. Conserva
anotaciones de uso en aliases y campos, padding, unions, bitfields, strides de
arrays y atomic. Los records C++ deben ser completos, triviales, de layout
estándar y sin bases. Se rechazan referencias, punteros a miembros incluso con
override de uso, herencia, lambdas, records no triviales y no_unique_address.
Una clase vacía normal tiene su tamaño C++ real y almacenamiento de padding;
no se le asigna el tamaño cero de un record vacío GNU C.

Este alcance no representa todos los tipos C++ de XNU ni implementa los
builtins privados Apple. Los casos no representados generan un error. No se
pretende compilar clases no soportadas como C o sustituirlas por records sombra.

`--cpp-layout-proof` exige un informe separado: mismo binario, API/versiones,
hashes actuales de implementación y fixtures, y cada rechazo esperado aprobado.
El informe C solo no selecciona los nuevos proveedores. La selección XNU añade
`CONFIG_ATLANTIC_CPP_LAYOUT` junto al proveedor C explícito; otras rutas conservan
el proveedor upstream. Las dos firmas de header esperadas son `1211` y `112`,
con resumen 6; el payload `void *` conserva la firma `1`. No son hashes
criptográficos.

## Pruebas

Las pruebas propias del compilador comprueban constantes y literals en IR sin
llamadas runtime. Incluyen offsets reales packed/aligned, padding, clase vacía,
aliases, records anidados y rechazo de representaciones no soportadas. La suite
C histórica se ejecuta por separado con sus expectativas originales.

`test_xnu_blocks.py` compara 17 constantes de los headers nativos en C y C++.
Comprueba los descriptores del objeto, sus relocaciones a las firmas, los imports
fuertes al allocator y los seis arrays zero-fill. Las pruebas negativas cubren
un tamaño de descriptor alterado, una firma incorrecta y la selección C++ ausente.

Los tres hooks kernel para retain/release/destruct de objetos externos son
vacíos **en la fuente nativa**. Se conservan y se identifican como tales; no
acreditan soporte Objective-C. Los arrays de clase son placeholders nativos,
no clases Objective-C operativas. Copia/liberación necesita memoria y contexto
Mach válidos, lifetime correcto y un allocator inicializado. Todo ello requiere
pruebas de ejecución posteriores.

## Reproducción

```sh
python3 tools/build_atlantic_clang.py --cache work/clang-layout-cpp --jobs 4
python3 tools/build_xnu_lab.py \
  --layout-compiler work/clang-layout-cpp/build/bin/clang \
  --layout-proof work/clang-layout-cpp/tests/report.json \
  --cpp-layout-proof work/clang-layout-cpp/cpp-tests/report.json
python3 tools/test_xnu_blocks.py --build work/xnu-lab-builds/BUILD_ID/report.json \
  --output work/blocks-tests
```

En WSL sobre checkout Windows, añadir `--windows-git` a los builders XNU/RPC.
BUILD_ID se obtiene del informe de compilación real, no se asume de otra revisión.
Ninguna receta monta o formatea un disco, accede al teléfono o escribe particiones.

Los archivos libclosure llevan copyright Apple y el marcador upstream
`@APPLE_LLVM_LICENSE_HEADER@`. El texto de licencia aplicable a esa revisión
sigue por identificar. La evidencia pública contiene informes, comandos y
fixtures propios. El builder obtiene las fuentes y genera los objetos nativos
localmente desde XNU fijado.
