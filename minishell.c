#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include "parser.h"

void free_pipes_memory(int **pipes, int npipes);

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
        line = tokenize(buf);        // Get TLine object
        ncommands = line->ncommands; // Get number of commands

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
                    file = open(line->redirect_input, O_RDONLY);
                    dup2(file, STDIN_FILENO);
                    close(file);
                }

                // Reedirigir salida ultimo comando

                if (line->redirect_output != NULL)
                {
                    file = open(line->redirect_output, O_WRONLY | O_CREAT, 0666);
                    dup2(file, STDOUT_FILENO);
                    close(file);
                }

                // Reedirigir error

                if (line->redirect_error != NULL)
                {
                    file = open(line->redirect_error, O_WRONLY | O_CREAT, 0666);
                    dup2(file, STDERR_FILENO);
                    close(file);
                }

                command_name = line->commands[0].argv[0];
                execvp(command_name, line->commands[0].argv);
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
                        file = open(line->redirect_input, O_RDONLY);
                        dup2(file, STDIN_FILENO);
                        close(file);
                    }

                    // Reedirigir salida ultimo comando
                    if (i == ncommands - 1)
                    {
                        if (line->redirect_output != NULL)
                        {
                            file = open(line->redirect_output, O_WRONLY | O_CREAT, 0666);
                            dup2(file, STDOUT_FILENO);
                            close(file);
                        }

                        if (line->redirect_error != NULL)
                        {
                            file = open(line->redirect_error, O_WRONLY | O_CREAT, 0666);
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
                    perror("Error execvp");
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

        // Esperar hijos y liberar memoria

        for (i = 0; i < ncommands; i++)
        {
            wait(NULL);
        }

        free(pids);
        free_pipes_memory(pipes, npipes);
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