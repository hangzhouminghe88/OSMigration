// netdd_send.cpp （跨平台版）
// 支持 Windows 和 Linux，自动跳过零块，支持网络发送或本地写入镜像
// 作者：Liang Zhi Gang，重构版本，已函数化 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define CLOSESOCKET closesocket
#define SOCKET_TYPE SOCKET
#define OPEN_DISK(path) CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL)
#define READ_DISK(handle, buffer, size, readSize) ReadFile(handle, buffer, size, readSize, NULL)
#define CLOSE_DISK CloseHandle
#define GET_ERROR GetLastError
#else
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define CLOSESOCKET close
#define SOCKET_TYPE int
#define OPEN_DISK(path) open(path, O_RDONLY)
#define READ_DISK(handle, buffer, size, readSize) ({ ssize_t r = read(handle, buffer, size); *readSize = r > 0 ? r : 0; r > 0; })
#define CLOSE_DISK close
#define GET_ERROR() errno
#endif

#define BUFFER_SIZE 64512
#define SEND_BUFFER_SIZE 65536
#define BLOCK_INDEX_OFFSET 1024
#define MAX_RETRIES 5
#define RETRY_DELAY 6
#define BYTES_1K 1024
#define BLOCK_SIZE_KB 63

bool checkZero(const char *buffer, int length) {
    for (int i = 0; i < length; i++) {
        if (buffer[i] != '\0') return false;
    }
    return true;
}

void displayLocalTime() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("当前时间：%s\n", buf);
}

SOCKET_TYPE connectServer(const char *ip, unsigned port) {
    SOCKET_TYPE sock;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        perror("创建 socket 失败");
        return INVALID_SOCKET;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    for (int i = 0; i < MAX_RETRIES; ++i) {
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return sock;
        }
        printf("连接失败，%d 秒后重试...\n", RETRY_DELAY);
#ifdef _WIN32
        Sleep(RETRY_DELAY * 1000);
#else
        sleep(RETRY_DELAY);
#endif
    }
    perror("连接服务器失败");
    CLOSESOCKET(sock);
    return INVALID_SOCKET;
}

long long getDiskSize(const char *diskPath) {
#ifdef _WIN32
    HANDLE h = OPEN_DISK(diskPath);
    if (h == INVALID_HANDLE_VALUE) return -1;
    GET_LENGTH_INFORMATION lenInfo;
    DWORD ret;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lenInfo, sizeof(lenInfo), &ret, NULL)) {
        CLOSE_DISK(h);
        return -1;
    }
    CLOSE_DISK(h);
    return lenInfo.Length.QuadPart;
#else
    struct stat st;
    if (stat(diskPath, &st) < 0) return -1;
    return st.st_size;
#endif
}

int main(int argc, char *argv[]) {
    char diskPath[256], ip[64], imgPath[512];
    unsigned port = 0;
    bool toNet = false;
    displayLocalTime();

    printf("请输入要读取的磁盘名（如 PhysicalDrive0 或 /dev/sda）：");
    scanf("%255s", diskPath);
#ifdef _WIN32
    char fullDiskPath[280];
    sprintf(fullDiskPath, "\\\\.\\%s", diskPath);
#else
    char *fullDiskPath = diskPath;
#endif

    long long diskSize = getDiskSize(fullDiskPath);
    if (diskSize <= 0) {
        printf("无法获取磁盘大小\n");
        return -1;
    }
    unsigned totalBlocks = (unsigned)(diskSize / (BLOCK_SIZE_KB * BYTES_1K));

    printf("网络发送？输入 yes/YES 为是：");
    char choice[8];
    scanf("%7s", choice);
    if (strcmp(choice, "yes") == 0 || strcmp(choice, "YES") == 0) {
        toNet = true;
        printf("请输入服务器 IP：");
        scanf("%63s", ip);
        printf("请输入服务器端口：");
        scanf("%u", &port);
    } else {
        printf("请输入本地镜像保存路径：");
        scanf("%511s", imgPath);
    }

    FILE *imgfp = NULL;
    if (!toNet) {
        imgfp = fopen(imgPath, "wb");
        if (!imgfp) {
            perror("无法创建镜像文件");
            return -1;
        }
    }

    SOCKET_TYPE sock = INVALID_SOCKET;
    if (toNet) {
        sock = connectServer(ip, port);
        if (sock == INVALID_SOCKET) return -1;
    }

    int diskHandle = OPEN_DISK(fullDiskPath);
    if (diskHandle < 0) {
        perror("打开磁盘失败");
        return -1;
    }

    char buffer[BUFFER_SIZE], sendBuffer[SEND_BUFFER_SIZE];
    unsigned blockIndex = 0, zeroBlocks = 0, sentBlocks = 0;
    size_t bytesRead = 0;

    while (READ_DISK(diskHandle, buffer, BUFFER_SIZE, &bytesRead) && bytesRead > 0) {
        ++blockIndex;
        bool isLast = (bytesRead < BUFFER_SIZE);
        sprintf(sendBuffer, "%u", blockIndex);
        memcpy(sendBuffer + BLOCK_INDEX_OFFSET, buffer, bytesRead);

        if (isLast) {
            char lastlen[8] = {0};
            sprintf(lastlen, "%lu", bytesRead);
            memcpy(sendBuffer + BLOCK_INDEX_OFFSET - 5, lastlen, 5);
        }

        if (!checkZero(buffer, bytesRead) || isLast) {
            if (toNet) {
                if (send(sock, sendBuffer, SEND_BUFFER_SIZE, 0) <= 0) break;
            } else {
                if (fwrite(sendBuffer, SEND_BUFFER_SIZE, 1, imgfp) != 1) break;
            }
            ++sentBlocks;
        } else {
            ++zeroBlocks;
        }

        if (blockIndex % 5000 == 0) {
            printf("进度：%.2f%%，总块：%u，零块：%u，已发送：%u\n", (blockIndex * 100.0) / totalBlocks, blockIndex, zeroBlocks, sentBlocks);
        }
    }

    printf("完成：总块 %u，零块 %u，发送 %u\n", blockIndex, zeroBlocks, sentBlocks);
    displayLocalTime();

    if (toNet) {
        CLOSESOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
    }
    if (!toNet && imgfp) fclose(imgfp);
    CLOSE_DISK(diskHandle);
    return 0;
}
