/*
 * osbackup.cpp - 网络磁盘镜像发送工具
 * 功能：  
          目前只支持 WINDOWS客户端
	  从本地 读取一个WINDOWS硬盘（比如 物理硬盘 PHYSICALDISK0 , C盘，D盘，具体要从WINDOWS 去查硬盘设备名，斜杠自动加）
          然后，提供选择，
	（1） 直接发送到网络 netdd.c (  类似 osSend.cpp 功能）
        （2） 如果万一不允许发送网络（比如工业内网，物理隔离,只能保存到U盘），那就保存成 特定格式的IMG文件，然后拔出U盘，放到其他机器，再用 OSSEND发送
	（3） 一定要用 超级管理员启动，否则无法读取物理硬盘，权限不够；
 * 作者:   Jiang Hang
           Liang zhigang  梁芝纲 
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

#define BUFFER_SIZE            64512           // 缓冲区大小
#define SEND_BUFFER_SIZE       65536           // 发送缓冲区大小
#define MAX_IP_ADDRESS_LENGTH  16              // IP地址最大长度
#define MAX_DISK_NAME_LENGTH   128             // 磁盘名称最大长度
#define MAX_IMG_PATH_LENGTH    512             // 图像路径最大长度
#define MAX_RETRIES            5               // 最大重试次数
#define RETRY_DELAY            6000            // 重试延迟时间（毫秒）
#define BLOCK_INDEX_OFFSET     1024            // 块索引偏移量
#define BYTES_1K               1024     
#define DISK_BLOCK_SIZE_KB     63              // 磁盘块大小（KB）

// 检查缓冲区是否全为零，全0 不发送
bool checkZero(const char *buffer, int length) {
	for (int i = 0; i < length; i++) {
		if (buffer[i] != '\0')
			return false;
	}
	return true;
}

// 显示当前时间
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

	bool isNet = false;
	SOCKET clientSocket = INVALID_SOCKET;
	FILE* imgfp = NULL;     // 本地镜像文件

	char diskName[MAX_DISK_NAME_LENGTH];
	char tempDiskName[MAX_DISK_NAME_LENGTH];
	printf("Please enter the disk name to transfer: "); // 准备备份哪个硬盘??
	scanf("%127s", tempDiskName);
	sprintf(diskName, "\\\\.\\%s", tempDiskName);
        //  打开硬盘
	HANDLE diskHandle = CreateFile(diskName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (diskHandle == INVALID_HANDLE_VALUE) {
		printf("Open disk Error: %d\n", GetLastError());
		return -1;
	}
	
	GET_LENGTH_INFORMATION diskLengthInfo;
	DWORD bytesReturned;

	// 使用 DeviceIoControl 获取磁盘长度 ，我的MACBOOK AIR A1466 有时候取到0 ?? 有待改进？ 
	BOOL result = DeviceIoControl(
		diskHandle,                     // 磁盘句柄
		IOCTL_DISK_GET_LENGTH_INFO,     // 控制码，用于获取磁盘长度
		NULL,                           // 输入缓冲区（不需要）
		0,                              // 输入缓冲区大小（不需要）
		&diskLengthInfo,                // 输出缓冲区，用于接收磁盘长度信息
		sizeof(diskLengthInfo),         // 输出缓冲区大小
		&bytesReturned,                 // 实际返回的字节数
		NULL                            // OVERLAPPED 结构（不需要，因为这里使用的是同步操作）
		);
	if (!result) {
		printf("IOCTL_DISK_GET_LENGTH_INFO Error: %d\n", GetLastError());
		CloseHandle(diskHandle);
		return -1;
	}

	long long diskSizeKB = diskLengthInfo.Length.QuadPart / BYTES_1K; // 将磁盘长度从字节转换为千字节
	unsigned int totalBlocks = (unsigned int)(diskSizeKB / DISK_BLOCK_SIZE_KB); // 假设每块大小为63KB

	printf("Send to net or Write local img, Please enter YES/yes send to net: ");
	char confirm[5];
	scanf("%4s", confirm);
	// 输入 YES  ，发送网络==》netdd.c 
	if (strcmp(confirm, "YES") == 0 || strcmp(confirm, "yes") == 0) {
		isNet = true;

		char ipAddress[MAX_IP_ADDRESS_LENGTH];
		unsigned port = 0;
		printf("Please enter the server IP address: ");
		scanf("%15s", ipAddress);
		printf("Please enter the server port: ");
		scanf("%u", &port);

		WSADATA wsadata;
		if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
			printf("Init Windows Socket Failed! Error: %d\n", WSAGetLastError());
			CloseHandle(diskHandle);
			return -1;
		}

		clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (clientSocket == INVALID_SOCKET) {
			printf("Create Socket Failed! Error: %d\n", WSAGetLastError());
			CloseHandle(diskHandle);
			WSACleanup();
			return -1;
		}

		struct sockaddr_in clientAddr;
		memset(&clientAddr, 0, sizeof(clientAddr));
		clientAddr.sin_family = AF_INET;
		clientAddr.sin_addr.s_addr = inet_addr(ipAddress);
		clientAddr.sin_port = htons(port);

		int connectResult = SOCKET_ERROR;
		for (int retries = 0; retries < MAX_RETRIES && connectResult == SOCKET_ERROR; retries++) {
			connectResult = connect(clientSocket, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
			if (connectResult == SOCKET_ERROR) {
				printf("Socket Connect Failed! Error:%d\n", WSAGetLastError());
				if (retries < MAX_RETRIES - 1) {
					printf("Retrying in %d seconds...\n", RETRY_DELAY / 1000);
					Sleep(RETRY_DELAY);
				}
			}
		}

		if (connectResult == SOCKET_ERROR) {
			printf("Unable to connect to server.\n");
			closesocket(clientSocket);
			CloseHandle(diskHandle);
			WSACleanup();
			return -1;
		}
	}
	else {          // 写本地 IMG文件，准备
		char localImgPath[MAX_IMG_PATH_LENGTH];
		printf("Please enter the local img path: ");
		scanf("%511s", localImgPath);
		imgfp = fopen(localImgPath, "wb");
		if (imgfp == NULL) {
			printf("Open or Create img file error:%d", GetLastError());
			CloseHandle(diskHandle);
			return -1;
		}
	}

	char buffer[BUFFER_SIZE] = { 0 };
	char sendBuffer[SEND_BUFFER_SIZE] = { 0 };
	unsigned int blockIndex = 0, zeroBlocks = 0, sentBlocks = 0;
	DWORD bytesRead = 0;
	bool IsLastBlock = false;

	while (ReadFile(diskHandle, buffer, BUFFER_SIZE, &bytesRead, NULL) && bytesRead > 0) {
		blockIndex++;
		IsLastBlock = (bytesRead < BUFFER_SIZE);
		// 准备发送缓冲区
		sprintf(sendBuffer, "%u", blockIndex);// 将块索引写入发送缓冲区的开头
		memcpy(sendBuffer + BLOCK_INDEX_OFFSET, buffer, bytesRead);// 将读取的数据复制到发送缓冲区中
		if (IsLastBlock) { //最后一块
			char last_real_len[24] = { 0 };
			sprintf(last_real_len, "%lu", bytesRead);
			memcpy(sendBuffer + BLOCK_INDEX_OFFSET - 5, last_real_len, 5); // 最后一块 真实长度
			if (isNet) {
				if (send(clientSocket, sendBuffer, SEND_BUFFER_SIZE, 0) == SOCKET_ERROR) {
					printf("Send Information Failed! Error:%d\n", WSAGetLastError());
					break;
				}
			}
			else {
				if (fwrite(sendBuffer, SEND_BUFFER_SIZE, 1, imgfp) != 1) {
					printf("Write img file Error:%d\n", GetLastError());
					break;
				}
			}
			sentBlocks++;
		}
		else { // 不是最后一块
			if (!checkZero(buffer, bytesRead)) {
				if (isNet) {
					if (send(clientSocket, sendBuffer, SEND_BUFFER_SIZE, 0) == SOCKET_ERROR) {
						printf("Send Information Failed! Error:%d\n", WSAGetLastError());
						break;
					}
				}
				else {
					if (fwrite(sendBuffer, SEND_BUFFER_SIZE, 1, imgfp) != 1) {
						printf("Write img file Error:%d\n", GetLastError());
						break;
					}
				}
				sentBlocks++;
			}
			else {
				zeroBlocks++;// 如果读取的是完整的零块，则增加零块计数
			}
		}
		// 打印进度信息
		if (blockIndex % 5000 == 0) {
			printf("current totalBlocks=%u, zeroBlocks=%u, sentBlocks=%u, progress=%.2f%%\n", blockIndex, zeroBlocks, sentBlocks, ((float)blockIndex / totalBlocks) * 100);
		}
		memset(buffer, 0, sizeof(buffer)); // 清除缓冲区以备下次读取
	}

	printf("Complete: totalBlocks=%u, zeroBlocks=%u, sentBlocks=%u\n", blockIndex, zeroBlocks, sentBlocks);
	DisplayLocalTime();
	end_time = clock();
	double cpu_time_used = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
	printf("cpu time used<%f>second\n", cpu_time_used);

	if (diskHandle != INVALID_HANDLE_VALUE) {
		CloseHandle(diskHandle);
	}
	if (isNet && clientSocket != INVALID_SOCKET) {
		closesocket(clientSocket);
		WSACleanup();
	}
	if (imgfp != NULL) {
		fclose(imgfp);
	}

	system("pause"); 
	return 1;
}
