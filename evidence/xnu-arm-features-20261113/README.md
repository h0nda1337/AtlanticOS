# Capacidades ARM de commpage y SME

La reconstrucción limpia de Clang ejecutó las pruebas C y C++ y el perfil
experimental compiló 162 unidades base. El carril estándar compiló 13. Los
dos builds experimentales, incremental y limpio, comparten las fuentes y
los nombres que arroja el enlace. Los informes conservan los hashes reales.

El enlace estricto de 188 objetos queda en 379 indefinidos, desde 432. Se
resolvieron 52 referencias al catálogo ARM y el getter de SME. Los objetos
de commpage y SME permiten inspeccionar los accesos reales al catálogo. Las
pruebas cubren los registros y configuraciones emitidos. Quedan pendientes
la inicialización del commpage, el arranque XNU y el handoff a EDK2.
