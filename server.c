#include <argp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <endian.h>


volatile sig_atomic_t stop = 0;

void handle_signal(int sig) {
    (void)sig;
    stop = 1;  
}

struct server_arguments {
	int port;
	int droppedPercentage;
    int cond;
};

error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
	error_t ret = 0;
	switch(key) {
	case 'p':
		/* Validate that port is correct and a number, etc!! */
		args->port = atoi(arg);
		if (args->port == 0) {
			argp_error(state, "Invalid option for a port, must be a number");
		}else if(args->port <= 1024){
            argp_error(state,"port must have a value greater than 1024");
        }
		break;
	case 'd':
		args->droppedPercentage = atoi(arg);
		if (args->droppedPercentage < 0 || args->droppedPercentage > 100) {
			argp_error(state, "Must be between 0 and 100");
		}
		break;
    case 'c':
        args->cond = 1;
        break;
	default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

void server_parseopt(struct server_arguments *args,int argc, char *argv[]) {
	bzero(args, sizeof(*args));
    args->droppedPercentage = 0;
	struct argp_option options[] = {
		{ "port", 'p', "port", 0, "The port to be used for the server" ,0},
		{ "droppedPercentage", 'd', "droppedPercentage", 0, "The droppedPercentage to be used for the server. Zero by default", 0},
		{0}
	};
	struct argp argp_settings = { options, server_parser, 0, 0, 0, 0, 0 };
	if (argp_parse(&argp_settings, argc, argv, 0, NULL, args) != 0) {
		printf("Got an error condition when parsing\n");
	}

    if (args->port == 0){
        fprintf(stderr,"server port error");
        exit(1);
    }
	printf("Got port %d and droppedPercentage %d \n", args->port, args->droppedPercentage);
}

void handleClients(int sock, struct server_arguments *args) {
    static struct sockaddr_in lastAddr[6];
    static uint32_t maxSeq[6];
    static time_t lastSeen[6];
    static int numClients = 0;

    fd_set readfds;
    struct timeval tv;

    while (!stop) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ready = select(sock + 1, &readfds, NULL, NULL, &tv);
        if (ready <= 0)
            continue;

        if (FD_ISSET(sock, &readfds)) {
            uint8_t buffer[24];
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);

            ssize_t len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &clientLen);
            if (len < 24)
                continue;

            uint32_t seq = ntohl(*(uint32_t*)buffer);
            uint32_t ver = ntohl(*(uint32_t*)(buffer + 4));
            if (ver != 7)
                continue;

            uint64_t clientSec = be64toh(*(uint64_t*)(buffer + 8));
            uint64_t clientNsec = be64toh(*(uint64_t*)(buffer + 16));

            if ((rand() % 100) < args->droppedPercentage)
                continue;

            int idx = -1;
            for (int i = 0; i < numClients; i++) {
                if (lastAddr[i].sin_addr.s_addr == clientAddr.sin_addr.s_addr && lastAddr[i].sin_port == clientAddr.sin_port) {
                    idx = i;
                    break;
                }
            }

            time_t now = time(NULL);
            if (idx == -1 && numClients < 6) {
                idx = numClients++;
                lastAddr[idx] = clientAddr;
                maxSeq[idx] = seq;
                lastSeen[idx] = now;
            } else if (idx != -1) {
                if (difftime(now, lastSeen[idx]) > 120)
                    maxSeq[idx] = seq;
                else if (seq < maxSeq[idx]) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(clientAddr.sin_addr), ip, INET_ADDRSTRLEN);
                    printf("%s:%d %u %u\n", ip, ntohs(clientAddr.sin_port), seq, maxSeq[idx]);
                    fflush(stdout);
                } else if (seq > maxSeq[idx]) {
                    maxSeq[idx] = seq;
                }
                lastSeen[idx] = now;
            }

            struct timespec nowT;
            clock_gettime(CLOCK_REALTIME, &nowT);

            uint8_t resp[40];
            *(uint32_t*)(resp) = htonl(seq);
            *(uint32_t*)(resp + 4) = htonl(7);
            *(uint64_t*)(resp + 8) = htobe64(clientSec);
            *(uint64_t*)(resp + 16) = htobe64(clientNsec);
            *(uint64_t*)(resp + 24) = htobe64(nowT.tv_sec);
            *(uint64_t*)(resp + 32) = htobe64(nowT.tv_nsec);

            sendto(sock, resp, sizeof(resp), 0, (struct sockaddr*)&clientAddr, clientLen);
        }
    }
}

int main(int argc, char *argv[]) {
    struct server_arguments args;
    server_parseopt(&args, argc, argv);

    srand(time(NULL));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket failed");
        exit(1);
    }
    signal(SIGINT, handle_signal);

    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(args.port);

    if (bind(sock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        perror("bind failed");
        close(sock);
        exit(1);
    }

    printf("Server listening on port %d (drop rate %d%%)\n",
           args.port, args.droppedPercentage);

    signal(SIGINT, handle_signal);

    while (!stop) {
        handleClients(sock, &args);
    }

    close(sock);
    printf("Server shutting down...\n");
    return 0;
}