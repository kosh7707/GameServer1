#include <winsock2.h>
#include <WS2tcpip.h>
#include <string>
#include <iostream>


#pragma comment(lib, "Ws2_32.lib")

constexpr int BUFFER_SIZE = 512;
constexpr const char* SERVER_IP = "127.0.0.1";
constexpr int SERVER_PORT = 9000;

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);
    serverAddr.sin_port = htons(SERVER_PORT);

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Connection to server failed with error: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to server" << std::endl;

    char buffer[BUFFER_SIZE];
    std::string message;

    while (true) {
        std::cout << "Enter message to send (or 'quit' to exit): ";
        std::getline(std::cin, message);

        if (message == "quit") {
            break;
        }

        int bytesSent = send(clientSocket, message.c_str(), message.length(), 0);
        if (bytesSent == SOCKET_ERROR) {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        std::cout << "Sent " << bytesSent << " bytes to server" << std::endl;

        int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesReceived == SOCKET_ERROR) {
            std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
            break;
        }
        else if (bytesReceived == 0) {
            std::cout << "Server closed connection" << std::endl;
            break;
        }

        buffer[bytesReceived] = '\0';
        std::cout << "Received from server: " << buffer << std::endl;
    }

    closesocket(clientSocket);
    WSACleanup();

    return 0;
}