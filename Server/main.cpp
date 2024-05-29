#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

constexpr int PORT = 9000;
constexpr int BUFFER_SIZE = 512;
constexpr int MAX_WORKER_THREADS = 4;

struct OverlappedEx {
    OVERLAPPED overlapped;
    WSABUF wsabuf;
    char buffer[BUFFER_SIZE];
    SOCKET socket;
    DWORD operation;
};

enum Operation {
    OP_ACCEPT = 1,
    OP_READ,
    OP_WRITE
};

std::atomic<bool> running(true);
HANDLE completionPort = nullptr;
std::vector<std::thread> workerThreads;

void WorkerThread() {
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    OverlappedEx* overlappedEx;

    while (running) {
        BOOL success = GetQueuedCompletionStatus(
            completionPort, &bytesTransferred, &completionKey,
            reinterpret_cast<LPOVERLAPPED*>(&overlappedEx), INFINITE);

        if (!success) {
            if (overlappedEx == nullptr) {
                break;
            }
            std::cerr << "GetQueuedCompletionStatus failed with error: " << GetLastError() << std::endl;
            closesocket(overlappedEx->socket);
            delete overlappedEx;
            continue;
        }

        SOCKET clientSocket = overlappedEx->socket;

        switch (overlappedEx->operation) {
        case OP_READ: {
            if (bytesTransferred == 0) {
                std::cout << "Client disconnected" << std::endl;
                closesocket(clientSocket);
                delete overlappedEx;
                break;
            }

            std::cout << "Received: " << overlappedEx->buffer << std::endl;

            overlappedEx->wsabuf.len = bytesTransferred;
            overlappedEx->operation = OP_WRITE;

            DWORD sendBytes;
            WSASend(clientSocket, &overlappedEx->wsabuf, 1, &sendBytes, 0, &overlappedEx->overlapped, nullptr);
            break;
        }
        case OP_WRITE: {
            std::cout << "Sent data" << std::endl;

            overlappedEx->wsabuf.len = BUFFER_SIZE;
            overlappedEx->operation = OP_READ;

            DWORD flags = 0;
            WSARecv(clientSocket, &overlappedEx->wsabuf, 1, nullptr, &flags, &overlappedEx->overlapped, nullptr);
            break;
        }
        default:
            break;
        }
    }
}

void InitializeWorkerThreads() {
    for (int i = 0; i < MAX_WORKER_THREADS; ++i) {
        workerThreads.emplace_back(WorkerThread);
    }
}

void AcceptConnections(SOCKET listenSocket) {
    while (running) {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "accept failed with error: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "New client connected" << std::endl;

        OverlappedEx* overlappedEx = new OverlappedEx;
        memset(&overlappedEx->overlapped, 0, sizeof(OVERLAPPED));
        overlappedEx->wsabuf.buf = overlappedEx->buffer;
        overlappedEx->wsabuf.len = BUFFER_SIZE;
        overlappedEx->buffer[0] = '\0';
        overlappedEx->socket = clientSocket;
        overlappedEx->operation = OP_READ;

        // 소켓을 IOCP에 연결
        CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), completionPort, static_cast<ULONG_PTR>(clientSocket), 0);

        DWORD flags = 0;
        DWORD recvBytes;
        int result = WSARecv(clientSocket, &overlappedEx->wsabuf, 1, &recvBytes, &flags, &overlappedEx->overlapped, nullptr);
        if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            std::cerr << "WSARecv failed with error: " << WSAGetLastError() << std::endl;
            closesocket(clientSocket);
            delete overlappedEx;
        }
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (completionPort == nullptr) {
        std::cerr << "CreateIoCompletionPort failed with error: " << GetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    InitializeWorkerThreads();

    std::thread acceptThread(AcceptConnections, listenSocket);

    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();

    running = false;

    for (auto& thread : workerThreads) {
        PostQueuedCompletionStatus(completionPort, 0, 0, nullptr);
    }

    for (auto& thread : workerThreads) {
        thread.join();
    }

    acceptThread.join();

    CloseHandle(completionPort);
    closesocket(listenSocket);
    WSACleanup();

    return 0;
}