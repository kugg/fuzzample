/*
** selectserver.c -- It does weird stuff
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "9034"   // port we're listening on

fd_set master;    // master file descriptor list
int fdmax;        // maximum file descriptor number
int listener;     // listening socket descriptor


void sig_handler(int signo)
{
  if (signo == SIGINT)
    exit(0);
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int recv_and_handle(int sockfd) {
	char buf[65535];    // buffer for client data
    int nbytes;
    int j;
    long int length;

	// handle data from a client
	if ((nbytes = recv(sockfd, buf, sizeof buf, 0)) <= 0) {
		// got error or connection closed by client
		if (nbytes == 0) {
			printf("selectserver: socket %d hung up\n", sockfd);
		} else {
			perror("recv");
		}
		close(sockfd); // bye!
		FD_CLR(sockfd, &master); // remove from master set
		return -1;
	}
    buf[nbytes] = 0;
    // we got some data from a client (note issues with fragmented packets)
	if(nbytes < 2)
		return -1;
	// Handle message types:
	switch (buf[0]) {
		case '#':
			// String based command			
			if (strlen("#close_this_connection") < nbytes && 
			    strncmp("#close_this_connection",buf,strlen("#close_this_connection")) == 0) {
				close(sockfd);
				FD_CLR(sockfd, &master); // remove from master set
				return 0;
			} else if (strlen("#number: ") < nbytes && 
					  strncmp("#number: ",buf,strlen("#number: ")) == 0) {
				length = strtol(buf+strlen("#number: "),NULL,16);
				if (length < -65536) {
					perror("Woo strtol misuse in wget found");
					raise(SIGSEGV);
				}
				return 0;
			} else if (strlen("#recurse") < nbytes &&
					  strncmp("#recurse",buf,strlen("#recurse")) == 0) {
				// DoS bug that blocks other clients from the server as it
				// calls recv on a blocking socket without data
				return recv_and_handle(sockfd);
			}
			break;
		case 0x01:
			// Binary based command
			switch (buf[1]) {
				case 0x01:
					close(sockfd);
					FD_CLR(sockfd, &master); // remove from master set
					return 0;
				case 0x02:
					length = *(long int*)(buf+2);
					if (length < 0) {
						raise(SIGSEGV);
					}
					break;
				case 0x03:
					// recurse
					// Memory exaustion in this case can only be found if
					// memory limit is low & the fuzzer handles multiple
					// recvs for one input.
					return recv_and_handle(sockfd);
				case 0x04:
					for(j = 0; j <= fdmax; j++) {
						if (j == listener || j == sockfd) {
							continue;
						}
						
						// send to everyone!
						if (!FD_ISSET(j, &master))
							continue;
							
						// This is unreachable unless we fuzz and emulate multiple clients
						if (send(j, buf, nbytes, 0) == -1) {
							perror("send");
						}
					}
					break;
			}		
	}
}

int main(void)
{
    fd_set read_fds;  // temp file descriptor list for select()
    
    
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    
    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, rv;

    struct addrinfo hints, *ai, *p;
	if (signal(SIGINT, sig_handler) == SIG_ERR)
		printf("\ncan't catch SIGINT\n");
    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }
	
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
		
        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for(;;) {
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (!FD_ISSET(i, &read_fds)) { 
				continue;
			}
			if (i != listener) {
				recv_and_handle(i);
				continue;
			}
			
			// So we are the listener, handle a new connection
			addrlen = sizeof remoteaddr;
			newfd = accept(listener, 
		   (struct sockaddr *)&remoteaddr,
						   &addrlen);

			if (newfd == -1) {
				perror("accept");
				continue;
			}
			FD_SET(newfd, &master); // add to master set
			if (newfd > fdmax) {    // keep track of the max
				fdmax = newfd;
			}
			printf("selectserver: new connection from %s on "
				   "socket %d\n",
				   inet_ntop(remoteaddr.ss_family,
				   get_in_addr((struct sockaddr*)&remoteaddr),
				   remoteIP, INET6_ADDRSTRLEN),
				   newfd);
		}
    } // END for(;;)--and you thought it would never end!
    return 0;
}

