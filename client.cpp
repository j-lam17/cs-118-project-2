#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <netdb.h>
#include <cstring>
#include <vector>
#include <fstream>
#include <iostream>
#include <bitset>
#include <queue> 

using namespace std;

#define MAX_ACK 102400
#define MIN_CWND 512
#define MAX_CWND 51200
#define RWND 51200
#define INIT_SSTHRESH 10000
#define PACKET_SIZE 524
#define CLOCKID CLOCK_MONOTONIC

static int serverSockFd;
static sockaddr *serverSockAddr;
static socklen_t serverSockAddrLength;

typedef struct packet_t {
  unsigned int sequence;
  unsigned int acknowledgment;
  unsigned short connectionID;
  char empty;
  char flags;
  char payload[512];
} packet_t;

typedef struct conn_t {
  // keeping track of client ID
  unsigned short ID = 0;
  // initialize current sequence number to 4321
  unsigned int currentSeq = 12345;
  unsigned int currentAck = 0;
  // need to add necessary congestion variables
  unsigned int cwnd = MIN_CWND;
  unsigned int ssthresh = INIT_SSTHRESH;
} conn_t;

queue<packet_t> q;
queue<ssize_t> sizes;

void connToHeader(conn_t *connection, packet_t &packet);
void setA(packet_t &packet, bool b);
void setS(packet_t &packet, bool b);
void setF(packet_t &packet, bool b);
bool getA(packet_t &packet);
bool getS(packet_t &packet);
bool getF(packet_t &packet);
void printPacket(packet_t &packet, conn_t *connection, bool recv);
void ntohPacket(packet_t &packet);
void htonPacket(packet_t &packet);
bool finPacket(packet_t &incomingPacket);
void dropPacket(packet_t &packet);

conn_t client_conn;
int fileToTransferFd;

// Timer handler
static void
abortHandler(union sigval val) {
  close(serverSockFd);
  cerr << "ERROR: aborting connection due to no packets within 10s" << endl;
  exit(1);
}

static void
finHandler(union sigval val) {
  close(fileToTransferFd);
  close(serverSockFd);
  cerr << "DEBUG: Closing file transmission connection\n";
  exit(0);
}

static void
RTO(union sigval val) {
  // Update CWND + SSTHRESH
  client_conn.ssthresh = client_conn.cwnd / 2;
  client_conn.cwnd = MIN_CWND;

  // retransmit most recent packet
  packet_t p = q.front();
  ssize_t size = sizes.front();
  cerr << "retransmitting " << ntohl(p.sequence) << " (" << strlen(p.payload) << "B)" << endl;
  sendto(serverSockFd, &p, 12 + size, 0, serverSockAddr, serverSockAddrLength);
  printPacket(p, &client_conn, false);
}

// MAIN ==========
int main(int argc, char *argv[]) {
  if (argc != 4) {
    cerr << "ERROR: Usage: " << argv[0] << " <HOSTNAME-OR-IP> <PORT> <FILENAME>" << endl;
    exit(1);
  }

  // Set the hints for getaddrinfo()
  struct addrinfo hints;
  struct addrinfo *result;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM; // UDP socket
  hints.ai_family = AF_INET;      // IPv4

  // Get server address info using hints
  // - argv[1]: HOSTNAME-OR-IP
  // - argv[2]: PORT#
  int ret;
  if ((ret = getaddrinfo(argv[1], argv[2], &hints, &result)) != 0) {
    cerr << "ERROR: " << ret << endl;
    exit(1);
  }

  serverSockAddr = result->ai_addr;
  serverSockAddrLength = result->ai_addrlen;

  // Create a UDP socket
  // - int socket(int domain, int type, int protocol)
  serverSockFd = socket(AF_INET, SOCK_DGRAM, 0);

  // // Open file to transfer from client to server
  // // - argv[3]: FILENAME
  fileToTransferFd = open(argv[3], O_RDONLY);
  if (fileToTransferFd == -1) {
    cerr << "ERROR: open()" << endl;
    exit(1);
  }

  struct stat fdStat;
  fstat(fileToTransferFd, &fdStat);

  // Initialize packet values
  packet_t packet;
  connToHeader(&client_conn, packet);
  setA(packet, false);
  setS(packet, true);
  setF(packet, false);

  // Print out packet sending
  printPacket(packet, &client_conn, false);

  // network ordering
  htonPacket(packet);

  // Send packet
  // syn packet is always size 12
  sendto(serverSockFd, &packet, 12, 0, serverSockAddr, serverSockAddrLength);
  client_conn.currentSeq++;
  // Timer variables
  timer_t timerid;
  struct sigevent sev;
  struct itimerspec its;

  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = abortHandler;
  sev.sigev_notify_attributes = NULL;
  timer_create(CLOCK_MONOTONIC, &sev, &timerid);

  // Start the 10 second abort timer for connection activity
  its.it_value.tv_sec = 5;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;
  timer_settime(timerid, 0, &its, NULL);

  // Receive packet
  packet = {0}; // reset values
  recvfrom(serverSockFd, &packet, 12, 0, serverSockAddr, &serverSockAddrLength);
  ntohPacket(packet);
  printPacket(packet, &client_conn, true);

  // Disarm abort timer
  timer_settime(timerid, 0, &its, NULL);

  // extract client number
  client_conn.ID = packet.connectionID;
  client_conn.currentAck = packet.sequence + 1;

  // sending file in packets of size 512 always
  // size of the file
  long int size = fdStat.st_size;
  // cerr << "DEBUG: file size = " << size << " bytes" << endl;

  char sendBuf[512];
  int bytesRead;
  bool first = true;

  // Loop through file data
  long int bytesLeftToRead = size;
  while (bytesLeftToRead > 0) {
    // Calculate # of packets left to send
    int nPacketsLeft = (bytesLeftToRead / 512);
    if (bytesLeftToRead % 512 > 0)
      nPacketsLeft += 1;

    // Calculate # of packets we can send out
    int nPackets = client_conn.cwnd / 512;
    if (nPackets > nPacketsLeft)
      nPackets = nPacketsLeft;

    unsigned int expectedAck = client_conn.currentSeq + 512;
    long int bytesSent = 0;

    cerr << "----------" << endl;

    // Send out packets
    for (int i = 0; i < nPackets; i++) {
      // Clear datagram
      packet = {0};

      // Populate header
      bool isFirstPacket = (bytesLeftToRead == size);
      if (!isFirstPacket)
        client_conn.currentAck = 0;
      connToHeader(&client_conn, packet);

      // Set flags
      setA(packet, isFirstPacket);
      setS(packet, false);
      setF(packet, false);

      // Read from file and copy into packet
      memset(sendBuf, 0, 512);
      bytesRead = read(fileToTransferFd, sendBuf, 512);
      memcpy(packet.payload, sendBuf, bytesRead);
      bytesLeftToRead -= 512;

      // Send packet
      cerr << "Sent packet" << endl;
      printPacket(packet, &client_conn, false);
      htonPacket(packet);
      sendto(serverSockFd, &packet, 12 + bytesRead, 0, serverSockAddr, serverSockAddrLength);
      q.push(packet);
      sizes.push(12 + bytesRead);
      bytesSent += bytesRead;

      // Update sequence #
      client_conn.currentSeq = (client_conn.currentSeq + bytesRead) % (MAX_ACK + 1);
    }

    // Timer variables for RTO
    timer_t timeridRTO;
    struct sigevent sevRTO;
    struct itimerspec itsRTO;

    sevRTO.sigev_notify = SIGEV_THREAD;
    sevRTO.sigev_notify_function = RTO;
    sevRTO.sigev_notify_attributes = NULL;
    timer_create(CLOCK_MONOTONIC, &sevRTO, &timeridRTO);

    // // Start the 0.5 second timer
    itsRTO.it_value.tv_sec = 0;
    itsRTO.it_value.tv_nsec = 500000000;
    itsRTO.it_interval.tv_sec = 0;
    itsRTO.it_interval.tv_nsec = 0;
    timer_settime(timeridRTO, 0, &itsRTO, NULL);

    long int bytesToAck = bytesSent;

    while (!q.empty()) {
      // Clear packet data
      packet = {0};

      // Wait for a response from server
      recvfrom(serverSockFd, &packet, 12, 0, serverSockAddr, &serverSockAddrLength);
      ntohPacket(packet);

      // Validate incoming packet
      if (packet.connectionID == client_conn.ID) {
        printPacket(packet, &client_conn, true);

        cerr << "Recieved ACK: "<< packet.acknowledgment << " (expected " << expectedAck << ")" << endl;
        if (packet.acknowledgment >= expectedAck) {
          // Reset RTO timer (ASSUMING CORRECT IN ORDER ACK)
          timer_settime(timeridRTO, 0, &itsRTO, NULL);

          // // Reset 10s timer for receiving data
          timer_settime(timerid, 0, &its, NULL);

          // Update CWND
          if (client_conn.cwnd < client_conn.ssthresh) // Slow-start mode
            client_conn.cwnd += 512;
          else if (client_conn.cwnd  < MAX_CWND) // Congestion avoidance
            client_conn.cwnd += ((512 * 512) / client_conn.cwnd);
          else
            client_conn.cwnd = MAX_CWND; // Max-out

          bytesToAck -=512;

          if (packet.acknowledgment > expectedAck) {
            expectedAck = packet.acknowledgment;
            while (!q.empty()) {
              q.pop();
              sizes.pop();
            }
          }
          else {
            expectedAck += (bytesToAck < 512) ? bytesToAck : 512;
            q.pop();
            sizes.pop();
          }

        }
      }
      // Drop packets from unknown senders
      else {
        dropPacket(packet);
      }
    }

    // Disarm RTO
    itsRTO.it_value.tv_sec = 0;
    itsRTO.it_value.tv_nsec = 0;
    itsRTO.it_interval.tv_sec = 0;
    itsRTO.it_interval.tv_nsec = 0;
    timer_settime(timeridRTO, 0, &itsRTO, NULL);
  }


  // sending FIN packet
  packet = {0};
  connToHeader(&client_conn, packet);
  packet.acknowledgment = 0;
  setA(packet, false);
  setS(packet, false);
  setF(packet, true);

  printPacket(packet, &client_conn, false);

  htonPacket(packet);
  sendto(serverSockFd, &packet, 12, 0, serverSockAddr, serverSockAddrLength);
  client_conn.currentSeq = (client_conn.currentSeq + 1) % (MAX_ACK + 1);

  // need to change the timer
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = finHandler;
  sev.sigev_notify_attributes = NULL;
  timer_create(CLOCK_MONOTONIC, &sev, &timerid);

  // Start the 2 second timer
  its.it_value.tv_sec = 2;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;
  timer_settime(timerid, 0, &its, NULL);

  // increment to 4323
  client_conn.currentAck++;

  // loop until timer goes off
  while (1) {
    packet = {0};
    // now deal with receiving response ack from server
    // cerr << "DEBUG: Waiting to read\n";
    recvfrom(serverSockFd, &packet, 12, 0, serverSockAddr, &serverSockAddrLength);
    // cerr << "DEBUG: read after ack" << endl;
    ntohPacket(packet);
    // condition where it is a FIN packet
    // respond with an ACK
    if (finPacket(packet)) {
      printPacket(packet, &client_conn, true);

      client_conn.currentAck = packet.sequence + 1;
      connToHeader(&client_conn, packet);
      setA(packet, true);
      setS(packet, false);
      setF(packet, false);

      printPacket(packet, &client_conn, false);

      htonPacket(packet);

      sendto(serverSockFd, &packet, 12, 0, serverSockAddr, serverSockAddrLength);
    }
    // drop packet
    else {
      dropPacket(packet);
    }
  }

  // Close socket + file
  close(serverSockFd);
  close(fileToTransferFd);

  exit(0);
}

void setA(packet_t &packet, bool b) {
  char tmp = packet.flags;
  // zero out previous flag
  // 00000011
  tmp &= 0x03;
  // set A flag with value b
  packet.flags = tmp | (b << 2);
}
void setS(packet_t &packet, bool b) {
  char tmp = packet.flags;
  // zero out previous flag
  // 00000101
  tmp &= 0x05;
  // set S flag with value b
  packet.flags = tmp | (b << 1);
}
void setF(packet_t &packet, bool b) {
  char tmp = packet.flags;
  // zero out previous flag
  // 00000110
  tmp &= 0x06;
  // set A flag with value b
  packet.flags = tmp | (b);
}

bool getA(packet_t &packet) {
  char tmp = packet.flags;
  return (tmp & 0x04);
}

bool getS(packet_t &packet) {
  char tmp = packet.flags;
  return (tmp & 0x02);
}

bool getF(packet_t &packet) {
  char tmp = packet.flags;
  return (tmp & 0x01);
}

// Print packet info
void printPacket(packet_t &packet, conn_t *connection, bool recv) {
  if (recv)
    cout << "RECV ";
  else
    cout << "SEND ";

  cout << packet.sequence << " "
       << packet.acknowledgment << " "
       << connection->ID << " "
       << connection->cwnd << " "
       << connection->ssthresh;

  if (getA(packet))
    cout << " ACK";

  if (getS(packet))
    cout << " SYN";

  if (getF(packet))
    cout << " FIN";

  // TODO: add duplicate check

  cout << endl;
}

void ntohPacket(packet_t &packet) {
  packet.sequence = ntohl(packet.sequence);
  packet.acknowledgment = ntohl(packet.acknowledgment);
  packet.connectionID = ntohs(packet.connectionID);
}
void htonPacket(packet_t &packet) {
  packet.sequence = htonl(packet.sequence);
  packet.acknowledgment = htonl(packet.acknowledgment);
  packet.connectionID = htons(packet.connectionID);
}

void connToHeader(conn_t *connection, packet_t &packet) {
  packet.sequence = connection->currentSeq;
  packet.acknowledgment = connection->currentAck;
  packet.connectionID = connection->ID;
}

bool finPacket(packet_t &incomingPacket) {
  char t = incomingPacket.flags;
  // want !S F
  return !((t >> 1) & 0x1) && (t & 0x1);
}

void dropPacket(packet_t &packet) {
  string msg = "DROP ";
  msg += to_string(packet.sequence);
  msg += " ";
  msg += to_string(packet.acknowledgment);
  msg += " ";
  msg += to_string(packet.connectionID);
  if (getA(packet)) {
    msg += " ACK";
  }
  if (getS(packet)) {
    msg += " SYN";
  }
  if (getF(packet)) {
    msg += " FIN";
  }
  msg += '\n';
  cout << msg;
}
