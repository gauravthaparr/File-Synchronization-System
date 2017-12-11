#ifndef _FTREE_H_
#define _FTREE_H_

#include "hash.h"

#define MAXPATH 128
#define MAXDATA 256
#define MAX_BACKLOG 20

// Input states
#define AWAITING_TYPE 0
#define AWAITING_PATH 1
#define AWAITING_SIZE 2
#define AWAITING_PERM 3
#define AWAITING_HASH 4
#define AWAITING_DATA 5

// Request types
#define REGFILE 1
#define REGDIR 2
#define TRANSFILE 3

#define OK 0
#define SENDFILE 1
#define ERROR 2

#ifndef PORT
    #define PORT 30100
#endif

struct request {
    int type;           // Request type is REGFILE, REGDIR, TRANSFILE
    char path[MAXPATH];
    mode_t mode;
    char hash[BLOCKSIZE];
    int size;
};

struct client {
	int fd;
	int s_count;
	struct client *next;
	struct request *req;
};

// Client handling functions for the server
int removeclient(struct client *client, struct client **clients);
int addclient(struct client *client, int fd, struct client **clients);
int handleclient(struct client *client);

// writing struct to server
int *transfer_struct(struct request *req, int *socket_fd);

// Connecting to the server
int *connect_to_server(char *source, char *host, unsigned short *port);

// Runs through the file tree and copies if needed
void file_manager(struct request *r, int *sock_fd, char *source, char *host, unsigned short *port);

int rcopy_client(char *source, char *host, unsigned short port);
void rcopy_server(unsigned short port);

#endif // _FTREE_H_
