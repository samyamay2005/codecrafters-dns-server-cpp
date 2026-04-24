#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include<vector>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // Disable output buffering
    setbuf(stdout, NULL);
    std::string resolverIP;
    int resolverPort;

    if (argc == 3 && std::string(argv[1]) == "--resolver") {
        std::string addr = argv[2];
        int colon = addr.find(':');
        resolverIP = addr.substr(0, colon);
        resolverPort = std::stoi(addr.substr(colon + 1));
    }

    int resolverSock = socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in resolverAddr{};
    resolverAddr.sin_family = AF_INET;
    resolverAddr.sin_port = htons(resolverPort);
    inet_pton(AF_INET, resolverIP.c_str(), &resolverAddr.sin_addr);
    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cout << "Logs from your program will appear here!" << std::endl;

      // TODO: Uncomment the code below to pass the first stage
    int udpSocket;
    struct sockaddr_in clientAddress;

    udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket == -1) {
        std::cerr << "Socket creation failed: " << strerror(errno) << "..." << std::endl;
        return 1;
    }

    // Since the tester restarts your program quite often, setting REUSE_PORT
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(udpSocket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "SO_REUSEPORT failed: " << strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in serv_addr = { .sin_family = AF_INET,
        .sin_port = htons(2053),
        .sin_addr = { htonl(INADDR_ANY) },
    };

    if (bind(udpSocket, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) != 0) {
        std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        return 1;
    }


    int bytesRead;

    char buffer[512];
    socklen_t clientAddrLen = sizeof(clientAddress);

    // Set timeout ONCE before loop
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(resolverSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (true) {

        // Receive from client
        bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer), 0,
        reinterpret_cast<struct sockaddr*>(&clientAddress), &clientAddrLen);

        if (bytesRead == -1) {
            perror("Error receiving data");
            break;
        }

        // Forward to resolver
        sendto(resolverSock, buffer, bytesRead, 0,
           (sockaddr*)&resolverAddr, sizeof(resolverAddr));

        // Receive from resolver
        char resolverResponse[512];
        socklen_t resolverLen = sizeof(resolverAddr);

        int resBytes = recvfrom(resolverSock, resolverResponse, 512, 0,
                           (sockaddr*)&resolverAddr, &resolverLen);

        if (resBytes < 0) {
            perror("Resolver recv failed");
            continue;
        }

        // Fix transaction ID
        resolverResponse[0] = buffer[0];
        resolverResponse[1] = buffer[1];

        // Send back to client
        sendto(udpSocket, resolverResponse, resBytes, 0,
           (sockaddr*)&clientAddress, clientAddrLen);
    }

    close(udpSocket);

    return 0;
}
