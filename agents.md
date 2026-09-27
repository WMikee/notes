# AGENT INSTRUCTIONS FOR QT DEVELOPMENT

When generating, modifying, or debugging Qt code, you MUST adhere to the following rules:

1. **Target Qt 6:** Assume all Qt development is for Qt 6 unless specified otherwise. Do not use deprecated Qt 5 classes, modules, or CMake macros (e.g., avoid `QRegExp`, use `QRegularExpression`; be mindful of module shifts).
2. **Consult Official Docs First:** Before making assumptions about class methods, signals, properties, or CMake integration, query the official Qt 6 documentation: 
**https://doc.qt.io/qt-6/**
3. **Use Modern C++ & CMake:** Prefer C++17/C++20 standards. Ensure build instructions and code snippets utilize modern Qt 6 CMake commands (e.g., `qt_add_executable`, `qt_add_qml_module`).
4. **No Hallucinations:** If you are unsure about a specific API signature or include path in Qt 6, state your uncertainty or perform a web search against `site:doc.qt.io/qt-6/` before providing a solution.
## Depuración de render (OpenGL)

Atajos de diagnóstico (solo depuración, no afectan al guardado):

- `F9`: alterna la presentación del framebuffer de supermuestreo entre reducción
  3x3 y copia sin filtrar.
- `F10`: activa/desactiva la máscara alpha del resaltador (test de profundidad).

Variables de entorno:

- `NOTES_NO_SSAA=1`: no crea framebuffer de supermuestreo, dibuja directo.
- `NOTES_NO_ALPHAMASK=1`: dibuja el resaltador por acumulación con blending, sin
  máscara de profundidad.
- `NOTES_EXIT_AFTER_MS=N`: sale de forma limpia pasados N ms. Sin esto, si el
  proceso muere por `timeout` los mensajes de Qt se quedan en su buffer.

Los avisos del programa salen por `qWarning`, pero en este sistema no se ven
salvo que se ejecute con `QT_FORCE_STDERR_LOGGING=1`: sin esa variable ni un
`qWarning` ni un `qCritical` llega a la consola.

## Invariantes de transformacion

El `id` de cada objeto (trazos, textos, imagenes y formas) es la clave con la que
trabajan la seleccion, `gizmoBox()`, `snapshot()` y los `commit*Transform`. Por
tanto:

- El `id` debe ser **unico entre los cuatro tipos a la vez**, no solo dentro de
  cada tipo. `gizmoBox()` busca un id en las cuatro tablas y `snapshot()` se
  lleva todo lo que coincide, asi que un id repetido convierte varios objetos en
  uno solo que se mueven juntos.
- El `id` va serializado en `storage.cpp` desde la version 6. Al leer ficheros
  antiguos, `normalizaIds()` reasigna ids unicos y sube `nextId` por encima del
  mayor id, porque `nextId` es un contador compartido por los cuatro tipos.
- Al tocar el formato, subir `kVersion`, anadir la lectura con `if (version >= N)` y
  **sumar la nueva constante a la lista de versiones aceptadas** en
  `deserializeDocument`. Si se olvida, el fichero se rechaza entero y la pagina no
  abre. Ojo: esa condicion esta partida en dos lineas.

En `SelectTool` hay cuatro puntos de referencia distintos y confundirlos produce
errores que dependen del angulo. No mezclarlos:

- `ImageItem`, `TextBox` y `ShapeItem` dibujan y miden sus limites girando
  alrededor de su **ancla** (`pos`), que es la esquina superior izquierda.
  `rotatedBounds()` tambien gira alrededor de ahi. Un giro es rigido si y solo
  si `rot` sube en el angulo total y la posicion del ancla se mueve con
  `rotateAbout(ancla, pivote, angulo)`. No restar ni sumar `mid`: restarlo
  hace que el objeto gire sobre su esquina y despegue del tirador.
- `gizmoBox()` devuelve un rect en ejes de mundo, centrado en el centro del
  AABB real de la geometria, con `gizmoRot()` como angulo. Si se recentra en el
  centro del rect sin rotar, el gizmo se separa del objeto y los tiradores no
  caen en sus esquinas.
- Al escalar, el punto fijo es la **esquina opuesta** al tirador
  (`handleAnchor`), y tanto las medidas (`sx`, `sy`) como `applyScale()` deben
  usar ese mismo punto. Medir desde la esquina fija y mover sobre el centro de la
  caja hace que la esquina anclada se desplace sola.
- `boxRot_` se resincroniza en `setSelection()` y en `commitDrag()` con el
  angulo real del objeto (`rotationOf`). Sin eso, seleccionar algo ya girado
  reutiliza un angulo obsoleto y el gizmo sale del tamano equivocado.
- Los trazos no tienen angulo propio: `rotationOf` devuelve 0 para ellos, y al
  rotarlos el angulo del gizmo vuelve a 0 al soltar.

