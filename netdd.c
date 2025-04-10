/*
 * netdd.c - 网络磁盘镜像接收工具 ( SOCKET 服务端）
 * 功能：
          接收网络上传来的磁盘镜像数据，然后写入 LINUX盘中
	  这个代码在 RESCUE CD 9.0 和 ZSTACK 4.0 测试通过
          基于 RESCUE CD建立一个虚拟机，主盘大小要超过 发送盘的大小
	  然后接收数据，直接写入到主盘，重新启动就是一台WINDOWS机器（跟源主机一样）
          碰到一个 VIRTO问题
          ./netdd 12332 /dev/vda  
 * 作者: Jiang  Hang
         Liang  zhigang 梁芝纲 
 * 日期: 2025-03-26
 *
 * 本程序是自由软件；你可以遵照 GNU 通用公共许可证（GPL v3）来修改和重新发布它。
 * 本程序是基于“原样”提供的，没有任何担保，包括适销性和适用于特定目的的隐含担保。
 *
 * 你应该已经收到了一份 GNU 通用公共许可证的副本。
 * 如果没有，请查看：https://www.gnu.org/licenses/gpl-3.0.html
 */
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

static uint64_t sequNum = 1;
static uint64_t totalBytesWritten =0;

void logMessage(const char *msg) {
    fprintf(stderr, "[LOG] %s\n", msg);
}

int recvLength(int fd, int sock_conn, size_t RecvLen) {
	char sRecvBuffer[BYTES_64K] = { 0 };
	int AlreadyReadLen = 0;
	ssize_t RealRecvLen;

	// 循环接收直到收满 RecvLen 字节
	while (AlreadyReadLen < RecvLen) {
		RealRecvLen = recv(sock_conn, sRecvBuffer + AlreadyReadLen, RecvLen - AlreadyReadLen, 0);
		if (RealRecvLen <= 0) return 0;
		AlreadyReadLen += RealRecvLen;
	}

	// 提取当前块号
	long currentSeq = atol(sRecvBuffer);
	if (sequNum < currentSeq) {
		lseek(fd, BYTES_63K * (currentSeq - sequNum), SEEK_CUR);
	}
	sequNum = currentSeq + 1;

	// 判断是否是最后一块
	if (sRecvBuffer[BYTES_1K - 5] == '\0') {
		ssize_t wlen = write(fd, sRecvBuffer + BYTES_1K, BYTES_63K);
		if (wlen != BYTES_63K) {
			logMessage("Failed to write data block");
			return 0;
		}
		totalBytesWritten += wlen;
	} else {
		char lastlen[6] = {0};
		memcpy(lastlen, sRecvBuffer + BYTES_1K - 5, 5);
		size_t lastSize = atol(lastlen);

		ssize_t wlen = write(fd, sRecvBuffer + BYTES_1K, lastSize);
		if (wlen != lastSize) {
			logMessage("Failed to write last data block");
			return 0;
		}
		totalBytesWritten += wlen;
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
	fprintf(stderr,"Disk device path: %s\n", argv[2]);
	fprintf(stderr,"Do you want to zero the disk? (YES/yes to confirm,else skip): ");

	char confirm[5] = { 0 };  // 增加到5个字符
	scanf("%3s", confirm);    // 读取最多3个字符
	if (strcmp(confirm, "YES") == 0 || strcmp(confirm, "yes") == 0) {
		fprintf(stderr,"Zeroing the disk...\n");
		int disk_fd = open(argv[2], O_WRONLY);
		if (disk_fd == -1) {
			perror("Failed to open disk device");
			exit(EXIT_FAILURE);
		}
		char buffer[BYTES_64K] = { 0 }; // 64K zero-byte buffer
		while (write(disk_fd, buffer, BYTES_64K) == BYTES_64K);
		close(disk_fd);
		fprintf(stderr,"Disk zeroing complete\n");
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
