#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include "parser.h"

#define BUFFER_SIZE 1024
#define MAX_JOBS 10

typedef enum
{
    FINISHED,
    RUNNING,
    STOPPED
} ProcessStatus;

typedef struct
{
    char command_line[BUFFER_SIZE];
    ProcessStatus status;
    int isInBackground; // 0 si no esta en background,o su ID de jobs si lo esta
    pid_t *pids;        // Lista de procesos en el trabajo
    int num_pids;
} Job;

Job jobs_list[MAX_JOBS];

void one_command_exec(tline *line, char buf[BUFFER_SIZE]);
void multiple_commands_exec(tline *line, char buf[BUFFER_SIZE]);
void free_pipes_memory(int **pipes, int npipes);
int open_file(char *filename, int flags);

void change_dir(tline *line);

void umask_command(tline *line);
mode_t str_to_octal(char *str);

int add_job(pid_t *pids, int isInBackground, char command_line[BUFFER_SIZE], int ncommands);
void process_cleanup(); // Mantiene la lista de trabajos eliminando trabajos de ella correctamente
void jobs();
int jobs_list_empty(); // Comprueba si la lista de jobs esta vacia
void bg(tline *line);
void terminate_jobs(); // Libera la memoria de cada job antes de salir

int main(void)
{
    char buf[BUFFER_SIZE] = {0};
    tline *line = NULL;
    int ncommands = 0;

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    // Inicializar lista de trabajos
    for (int i = 0; i < MAX_JOBS; i++)
    {
        jobs_list[i].isInBackground = 0;
        jobs_list[i].num_pids = 0;
        jobs_list[i].pids = NULL;
        jobs_list[i].command_line[0] = '\0';
        jobs_list[i].status = FINISHED;
    }

    // Bucle principal
    printf("msh> ");
    while (fgets(buf, BUFFER_SIZE, stdin))
    {
        process_cleanup();
        line = tokenize(buf);
        if (line == NULL || line->ncommands == 0)
        {
            printf("msh> ");
            continue;
        }

        // Cambiar de directorio
        if (strcmp(line->commands[0].argv[0], "cd") == 0)
        {
            change_dir(line);
        }
        else if (strcmp(line->commands[0].argv[0], "umask") == 0)
        {
            umask_command(line);
        }
        else if (strcmp(line->commands[0].argv[0], "jobs") == 0)
        {
            jobs();
        }
        else if (strcmp(line->commands[0].argv[0], "bg") == 0)
        {
            bg(line);
        }
        else if (strcmp(line->commands[0].argv[0], "exit") == 0)
        {
            terminate_jobs();
            exit(EXIT_SUCCESS);
        }
        else
        {
            // Otros commandos
            ncommands = line->ncommands;

            // Caso: Un unico comando
            if (ncommands == 1)
            {
                one_command_exec(line, buf);
            }
            // Caso: 2 o mas comandos
            else
            {
                multiple_commands_exec(line, buf);
            }
        }

        printf("\nmsh> ");
    }
    return 0;
}

void one_command_exec(tline *line, char buf[BUFFER_SIZE])
{
    pid_t *pid = NULL;
    int file = 0;
    int wstatus = 0;
    int job_pos = -1;
    char *command_name = NULL;

    pid = malloc(sizeof(pid_t) * 1);
    pid[0] = fork();
    if (pid[0] < 0)
    {
        fprintf(stderr, "Falló el fork().\n%s\n", strerror(errno));
        exit(1);
    }
    else if (pid[0] == 0)
    {

        if (!line->background)
        {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
        }

        if (line->redirect_input != NULL)
        {
            file = open_file(line->redirect_input, O_RDONLY);
            dup2(file, STDIN_FILENO);
            close(file);
        }

        if (line->redirect_output != NULL)
        {
            file = open_file(line->redirect_output, O_WRONLY | O_CREAT);
            dup2(file, STDOUT_FILENO);
            close(file);
        }

        if (line->redirect_error != NULL)
        {
            file = open_file(line->redirect_error, O_WRONLY | O_CREAT);
            dup2(file, STDERR_FILENO);
            close(file);
        }

        command_name = line->commands[0].argv[0];

        execvp(command_name, line->commands[0].argv);
        fprintf(stderr, "%s: No se encuentra el mandato", command_name);
        exit(EXIT_FAILURE);
    }

    // Proceso Padre

    if (line->background)
    {
        // Add job to list
        add_job(pid, 1, buf, line->ncommands);
    }
    else
    {
        job_pos = add_job(pid, 0, buf, line->ncommands);
        waitpid(pid[0], &wstatus, WUNTRACED);
        if (WIFSTOPPED(wstatus))
        {
            // Marcar job como detenido
            jobs_list[job_pos].status = STOPPED;
            jobs_list[job_pos].isInBackground = job_pos + 1;
            printf("\n[%d]+  Stopped \t %s\n", job_pos + 1, jobs_list[job_pos].command_line);
        }
    }
}

void multiple_commands_exec(tline *line, char buf[BUFFER_SIZE])
{
    int **pipes = NULL; // 0 lectura, 1 escritura
    pid_t *pids = NULL;
    int npipes = 0;
    int ncommands = 0;
    int wstatus = 0;
    char *command_name = NULL;
    int i = 0;
    int j = 0;
    int file = 0;
    int job_pos = -1;
    int job_stopped = -1;

    ncommands = line->ncommands;
    npipes = ncommands - 1;

    pids = malloc(sizeof(pid_t) * ncommands);
    pipes = malloc(sizeof(int *) * npipes);

    /* Crear tuberias para ejecutar line->ncommands */
    for (i = 0; i < npipes; i++)
    {
        pipes[i] = malloc(sizeof(int) * 2);
        pipe(pipes[i]);
    }

    // Crear n procesos para ejecutar ncommands
    for (i = 0; i < ncommands; i++)
    {
        command_name = line->commands[i].argv[0];

        pids[i] = fork();
        if (pids[i] < 0)
        { /* Error */
            fprintf(stderr, "Falló el fork() con pid: %d.\n%s\n", pids[i], strerror(errno));
            exit(EXIT_FAILURE);
        }
        else if (pids[i] == 0)
        {
            // Proceso hijo i
            if (!line->background)
            {
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
            }

            // Reedirigr entrada solo al primer proceso
            if (i == 0 && line->redirect_input != NULL)
            {
                file = open_file(line->redirect_input, O_RDONLY);
                dup2(file, STDIN_FILENO);
                close(file);
            }

            // Reedirigir salida ultimo comando
            if (i == ncommands - 1)
            {
                if (line->redirect_output != NULL)
                {
                    file = open_file(line->redirect_output, O_WRONLY | O_CREAT);
                    dup2(file, STDOUT_FILENO);
                    close(file);
                }

                if (line->redirect_error != NULL)
                {
                    file = open_file(line->redirect_error, O_WRONLY | O_CREAT);
                    dup2(file, STDERR_FILENO);
                    close(file);
                }
            }

            // Primer proceso no se le redirige entrada a un pipe
            if (i != 0)
            {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            // Ultimo proceso no se le redirige la salida a un pipe
            if (i != ncommands - 1)
            {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Una vez reedireccionado cierro todo
            for (j = 0; j < npipes; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(command_name, line->commands[i].argv);
            fprintf(stderr, "%s: No se encuentra el mandato", command_name);
            exit(EXIT_FAILURE);
        }
    }

    // Proceso Padre
    for (i = 0; i < npipes; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Agregar trabajo (bg) o esperar hijos (fg)
    if (line->background)
    {
        add_job(pids, 1, buf, ncommands);
    }
    else
    {
        job_pos = add_job(pids, 0, buf, ncommands);

        // Esperar cada hijo y manejar si se detiene
        job_stopped = 0;
        for (i = 0; i < ncommands; i++)
        {
            waitpid(pids[i], &wstatus, WUNTRACED);
            if (WIFSTOPPED(wstatus))
            {
                job_stopped = 1;
            }
        }

        if (job_stopped)
        {
            jobs_list[job_pos].status = STOPPED;
            jobs_list[job_pos].isInBackground = job_pos + 1;
            printf("\n[%d]+  Stopped \t %s\n", job_pos + 1, jobs_list[job_pos].command_line);
        }
    }
    free_pipes_memory(pipes, npipes);
}

void free_pipes_memory(int **pipes, int npipes)
{
    if (pipes == NULL)
    {
        return;
    }

    for (int i = 0; i < npipes; i++)
    {
        if (pipes[i] != NULL)
        {
            free(pipes[i]);
        }
    }

    free(pipes);
}

void change_dir(tline *line)
{
    char *path = NULL;

    path = getenv("HOME");
    if (line->commands[0].argc > 2)
    {
        fprintf(stderr, "cd: demasiados argumentos\n");
        exit(EXIT_FAILURE);
    }

    if (line->commands[0].argc == 2)
    {
        path = line->commands[0].argv[1];
    }

    if (chdir(path) == -1)
    {
        fprintf(stderr, "cd: Error al cambiar de directorio a '%s'\n%s\n",
                path,
                strerror(errno));
    }
}

int open_file(char *filename, int flags)
{
    int fd = 0;
    int access_mode = 0;

    access_mode = flags & O_ACCMODE; // Extraer de flags solo si es de escritura o lectura

    if (access_mode == O_RDONLY)
    {
        fd = open(filename, flags);
    }
    else if (access_mode == O_WRONLY)
    {
        fd = open(filename, flags, 0666);
    }

    if (fd == -1)
    {
        fprintf(stderr, "'%s': Error. %s\n",
                filename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    return fd;
}

void umask_command(tline *line)
{
    mode_t mask = 0;

    if (line->commands[0].argc > 2)
    {
        return;
    }
    // Caso 1: Imprimir mascara actual
    if (line->commands[0].argc == 1)
    {
        mask = umask(0);
        printf("%04o\n", (unsigned int)mask);
        umask(mask);
    }
    else if (line->commands[0].argc == 2)
    {
        // 1. Comprobar que el argumento sea un octal

        mask = str_to_octal(line->commands[0].argv[1]);
        if (mask == (mode_t)-1)
        {
            fprintf(stderr, "%s no es un octal\n", line->commands[0].argv[1]);
            return;
        }

        // 2. Cambiar masacara
        umask(mask);
    }

    return;
}

mode_t str_to_octal(char *str)
{
    long long_val = 0;
    mode_t octal_num = 0;

    // Comrporbar que es un octal
    if (str == NULL || *str == '\0')
    {
        return -1;
    }

    for (int i = 0; str[i] != '\0'; i++)
    {
        if (str[i] < '0' || str[i] > '7')
        {
            return -1;
        }
    }

    // Convertir de str a octal
    long_val = strtol(str, NULL, 8);
    octal_num = (mode_t)long_val;

    return (mode_t)octal_num;
}

int add_job(pid_t *pids, int isInBackground, char command_line[BUFFER_SIZE], int ncommands)
{
    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++)
    {
        // Si un job a terminado, lo agrego a la lista
        if (jobs_list[i].status == FINISHED)
        {
            slot = i;
            break;
        }
    }
    if (slot >= MAX_JOBS - 1)
    {
        fprintf(stderr, "Número máximo de trabajos alcanzado\n");
        exit(EXIT_FAILURE);
    }

    strcpy(jobs_list[slot].command_line, command_line);
    jobs_list[slot].pids = pids;
    jobs_list[slot].num_pids = ncommands;
    jobs_list[slot].status = RUNNING;

    if (isInBackground)
    {
        jobs_list[slot].isInBackground = slot + 1;
        printf("[%d] %d\n", slot + 1, pids[0]);
    }
    else
    {
        jobs_list[slot].isInBackground = 0;
    }

    return slot;
}

void process_cleanup()
{
    pid_t pstatus = 0;
    int running = 0;

    for (int i = 0; i < MAX_JOBS; i++)
    {

        if (jobs_list[i].status != RUNNING)
            continue;

        if (jobs_list[i].num_pids == 0)
        {
            continue;
        }

        // Limpiar proceso en fg
        if (!jobs_list[i].isInBackground)
        {
            if (jobs_list[i].pids != NULL)
            {
                free(jobs_list[i].pids);
            }

            jobs_list[i].pids = NULL;
            jobs_list[i].num_pids = 0;
            jobs_list[i].isInBackground = 0;
            jobs_list[i].status = FINISHED;

            continue;
        }

        running = 0;
        // Limpiar procesos en bg
        for (int j = 0; j < jobs_list[i].num_pids; j++)
        {
            if (jobs_list[i].pids[j] == 0)
                continue;

            pstatus = waitpid(jobs_list[i].pids[j], NULL, WNOHANG);

            // Comprobar si surgio un error(-1) o termino (>0)
            if (pstatus == -1 || pstatus > 0)
            {
                jobs_list[i].pids[j] = 0;
            }
            else
            {
                running++;
            }
        }

        // Job terminado
        if (running == 0)
        {
            free(jobs_list[i].pids);
            jobs_list[i].pids = NULL;
            jobs_list[i].num_pids = 0;
            jobs_list[i].status = FINISHED;

            printf("[%d]+  Done \t %s\n", jobs_list[i].isInBackground, jobs_list[i].command_line);
            jobs_list[i].isInBackground = 0;
        }
    }
}

void jobs()
{
    Job job = {0};
    for (int i = 0; i < MAX_JOBS; i++)
    {
        job = jobs_list[i];

        if (!job.isInBackground)
        {
            continue;
        }

        if (job.status == RUNNING)
        {
            printf("[%d]+  Running \t %s\n", job.isInBackground, job.command_line);
        }
        else if (job.status == STOPPED)
        {
            printf("[%d]-  Stopped \t %s\n", job.isInBackground, job.command_line);
        }
    }
}

int jobs_list_empty()
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_list[i].pids != NULL)
        {
            return 0;
        }
    }

    return 1;
}

void bg(tline *line)
{
    int job_pos = -1;
    int job_id = -1;
    size_t len = 0;

    // Lista vacia nada que reanudar
    if (jobs_list_empty())
    {
        return;
    }

    if (line->ncommands != 1)
    {
        return;
    }

    if (line->commands[0].argc < 1 || line->commands[0].argc > 2)
    {
        return;
    }

    // Parte 1, encontrar el Job
    if (line->commands[0].argc == 1)
    {
        // Sin argumento, reanudar el ultimo agregado
        for (int i = MAX_JOBS - 1; i >= 0; i--)
        {
            if (jobs_list[i].status == STOPPED)
            {
                job_pos = i;
                break;
            }
        }
    }
    else
    {
        // Seleccionar jobs id
        job_id = atoi(line->commands[0].argv[1]);

        if (job_id < 1 || job_id > MAX_JOBS)
        {
            return;
        }

        job_pos = job_id - 1;
        if (jobs_list[job_pos].status != STOPPED)
        {
            return;
        }
    }

    // Parte 2, depsertar los proceos
    for (int i = 0; i < jobs_list[job_pos].num_pids; i++)
    {
        kill(jobs_list[job_pos].pids[i], SIGCONT);
    }

    jobs_list[job_pos].status = RUNNING;

    len = strlen(jobs_list[job_pos].command_line);
    if (len > 0 && jobs_list[job_pos].command_line[len - 1] == '\n')
    {
        jobs_list[job_pos].command_line[len - 1] = '\0';
    }
    strcat(jobs_list[job_pos].command_line, " &\n");

    printf("[%d]+ %s\n", jobs_list[job_pos].isInBackground, jobs_list[job_pos].command_line);
}

void terminate_jobs()
{
    for (int i = 0; i < MAX_JOBS; i++)
    {
        if (jobs_list[i].pids != NULL)
        {
            free(jobs_list[i].pids);
        }
    }
}