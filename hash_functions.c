#include <stdio.h>
#include <stdlib.h>
#include "hash.h"

#define BLOCK_SIZE 8


char *hash(FILE *f) {
	char *hash_val = malloc(BLOCK_SIZE);
	for (int i = 0; i < BLOCK_SIZE; i++) {
		hash_val[i] = '\0';
	}
	
	char input;
	int count = 0;
	while (fread(&input, sizeof(char), 1, f) != 0) {
		
		if (count == BLOCK_SIZE) {
			count = 0;
		}
		hash_val[count] = input ^ hash_val[count];

		count ++;
	}
	
	return hash_val;
}