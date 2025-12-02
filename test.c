#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "parser.h"

int main(void)
{
	char buf[1024];
	tline *line;
	int i;

	printf("==> ");
	while (fgets(buf, 1024, stdin))
	{

		line = tokenize(buf);
		if (line == NULL)
		{
			continue;
		}
		if (line->redirect_input != NULL)
		{
			printf("redirección de entrada: %s\n", line->redirect_input);
		}
		if (line->redirect_output != NULL)
		{
			printf("redirección de salida: %s\n", line->redirect_output);
		}
		if (line->redirect_error != NULL)
		{
			printf("redirección de error: %s\n", line->redirect_error);
		}
		if (line->background)
		{
			printf("comando a ejecutarse en background\n");
		}

		int pids[2];
		for (i = 0; i < line->ncommands; i++)
		{

			char *command = line->commands[i].argv[0];

			pids[i] = fork();

			if (pids[i] == 0)
			{
				execvp(command, line->commands[i].argv);
			}
		}

		for (i = 0; i < line->ncommands; i++)
		{
			wait(NULL);
		}
		printf("==> ");
	}
	return 0;
}
