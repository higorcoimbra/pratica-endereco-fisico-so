#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#define PAGEMAP_LENGTH 8

int main(){
	int a;
	printf("%p\n",&a);
	unsigned long offset = (unsigned long)&a / getpagesize();
	printf("%lu\n",offset);
	printf("%d\n",getpagesize());
	return 0;
}