#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main() {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // Disable output buffering
    setbuf(stdout, NULL);

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

//    // Since the tester restarts your program quite often, setting REUSE_PORT
//    // ensures that we don't run into 'Address already in use' errors
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
//
    while (true) {
    // Receive data
        bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer), 0, reinterpret_cast<struct sockaddr*>(&clientAddress), &clientAddrLen);
        if (bytesRead == -1) {
            perror("Error receiving data");
            break;
        }

        // buffer[bytesRead] = '\0';
        std::cout << "Received " << bytesRead << " bytes: " << buffer << std::endl;

        // Step 1: Find question section length
        int pos = 12;

        while (buffer[pos] != 0) {
            pos += buffer[pos] + 1;
        }

        pos += 1;  // null byte
        pos += 4;  // type + class

        int questionLength = pos - 12;

        // Step 2: Create response buffer
        char response[512];
        memset(response, 0, 512);

        // Step 3: Copy ID
        response[0] = buffer[0];
        response[1] = buffer[1];

        // Step 4: Flags (QR = 1)
        uint16_t flags = htons(0x8000);
        memcpy(&response[2], &flags, 2);

        // Step 5: QDCOUNT = 1
        uint16_t qdcount = htons(1);
        memcpy(&response[4], &qdcount, 2);

        // ANCOUNT, NSCOUNT, ARCOUNT = 0
        uint16_t zero = 0;
        uint16_t ancount = htons(1);
        memcpy(&response[6], &ancount, 2); // ANCOUNT = 1
        memcpy(&response[8], &zero, 2);
        memcpy(&response[10], &zero, 2);

        // Step 6: Copy question section
        memcpy(&response[12], &buffer[12], questionLength);

        // Step 7: Send response
        int responseSize = 12 + questionLength;
        int ansStart = responseSize;
        responseSize = ansStart;
        // ---- ANSWER SECTION ----

        // NAME (pointer to domain at offset 12)
        response[ansStart++] = 0xC0;
        response[ansStart++] = 0x0C;

        // TYPE = A (1)
        uint16_t type = htons(1);
        memcpy(&response[ansStart], &type, 2);
        ansStart += 2;

        // CLASS = IN (1)
        uint16_t class_ = htons(1);
        memcpy(&response[ansStart], &class_, 2);
        ansStart += 2;

        // TTL = 60 seconds
        uint32_t ttl = htonl(60);
        memcpy(&response[ansStart], &ttl, 4);
        ansStart += 4;

        // RDLENGTH = 4
        uint16_t rdlength = htons(4);
        memcpy(&response[ansStart], &rdlength, 2);
        ansStart += 2;

        // RDATA = IP (example: 8.8.8.8)
        response[ansStart++] = 8;
        response[ansStart++] = 8;
        response[ansStart++] = 8;
        response[ansStart++] = 8;
        // if (sendto(udpSocket, response, responseSize, 0,
        //         reinterpret_cast<struct sockaddr*>(&clientAddress),
        //         sizeof(clientAddress)) == -1) {
        //     perror("Failed to send response");
        // }


    }
//
    close(udpSocket);

    return 0;
}
