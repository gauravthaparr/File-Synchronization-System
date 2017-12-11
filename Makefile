PORT=51261
CFLAGS = -DPORT=$(PORT) -g -Wall -std=gnu99

all: rcopy_client rcopy_server

rcopy_client: rcopy_client.o
	gcc ${CFLAGS} -o $@ $^ ftree.c hash_functions.c

rcopy_server: rcopy_server.o
	gcc ${CFLAGS} -o $@ $^ ftree.c hash_functions.c

%.o: %.c
	gcc ${CFLAGS} -c $<

clean:
	rm *.o rcopy_client rcopy_server
