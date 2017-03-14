#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>
#include <chrono>
#include <ctime>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <ctime>

#define CHUNCK_SIZE 3
#define PACKET_SIZE 1472

struct PacketHeader {
    unsigned int type;     // 0: START; 1: END; 2: DATA; 3: ACK
    unsigned int seqNum;   // Described below
    unsigned int length;   // Length of data; 0 for ACK, START and END packets
    unsigned int checksum; // 32-bit CRC
};

int main(int argc, char *argv[]) {
	int wStart = 0;
	int wSize = atoi(argv[2]);
	std::vector<std::chrono::time_point<std::chrono::system_clock>> wTime;
	int time_idx = 0; // points to time of start of window

	std::ifstream is{argv[1], std::ios::binary | std::ios::ate};
	if (!is) {
		std::cout << "Could not open file\n";
		return 1;
	}
	auto size = is.tellg();
	std::string str(size, '\0'); // construct string to stream size
	is.seekg(0);
	is.read(&str[0], size);
	std::cout << str << "\n";


	int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[4], argv[5], &hints, &servinfo)) != 0) {
        perror("socket");
        return 1;
    }

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client:connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
		perror("failed to create socket\n");
		abort();
    }
    char buffer[PACKET_SIZE];
    memset(buffer, '\0', PACKET_SIZE);
    strcpy(buffer, "abc");

    if ((numbytes = send(sockfd, buffer, PACKET_SIZE, 0) == -1)) {
        perror("sendto");
        exit(1);
    }

    freeaddrinfo(servinfo);
    return 0;
}