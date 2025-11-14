#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <argp.h>
#include <netinet/in.h>  
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>     
#include <sys/select.h>   
#include <unistd.h>
#include <sysexits.h>
#include <stdbool.h>
#include <time.h>
#include <endian.h> 



struct client_arguments {
	struct sockaddr_in server_addr; // stores ip_address and port number
    /*
        Set port: server_addr.sin_port = htons(port);
        Get port: ntohs(server_addr.sin_port)
        Set IP: inet_pton(AF_INET, ip_str, &server_addr.sin_addr);
        Get IP: inet_ntop(AF_INET, &server_addr.sin_addr, buffer, buflen)
        */
	int timeRequests;
	int timeout;
};

error_t client_parser(int key, char *arg, struct argp_state *state) {
	struct client_arguments *args = state->input;
	error_t ret = 0;
    char *endptr;

	switch(key) {
	case 'a':
		/* validate that address parameter makes sense */
        args->server_addr.sin_family = AF_INET; // get ready to accept to IPv4
		if (inet_pton(AF_INET, arg, &args->server_addr.sin_addr) <= 0) {
			argp_error(state, "Invalid address");
		}
		break;
	case 'p':
		/* Validate that port is correct and a number, etc!! */
        long port = strtol(arg, &endptr, 10);

        // Check to make sure entire string is a number
        if (*endptr != '\0') {
            argp_error(state, "Port must be a valid number");
        }
        args->server_addr.sin_port = htons((uint16_t)port);
		if (port <= 0 || port > 65535) {
            argp_error(state, "Port must be between 1 and 65535");
        }
		break;
	case 'n':
        // Check to make sure entire string is a number
        long requests = strtol(arg, &endptr, 10);
        if (*endptr != '\0') {
            argp_error(state, "Time requests must be a valid number");
        }
        // Checks to make sure that requests is a positive int
        if (requests<0){
            argp_error(state,"Time requests cannot be a negative number");
        }
        args->timeRequests = (int)requests;
		break;
	case 't': 
		/* validate arg */
        long time = strtol(arg, &endptr, 10);
        // checks to make sure that timout is valid number
        if (*endptr != '\0') {
            argp_error(state, "Requests must be a valid number");
        }
		args->timeout = (int)time;
		break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

void client_parseopt(struct client_arguments *args,int argc, char *argv[]) {
	struct argp_option options[] = {
		{ "addr", 'a', "addr", 0, "The IP address the server is listening at", 0},
		{ "port", 'p', "port", 0, "The port that is being used at the server", 0},
		{ "timerequests", 'n', "timerequests", 0, "The number of time requests to send to the server", 0},
		{ "timout", 't', "timout", 0, "The time in seconds that the client will wait after sending its last time request to receive a time response ", 0},	
		{0}
	};

	struct argp argp_settings = { options, client_parser, 0, 0, 0, 0, 0 };

    memset(args, 0, sizeof(*args));

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		printf("Got error in parse\n");
	}
    if(args->server_addr.sin_addr.s_addr == 0){
        fprintf(stderr,"Missing IP address");
    }
    if(args->server_addr.sin_port == 0){
        fprintf(stderr,"Missing port");
        exit(1);
    }
    if(!args->timeRequests){
        fprintf(stderr,"Missing time requests");
        exit(1);

    }
    if (args->timeout < 0) {
        fprintf(stderr, "Timeout cannot be negative\n");
        exit(1);
    }
}

int main(int argc, char *argv[]){

    struct client_arguments args; 
    client_parseopt(&args,argc,argv);

    int sock = socket(AF_INET,SOCK_DGRAM,0);
    if (sock < 0){
        perror("socket failed");
        exit(1);
    }
    
    int timeReqs = args.timeRequests;

    double theta[timeReqs+1];
    double delta[timeReqs+1];
    bool received[timeReqs+1];
    memset(received,0,sizeof(received));

    // Sending timeout requests
    for (int i = 1;i<=timeReqs;i++){
        struct timespec timeSent;
        clock_gettime(CLOCK_REALTIME,&timeSent);

        uint8_t req[24];
        *(uint32_t*)(req) = htonl(i);
        *(uint32_t*)(req+4) = htonl(7);
        *(uint64_t*)(req + 8) = htobe64(timeSent.tv_sec);
        *(uint64_t*)(req + 16) = htobe64(timeSent.tv_nsec);

        ssize_t sendy = sendto(sock, req, sizeof(req), 0,(struct sockaddr*)&args.server_addr,sizeof(args.server_addr));

        if (sendy < 0){
            perror("send failed");
            close(sock);
            exit(1);
        }
    }
    // receiving requests 

    struct timeval tv;
    tv.tv_sec = args.timeout;
    tv.tv_usec = 0;

    fd_set readf; 
    
    int receivedCount = 0;

    while (1) {
        FD_ZERO(&readf);
        FD_SET(sock, &readf);

        struct timeval *timeout_ptr = NULL;
        struct timeval tv;

        if (args.timeout > 0) {
            tv.tv_sec = args.timeout;
            tv.tv_usec = 0;
            timeout_ptr = &tv;
        }

        int ready = select(sock + 1, &readf, NULL, NULL, timeout_ptr);
        if (ready < 0) {
            perror("select failed");
            break;
        } else if (ready == 0) {
            break;
        }

        if (FD_ISSET(sock, &readf)) {
            uint8_t response[40];
            struct sockaddr_in server;
            socklen_t serverlen = sizeof(server);

            ssize_t len = recvfrom(sock, response, sizeof(response), 0,
                                (struct sockaddr*)&server, &serverlen);
            if (len < 40) continue;

            struct timespec t2;
            clock_gettime(CLOCK_REALTIME, &t2);

            uint32_t seq = ntohl(*(uint32_t*)response);
            uint64_t csec = be64toh(*(uint64_t*)(response + 8));
            uint64_t cnsec = be64toh(*(uint64_t*)(response + 16));
            uint64_t ssec = be64toh(*(uint64_t*)(response + 24));
            uint64_t snsec = be64toh(*(uint64_t*)(response + 32));

            double T0 = csec + cnsec / 1e9;
            double T1 = ssec + snsec / 1e9;
            double T2 = t2.tv_sec + t2.tv_nsec / 1e9;

            theta[seq] = ((T1 - T0) + (T1 - T2)) / 2.0;
            delta[seq] = T2 - T0;
            if (!received[seq]) {
                received[seq] = true;
                receivedCount++;
            }

            if (receivedCount == timeReqs)
                break;
        }
    }

    for (int i = 1; i <= timeReqs; i++) {
        if (received[i])
            printf("%d: %.4f %.4f\n", i, theta[i], delta[i]);
        else
            printf("%d: Dropped\n", i);
    }

    close(sock);
    return 0;
}