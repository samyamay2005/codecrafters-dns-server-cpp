#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include<vector>
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

    while (true) {
    // Receive data
        bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer), 0, reinterpret_cast<struct sockaddr*>(&clientAddress), &clientAddrLen);
        if (bytesRead == -1) {
            perror("Error receiving data");
            break;
        }

        // buffer[bytesRead] = '\0';
        std::cout << "Received " << bytesRead << " bytes: " << buffer << std::endl;

        sendto(resolverSock, buffer, bytesRead, 0,
        (sockaddr*)&resolverAddr, sizeof(resolverAddr));

        char resolverResponse[512];
        socklen_t resolverLen = sizeof(resolverAddr);

        int resBytes = recvfrom(resolverSock, resolverResponse, 512, 0,
                       (sockaddr*)&resolverAddr, &resolverLen);
        resolverResponse[0] = buffer[0];
        resolverResponse[1] = buffer[1];
        // Step 1: Find question section length
        int pos = 12;

        std::vector<std::string> domains;
        std::vector<std::pair<uint16_t, uint16_t>> qinfo;

        // helper lambda for reading domain (handles compression)
        auto readDomain = [&](int &p) {
        std::string domain = "";
        int jumped = 0, jumpPos = -1;

        while (buffer[p] != 0) {
            if ((buffer[p] & 0xC0) == 0xC0) {
                int offset = ((buffer[p] & 0x3F) << 8) | buffer[p + 1];
                if (!jumped) jumpPos = p + 2;
                p = offset;
                jumped = 1;
                continue;
            }

        int len = buffer[p++];
        if (!domain.empty()) domain += ".";

        for (int i = 0; i < len; i++) {
            domain += buffer[p++];
        }
        }

            if (!jumped) p++;
            else p = jumpPos;

            return domain;
        };
        uint16_t qdcount;
        memcpy(&qdcount, &buffer[4], 2);
        qdcount = ntohs(qdcount);
        // parse ALL questions
        for (int i = 0; i < qdcount; i++) {
            std::string domain = readDomain(pos);
            domains.push_back(domain);

            uint16_t type, class_;
            memcpy(&type, &buffer[pos], 2); pos += 2;
            memcpy(&class_, &buffer[pos], 2); pos += 2;

            qinfo.push_back({type, class_});
        }

        // Step 2: Create response buffer
        char response[512];
        memset(response, 0, 512);

        // Step 3: Copy ID
        response[0] = buffer[0];
        response[1] = buffer[1];

        // Step 4: Flags (QR = 1)
        // Extract request flags
        uint16_t reqFlags;
        memcpy(&reqFlags, &buffer[2], 2);
        reqFlags = ntohs(reqFlags);

        // Extract OPCODE and RD
        uint16_t opcode = (reqFlags >> 11) & 0xF;
        uint16_t rd = (reqFlags >> 8)  & 0x1;

        // Build response flags
        uint16_t flags = 0;

        // QR = 1 (response)
        flags |= (1 << 15);

        // OPCODE (copy from request)
        flags |= (opcode << 11);

        // RD (copy from request)
        flags |= (rd << 8);

        // RCODE
        if (opcode == 0) {
            flags |= 0;   // no error
        } else {
            flags |= 4;   // not implemented
        }
        uint16_t qdcount_net = htons(qdcount);
        memcpy(&response[4], &qdcount_net, 2);
        // Convert to network byte order
        flags = htons(flags);

        // Copy into response
        memcpy(&response[2], &flags, 2);    

        // Step 5: QDCOUNT = 1
        // uint16_t qdcount;
        // memcpy(&qdcount, &buffer[4], 2);
        // qdcount = ntohs(qdcount);

        // ANCOUNT, NSCOUNT, ARCOUNT = 0
        uint16_t zero = 0;

        // QDCOUNT (already set earlier)

        // ANCOUNT (set later)

        // NSCOUNT = 0
        memcpy(&response[8], &zero, 2);

        // ARCOUNT = 0
        memcpy(&response[10], &zero, 2);

        // Step 6: Copy question section
        int resPos = 12;

        for (int i = 0; i < qdcount; i++) {
            std::string &domain = domains[i];

            // encode domain
            size_t start = 0;
            while (true) {
            size_t dot = domain.find('.', start);
            std::string label = (dot == std::string::npos)
                ? domain.substr(start)
                : domain.substr(start, dot - start);

                response[resPos++] = label.size();
                for (char c : label) response[resPos++] = c;

                if (dot == std::string::npos) break;
                start = dot + 1;
            }

            response[resPos++] = 0;

            memcpy(&response[resPos], &qinfo[i].first, 2);
            resPos += 2;

            memcpy(&response[resPos], &qinfo[i].second, 2);
            resPos += 2;
        }

        // Step 7: Send response
        int resPosAfterQ = resPos;
        sendto(udpSocket, resolverResponse, resBytes, 0,
        (sockaddr*)&clientAddress, clientAddrLen);
        // set ANCOUNT = number of questions
        uint16_t ancount_net = htons(qdcount);
        memcpy(&response[6], &ancount_net, 2);

        for (int i = 0; i < qdcount; i++) {
            std::string &domain = domains[i];

            // NAME (uncompressed)
            size_t start = 0;
            while (true) {
                size_t dot = domain.find('.', start);
                std::string label = (dot == std::string::npos)
                ? domain.substr(start)
                : domain.substr(start, dot - start);

                response[resPos++] = label.size();
                for (char c : label) response[resPos++] = c;

                if (dot == std::string::npos) break;
                start = dot + 1;
            }

            response[resPos++] = 0;

            // TYPE = A
            uint16_t type = htons(1);
            memcpy(&response[resPos], &type, 2);
            resPos += 2;

            // CLASS = IN
            uint16_t class_ = htons(1);
            memcpy(&response[resPos], &class_, 2);
            resPos += 2;

            // TTL
            uint32_t ttl = htonl(60);
            memcpy(&response[resPos], &ttl, 4);
            resPos += 4;

            // RDLENGTH
            uint16_t rdlength = htons(4);
            memcpy(&response[resPos], &rdlength, 2);
            resPos += 2;

            // IP (any)
            response[resPos++] = 1;
            response[resPos++] = 2;
            response[resPos++] = 3;
            response[resPos++] = 4;
        }

        int responseSize = resPos;
        if (sendto(udpSocket, response, responseSize, 0,
                reinterpret_cast<struct sockaddr*>(&clientAddress),
                sizeof(clientAddress)) == -1) {
            perror("Failed to send response");
        }


    }
//
    close(udpSocket);

    return 0;
}
