# Manual de Usuario - SyncAccesible (Versión 2.0 C++ Nativo)

**Autor y Desarrollador:** José Manuel Fernández Cortés  
**Año:** 2026  
**Licencia:** Libre / Código Abierto  

---

## 📖 Índice de Contenidos

1. [Introducción y Filosofía de Diseño Accesible](#1-introducción-y-filosofía-de-diseño-accesible)
2. [Requisitos e Instalación](#2-requisitos-e-instalación)
3. [Estructura de la Interfaz](#3-estructura-de-la-interfaz)
4. [Gestión de Pares de Carpetas](#4-gestión-de-pares-de-carpetas)
5. [Modos de Sincronización](#5-modos-de-sincronización)
6. [Invertir Pares de Carpetas](#6-invertir-pares-de-carpetas)
7. [Comparación y Vista Previa](#7-comparación-y-vista-previa)
8. [Puntero Virtual de Columnas](#8-puntero-virtual-de-columnas)
9. [Sistema de Feedback Accesible (Barra, Sonido y Voz)](#9-sistema-de-feedback-accesible)
10. [Exportación de Informes y Diagnóstico (.log)](#10-exportación-de-informes-y-diagnóstico-log)
11. [Ejecución en Serie y Cola de Lotes](#11-ejecución-en-serie-y-cola-de-lotes)
12. [Bandeja del Sistema y Minimización](#12-bandeja-del-sistema-y-minimización)
13. [Tabla Completa de Atajos de Teclado](#13-tabla-completa-de-atajos-de-teclado)
14. [Créditos](#14-créditos)

---

## 1. Introducción y Filosofía de Diseño Accesible

**SyncAccesible** es una herramienta de sincronización de archivos y carpetas desarrollada por **José Manuel Fernández Cortés** para el sistema operativo Windows.

A diferencia de las utilidades convencionales de sincronización, SyncAccesible ha sido diseñada y construida desde su núcleo bajo un principio rector: **la accesibilidad universal total**. Está optimizada para personas usuarias de lectores de pantalla (como NVDA y JAWS) y personas que operan exclusivamente mediante el teclado, sin descuidar una interfaz gráfica limpia, clara y visualmente cómoda.

### Características Principales:
* **Ejecutable nativo en C++ de alto rendimiento:** Sin dependencias externas voluminosas, con arranque instantáneo y uso mínimo de memoria RAM.
* **Integración con lectores de pantalla:** Soporte directo con `nvdaControllerClient` y sintetizadores SAPI de Windows.
* **Navegación sin barreras:** Todas las funciones, botones, diálogos, menús contextuales y selecciones son operables al 100% por teclado.
* **Puntero Virtual de Columnas:** Permite inspeccionar celda a celda cada atributo sin mover la selección de la fila.
* **Feedback multisensorial durante operaciones:** Barra de progreso gráfica continua, sonido de espera periódico tipo latido y avisos por voz automáticos cada 25% de progreso (25%, 50%, 75% y finalización).

---

## 2. Requisitos e Instalación

* **Sistema Operativo:** Windows 10 o Windows 11 (32 o 64 bits).
* **Portabilidad:** No requiere instalación. Puede ejecutarse directamente desde cualquier carpeta, disco externo o memoria USB ejecutando `SyncAccesible.exe`.
* **Archivos asociados:**
  * `SyncAccesible.exe`: Aplicación ejecutable principal.
  * `config.json`: Archivo de configuración general y proyectos recientes.
  * `nvdaControllerClient64.dll` / `nvdaControllerClient32.dll`: Librerías de enlace con NVDA (opcionales pero recomendadas).

---

## 3. Estructura de la Interfaz

La ventana principal de SyncAccesible se divide de forma clara en cuatro zonas lógicas:

1. **Barra de Menús Superior:**
   * **Archivo (`Alt + A`):** Nuevo proyecto, Abrir, Guardar, Guardar como y Cerrar por completo (`Alt + F4`).
   * **Sincronización (`Alt + S`):** Comparar (`F5`), Sincronizar (`F6`), En serie (`F7`), Todas en serie (`Ctrl + F6`) y Cola de lotes (`Alt + L`).
   * **Pares de Carpetas (`Alt + P`):** Añadir (`Ctrl + N`), Editar (`Ctrl + E`), Invertir (`Ctrl + I`) y Eliminar (`Supr`).
   * **Ver (`Alt + V`):** Pestaña 1 (`Alt + 1`), Pestaña 2 (`Alt + 2`), Cambiar de pestaña (`Ctrl + Tab`).
   * **Herramientas (`Alt + H`):** Configuración general.
   * **Ayuda (`Alt + Y`):** Manual de Usuario (`F1`) y Acerca de...
2. **Barra de Botones Rápidos:** Botones de pulsación directa para las acciones más frecuentes.
3. **Contenedor de Pestañas:**
   * **Pestaña 1: Pares de Carpetas:** Lista con todos los pares configurados en el proyecto activo.
   * **Pestaña 2: Vista Previa de Sincronización:** Lista detallada tras comparar, con acciones pendientes (copiar, borrar, idéntico).
4. **Zona de Progreso y Estado:**
   * **Barra de Progreso Visible:** Muestra visualmente el porcentaje completado de 0 a 100%.
   * **Barra de Estado:** Informa el número de elementos, tarea en curso y resultado.

---

## 4. Gestión de Pares de Carpetas

Un "Par de Carpetas" define una carpeta de **Origen** (Izquierda) y una de **Destino** (Derecha).

* **Añadir un par (`Ctrl + N`):** Abre la ventana de configuración para asignar nombre, rutas, modo de sincronización, programaciones y exclusiones.
* **Editar un par (`Ctrl + E` o `Enter`):** Modifica los parámetros del par seleccionado.
* **Eliminar un par (`Supr`):** Pide confirmación y retira el par del proyecto.

---

## 5. Modos de Sincronización

* **Espejo (Mirror):** Hace que la carpeta de destino sea idéntica a la de origen. Copia los archivos nuevos y modificados de origen a destino, y elimina en destino aquellos archivos que ya no existan en el origen.
* **Actualizar (Update):** Copia los archivos nuevos y modificados de origen a destino, pero nunca elimina archivos en destino.
* **Bidireccional (Two-Way):** Examina ambas carpetas y copia los archivos más recientes en cualquiera de los dos lados para mantener ambas sincronizadas.

---

## 6. Invertir Pares de Carpetas

SyncAccesible permite intercambiar fácilmente la dirección de un par:

1. **Desde la lista principal:** Selecciona el par y pulsa **`Ctrl + I`**. El sintetizador anunciará: *"Par 'nombre' invertido. Origen y destino intercambiados."*
2. **Desde el diálogo de edición:** Pulsa el botón **`Invertir Carpetas (Alt + I)`**.

---

## 7. Comparación y Vista Previa

Antes de sincronizar, puedes comprobar qué cambios se realizarán:

* Pulsa **`F5`** sobre el par seleccionado.
* La aplicación analiza los archivos de ambas carpetas.
* Al finalizar, el programa pasa automáticamente a la **Pestaña 2 (Vista Previa)** y verbaliza el resumen de cambios detectados.

### Filtrado y Ordenación Inteligente para Grandes Volúmenes:
Cuando una carpeta contiene miles o cientos de miles de archivos, la mayoría son idénticos. Para evitar bloqueos, mejorar la fluidez y centrar la atención en lo importante:
* **Filtrar solo cambios (`Ctrl + H`):** Por defecto, la vista previa solo muestra los archivos que van a ser copiados, eliminados o actualizados, ocultando los idénticos. Puedes alternar este filtro en cualquier momento desde el menú `Ver -> Filtrar solo cambios` o pulsando `Ctrl + H`.
* **Ordenar por Acción (`Alt + O`):** Prioriza en los primeros lugares los archivos con acciones pendientes (conflictos, copias a destino, actualizaciones y borrados). También disponible en el menú contextual y menú `Ver -> Ordenar por -> Acción requerida`.
* **Ordenar por Columnas:** Puedes pulsar en la cabecera de las columnas (Acción, Archivo, Tamaño, etc.) para ordenar alfabéticamente o por tamaño.

---

## 8. Puntero Virtual de Columnas

Tanto en la lista de Pares de Carpetas como en la Vista Previa:
* Con las **Flechas Arriba y Abajo** seleccionas el elemento/fila.
* Con las **Flechas Izquierda y Derecha** navegas horizontalmente por las columnas de esa fila.
* El lector de pantalla anuncia de forma fluida el encabezado y el valor de cada celda (*"Modo: Espejo"*, *"Programación: Diaria 22:00"*, *"Tamaño Origen: 4.5 MB"*), sin mover el foco de fila ni perder la posición.

---

## 9. Sistema de Feedback Accesible

Para garantizar una experiencia informativa tanto para personas ciegas como para personas con resto visual:

1. **Barra de Progreso Visible:** Ubicada sobre la barra de estado, avanza de 0% a 100% de manera suave y continua durante comparaciones y sincronizaciones.
2. **Sonido de Espera (Latido de Operación):** Durante el proceso se emite un latido sonoro periódico suave (cada 1.2 segundos), ofreciendo certeza acústica inmediata de que la aplicación continúa procesando sin bloquearse.
3. **Anuncio por Voz cada 25%:** El sintetizador verbaliza automáticamente los hitos de progreso:
   * **25 por ciento**
   * **50 por ciento**
   * **75 por ciento**
   * Y al finalizar, verbaliza el resultado completo (*"Sincronización finalizada: X archivos copiados con éxito"*).
4. **Alerta sonora de finalización:** Emite un tono del sistema al completarse la tarea.

---

## 10. Exportación de Informes y Diagnóstico (.log)

### Exportar Informe de Resultados a TXT (`Ctrl + P`):
Puedes generar en cualquier momento un informe en formato `.txt` con los resultados de la comparación o sincronización:
* Menú **Archivo -> Exportar Informe de Resultados a TXT...** o pulsa **`Ctrl + P`**.
* Se guarda un archivo de texto estructurado con codificación UTF-8 que detalla: fecha, par de carpetas analizado, rutas, resumen estadístico (total analizados, copiados, eliminados, sin cambios) y la lista desglosada de archivos clasificados por su acción.
* La aplicación ofrece abrirlo directamente en el Bloc de Notas o editor predeterminado.

### Sistema de Registro y Diagnóstico (`sync_accesible.log`):
Para garantizar estabilidad total y prevenir cierres inesperados o estados de "No responde":
* Todas las operaciones de escaneo, sincronización, colas de lotes e hilos en segundo plano quedan registradas con marca de tiempo precisa en `sync_accesible.log` en el directorio de la aplicación.
* Si el sistema detecta un error de permisos, disco o excepción no controlada, queda capturado inmediatamente en el log para su análisis.
* Puedes inspeccionar el registro en cualquier momento desde el menú **Herramientas -> Ver Archivo de Registro (.log)...**.

---

## 11. Ejecución en Serie y Cola de Lotes

Para no saturar el rendimiento del disco ni el ancho de banda de red, SyncAccesible ejecuta las tareas en estricto orden secuencial:

* **Sincronizar Selección en Serie (`F7`):** Puedes seleccionar varios pares con `Shift + Flechas` o `Ctrl + Espacio` y pulsar `F7`. Se sincronizará el primer par y, al terminar, el segundo, y así sucesivamente.
* **Sincronizar Todos en Serie (`Ctrl + F6`):** Procesa uno tras otro todos los pares activos del proyecto.
* **Cola de Lotes (`Alt + L`):** Permite encadenar múltiples proyectos diferentes, añadir pares, cambiar el orden de ejecución con los botones Subir y Bajar, y supervisar el progreso global.

---

## 12. Bandeja del Sistema y Minimización

* **Minimizar con `Esc`:** Al pulsar `Esc` desde la ventana principal, la aplicación se oculta en la bandeja del sistema para seguir trabajando en segundo plano.
* **Acceso desde el lector (`Windows + B`):**
  1. Pulsa `Windows + B` para enfocar la bandeja del sistema.
  2. Si está en **Mostrar iconos ocultos (`^`)**, pulsa `Enter` o `Espacio` y navega con las flechas hasta **SyncAccesible (C++)**.
  3. Pulsa **`Enter`** o **`Espacio`** para restaurar la ventana al instante.
  4. Pulsa la **tecla Aplicaciones** o `Shift + F10` para abrir el menú de la bandeja.

---

## 13. Tabla Completa de Atajos de Teclado

| Atajo | Función |
| :--- | :--- |
| **Shift + F1** | **Abrir la ventana rápida con todos los Atajos de Teclado** |
| **F1** | **Abrir el Manual de Usuario en HTML (navegable en navegador web)** |
| **F5** | Comparar el par de carpetas seleccionado |
| **F6** | Sincronizar el par seleccionado |
| **F7** | Sincronizar en serie los pares seleccionados (uno tras otro) |
| **Ctrl + F6** | Sincronizar todos los pares activos del proyecto en serie |
| **Alt + L** | Abrir la Cola de Lotes para múltiples proyectos |
| **Ctrl + P** | **Exportar informe de resultados a fichero .txt** |
| **Ctrl + H** | **Alternar filtro en Vista Previa (solo cambios / todos)** |
| **Alt + O** | **Ordenar Vista Previa por acción requerida** |
| **Ctrl + N** | Añadir un nuevo par de carpetas al proyecto |
| **Ctrl + E / Enter** | Editar el par de carpetas seleccionado |
| **Ctrl + I** | Invertir el par seleccionado (Origen <-> Destino) |
| **Supr (Delete)** | Eliminar el par seleccionado |
| **Ctrl + O** | Abrir archivo de Proyecto (.syncproj) |
| **Ctrl + S** | Guardar Proyecto actual |
| **Ctrl + Shift + S** | Guardar Proyecto Como... |
| **Ctrl + Shift + N** | Nuevo Proyecto limpio |
| **Ctrl + Tab** | Alternar entre Pestaña 1 y Pestaña 2 |
| **Alt + 1 / Ctrl + 1** | Ir a la Pestaña 1 (Pares de Carpetas) |
| **Alt + 2 / Ctrl + 2** | Ir a la Pestaña 2 (Vista Previa de Sincronización) |
| **Flecha Derecha / Izquierda** | Puntero virtual de columnas (anuncia celda por celda) |
| **Shift + F10 / Tecla Aplicaciones** | Abrir menú contextual del elemento seleccionado |
| **Tab / Shift + Tab** | Desplazarse entre controles y listas |
| **Esc** | Minimizar la aplicación a la bandeja del sistema |
| **Alt + F4** | Cerrar y salir por completo de la aplicación |

---

## 14. Créditos

* **Desarrollo y Diseño de Accesibilidad:** José Manuel Fernández Cortés  
* **Año:** 2026  
* **Dedicatoria:** Creado con especial dedicación para la comunidad hispanohablante con discapacidad visual, promoviendo el software libre, accesible y sin barreras.

