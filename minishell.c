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

void free_pipes_memory(int **pipes, int npipes);
int open_file(char *filename, int flags);
void change_dir(tline *line);
void umask_command(tline *line);
mode_t str_to_octal(char *str);

int main(void)
{
    char buf[1024];
    pid_t *pids;
    tline *line;
    int **pipes;
    int ncommands;
    int npipes;
    int i, j;
    int file;
    char *command_name;

    printf("msh> ");
    while (fgets(buf, 1024, stdin))
    {
        pipes = NULL;
        npipes = 0;
        line = tokenize(buf);
        if (line != NULL)
        {

            ncommands = line->ncommands;
            // Cambiar de directorio

            if (strcmp(line->commands[0].argv[0], "cd") == 0)
            {
                change_dir(line);
            }
            // Otros comandos
            else if (strcmp(line->commands[0].argv[0], "umask") == 0)
            {
                umask_command(line);
            }
            else
            {

                /* Crear n procesos para ejecutar line->ncommands */
                pids = malloc(sizeof(pid_t) * ncommands); // Reservar memoria para los pids, de ncommnads

                // Caso: Un unico comando
                if (ncommands == 1)
                {

                    pids[0] = fork();
                    if (pids[0] < 0)
                    {
                        fprintf(stderr, "Falló el fork().\n%s\n", strerror(errno));
                        exit(1);
                    }
                    else if (pids[0] == 0)
                    {
                        // Reedirigr entrada solo al primer proceso
                        if (line->redirect_input != NULL)
                        {
                            file = open_file(line->redirect_input, O_RDONLY);

                            dup2(file, STDIN_FILENO);
                            close(file);
                        }

                        // Reedirigir salida ultimo comando

                        if (line->redirect_output != NULL)
                        {
                            file = open_file(line->redirect_output, O_WRONLY | O_CREAT);
                            dup2(file, STDOUT_FILENO);
                            close(file);
                        }

                        // Reedirigir error

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
                }
                // Caso: 2 o mas comandos
                else
                {
                    // Create an array of ncommands pipes

                    npipes = ncommands - 1; // define number of pipes to create
                    pipes = malloc(sizeof(int *) * npipes);
                    for (i = 0; i < npipes; i++)
                    {
                        pipes[i] = malloc(sizeof(int) * 2);
                        pipe(pipes[i]);
                    }

                    // Create processes and execute commands
                    for (i = 0; i < ncommands; i++)
                    {
                        command_name = line->commands[i].argv[0];

                        pids[i] = fork();
                        if (pids[i] < 0)
                        { /* Error */
                            fprintf(stderr, "Falló el fork() con pid: %d.\n%s\n", pids[i], strerror(errno));
                            exit(1);
                        }
                        else if (pids[i] == 0)
                        {
                            // Proceso hijo i

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

                            // Primer proceso no se le reedirige entrada
                            if (i != 0)
                            {
                                dup2(pipes[i - 1][0], STDIN_FILENO);
                            }

                            // Ultimo proceso no se le reedirige la salida
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

                            command_name = line->commands[i].argv[0];
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
                }

                // Esperar hijos y liberar memoria para 1 o mas comandos, cuando no sean: cd
                for (i = 0; i < ncommands; i++)
                {
                    wait(NULL);
                }

                free(pids);
                free_pipes_memory(pipes, npipes);
            }
        }

        printf("\nmsh> ");
    }
    return 0;
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
    char *path = getenv("HOME");
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
    int fd;
    int access_mode = flags & O_ACCMODE;

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
        fprintf(stderr, "'%s': Error\n%s\n",
                filename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    return fd;
}

void umask_command(tline *line)
{
    mode_t mask;

    if (line->commands[0].argc > 2)
    {
        return;
    }
    // Caso 1: Imprimir mascara actual
    if (line->commands[0].argc == 1)
    {
        mask = umask(0);
        printf("0%o\n", (unsigned int)mask);
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
    long long_val;
    mode_t octal_num;

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