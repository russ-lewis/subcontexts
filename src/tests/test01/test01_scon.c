#include <stdio.h>

void test_inner()
{
    printf("in test01_scon\n");    
}

void *test(void *arg)
{
    test_inner();
    return NULL;
}

