#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
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

        uint16_t qdcount;
        memcpy(&qdcount, &buffer[4], 2);
        qdcount = ntohs(qdcount);

        int pos = 12;

        // store raw question chunks
        std::vector<std::vector<char>> questions;

        for (int i = 0; i < qdcount; i++) {
            int start = pos;

            // move over domain name (labels)
            while (true) {
                // Check for compression pointer (11xxxxxx)
                if ((buffer[pos] & 0xC0) == 0xC0) {
                    pos += 2; // pointer is 2 bytes
                    break;
                }

                // Normal label
                if (buffer[pos] == 0) {
                    pos += 1; // end of domain
                    break;
                }

                pos += buffer[pos] + 1;
            }

            pos += 4; // TYPE (2 bytes) + CLASS (2 bytes)

            int end = pos;

            // copy raw question
            questions.emplace_back(buffer + start, buffer + end);
        }
        std::vector<std::vector<char>> responses;

        for (int i = 0; i < qdcount; i++) {

            char newPacket[512];
            memset(newPacket, 0, 512);

            // Copy original header
            memcpy(newPacket, buffer, 12);

            // generate unique ID per query
            uint16_t newID = htons(i + 1);  // simple and works
            memcpy(&newPacket[0], &newID, 2);

            // Set QDCOUNT = 1
            uint16_t one = htons(1);
            memcpy(&newPacket[4], &one, 2);

            // Copy ONE question
            int newPos = 12;
            memcpy(newPacket + newPos, questions[i].data(), questions[i].size());
            newPos += questions[i].size();

            // Send to resolver
            sendto(resolverSock, newPacket, newPos, 0,
                (sockaddr*)&resolverAddr, sizeof(resolverAddr));

            // Receive response
            char resp[512];
            socklen_t len = sizeof(resolverAddr);

            int rBytes = recvfrom(resolverSock, resp, 512, 0,(sockaddr*)&resolverAddr, &len);

            if (rBytes < 0) continue;

            
            responses.emplace_back(resp, resp + rBytes);
        }
    
        char finalResp[512]={0};
 
        

        // Copy header from original request
        memcpy(finalResp, buffer, 12);

        // Set QR = 1 (response)
        finalResp[2] |= 0x80;
        int actualQuestions = 0;
        // Set QDCOUNT = original qdcount
       

        // ANCOUNT = number of answers
        int actualAnswers = 0;
        // NSCOUNT, ARCOUNT = 0
        uint16_t zero = 0;
        memcpy(&finalResp[8], &zero, 2);
        memcpy(&finalResp[10], &zero, 2);

        // Copy ALL original questions
        int posFinal = 12;
        
        for (auto &q : questions) {
            if (posFinal + q.size() > 512) {
                finalResp[2] |= 0x02; // truncated
                break;
            }
            memcpy(finalResp + posFinal, q.data(), q.size());
            posFinal += q.size();
            actualQuestions++;
        }
        uint16_t qd_net = htons(actualQuestions);
        memcpy(&finalResp[4], &qd_net, 2);
        for (int i = 0; i < responses.size(); i++) {

            char* resp = responses[i].data();

            // skip header (12 bytes)
            int p = 12;

            // skip question
            while (true) {
            // Check for compression pointer (11xxxxxx)
            if ((resp[p] & 0xC0) == 0xC0) {
                p += 2; // pointer is 2 bytes
                break;
            }

            // Normal label
            if (resp[p] == 0) {
                p += 1; // end of domain
                break;
            }

            p += resp[p] + 1;
        }
            p += 4; // type + class

            // remaining = answer section
            int ansLen = responses[i].size() - p;

            // how many answers in this response
            uint16_t an;
            memcpy(&an, &responses[i][6], 2);
            int thisAnsCount = ntohs(an);

            if (posFinal + ansLen > 512) {
                finalResp[2] |= 0x02; // truncated
                break;
            }
            // fix compression pointer if needed
            
            memcpy(finalResp + posFinal, resp + p, ansLen);
            posFinal += ansLen;
            actualAnswers += thisAnsCount; 
        }
        uint16_t an_net = htons(actualAnswers);
        memcpy(&finalResp[6], &an_net, 2);

        // Send back to client
        sendto(udpSocket, finalResp, posFinal, 0,(sockaddr*)&clientAddress, clientAddrLen);
    }

    close(udpSocket);

    return 0;
}
