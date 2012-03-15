#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    /*
    printf("stuff\n");
    fprintf(stderr, "bad\n");
    printf("bye\n");
    */

    /*
    for (int i = 0; i < 4; ++i) {
        printf("O");
        fprintf(stderr, "E");
    }
    fprintf(stderr, "\n");
    printf("\n");

    for (int i = 0; i < 4; ++i) {
        fprintf(stderr, "!");
        printf("=");
    }
    printf("\n");
    fprintf(stderr, "\n");
    */

    printf("hello_");
    fprintf(stderr, "oh_noes");
    printf("world\n");
    abort();
}
