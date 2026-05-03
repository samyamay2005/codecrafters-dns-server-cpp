#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <arpa/inet.h>



// Expand a domain name at `pos` in `pkt` (len=pktLen) into a flat label sequence.
// Returns the number of bytes written into `out`, and advances `pos` past the
// name field in the original packet (ignoring pointer targets).
// Writes labels as: <len><bytes>...<len><bytes>0x00
// Returns -1 on error.
static int expandName(const char* pkt, int pktLen, int& pos, char* out, int outMax) {
    int written = 0;
    bool jumped = false;
    int origPos = pos;

    while (pos < pktLen) {
        uint8_t b = (uint8_t)pkt[pos];

        if ((b & 0xC0) == 0xC0) {
            // Compression pointer
            if (pos + 1 >= pktLen) return -1;
            uint16_t target = ((b & 0x3F) << 8) | (uint8_t)pkt[pos + 1];
            if (!jumped) origPos = pos + 2; // save where to resume after pointer
            pos = target;
            jumped = true;
            continue;
        }

        if (b == 0) {
            // End of name
            if (written + 1 > outMax) return -1;
            out[written++] = 0;
            if (!jumped) pos++; // advance past the 0x00 only if not mid-pointer-chain
            else pos = origPos; // restore to after-pointer position
            return written;
        }

        // Normal label
        int labelLen = b;
        if (pos + 1 + labelLen > pktLen) return -1;
        if (written + 1 + labelLen > outMax) return -1;
        out[written++] = (char)labelLen;
        memcpy(out + written, pkt + pos + 1, labelLen);
        written += labelLen;
        pos += 1 + labelLen;
    }
    return -1;
}

// Copy one answer RR from `resp` starting at `p` into `out` at `outPos`.
// Expands any compressed name in the RR name field to avoid cross-packet pointers.
// Returns bytes written, or -1 on error / overflow.
static int copyAnswer(const char* resp, int respLen, int p,
                      char* out, int outPos, int outMax) {
    // Expand the name
    char nameBuf[256];
    int nameLen = expandName(resp, respLen, p, nameBuf, sizeof(nameBuf));
    if (nameLen < 0) return -1;

    // After expandName, p points past the name in resp.
    // We need TYPE(2) + CLASS(2) + TTL(4) + RDLENGTH(2) + RDATA
    if (p + 10 > respLen) return -1;
    uint16_t rdlen;
    memcpy(&rdlen, resp + p + 8, 2);
    rdlen = ntohs(rdlen);
    int fixedFields = 10 + rdlen;
    if (p + fixedFields > respLen) return -1;

    int needed = nameLen + fixedFields;
    if (outPos + needed > outMax) return -1;

    memcpy(out + outPos, nameBuf, nameLen);
    outPos += nameLen;
    memcpy(out + outPos, resp + p, fixedFields);
    outPos += fixedFields;

    return needed;
}

int main(int argc, char *argv[]) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    setbuf(stdout, NULL);

    std::string resolverIP;
    int resolverPort = 0;

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

    std::cout << "Logs from your program will appear here!" << std::endl;

    int udpSocket;
    struct sockaddr_in clientAddress;

    udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket == -1) {
        std::cerr << "Socket creation failed: " << strerror(errno) << std::endl;
        return 1;
    }

    int reuse = 1;
    if (setsockopt(udpSocket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "SO_REUSEPORT failed: " << strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(2053),
        .sin_addr   = { htonl(INADDR_ANY) },
    };

    if (bind(udpSocket, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) != 0) {
        std::cerr << "Bind failed: " << strerror(errno) << std::endl;
        return 1;
    }

    struct timeval tv{ .tv_sec = 2, .tv_usec = 0 };
    setsockopt(resolverSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buffer[512];
    socklen_t clientAddrLen = sizeof(clientAddress);

    while (true) {
        int bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer), 0,
            reinterpret_cast<struct sockaddr*>(&clientAddress), &clientAddrLen);
        if (bytesRead == -1) {
            perror("Error receiving data");
            break;
        }

        uint16_t qdcount;
        memcpy(&qdcount, &buffer[4], 2);
        qdcount = ntohs(qdcount);

        int pos = 12;

        // Collect raw question sections
        std::vector<std::vector<char>> questions;
        for (int i = 0; i < qdcount; i++) {
            int start = pos;
            while (pos < bytesRead) {
                if ((buffer[pos] & 0xC0) == 0xC0) { pos += 2; break; }
                if (buffer[pos] == 0)               { pos += 1; break; }
                pos += (uint8_t)buffer[pos] + 1;
            }
            pos += 4; // TYPE + CLASS
            questions.emplace_back(buffer + start, buffer + pos);
        }

        // Forward each question individually to resolver
        std::vector<std::vector<char>> responses;
        for (int i = 0; i < qdcount; i++) {
            char newPacket[512] = {};
            memcpy(newPacket, buffer, 12);

            uint16_t newID = htons(i + 1);
            memcpy(&newPacket[0], &newID, 2);

            uint16_t one = htons(1);
            memcpy(&newPacket[4], &one, 2);

            // Clear ANCOUNT / NSCOUNT / ARCOUNT
            memset(&newPacket[6], 0, 6);

            int newPos = 12;
            memcpy(newPacket + newPos, questions[i].data(), questions[i].size());
            newPos += questions[i].size();

            sendto(resolverSock, newPacket, newPos, 0,
                   (sockaddr*)&resolverAddr, sizeof(resolverAddr));

            char resp[512];
            socklen_t len = sizeof(resolverAddr);
            int rBytes = recvfrom(resolverSock, resp, 512, 0,
                                  (sockaddr*)&resolverAddr, &len);
            if (rBytes > 0)
                responses.emplace_back(resp, resp + rBytes);
            else
                responses.emplace_back(); // empty placeholder keeps indexing consistent
        }

        // Build merged response
        char finalResp[512] = {};
        memcpy(finalResp, buffer, 12);

        // QR=1 (response)
        finalResp[2] |= 0x80;

        // Copy flags from first non-empty resolver response (RCODE etc.)
        for (auto& r : responses) {
            if (r.size() >= 4) {
                finalResp[2] = (finalResp[2] & 0x80) | (r[2] & 0x7F);
                finalResp[3] = r[3];
                break;
            }
        }

        // Zero out counts; we'll fill them in
        memset(&finalResp[4], 0, 8);

        int posFinal = 12;
        int actualQuestions = 0;

        // Write all questions
        for (auto& q : questions) {
            if (posFinal + (int)q.size() > 512) {
                finalResp[2] |= 0x02; // TC
                break;
            }
            memcpy(finalResp + posFinal, q.data(), q.size());
            posFinal += q.size();
            actualQuestions++;
        }

        uint16_t qd_net = htons(actualQuestions);
        memcpy(&finalResp[4], &qd_net, 2);

        int actualAnswers = 0;

        // Write answers, expanding compression pointers from each resolver response
        for (int i = 0; i < (int)responses.size(); i++) {
            if (responses[i].empty()) continue;

            const char* resp = responses[i].data();
            int respLen     = responses[i].size();
            if (respLen < 12) continue;

            // How many answers did this response carry?
            uint16_t an;
            memcpy(&an, resp + 6, 2);
            int thisAnsCount = ntohs(an);
            if (thisAnsCount == 0) continue;

            // Skip header (12) + question section in the resolver response
            int p = 12;
            while (p < respLen) {
                if ((resp[p] & 0xC0) == 0xC0) { p += 2; break; }
                if (resp[p] == 0)               { p += 1; break; }
                p += (uint8_t)resp[p] + 1;
            }
            p += 4; // TYPE + CLASS of question

            // Copy each answer RR with pointer expansion
            for (int a = 0; a < thisAnsCount && p < respLen; a++) {
                int written = copyAnswer(resp, respLen, p,
                                         finalResp, posFinal, 512);
                if (written < 0) {
                    finalResp[2] |= 0x02; // TC
                    break;
                }
                posFinal += written;
                actualAnswers++;

                // Advance p past this RR in the *original* resolver packet
                // (skip the name, then fixed 10 bytes + rdlength bytes)
                int p2 = p;
                while (p2 < respLen) {
                    if ((resp[p2] & 0xC0) == 0xC0) { p2 += 2; break; }
                    if (resp[p2] == 0)               { p2 += 1; break; }
                    p2 += (uint8_t)resp[p2] + 1;
                }
                if (p2 + 10 > respLen) break;
                uint16_t rdlen;
                memcpy(&rdlen, resp + p2 + 8, 2);
                p2 += 10 + ntohs(rdlen);
                p = p2;
            }
        }

        uint16_t an_net = htons(actualAnswers);
        memcpy(&finalResp[6], &an_net, 2);

        sendto(udpSocket, finalResp, posFinal, 0,
               (sockaddr*)&clientAddress, clientAddrLen);
    }

    close(udpSocket);
    return 0;
}