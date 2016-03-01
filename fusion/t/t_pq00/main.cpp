/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

/*
 *  Test for pq (priority queue class)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/md.h"
#include "mb/pq.h"

bool modulo_sort(int a, int b) {
	if (a < 0) a = -a;
	if (b < 0) b = -b;

	return a < b;
}

pq_t<int, modulo_sort> test;

int main(int argc, const char** argv) {
	for (int i = 1; i < argc; ++i)
		printf("%s ", argv[i]);

  printf("\n");

  for (int i = 1; i < argc; ++i)
		test.put(atoi(argv[i]));

	int i;

	while (test.get(i))
		printf("%d ", i);

	return 0;
}

