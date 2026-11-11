# SyncAccesible (C++ Nativo) - Sincronizador Gráfico Ultraligero y Accesible

**Autor y Desarrollador:** José Manuel Fernández Cortés  
**Año:** 2026  

**SyncAccesible (C++)** es una aplicación nativa en C++ y Win32 API desarrollada por **José Manuel Fernández Cortés**, diseñada como alternativa ultraligera y **100% accesible** a *FreeFileSync*. Permite gestionar **proyectos independientes**, sincronizar **pares de carpetas uno a continuación de otro en serie**, y encadenar **múltiples proyectos en una cola de ejecución por lotes**.

---

## 🔊 Sistema de Feedback Accesible (Barra, Sonido y Voz)

Para garantizar la máxima retroalimentación a usuarios con o sin visión durante operaciones de comparación o sincronización:
1. **Barra de Progreso Visible:** Control gráfico nativo situado sobre la barra de estado que muestra de 0% a 100% el progreso continuo.
2. **Sonido de Espera Periódico:** Emite un pulso suave (latido acústico cada 1.2 segundos) que confirma que la tarea sigue procesándose.
3. **Anuncio de Porcentaje por Voz:** El lector de pantalla (NVDA, JAWS o SAPI) verbaliza automáticamente cada hito del 25% de avance:
   * *"25 por ciento"*
   * *"50 por ciento"*
   * *"75 por ciento"*
   * Anuncio final de resumen con alerta sonora al completarse la operación.

---

## ⚡ Rendimiento y Peso

* **Tamaño del ejecutable:** Solo **200 KB** (vs 18.8 MB en Python o +40 MB en .NET autocontenido).
* **Consumo de memoria RAM:** Apenas **2 a 4 MB de RAM** en reposo.
* **Arranque:** Instantáneo (< 20 milisegundos).
* **Accesibilidad:** 100% nativa con lectores de pantalla (NVDA, JAWS y Narrador) a través de controles Win32 del sistema y motor de voz integrado.

---

## 📁 Gestión de Proyectos (`.syncproj`)

Ahora puedes organizar tus pares de sincronización en proyectos independientes:

* **Nuevo Proyecto (`Ctrl + Shift + N`):** Inicia un proyecto limpio.
* **Abrir Proyecto (`Ctrl + O`):** Carga un archivo de proyecto (`.syncproj` o `.json`).
* **Guardar Proyecto (`Ctrl + S`):** Guarda los pares configurados en el archivo actual.
* **Guardar Proyecto Como... (`Ctrl + Shift + S`):** Guarda el proyecto con un nuevo nombre o ubicación.
* El nombre del proyecto activo se refleja directamente en la barra de título de la ventana.

---

## 🔄 Ejecución Secuencial (Uno a continuación de otro)

Para evitar saturar el disco o la red, el sistema procesa las tareas **en riguroso orden secuencial**:

1. **Sincronización de Selección en Serie (`F7`):**
   * Selecciona uno o varios pares en la lista (usando `Shift + Flechas`, `Ctrl + Espacio` o ratón) y pulsa **F7**.
   * Se ejecutará el primer par hasta completarse, después el segundo, el tercero, y así sucesivamente.

2. **Sincronizar Todos los Pares en Serie (`Ctrl + F6`):**
   * Recorre todos los pares activos del proyecto actual uno detrás de otro.

3. **Cola de Lotes para Múltiples Proyectos (`Alt + L`):**
   * Abre la ventana de **Ejecución en Serie de Proyectos y Pares de Carpetas**.
   * Permite:
     * Añadir diferentes archivos de proyecto (`.syncproj` o `.json`).
     * Añadir los pares del proyecto actual.
     * Reordenar las tareas con los botones **Subir** y **Bajar**.
     * Quitar elementos de la cola.
     * Iniciar la ejecución secuencial completa con botón de parada de emergencia (**Detener**).
     * El lector de pantalla (NVDA/JAWS/SAPI) anuncia cada fase: *"Elemento 1 de 3: Iniciando par...", "Elemento 1 completado...", "Lote completado con éxito"*.

---

## ⚙️ Opciones de Configuración General

Accesible desde el menú **Herramientas > Configuración General** o mediante el botón **Ajustes**:

1. **Iniciar con Windows**: Registra la aplicación en el inicio de usuario de Windows (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`), iniciando automáticamente con el parámetro `--minimized` para no estorbar en pantalla.
2. **Iniciar minimizado en la bandeja del sistema**: Arranca directamente en segundo plano en el área de notificación (System Tray), listo para sincronizar programaciones sin abrir la ventana principal.
3. **Minimizar a la bandeja al pulsar cerrar (X)**: Al cerrar la ventana, no finaliza el proceso, sino que se oculta en la bandeja para continuar con la sincronización automática.
4. **Activar voz para lectores de pantalla**: Anuncia estados y eventos con NVDA, JAWS o voz SAPI de Windows.
5. **Alertas sonoras de finalización**: Emite sonidos del sistema al completar tareas.
6. **Sincronizar automáticamente al arrancar**: Comienza la sincronización de los pares activos nada más iniciar el programa.

---

## 📄 Exportación de Informes y Diagnóstico (.log)

* **Exportar Informe a TXT (`Ctrl + P`):** Genera un informe detallado con estadísticas (total escaneados, a copiar, eliminar, idénticos) y la lista ordenada de archivos con codificación UTF-8. Puede abrirse automáticamente en el Bloc de Notas.
* **Filtrado Inteligente de Cambios (`Ctrl + H`):** Para carpetas con miles de archivos, oculta los idénticos y muestra solo los que requieren acción para una respuesta instantánea.
* **Ordenación por Acción (`Alt + O`):** Prioriza en los primeros lugares los archivos con acciones pendientes (conflictos, copias a destino, borrados).
* **Registro de Diagnóstico (`sync_accesible.log`):** Registra eventos, tiempos, lecturas y previene bloqueos por accesos a carpetas protegidas o enlaces simbólicos. Accesible desde **Herramientas > Ver Archivo de Registro (.log)...**.

---

## ⌨️ Atajos de Teclado Rápidos

| Atajo | Acción |
| :--- | :--- |
| **Shift + F1** | **Ventana rápida con todos los Atajos de Teclado (copiable al portapapeles)** |
| **F1** | **Abrir el Manual de Usuario en HTML navegable por encabezados, listas y tablas** |
| **Ctrl + U** | **Buscar Actualizaciones de SyncAccesible en línea (automático o manual)** |
| **Ctrl + D / F2** | **Ficha de Atributos del Elemento (tabla accesible vertical para JAWS, NVDA, Narrador)** |
| **Ctrl + Shift + N** | Nuevo Proyecto |
| **Ctrl + O** | Abrir archivo de Proyecto (`.syncproj`) |
| **Ctrl + S** | Guardar Proyecto actual |
| **Ctrl + Shift + S** | Guardar Proyecto Como... |
| **Ctrl + P** | **Exportar informe de resultados a fichero .txt** |
| **Ctrl + H** | **Alternar filtro en Vista Previa (solo cambios / todos)** |
| **Alt + O** | **Ordenar Vista Previa por acción requerida** |
| **F5** | Comparar el par seleccionado |
| **F6** | Sincronizar el par seleccionado |
| **Ctrl + I** | **Invertir el par seleccionado (Origen <-> Destino)** |
| **Flecha Derecha / Izquierda** | **Puntero Virtual de Columnas:** Recorre y verbaliza celda por celda (`Columna: Valor`) sin mover el foco de fila |
| **F7** | **Sincronizar los pares seleccionados en serie (uno tras otro)** |
| **Ctrl + F6** | **Sincronizar todos los pares activos en serie** |
| **Alt + L** | **Abrir la Cola de Lotes para múltiples proyectos** |
| **Ctrl + N** | Añadir un nuevo par de carpetas al proyecto |
| **Ctrl + E** | Editar el par seleccionado |
| **Supr (Delete)** | Eliminar el par seleccionado |
| **Ctrl + Tab** | **Cambiar entre pestañas (Pares de Carpetas / Vista Previa)** |
| **Alt + 1 / Ctrl + 1** | **Ir a la Pestaña 1: Pares de Carpetas** |
| **Alt + 2 / Ctrl + 2** | **Ir a la Pestaña 2: Vista Previa de Sincronización** |
| **Tab / Shift + Tab** | Navegar entre controles y listas de la aplicación |
| **Shift + F10 / Tecla Aplicaciones** | Abrir menú contextual del elemento seleccionado |
| **Alt + F4** | Cerrar y salir por completo de la aplicación |
| **Esc / Cerrar (X)** | Minimizar a la bandeja del sistema |

---

## 🚀 Archivos del Proyecto

* **Manual de Usuario:** [`MANUAL.html`](file:///D:/onedrive/antigravity/sync_accesible_cpp/MANUAL.html) (HTML accesible) y [`MANUAL.md`](file:///D:/onedrive/antigravity/sync_accesible_cpp/MANUAL.md) (Autor: José Manuel Fernández Cortés)
* **Ejecutable nativo:** [`SyncAccesible.exe`](file:///D:/onedrive/antigravity/sync_accesible_cpp/SyncAccesible.exe)
* **Archivo de configuración:** [`config.json`](file:///D:/onedrive/antigravity/sync_accesible_cpp/config.json)
* **Script de compilación:** [`build.bat`](file:///D:/onedrive/antigravity/sync_accesible_cpp/build.bat)
* **Código fuente C++:** Directorio [`src/`](file:///D:/onedrive/antigravity/sync_accesible_cpp/src)
