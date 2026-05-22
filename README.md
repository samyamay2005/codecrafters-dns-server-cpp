[![progress-banner](https://backend.codecrafters.io/progress/dns-server/fa33d98a-91a9-4d69-8013-abe8a50cb1ea)](https://app.codecrafters.io/users/codecrafters-bot?r=2qF)

This is a starting point for C++ solutions to the
["Build Your Own DNS server" Challenge](https://app.codecrafters.io/courses/dns-server/overview).

In this challenge, you'll build a DNS server that's capable of parsing and
creating DNS packets, responding to DNS queries, handling various record types
and doing recursive resolve. Along the way we'll learn about the DNS protocol,
DNS packet format, root servers, authoritative servers, forwarding servers,
various record types (A, AAAA, CNAME, etc) and more.

**Note**: If you're viewing this repo on GitHub, head over to
[codecrafters.io](https://codecrafters.io) to try the challenge.

# DNS Forwarder in C++

A lightweight DNS forwarder written in modern C++ using UDP sockets and low-level packet parsing.

This project receives DNS queries from clients, forwards them to an upstream DNS resolver, collects the responses, merges them safely, and sends a valid DNS response back to the client.

It handles:

* UDP socket communication
* DNS packet parsing
* DNS compression pointers
* Multiple questions in a single packet
* Response merging
* Timeout handling
* Packet validation

This project was built as part of the DNS Server challenge from CodeCrafters.

---

# Features

* UDP-based DNS server
* Forwards DNS queries to an upstream resolver
* Supports multiple DNS questions (`QDCOUNT > 1`)
* Handles DNS compression pointers
* Expands compressed domain names safely
* Merges multiple resolver responses
* Prevents invalid cross-packet compression references
* Timeout support for upstream resolver
* Packet truncation handling (`TC` flag)
* Built using modern C++

---

# Project Structure

```text
.
├── .codecrafters/
│   ├── compile.sh
│   └── run.sh
│
├── src/
│   └── main.cpp
│
├── .gitignore
├── .gitattributes
├── CMakeLists.txt
├── codecrafters.yml
├── README.md
├── vcpkg.json
├── vcpkg-configuration.json
└── your_program.sh
```

---

# How It Works

## Request Flow

```text
Client
   |
   v
DNS Forwarder (Port 2053)
   |
   v
Upstream DNS Resolver
   |
   v
Merged DNS Response
```

---

# Core Workflow

## 1. Receive DNS Query

The server listens on UDP port `2053` and accepts incoming DNS requests.

```cpp
recvfrom(...)
```

The DNS packet header is parsed to extract:

* Transaction ID
* Flags
* Question count (`QDCOUNT`)
* Question section

---

## 2. Parse and Expand Domain Names

DNS packets commonly use compression pointers to reduce packet size.

The function:

```cpp
expandName(...)
```

handles:

* Standard labels
* Compression pointers (`0xC0`)
* Pointer jumps
* Bounds checking
* Recursive expansion

Example compressed format:

```text
3www6google3com0
```

or pointer-based references:

```text
0xC0 0x0C
```

---

## 3. Split Multi-Question Queries

Some DNS packets contain multiple questions.

Instead of forwarding the entire packet directly, this server:

* Extracts each question
* Creates independent DNS packets
* Sends them separately to the upstream resolver

This improves compatibility with resolvers that expect a single question per request.

---

## 4. Forward Queries to Resolver

Queries are forwarded using UDP:

```cpp
sendto(...)
```

The upstream resolver is provided through:

```bash
--resolver IP:PORT
```

Example:

```bash
./your_program.sh --resolver 8.8.8.8:53
```

---

## 5. Receive Resolver Responses

The server waits for replies from the upstream resolver.

It:

* Validates transaction IDs
* Discards stale packets
* Handles timeouts
* Stores valid responses

---

## 6. Merge Responses

The final DNS response packet is constructed by:

* Preserving the original client transaction ID
* Combining all answer records
* Expanding compressed names
* Updating DNS counts (`ANCOUNT`)
* Setting response flags correctly

The merged response is then returned to the client.

---

# Important Functions

## `expandName(...)`

Expands compressed DNS names into flat label sequences.

Handles:

* Compression pointers
* Nested pointers
* Bounds validation
* Packet-safe parsing

---

## `skipName(...)`

Safely skips DNS names while parsing packets.

Used while navigating DNS sections.

---

## `copyAnswer(...)`

Copies answer resource records from resolver responses into the final packet.

It:

* Expands compressed names
* Preserves metadata
* Prevents invalid pointer references
* Validates packet bounds

---

# Networking Concepts Used

This project demonstrates several important low-level networking concepts:

* UDP socket programming
* DNS protocol internals
* Binary packet parsing
* Network byte ordering
* Compression pointer handling
* Packet serialization
* Resource Records (RR)
* Timeout handling
* Socket APIs
* Memory safety checks

---

# Build Instructions

## Requirements

* Linux
* g++
* CMake
* POSIX socket support

---

## Build Using CMake

```bash
mkdir build
cd build
cmake ..
make
```

---

# Running the Server

## Start the DNS Forwarder

```bash
./your_program.sh --resolver 8.8.8.8:53
```

---

# Testing

You can test the server using `dig`.

## Example

```bash
dig @127.0.0.1 -p 2053 google.com
```

or:

```bash
dig @127.0.0.1 -p 2053 openai.com
```

---

# Example Output

```text
Logs from your program will appear here!
```

If everything works correctly, the DNS response should resolve successfully.

---

# Example Architecture

```text
+-------------+
| DNS Client  |
+-------------+
        |
        v
+-------------------+
| DNS Forwarder     |
| Port 2053         |
+-------------------+
        |
        v
+-------------------+
| Upstream Resolver |
| 8.8.8.8:53        |
+-------------------+
```

---

# Limitations

Current limitations:

* UDP only
* No TCP fallback
* No caching
* No recursive resolution
* No IPv6 support
* Minimal logging
* Single-threaded architecture

---

# Possible Improvements

Future upgrades could include:

* DNS caching
* TCP support
* Async I/O
* Multithreading
* IPv6 support
* Recursive resolver support
* DNS-over-HTTPS (DoH)
* Better logging and metrics
* Config file support

---

# Learning Outcomes

This project is useful for understanding:

* DNS protocol internals
* Systems programming
* Socket programming
* Packet parsing
* Network communication
* Low-level C++ programming

It is a great hands-on networking project for anyone learning backend systems or infrastructure engineering.

---

# License

MIT License

Feel free to fork, modify, and improve the project.

---

# Author

Samyamay Bhattacharjee
