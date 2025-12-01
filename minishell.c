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
    tline *line;

    printf("msh> ");
    while (fgets(buf, 1024, stdin))
    {
        line = tokenize(buf);

        printf("msh> ");
    }

    return 0;
}