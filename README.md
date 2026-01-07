# MiniShell (msh) 🚀

Una implementación personalizada de una shell de UNIX desarrollada en **C**. Este proyecto demuestra el uso avanzado de llamadas al sistema de bajo nivel para la creación de procesos, comunicación mediante pipes, redirecciones y control de trabajos (jobs).

## ✨ Características

* **Ejecución de Mandatos:** Uso de `fork`, `execvp` y `waitpid`.
* **Pipelines (Tuberías):** Soporte para encadenar múltiples comandos (`|`).
* **Redirecciones:** Entrada (`<`), salida (`>`) y errores (`2>`).
* **Gestión de Trabajos (Job Control):**
    * Ejecución en segundo plano (`&`).
    * Comandos internos: `jobs` (listar) y `bg` (reanudar).
    * Manejo de estados: `RUNNING`, `STOPPED`, `FINISHED`.
* **Comandos Internos (Built-ins):** `cd`, `umask` y `exit`.
* **Manejo de Señales:** Gestión de `SIGINT` (Ctrl+C) y `SIGTSTP` (Ctrl+Z).

## 🛠️ Instalación y Compilación

Para compilar el proyecto, asegúrate de tener instalada la librería del parser y ejecuta:

```bash
make