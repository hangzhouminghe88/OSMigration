#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define BUFFER_SIZE 64512           // ��������С
#define SEND_BUFFER_SIZE 65536      // ���ͻ�������С
#define MAX_IP_ADDRESS_LENGTH 16    // IP��ַ��󳤶�
#define MAX_DISK_NAME_LENGTH  128   // ����������󳤶�
#define MAX_IMG_PATH_LENGTH   512   // ͼ��·����󳤶�
#define MAX_RETRIES 5               // ������Դ���
#define RETRY_DELAY 6000            // �����ӳ�ʱ�䣨���룩
#define BLOCK_INDEX_OFFSET 1024     // ������ƫ����
#define BYTES_1K           1024     
#define DISK_BLOCK_SIZE_KB 63       // ���̿��С��KB��

// ��黺�����Ƿ�ȫΪ��
bool checkZero(const char *buffer, int length) {
	for (int i = 0; i < length; i++) {
		if (buffer[i] != '\0')
			return false;
	}
	return true;
}

// ��ʾ��ǰʱ��
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
	FILE* imgfp = NULL;     // ���ؾ����ļ�

	char diskName[MAX_DISK_NAME_LENGTH];
	char tempDiskName[MAX_DISK_NAME_LENGTH];
	printf("Please enter the disk name to transfer: ");
	scanf("%127s", tempDiskName);
	sprintf(diskName, "\\\\.\\%s", tempDiskName);

	HANDLE diskHandle = CreateFile(diskName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (diskHandle == INVALID_HANDLE_VALUE) {
		printf("Open disk Error: %d\n", GetLastError());
		return -1;
	}

	GET_LENGTH_INFORMATION diskLengthInfo;
	DWORD bytesReturned;

	// ʹ�� DeviceIoControl ��ȡ���̳���
	BOOL result = DeviceIoControl(
		diskHandle,                     // ���̾��
		IOCTL_DISK_GET_LENGTH_INFO,     // �����룬���ڻ�ȡ���̳���
		NULL,                           // ���뻺����������Ҫ��
		0,                              // ���뻺������С������Ҫ��
		&diskLengthInfo,                // ��������������ڽ��մ��̳�����Ϣ
		sizeof(diskLengthInfo),         // �����������С
		&bytesReturned,                 // ʵ�ʷ��ص��ֽ���
		NULL                            // OVERLAPPED �ṹ������Ҫ����Ϊ����ʹ�õ���ͬ��������
		);
	if (!result) {
		printf("IOCTL_DISK_GET_LENGTH_INFO Error: %d\n", GetLastError());
		CloseHandle(diskHandle);
		return -1;
	}

	long long diskSizeKB = diskLengthInfo.Length.QuadPart / BYTES_1K; // �����̳��ȴ��ֽ�ת��Ϊǧ�ֽ�
	unsigned int totalBlocks = (unsigned int)(diskSizeKB / DISK_BLOCK_SIZE_KB); // ����ÿ���СΪ63KB

	printf("Send to net or Write local img, Please enter YES/yes send to net: ");
	char confirm[5];
	scanf("%4s", confirm);
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
	else {
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
		// ׼�����ͻ�����
		sprintf(sendBuffer, "%u", blockIndex);// ��������д�뷢�ͻ������Ŀ�ͷ
		memcpy(sendBuffer + BLOCK_INDEX_OFFSET, buffer, bytesRead);// ����ȡ�����ݸ��Ƶ����ͻ�������
		if (IsLastBlock) { //���һ��
			char last_real_len[24] = { 0 };
			sprintf(last_real_len, "%lu", bytesRead);
			memcpy(sendBuffer + BLOCK_INDEX_OFFSET - 5, last_real_len, 5); // ���һ�� ��ʵ����
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
		else { // �������һ��
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
				zeroBlocks++;// �����ȡ������������飬������������
			}
		}
		// ��ӡ������Ϣ
		if (blockIndex % 5000 == 0) {
			printf("totalBlocks=%u, zeroBlocks=%u, sentBlocks=%u, progress=%.2f%%\n", blockIndex, zeroBlocks, sentBlocks, ((float)blockIndex / totalBlocks) * 100);
		}

		memset(buffer, 0, sizeof(buffer)); // ����������Ա��´ζ�ȡ
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
