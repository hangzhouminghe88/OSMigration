#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#define BYTES_1K   1024
#define BYTES_63K  64512
#define BYTES_64K  65536
#define LOG_FILE   "/netdd.txt"

static uint64_t sequNum = 1;
static uint64_t totalBytesWritten =0;     

void logMessage(const char *msg) {
    fprintf(stderr, "[progress] %s\n", msg);
}

int recvLength(int fd, int sock_conn, size_t RecvLen) {
	char sRecvBuffer[BYTES_64K] = { 0 };
	int RealRecvLen = 0, AlreadyReadLen = 0;

	while (1) {
		RealRecvLen = recv(sock_conn, sRecvBuffer + AlreadyReadLen, RecvLen, 0);
		if (RealRecvLen == RecvLen) {
			break;
		}
		else if (RealRecvLen == 0) {
			return 0;
		}
		else {
			RecvLen -= RealRecvLen;
			AlreadyReadLen += RealRecvLen;
		}
	}

	// 处理全\0数据，移动文件指针
	if (sequNum < atol(sRecvBuffer)) {
		lseek(fd, BYTES_63K * (atol(sRecvBuffer) - sequNum), SEEK_CUR);
	}
	sequNum = atol(sRecvBuffer) + 1;

	if (sRecvBuffer[BYTES_1K - 5] == '\0') { // Not the last block of data
		if (!write(fd, sRecvBuffer + BYTES_1K, BYTES_63K)) {
			logMessage("Failed to write data block");
			return 0;
		}
	}
	else { // Last block of data
		char lastlen[64] = { 0 };
		memcpy(lastlen, sRecvBuffer + BYTES_1K - 5, 5);
		if (!write(fd, sRecvBuffer + BYTES_1K, atol(lastlen))) {
			logMessage("Failed to write the last data block");
			return 0;
		}
		return 1;
	}
	
	// 每接收 100MB，打印一次进度
	if (totalBytesWritten / (100 * 1024 * 1024) > ((totalBytesWritten - BYTES_63K) / (100 * 1024 * 1024))) {
		char logbuf[128];
		snprintf(logbuf, sizeof(logbuf), "已接收数据总量：%.2f MB", totalBytesWritten / (1024.0 * 1024));
		logMessage(logbuf);
	}

	return 1;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <port> <device path>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// 显示磁盘设备路径
	printf("Disk device path: %s\n", argv[2]);
	printf("Do you want to zero the disk? (YES/yes to confirm): ");

	char confirm[5] = { 0 }; // 增加到5个字符
	scanf("%3s", confirm); // 读取最多3个字符
	if (strcmp(confirm, "YES") == 0 || strcmp(confirm, "yes") == 0) {
		printf("Zeroing the disk...\n");
		int disk_fd = open(argv[2], O_WRONLY);
		if (disk_fd == -1) {
			perror("Failed to open disk device");
			exit(EXIT_FAILURE);
		}
		char buffer[BYTES_64K] = { 0 }; // 64K zero-byte buffer
		while (write(disk_fd, buffer, BYTES_64K) == BYTES_64K);
		close(disk_fd);
		printf("Disk zeroing complete\n");
	}
	else {
		printf("Disk zeroing canceled\n");
	}

	logMessage("netdd receive service started.....");

	char cmd[64] = { 0 };
	snprintf(cmd, sizeof(cmd), "Port number: %s", argv[1]);
	logMessage(cmd);

	int fd = open(argv[2], O_RDWR);
	if (fd == -1) {
		snprintf(cmd, sizeof(cmd), "Failed to open file: %s", argv[2]);
		logMessage(cmd);
		exit(EXIT_FAILURE);
	}
	else {
		snprintf(cmd, sizeof(cmd), "Successfully opened file: %s", argv[2]);
		logMessage(cmd);
	}

	int sock_listen = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_listen == -1) {
		logMessage("Failed to create socket");
		close(fd);
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_port = htons(atoi(argv[1]));
	if (bind(sock_listen, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1) {
		logMessage("Failed to bind address");
		close(sock_listen);
		close(fd);
		exit(EXIT_FAILURE);
	}

	if (listen(sock_listen, 5) == -1) {
		logMessage("Failed to listen");
		close(sock_listen);
		close(fd);
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);
	int sock_conn = accept(sock_listen, (struct sockaddr *)&client_addr, &len);
	if (sock_conn == -1) {
		logMessage("Failed to accept client connection");
		close(sock_listen);
		close(fd);
		exit(EXIT_FAILURE);
	}

	logMessage("Started receiving data.....");

	sequNum = 1;
	while (recvLength(fd, sock_conn, BYTES_64K));

	logMessage("Data reception completed");

	close(sock_conn);
	close(sock_listen);
	close(fd);

	return 0;
}
