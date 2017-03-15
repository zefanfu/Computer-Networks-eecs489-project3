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
#include <deque>

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

void parse_data(PacketHeader * header, char * buf) {
	int i = 0;
	header->type = char_to_int(buf, i);
	header->seqNum = char_to_int(buf, i);
	header->length = char_to_int(buf, i);
	header->checksum = char_to_int(buf, i);
}

void setWindow(std::deque<char*>& window, int size, 
	std::string & file_str, int& file_idx, int& seqNum) {
	string data_str;
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

		PacketHeader header;
		header.type = 2; // Data
		header.seqNum = seqNum++;
		header.length = data_str.size();
		header.checksum = crc32(data_str.c_str(), data_str.size());

		
		// delete as soon as recv
			// delete [] window[0];
			// window.pop_front();


		char * buf = new char [PACKET_SIZE];
		memset(buf, '/0', PACKET_SIZE);
		header_to_char(&header, buf);
		// copy
		for (int j = 0; j < data_str.size(); j++) {
			buf[12 + j] = data_str[j];
		}

		window.push_back(buf);
	}
}

// start: starting point in window to send packets
void sendWindow(std::deque<char*>& window, 
	std::deque<std::chrono::time_point<std::chrono::system_clock>>& wTime, int sockfd
	int start) { 
	for (int i = start; i < window.size(); i++) {
		if ((numbytes = send(sockfd, window[i], PACKET_SIZE, 0) == -1)) {
			perror("send");
			exit(1);
		}
		wTime.push_back(now());
	}
}


int main(int argc, char *argv[]) {
	// int wStart = 0;
	int wSize = atoi(argv[2]);
	// int lowSeqNum = 0;
	int seqNum = 0; // next seqNum to be added
	int file_idx = 0;
	std::deque<std::chrono::time_point<std::chrono::system_clock>> wTime;
	std::deque<char*> window(wSize);
	// for (int i = 0; i < wSize; i++) {
	// 	window[i] = new char [PACKET_SIZE];
	// }
	// int time_idx = 0; // points to time of start of window

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
	// char buffer[PACKET_SIZE];
	// memset(buffer, '\0', PACKET_SIZE);
	// strcpy(buffer, "abc");

	// if ((numbytes = send(sockfd, buffer, PACKET_SIZE, 0) == -1)) {
	//     perror("sendto");
	//     exit(1);
	// }

	setWindow(window, wStart, wSize, file_str, file_idx, seqNum, wSize);

	sendWindow(window, wTime, sockfd, 0, wSize);

	bool endFile = false;
	char buffer[PACKET_SIZE];
	memset(buffer, '\0', PACKET_SIZE);

	while (true) {
		numbytes = recv(sockfd, buffer, PACKET_SIZE , 0);
		if (numbytes == -1) {
			// perror("recvfrom");
			// exit(1);
		} else {
			PacketHeader header;
			parse_data(header, buffer);
			if ()
		}
		

	}

	freeaddrinfo(servinfo);

	for (int i = 0; i < window.size(); i++) {
		delete [] window[i];
	}
	return 0;
	
}