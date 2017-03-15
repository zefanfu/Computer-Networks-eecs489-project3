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

bool setWindow(std::vector<char*>& window, int& wStart, int size, 
	std::string & file_str, int& file_idx, int& seqNum, int wSize) {
	string data_str;
	bool isEnd = false;

	for (int i = 0; i < size; i++) {
		if (file_idx + CHUNCK_SIZE < file_str.size()) {
			data_str = file_str.substr(file_idx, CHUNCK_SIZE);
		} else {
			data_str = file_str.substr(file_idx);
			isEnd = true; 
		}

		file_idx += CHUNCK_SIZE;

		PacketHeader header;
		header.type = 2; // Data
		header.seqNum = seqNum++;
		header.length = data_str.size();
		header.checksum = crc32(data_str.c_str(), data_str.size());

		memset(window[wStart], '/0', PACKET_SIZE);
		header_to_char(&header, window[wStart]);
		// copy
		for (int j = 0; j < CHUNCK_SIZE; j++) {
			window[wStart][12 + j] = data_str[j];
		}

		wStart = (wStart + 1) % wSize;
	}
	return isEnd;
}

void sendWindow(std::vector<char*>& window, 
	std::vector<std::chrono::time_point<std::chrono::system_clock>>& wTime, int sockfd
	int wStart, int num_pack) { 
	for (unsigned i= 0; i < window.size(); i++) {

		if ((numbytes = send(sockfd, buffer, PACKET_SIZE, 0) == -1)) {
			perror("send");
			exit(1);
		}
	}
}
int main(int argc, char *argv[]) {
	int wStart = 0;
	int wSize = atoi(argv[2]);
	// int lowSeqNum = 0;
	int seqNum = 0; // next seqNum to be added
	int file_idx = 0;
	std::vector<std::chrono::time_point<std::chrono::system_clock>> wTime;
	std::vector<char*> window(wSize);
	for (int i = 0; i < wSize; i++) {
		window[i] = new char [PACKET_SIZE];
	}
	int time_idx = 0; // points to time of start of window

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
	// std::cout << file_str << "\n";
	// char buf[CHUNCK_SIZE + 1];
	// for (int i = 0; i <= num_chunk; i++) {
	// 	memset(buf, 0, CHUNCK_SIZE + 1);
	// 	is.read(buf, CHUNCK_SIZE);
	// 	is.
	// }

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

	// if ((numbytes = send(sockfd, buffer, PACKET_SIZE, 0) == -1)) {
	//     perror("sendto");
	//     exit(1);
	// }

	setWindow(window, wStart, wSize, file_str, file_idx, seqNum, wSize);

	sendWindow(window, wTime, sockfd, 0, wSize);

	while (true) {

	}

	freeaddrinfo(servinfo);

	for (int i = 0; i < wSize; i++) {
		delete [] window[i];
	}
	return 0;
	
}