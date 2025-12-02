#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>

#include "parser.h"

int main(void)
{
    char buf[1024];
    pid_t *pids;
    tline *line;
    int **pipes;
    int ncommands;
    int npipes;
    int i, j;
    char *command_name;

    printf("msh> ");
    while (fgets(buf, 1024, stdin))
    {
        line = tokenize(buf);        // Get TLine object
        ncommands = line->ncommands; // Get number of commands
        npipes = ncommands - 1;      // define number of pipes to create

        /* Create n process to execute line->ncommands */
        pids = malloc(sizeof(pid_t) * ncommands); // Reserve memorie for the pids, for the ncommnads

        if (ncommands == 1)
        {
            pids[0] = fork();
            if (pids[0] == 0)
            {
                command_name = line->commands[0].argv[0];
                execvp(command_name, line->commands[0].argv);
            }

            wait(NULL);
        }
        else
        {
            // Create an array of ncommands pipes
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
                if (pids[i] == 0)
                {
                    // Proceso hijo i

                    // El primer comando solo reedirecciona la salida
                    if (i != 0)
                    {
                        dup2(pipes[i - 1][0], STDIN_FILENO);
                    }

                    if (i != ncommands - 1)
                    {

                        // El ultimo comando solo reedirecciona la entrada
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
            for (i = 0; i < ncommands; i++)
            {
                wait(NULL);
            }
        }

        printf("\nmsh> ");
    }

    return 0;
}