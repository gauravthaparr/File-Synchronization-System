#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "ftree.h"
#include "hash.h"

// Code for the server
void rcopy_server(unsigned short port) {

	int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }
    
    // Set information about the port (and IP) we want to be connected to.
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    memset(&server.sin_zero, 0, 8);
    
    // Bind the selected port to the socket (give it a name).
    if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }
    
    // Announce willingness to accept connections on this socket.
    if (listen(sock_fd, MAX_BACKLOG) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }
    
    struct client *clients[MAX_BACKLOG]; // To store the clients
    for (int j = 0; j < MAX_BACKLOG; j++) {
    	clients[j] = NULL;
    }
    struct client *client = NULL;
    struct sockaddr_in peer;
    socklen_t socklen = sizeof(peer);
    int client_fd; 			// to store the accepted client's fd
    int maxfd;
        
    int i;
    
    fd_set allset;
    fd_set rset;
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(sock_fd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = sock_fd;
	
    
    while(1) {
    	// make a copy of the set before we pass it into select
        rset = allset;
        
        // Select the ready fd 
        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            continue;
        }
        
    	
    	if (FD_ISSET(sock_fd, &rset)) {
            printf("a new client is connecting\n");

            if ((client_fd = accept(sock_fd, (struct sockaddr *)&peer, &socklen)) < 0) {
                perror("accept");
                exit(1);
            }
            FD_SET(client_fd, &allset);
            if (client_fd > maxfd) {
                maxfd = client_fd;
            }
            printf("connection from %s\n", inet_ntoa(peer.sin_addr));
            if (addclient(client, client_fd, clients) == -1){
            	fprintf(stderr, "No more clients should have been accepted\n");
            }
        }
        
        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) { // Check which fd is ready
                for (int ready_fd = 0; ready_fd < MAX_BACKLOG; ready_fd++) { 
                    if (clients[ready_fd] != NULL && clients[ready_fd]->fd == i) { //check which client has the ready fd
                        struct client* rclient = clients[ready_fd];
                        int result = handleclient(rclient);
                        if (result == 1) { // If client is done
                            int tmp_fd = rclient->fd;
                            if(removeclient(rclient, clients) == -1) {
                            	fprintf(stderr, "Couldn't find client with fd %d in the list\n", rclient->fd);
                            }
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                        }
                        break;
                    }
                }
            }
        }
        
        // Read struct and increment a counter accordingly for state change
    }
    close(sock_fd);
    close(client_fd);
}

int handleclient(struct client *client) {
    // To pass in addresses of MACROS when writing
    int sf_flag = SENDFILE;
    int ok_flag = OK;
    int er_flag = ERROR;
    // integers to convert from ntohl
    int size, type, mode;
    int num_read;
    char reader[MAXDATA];

    // STATE 1: Read type
    if(client->s_count == AWAITING_TYPE) {

    	printf("AWAITING TYPE being run\n");
    	if(read(client->fd, &(type), sizeof(int)) < 0) {
    	perror("Reading the type");
		}
		client->req->type = ntohl(type);
		printf("%d\n", client->req->type);
		client->s_count = AWAITING_PATH;
    }

    // Dealing with Directories
    else if (client->req->type == REGDIR) {
    	if(client->s_count == AWAITING_PATH) {
    		read(client->fd, client->req->path, MAXPATH);
    		printf("%s\n",client->req->path);
    		client->s_count = AWAITING_PERM;
    		printf("AWAITING PATH was done\n");
    	}
    	else if (client->s_count == AWAITING_PERM) {
    		read(client->fd, &mode, sizeof(int));
			client->req->mode = (mode_t)ntohl(mode);

    		struct stat dup_info;
		    // Get info on 'same' file in dest
		    if (lstat(client->req->path, &dup_info) == -1) {
		        perror("lstat");
		    }
		    // To detect mismatch
		    if (!S_ISDIR(dup_info.st_mode)) {
		        fprintf(stderr, "There was a mismatch between file with relative path: %s\n",
		       			client->req->path);
		    	if (write(client->fd, &er_flag, sizeof(int)) < 0) {
            	    perror("Failed ERROR response to client fd.");
            	}
	        }
	        printf("Here?\n");
			// If there exists no such directory in the destination directory 
			if (access(client->req->path, F_OK) == -1) {
	  		    mkdir(client->req->path, client->req->mode);
	  		}
	  		else {// Check if permissions are the same
	  		    struct stat dup_info;
	  		    if(lstat(client->req->path, &dup_info) == -1) {
	  		        perror("lstat");
	  		    }
	  		    if (dup_info.st_mode != client->req->mode) {
	  		        mkdir(client->req->path, client->req->mode);
	  		    }
	  		}
	  		printf("Write\n");
			if(write(client->fd, &ok_flag, sizeof(int)) < 0) {
			    perror("Failed OK response to client fd.");
			}
			client->s_count = AWAITING_TYPE;
	 	}
	}
    		
        
    // Dealing with a regular file
    else if (client->req->type == REGFILE) {
    	int match = 0;   // flag for match

	    if(client->s_count == AWAITING_PATH) {
	        read(client->fd, client->req->path, MAXPATH);
	        printf("%s\n",client->req->path);
	        client->s_count = AWAITING_SIZE;
	    }
	    else if(client->s_count == AWAITING_SIZE) {
	        read(client->fd, &size, sizeof(int));
	        client->req->size = ntohl(size);
	        client->s_count = AWAITING_PERM;
	    }
	    else if (client->s_count == AWAITING_PERM) {
	        read(client->fd, &mode, sizeof(int));
	        client->req->mode = (mode_t)ntohl(mode);
	        client->s_count = AWAITING_HASH;
	    }
	    else if (client->s_count == AWAITING_HASH) {
	    	read(client->fd, client->req->hash, BLOCKSIZE);
	        client->s_count = AWAITING_TYPE;
		}

    	if (access(client->req->path, F_OK) == 0) { 
	  	    struct stat dup_info;
            // Get info on 'same' file in dest
            if (lstat(client->req->path, &dup_info) == -1) {
                perror("lstat");
                exit(1);
            }
            // To detect mismatch
            if (!S_ISREG(dup_info.st_mode)) {
                fprintf(stderr, "There was a mismatch between file with relative path: %s\n",
                		client->req->path);
                if (write(client->fd, &er_flag, sizeof(int)) < 0) {
                    perror("Failed ERROR response to client fd.");
                    exit(1);
            	}
            }
	        // Compare sizes
	        if (client->req->size == dup_info.st_size) { 
                // if size is same then check the hashes
                FILE *d_fs = fopen(client->req->path, "rb");
                if (d_fs == NULL) {
                    perror("fopen");
                    exit(1);
                }
                char *d_file_hash = hash(d_fs);
                fclose(d_fs);
                
                // if hash is same then the files match (are the exact same)
                if (strncmp(d_file_hash, client->req->hash, BLOCKSIZE) == 0) {
                    match = 1;
                }
            }
	    }
	    // Write back appropriate response according to match flag
	     	if (!match) {
	          	if (write(client->fd, &sf_flag, sizeof(int)) < 0) {
        		perror("Failed SENDFILE response to client fd.");
	          	}
      	} else {
	        if (write(client->fd, &ok_flag, sizeof(int)) < 0) {
				perror("Failed OK response to client_fd");
	        	}
	    }
    		
    }// end of If REGFILE
        
        
   // Dealing with transferring data
    else if (client->req->type == TRANSFILE) {
		
		if(client->s_count == AWAITING_PATH) {
		    read(client->fd, client->req->path, MAXPATH);
		    printf("%s\n",client->req->path);
		    client->s_count = AWAITING_SIZE;
		}
		else if(client->s_count == AWAITING_SIZE) {
		    read(client->fd, &size, sizeof(int));
		    client->req->size = ntohl(size);
			client->s_count = AWAITING_PERM;
		}
		else if (client->s_count == AWAITING_PERM) {
		    read(client->fd, &mode, sizeof(int));
		    client->req->mode = (mode_t)ntohl(mode);
		    client->s_count = AWAITING_HASH;
		}
		else if (client->s_count == AWAITING_HASH) {
		    read(client->fd, client->req->hash, BLOCKSIZE);
		    client->s_count = AWAITING_DATA;
		}
		else if (client->s_count == AWAITING_DATA) {
			FILE *d_fs = fopen(client->req->path, "wb");
			if (d_fs == NULL) {
        		perror("fopen");
        		exit(1);
        	}
        	while((num_read = read(client->fd, reader, MAXDATA)) != 0) {
		    	fwrite(&reader, sizeof(char), MAXDATA, d_fs);
		    }

		    fclose(d_fs);
		    chmod(client->req->path, (client->req->mode & 0777));
	    	client->s_count = AWAITING_TYPE;
	    	if (write(client->fd, &ok_flag, sizeof(int)) < 0) {
        		perror("Failed OK response to client_fd");
        	}
	    	return 1;
	    }
		
    }
    return 0;

}

int addclient(struct client *client, int fd, struct client **clients) {
    client = malloc(sizeof(struct client));
    struct request *r = malloc(sizeof(struct request));

    client->fd = fd;
    client->s_count = AWAITING_TYPE;
    client->req = r;
    for (int c = 0; c < MAX_BACKLOG; c++) {
    	if (clients[c] == NULL) {
    		clients[c] = client;
    		return 0;
    	}
    }
    return -1;
}

int removeclient(struct client *client, struct client **clients) {

    for (int r = 0; r < MAX_BACKLOG; r++) {
    	if(clients[r] == client) {
    		clients[r] = NULL;
    		free(client);
    		return 0;
		}
    }
    return -1;
}



// Code for the client
int rcopy_client(char *source, char *host, unsigned short port) {
	 
	// Create the request struct
	struct request req;
	struct stat f_info;
    // Get all info on file using lstat
 	if (lstat(source, &f_info) == -1) {
 	    perror("lstat");
 	    exit(1);
 	}
	 
    // Update info in req according to type of request
	if (S_ISDIR(f_info.st_mode)) {
	    req.type = REGDIR;
	}
	else if (S_ISREG(f_info.st_mode)) {
	    req.type = REGFILE;
	}
	
	//Set path to basename of path
	char *fname = basename(source);
	if (strlen(fname) >= MAXPATH) {
	    fprintf(stderr, "Path name is too long");
	    exit(1);
	}
	strncpy(req.path, fname, MAXPATH);
	 
	// Get mode by getting lstat
	req.mode = f_info.st_mode;
	 
	// Get info on hash of file
	FILE *fs = fopen(source, "rb");
    if (fs == NULL) {
    	perror(source);
        exit(1);
    }
    
    char *file_hash = hash(fs);
    strcpy(req.hash, file_hash);
    fclose(fs);
	 
	// Set the size of the file
	req.size = f_info.st_size;
	
	
	int *sock_fd = connect_to_server(source, host, &port);
	
    // Function to walk file tree structure
    file_manager(&req, sock_fd, source, host, &port);
    
    close(*sock_fd);
    
    return 1;
}

//Recursive function to process each file or directory
void file_manager(struct request *r, int *sock_fd, char *source, char *host, unsigned short *port) {
    //send struct to the server and waits for a response
    printf("%s\n",r->path);
    int *response = transfer_struct(r, sock_fd);
    printf("Did not get stuck in transfer_struct\n");
    
    if (*response == OK) {
        if (r->type == REGDIR) {
            struct dirent *dp;
            DIR *dir = opendir(r->path);
            while ((dp = readdir(dir)) != NULL) {
                //Exclude directories that start with .
                if ((*dp).d_name[0] != '.') {
                
                    //Add file path to path of directory
                    char *file_path = malloc(strlen(r->path) + strlen((*dp).d_name) +2);
                    strcpy(file_path, r->path);
                    strcat(file_path, "/");
                    strcat(file_path, (*dp).d_name);
                
		              //lstat file_path to obtain mode and size of next file
                    struct stat f;
                    if (lstat(file_path, &f) == -1) {
                        perror("lstat");
                        exit(-1);
                    } //stat
                
		              //
                    struct request *next = malloc(sizeof(struct request)); 
                    strcpy(next->path, file_path);
                    next->mode = f.st_mode;
                    next->size = f.st_size;

                    FILE *fp;
                    // if file is a FILE
                    if ((fp = fopen(file_path, "rb")) != NULL) {
                        strncpy(next->hash, hash(fp), BLOCKSIZE);
                    }
                    // if file is a DIRECTORY
                    else {
			               memset(next->hash, '\0', BLOCKSIZE);
                    }
    
                    //Recursive call
                    file_manager(next, sock_fd, source, host, port);

                } //if name does not start with .
            } //while directory has files
        }//if type is REGDIR
    }//if "OK" was the response
    
        
    // if rcopy server responds with SENDFILE then do a fork for copying
    else if (*response == SENDFILE) {    
        pid_t child_pid;
	     child_pid = fork();
	
	     //Fork failed
            if (child_pid < 0) {
                perror("fork");
                exit(-1);
            } 
	
	     //Child process to copy the file
	     if (child_pid == 0) {
            r->type = TRANSFILE;
            connect_to_server(source, host, port);
            int *response = transfer_struct(r, sock_fd);
            if (*response == OK) {
                exit(0);
            }
            fprintf(stderr, "Response was %d when transferring file at path %s\n", 
            		*response, r->path);
            exit(1);
            
	     } //child process
	     
        //Exclude links
    }//if server responds with SENDFILE
    else if (*response == ERROR) {
        
    }
} //file_manager



// Helper for writing a struct to the server socket fd
int *transfer_struct(struct request *req, int *socket_fd) {
    int num_written;
    // 0.First write request type to let the server know what kind of data its receiving.
    int type = htonl(req->type);
    num_written = write(*socket_fd, &(type), sizeof(int));
    if (num_written == -1) {
        perror("writing request type");
    	  close(*socket_fd);
    	  exit(1);
    }
    printf("%s\n",req->path);
    // 1.Next write the path
    num_written = write(*socket_fd, req->path, MAXPATH);
    if (num_written == -1) {
        perror("writing the path");
    	  close(*socket_fd);
    	  exit(1);
    }
    
    // 2.Write size if not REGDIR
    if(req->type != REGDIR) {
        int size = htonl(req->size);
        num_written = write(*socket_fd, &size, sizeof(int));
        if (num_written == -1) {
            perror("writing the size");
    	      close(*socket_fd);
    	      exit(1);
        }
     }
    
    // 3.Now write the mode
    int mode = htonl((int)req->mode);
    num_written = write(*socket_fd, &(mode), sizeof(int));
    if (num_written == -1) {
        perror("writing the mode");
    	  close(*socket_fd);
    	  exit(1);
    }
     
    
    // 4.Write the hash if not REGDIR
    if(req->type != REGDIR) {
        num_written = write(*socket_fd, &(req->hash), BLOCKSIZE);
        if (num_written == -1) {
            perror("writing the hash");
    	      close(*socket_fd);
    	      exit(1);
    	  }
    }
    
    // 5.Write Data if TRANSFILE
    if (req->type == TRANSFILE){
        FILE *s_fs = fopen(req->path, "rb");
        if (s_fs == NULL) {
            perror("fopen");
            exit(-1);
        }
        char writer[MAXDATA];
        while(fread(writer, sizeof(char), MAXDATA, s_fs)) {
            write(*socket_fd, writer, sizeof(char)*MAXDATA);
        }
        fclose(s_fs);
    }
    

    printf("Just before attempting to read from server\n");
    int *response = malloc(sizeof(int));
    if(read(*socket_fd, response, sizeof(int)) == -1) {
        perror(req->path);
        exit(1);
    }
    return response;
    
}

int *connect_to_server(char *source, char *host, unsigned short *port) {
    // Socket for the client
	 int *sock_fd = malloc(sizeof(int));
	 *sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }
    
    // Set the IP and port of the server to connect to.
    struct sockaddr_in *server = malloc(sizeof(struct sockaddr_in));
    server->sin_family = AF_INET;
    server->sin_port = htons(*port);
    if (inet_pton(AF_INET, host, &server->sin_addr) < 1) {
        perror("client: inet_pton");
        close(*sock_fd);
        exit(1);
    }

    // Connect to the server.
    if (connect(*sock_fd, (struct sockaddr *)server, sizeof(*server)) == -1) {
        perror("client: connect");
        close(*sock_fd);
        exit(1);
    }
    return sock_fd;
    
}

// === RECURSION
// === SERVER CASES (PROPER RESPONSE): REGFILE, TRANSFILE
// WAIT FOR CHILDREN TO FINISH - IN CLIENT
// GARBAGE COLLECTION IN COPYING
// === RELATIVE PATHNAME FOR RECURSION
// === MKDIR ONLY IF IT DOESN'T ALREADY EXIST
// === ERROR CHECKING 
// MISMATCH
// TAKE OUT MALLOC FROM PATHNAME?
// WHAT HAPPENS WHEN MAIN CLIENT IS DONE WRITING: AFTER EACH STRUCT
// 												  AFTER COPYING EVERYTHING