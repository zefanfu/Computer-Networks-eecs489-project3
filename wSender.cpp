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

// put at most size packets to the window
void setWindow(std::deque<char*>& window, int size, 
	std::string & file_str, int& file_idx, int& seqNum) {
	std::string data_str;
	// bool isEnd = false;

	for (int i = 0; i < size; i++) {
		// end of file
		if (file_idx >= file_str.size()) {
			return;
		}

		if (file_idx + CHUNCK_SIZE < file_str.size()) {
			data_str = file_str.substr(file_idx, CHUNCK_SIZE);
		} else {
			data_str = file_str.substr(file_idx);
			// isEnd = true; 
		}

		file_idx += data_str.size();

		 
		header.checksum = crc32(data_str.c_str(), data_str.size());

		char * buf = new char [PACKET_SIZE];
		memset(buf, '\0', PACKET_SIZE);
		header_to_char(&header, buf);
		// copy
		for (int j = 0; j < data_str.size(); j++) {
			buf[16 + j] = data_str[j];
		}

		window.push_back(buf);
	}
}

// start: starting point in window to send packets
void sendWindow(std::deque<char*>& window, std::deque<std::chrono::time_point<std::chrono::system_clock>>& wTime, 
	int sockfd, int start) {

	int numbytes;
	for (int i = start; i < window.size(); i++) {
		if ((numbytes = send(sockfd, window[i], PACKET_SIZE, 0) == -1)) {
			perror("send");
			exit(1);
		}
		wTime.push_back(std::chrono::high_resolution_clock::now());
	}
}

void sendConnection(int sockfd, int type) {
	PacketHeader header;
	header.type = type;
	header.seqNum = 1000; // random >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	header.length = 0;
	header.checksum = 0;

	char buf[PACKET_SIZE];
	memset(buf, '\0', PACKET_SIZE);
	header_to_char(&header, buf);

	if ((numbytes = send(sockfd, window[i], PACKET_SIZE, 0) == -1)) {
		perror("send");
		exit(1);
	}

	char rbuf[PACKET_SIZE];
	memset(rbuf, '\0', PACKET_SIZE);
	PacketHeader rheader;

	std::chrono::time_point<std::chrono::system_clock> start, current;
	start = std::chrono::high_resolution_clock::now();
	std::chrono::milliseconds msec(500);
	std::chrono::duration<double> timeout(msec);

	while (true) {
		numbytes = recv(sockfd, rbuf, PACKET_SIZE , 0);
		if (numbytes == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) { // if not timeout
				perror("recvfrom");
				exit(1);
			}
		} else {
			parse_header(&rheader, rbuf);
			if (rheader.type == 3 && rheader.seqNum == 1000) { // random >>>>>>>>>>>>>>>>>>>>
				break;
			}
		}

		// when timeout
		current = std::chrono::high_resolution_clock::now();
		if (current - start >= timeout) {
			if ((numbytes = send(sockfd, window[i], PACKET_SIZE, 0) == -1)) {
				perror("send");
				exit(1);
			}
			start = std::chrono::high_resolution_clock::now();
		}
	}


}

int main(int argc, char *argv[]) {
	// int wStart = 0;
	int wSize = atoi(argv[2]);
	int lowSeqNum = 0;
	int seqNum = 0; // next seqNum to be added
	int file_idx = 0;
	std::deque<std::chrono::time_point<std::chrono::system_clock>> wTime;
	std::deque<char*> window(wSize);

	std::ifstream is{argv[1], std::ios::binary | std::ios::ate};
	if (!is) {
		std::cout << "Could not open file\n";
		return 1;
	}
	auto size = is.tellg();
	// auto num_chunk  = size / CHUNCK_SIZE; // num of chunck - 1
	std::string file_str(size, '\0'); // construct string to stream size
	is.seekg(0);
	is.read(&file_str[0], size);


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
	sendConnection(sockfd, 0); // 0: start

	setWindow(window, wSize, file_str, file_idx, seqNum);

	sendWindow(window, wTime, sockfd, 0);

	// bool endFile = false;
	char buffer[PACKET_SIZE];
	memset(buffer, '\0', PACKET_SIZE);
	int ack_pack, old_size, add_size, start;
	std::chrono::time_point<std::chrono::system_clock> current_time;
	std::chrono::milliseconds msec(500);
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
			if (header.type == 3) {
				if (header.seqNum > lowSeqNum) {
					ack_pack = header.seqNum - lowSeqNum; // # of acked packets

					// delete from window
					for (int i = 0; i < ack_pack; i++) {
						delete [] window[0];
						window.pop_front();
						wTime.pop_front();
					}

					old_size = window.size();
					setWindow(window, ack_pack, file_str, file_idx, seqNum);
					add_size = window.size() - old_size;
					start = window.size() - add_size;

					sendWindow(window, wTime, sockfd, start);

					lowSeqNum = header.seqNum;
				}
			}
		}


		if (window.size() == 0) {
			break;
		}

		assert(!wTime.empty());
		// check timeout
		current_time = std::chrono::high_resolution_clock::now();
		if (current_time - wTime[0] >= timeout) {
			sendWindow(window, wTime, sockfd, 0); // send the whole window
		}
	}

	sendConnection(sockfd, 1);

	freeaddrinfo(servinfo);

	return 0;
	
}