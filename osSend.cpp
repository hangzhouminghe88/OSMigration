/*
 * osSend.cpp - 网络磁盘镜像 发送工具
 * 功能：  
         从本地读取 img文件 ，然后发送到网络接收端 => netdd.c
	 目前只支持 WINDOWS客户端
 * 作者:  Jiang Hang
          Liang zhigang 
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
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define SEND_BUFFER_SIZE 65536     // 64K bytes
#define MAX_IP_ADDRESS_LENGTH 16
#define MAX_DISK_NAME_LENGTH  128
#define MAX_IMG_PATH_LENGTH   512
#define MAX_RETRIES 5
#define RETRY_DELAY 6000           // 6 seconds

void DisplayLocalTime() {
	time_t  local_time;
	struct tm *local_tm;
	char local_str[100];
	time(&local_time);
	local_tm = localtime(&local_time);
	strftime(local_str, sizeof(local_str), "%Y-%m-%d %H:%M:%S", local_tm);
	printf("current time: <%s>\n", local_str);
}

int main(int argc, char *argv[]) {
	clock_t start_time, end_time;
	start_time = clock();
	DisplayLocalTime();

	char localImgPath[MAX_IMG_PATH_LENGTH];
	printf("Please enter the local img path:");
	scanf("%511s", localImgPath);

	char ipAddress[MAX_IP_ADDRESS_LENGTH];
	unsigned port = 0;
	printf("Please enter the server IP address:");
	scanf("%15s", ipAddress);
	printf("Please enter the server port:");
	scanf("%u", &port);

	printf("<%s----%s:%u>\n\r", localImgPath, ipAddress, port);

	HANDLE localImgHandle = CreateFile(localImgPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (localImgHandle == INVALID_HANDLE_VALUE) {
		printf("Open img file Error:%d\n", GetLastError());
		return -1;
	}

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(localImgHandle, &fileSize)) {
		printf("Failed to get img file size.\n");
		CloseHandle(localImgHandle);
		return -1;
	}

	long long diskSizeKB = fileSize.QuadPart / 1024;
	unsigned int totalBlocks = (unsigned int)(diskSizeKB / 64);

	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
		printf("Init Windows Socket Failed! Error: %d\n", WSAGetLastError());
		CloseHandle(localImgHandle);
		return -1;
	}

	SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientSocket == INVALID_SOCKET) {
		printf("Create Socket Failed! Error: %d\n", WSAGetLastError());
		CloseHandle(localImgHandle);
		WSACleanup();
		return -1;
	}

	struct sockaddr_in clientAddr;
	memset(&clientAddr, 0, sizeof(clientAddr));
	clientAddr.sin_family = AF_INET;
	clientAddr.sin_addr.s_addr = inet_addr(ipAddress);
	clientAddr.sin_port = htons(port);

	bool isConnected = false;
	for (int retries = 0; retries < MAX_RETRIES; retries++) {
		if (connect(clientSocket, (struct sockaddr *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR) {
			printf("Socket Connect Failed! Error:%d\n", WSAGetLastError());
			if (retries < MAX_RETRIES - 1) {
				printf("Retrying in %d seconds...\n", RETRY_DELAY / 1000);
				Sleep(RETRY_DELAY);
			}
		}
		else {
			printf("Socket Connect Succeed!\n");
			isConnected = true;
			break;
		}
	}

	if (!isConnected) {
		printf("Unable to connect to server.\n");
		closesocket(clientSocket);
		CloseHandle(localImgHandle);
		WSACleanup();
		return -1;
	}

	char sendBuffer[SEND_BUFFER_SIZE] = { 0 };
	unsigned int sentBlocks = 0;
	DWORD bytesRead = 0;
	int sendResult;

	while (ReadFile(localImgHandle, sendBuffer, SEND_BUFFER_SIZE, &bytesRead, NULL) && bytesRead > 0) {
		sendResult = send(clientSocket, sendBuffer, bytesRead, 0);
		if (sendResult == SOCKET_ERROR || sendResult != bytesRead) {
			printf("Send Information Failed! Error:%d\n", WSAGetLastError());
			break;
		}
		sentBlocks++;
		if (sentBlocks % 3000 == 0) {
			printf("progress------------------%.2f%%\n", ((float)sentBlocks / totalBlocks) * 100);
		}
		memset(sendBuffer, 0, sizeof(sendBuffer));
	}

	printf("Complete:  sentBlocks=%u\n", sentBlocks);

	DisplayLocalTime();

	end_time = clock();
	double cpu_time_used = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
	printf("cpu time used<%f>second\n", cpu_time_used);

	if (localImgHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(localImgHandle);
	}
	if (clientSocket != INVALID_SOCKET) {
		closesocket(clientSocket);
	}
	WSACleanup();

	system("pause");
	return 1;
}
