#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <fstream>
#include <deque>

#include "crc32.h"

#define CHUNCK_SIZE 3
#define PACKET_SIZE 1472

struct PacketHeader {
	unsigned int type;     // 0: START; 1: END; 2: DATA; 3: ACK
	unsigned int seqNum;   // Described below
	unsigned int length;   // Length of data; 0 for ACK, START and END packets
	unsigned int checksum; // 32-bit CRC
};

void int_to_char(char * buf, unsigned int value, int & i) {
	buf[i++] = value >> 24;
	buf[i++] = (value >> 16) & 0xff;
	buf[i++] = (value >> 8) & 0xff;
	buf[i++] = value & 0xff;
}

void header_to_char(PacketHeader* header, char * buf) {
	int i = 0;
	int_to_char(buf, header->type, i);
	int_to_char(buf, header->seqNum, i);
	int_to_char(buf, header->length, i);
	int_to_char(buf, header->checksum, i);
}

unsigned int char_to_int(char * buf, int &i) {
	unsigned int ret;
	ret += buf[i++] << 24;
	ret += buf[i++] << 16;
	ret += buf[i++] << 8;
	ret += buf[i++];
	return ret;
}

void parse_header(PacketHeader * header, char * buf) {
	int i = 0;
	header->type = char_to_int(buf, i);
	header->seqNum = char_to_int(buf, i);
	header->length = char_to_int(buf, i);
	header->checksum = char_to_int(buf, i);
}

void parse_packet(PacketHeader *header, char *buf, char *data) {
	parse_header(header, buf);

	if (header->length == 0) {
		return;
	}

	buf += 16;
	for (int i = 0; i < header->length; i++) {
		*data++ = *buf++;
	}
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[]) {
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	// struct sockaddr_storage their_addr;
    // struct sockaddr_storage sender_addr;
	// char buf[MAXBUFLEN];
	// socklen_t addr_len;
    // socklen_t sender_len;
	char their_ip[INET6_ADDRSTRLEN];
	char sender_ip[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("listener: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "listener: failed to bind socket\n");
		return 2;
	}

	freeaddrinfo(servinfo);

	printf("listener: waiting to recvfrom...\n");

	int num_file = 1;
	std::string filename;

	char dbuf[PACKET_SIZE]; // buffer to recv data packets
	char abuf[PACKET_SIZE]; // buffer to send back acks
	char data[CHUNCK_SIZE];
	PacketHeader dheader; // header for data packets
	PacketHeader aheader; // hedaer for ACKs
	std::deque<char*> window;
	int wSize = atoi(argv[3]);
	int expSeqNum = 0; // expected seqnum;
	bool inConnection = false; // in the middle of a connection
	std::ofstream outfile;

	memset(sender_ip, '\0', INET6_ADDRSTRLEN);
	// loop for recv data
	while (true) {
		struct sockaddr_storage their_addr;
		socklen_t addr_len;

		addr_len = sizeof their_addr;
		memset(dbuf, '\0', PACKET_SIZE);
		if ((numbytes = recvfrom(sockfd, dbuf, PACKET_SIZE , 0,
		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
			perror("recvfrom");
			exit(1);
		}

		memset(data, '\0', CHUNCK_SIZE);
		parse_packet(&dheader, dbuf, data);

		// check checksum
		int checksum = crc32(data, dheader.length);
		if (dheader.checksum != checksum) {
			continue;
		}

		// get ip
		memset(their_ip, '\0', INET6_ADDRSTRLEN);
		inet_ntop(their_addr.ss_family,
					get_in_addr((struct sockaddr *)&their_addr),
					their_ip, sizeof their_ip);

		if (dheader.type == 0) { // START

			if (!inConnection) { // start a new connection
				strcpy(sender_ip, their_ip);
				// send ACK
				aheader.type = 3;
				aheader.seqNum = dheader.seqNum;
				aheader.length = 0;
				aheader.checksum = 0;
				memset(abuf, '\0', PACKET_SIZE);
				header_to_char(&aheader, abuf);

				// open new file
				filename = "FILE-" + std::to_string(num_file);
				num_file++;
				outfile.open(filename.c_str(), std::ofstream::binary);

				inConnection = true;

			} else if (strcmp(sender_ip, their_ip) == 0) { // START from the same connection
				// send ACK
				aheader.type = 3;
				aheader.seqNum = dheader.seqNum;
				aheader.length = 0;
				aheader.checksum = 0;
				memset(&abuf, '\0', PACKET_SIZE);
				header_to_char(&aheader, abuf);
				
			} else { // START from other ip, ignore
				continue;
			}

		} else if (dheader.type = 1) { // END

			aheader.type = 3;
			aheader.seqNum = dheader.seqNum;
			aheader.length = 0;
			aheader.checksum = 0;
			memset(abuf, '\0', PACKET_SIZE);
			header_to_char(&aheader, abuf);

			if (strcmp(sender_ip, their_ip) == 0) { // end a connection
				memset(sender_ip, '\0', INET6_ADDRSTRLEN);
				inConnection = false;
				outfile.close();
			}

		} else { // DATA

			if (strcmp(sender_ip, their_ip) != 0) { // Data from other sender, ignore
				continue;
			}

			if (dheader.seqNum < expSeqNum || dheader.seqNum >= expSeqNum + window.size()) {
				continue; // ignore, no ACK sent
			}

            // if there's gap in window, insert NULL
			for (int i = window.size(); i <= dheader.seqNum - expSeqNum; i++) {
				window.push_back(NULL);
			}

			char *newData = new char[CHUNCK_SIZE];
			for (int i = 0; i < dheader.length; i++) {
				newData[i] = data[i];
			}
			window[dheader.seqNum - expSeqNum] = newData;

			if (dheader.seqNum == expSeqNum) { // write to file
				while (!window.empty() && window[0] != NULL) {
					outfile.write(data, dheader.length);
					delete [] window[0];
					window.pop_front();
					expSeqNum++;
				}
			}

			aheader.type = 3;
			aheader.seqNum = expSeqNum;
			aheader.length = 0;
			aheader.checksum = 0;
			memset(&abuf, '\0', PACKET_SIZE);
			header_to_char(&aheader, abuf);
		}

		// send ACK
		if ((numbytes = sendto(sockfd, abuf, PACKET_SIZE, 0,
			(struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("talker: sendto");
				exit(1);
		}

	}


	return 0;
}