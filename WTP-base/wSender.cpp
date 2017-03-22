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
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <chrono>
#include <ctime>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <ctime>
#include <deque>
#include <assert.h>
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

#include "crc32.h"

#define CHUNCK_SIZE 1456
#define PACKET_SIZE 1472
#define RETRANS_TIME 500

struct PacketHeader {
	unsigned int type;     // 0: START; 1: END; 2: DATA; 3: ACK
	unsigned int seqNum;   // Described below
	unsigned int length;   // Length of data; 0 for ACK, START and END packets
	unsigned int checksum; // 32-bit CRC
};

void header_to_char(PacketHeader* header, char * buf) {
    memcpy(buf , (char*)&(header->type), 4);
	memcpy(buf + 4 , (char*)&(header->seqNum), 4);
	memcpy(buf + 8 , (char*)&(header->length), 4);
	memcpy(buf + 12 , (char*)&(header->checksum), 4);
}

void parse_header(PacketHeader * header, char * buf) {
	memcpy((char*)&(header->type), buf, 4);
	memcpy((char*)&(header->seqNum), buf + 4, 4);
	memcpy((char*)&(header->length), buf + 8, 4);
	memcpy((char*)&(header->checksum), buf + 12, 4);
}

void to_packet(PacketHeader * header, char * buf, char * data, int length) {
	header_to_char(header, buf);
	// copy data
	for (int j = 0; j < length; j++) {
		buf[16 + j] = data[j];
	}
}

// put at most size packets to the window
void setWindow(std::deque<char*>& window, int size, std::istream& is, int& seqNum) {
	// std::string data_str;
	char data[CHUNCK_SIZE];
	int length;
	
	for (int i = 0; i < size; i++) {

		
		is.read(data, CHUNCK_SIZE);

		length = is.gcount();

		// end of file
		if (length <= 0) {
			return;
		}

		PacketHeader header;
		header.type = 2; // DATA
		header.seqNum = seqNum++;
		header.length = length;
		header.checksum = crc32(data, length);

		char * buf = new char [PACKET_SIZE];
		memset(buf, '\0', PACKET_SIZE);
		to_packet(&header, buf, data, length); // convert header and data to a packet
		
		window.push_back(buf);
		
	}
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


// start: starting point in window to send packets
void sendWindow(std::deque<char*>& window, std::deque<std::chrono::time_point<std::chrono::system_clock>>& wTime, 
	int sockfd, int start, std::ofstream& logfile) {

	int numbytes;
	for (int i = start; i < window.size(); i++) {

		char data[CHUNCK_SIZE];
		memset(data, 0, CHUNCK_SIZE);

		// get header from packets in the window
		PacketHeader cheader;
		parse_packet(&cheader, window[i], data);
		// std::cout << cheader.type << std::endl;

		if ((numbytes = send(sockfd, window[i], PACKET_SIZE, 0) == -1)) {
			perror("send");
			exit(1);
		}

		logfile << cheader.type << '\t' << cheader.seqNum << '\t' << cheader.length << '\t' << cheader.checksum << std::endl;
		std::cout << cheader.type << '\t' << cheader.seqNum << '\t' << cheader.length << '\t' << cheader.checksum << std::endl;

		wTime.push_back(std::chrono::high_resolution_clock::now());
	}
}


// send START or END
void sendConnection(int sockfd, int type, std::ofstream& logfile) {
	srand (time(NULL));

	PacketHeader cheader; // header of START or END
	cheader.type = type;
	cheader.seqNum = rand(); // random >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	cheader.length = 0;
	cheader.checksum = 0;

	char cbuf[PACKET_SIZE]; // buffer for send data Packets
	memset(cbuf, '\0', PACKET_SIZE);
	header_to_char(&cheader, cbuf);
	int numbytes;

	if ((numbytes = send(sockfd, cbuf, PACKET_SIZE, 0) == -1)) {
		perror("send");
		exit(1);
	}
	logfile << cheader.type << '\t' << cheader.seqNum << '\t' << cheader.length << '\t' << cheader.checksum << std::endl;
	std::cout << cheader.type << '\t' << cheader.seqNum << '\t' << cheader.length << '\t' << cheader.checksum << std::endl;

	char rbuf[PACKET_SIZE];
	memset(rbuf, '\0', PACKET_SIZE);
	PacketHeader rheader; // header of response from receiver

	std::chrono::time_point<std::chrono::system_clock> start, current;
	start = std::chrono::high_resolution_clock::now();
	std::chrono::milliseconds msec(RETRANS_TIME);
	std::chrono::duration<double> timeout(msec);

	while (true) {
		numbytes = recv(sockfd, rbuf, PACKET_SIZE , 0);
		if (numbytes == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				perror("recvfrom");
				exit(1);
			}
		} else {
			parse_header(&rheader, rbuf);

			logfile << rheader.type << '\t' << rheader.seqNum << '\t' << rheader.length << '\t' << rheader.checksum << std::endl;
			std::cout << rheader.type << '\t' << rheader.seqNum << '\t' << rheader.length << '\t' << rheader.checksum << std::endl;

			if (rheader.type == 3 && rheader.seqNum == cheader.seqNum) {
				// START or END ACKed
				break;
			}
		}

		// when timeout
		current = std::chrono::high_resolution_clock::now();
		if (current - start >= timeout) {
			if ((numbytes = send(sockfd, cbuf, PACKET_SIZE, 0) == -1)) {
				perror("send");
				exit(1);
			}
			logfile << cheader.type << '\t' << cheader.seqNum << '\t' << cheader.length << '\t' << cheader.checksum << std::endl;
			std::cout << cheader.type << '\t' << cheader.seqNum << '\t' << cheader.length << '\t' << cheader.checksum << std::endl;
			start = std::chrono::high_resolution_clock::now();
		}
	}


}

int main(int argc, char *argv[]) {
	int wSize = atoi(argv[2]);
	int lowSeqNum = 0;
	int seqNum = 0; // next seqNum to be added
	std::deque<std::chrono::time_point<std::chrono::system_clock>> wTime;
	std::deque<char*> window;

	std::ifstream is(argv[1], std::ios::binary);
	if (!is) {
		std::cout << "Could not open file\n";
		return 1;
	}
	std::ofstream logfile;
	logfile.open(argv[3]);

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

		// non-blocking
		fcntl(sockfd, F_SETFL, O_NONBLOCK);

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


	// send start
	sendConnection(sockfd, 0, logfile); // 0: start

	setWindow(window, wSize, is, seqNum);

	sendWindow(window, wTime, sockfd, 0, logfile);

	char buffer[PACKET_SIZE];
	memset(buffer, '\0', PACKET_SIZE);
	int ack_pack, old_size, add_size, start;
	std::chrono::time_point<std::chrono::system_clock> current_time;
	std::chrono::milliseconds msec(RETRANS_TIME);
	std::chrono::duration<double> timeout(msec);


	while (true) {
		memset(buffer, '\0', PACKET_SIZE);
		numbytes = recv(sockfd, buffer, PACKET_SIZE , 0);
		if (numbytes == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) { // if not timeout
				perror("recvfrom");
				exit(1);
			}
		} else {
			PacketHeader header;
			parse_header(&header, buffer);
			
			logfile << header.type << '\t' << header.seqNum << '\t' << header.length << '\t' << header.checksum << std::endl;
			std::cout << header.type << '\t' << header.seqNum << '\t' << header.length << '\t' << header.checksum << std::endl;

			if (header.type == 3) { // ACK

				if (header.seqNum > lowSeqNum) {
					ack_pack = header.seqNum - lowSeqNum; // # of acked packets, max # of new packets added to the window

					// delete from window
					for (int i = 0; i < ack_pack; i++) {
						delete [] window[0];
						window.pop_front();
						wTime.pop_front();
					}

					old_size = window.size();
					setWindow(window, ack_pack, is, seqNum);
					add_size = window.size() - old_size;
					start = window.size() - add_size;

					sendWindow(window, wTime, sockfd, start, logfile);

					lowSeqNum = header.seqNum;
				}
			}
		}


		if (window.size() == 0) {
			// All data acked
			break;
		}

		assert(!wTime.empty());
		// check timeout
		current_time = std::chrono::high_resolution_clock::now();
		if (current_time - wTime[0] >= timeout) {
			wTime.clear(); // empty times before reset
			sendWindow(window, wTime, sockfd, 0, logfile); // send the whole window
		}
	}

	// send END
	sendConnection(sockfd, 1, logfile);

	freeaddrinfo(servinfo);

	return 0;
	
}