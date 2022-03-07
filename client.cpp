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

using namespace std;

#define PACKET_SIZE 524
#define ACK 0x04     // 00000100
#define SYN 0x02     // 00000010
#define FIN 0x01     // 00000001
#define SYN_ACK 0x06 // 00000110
#define FIN_ACK 0x05 // 00000101

static size_t bytesRead;
static int serverSockFd;
static sockaddr *serverSockAddr;
static socklen_t serverSockAddrLength;

typedef struct packet_t
{
  uint32_t sequence;
  uint32_t acknowledgment;
  uint16_t connectionID;
  uint16_t flags;
  uint8_t payload[512];
} packet_t;

typedef struct conn_t
{
  // keeps track of where to send the packet back to
  struct sockaddr addr;
  // keeping track of packets
  uint16_t ID;
  // initialize current sequence number to 4321
  uint32_t currentSeq;
  uint32_t currentAck;
  // need to add necessary congestion variables
  uint32_t cwnd;
  uint32_t ssthresh;
  // ofstream to write to
  ofstream *fs;
} conn_t;

bool getA(packet_t &packet);
bool getS(packet_t &packet);
bool getF(packet_t &packet);
unsigned int payloadSize(packet_t &packet);
void printPacket(packet_t &packet, conn_t *connection, bool recv);

// Timer handler
static void
abortHandler(union sigval val)
{
  close(serverSockFd);
  cerr << "ERROR: aborting connection (no packets recieved)" << endl;
  exit(1);
  // sendto(serverSockFd, val.sival_ptr, bytesRead, MSG_CONFIRM, serverSockAddr, serverSockAddrLength);
}

// MAIN ==========
int main(int argc, char *argv[])
{
  if (argc != 4)
  {
    cerr << "ERROR: Usage: " << argv[0] << " <HOSTNAME-OR-IP> <PORT> <FILENAME>" << endl;
    exit(1);
  }

  // Set the hints for getaddrinfo()
  struct addrinfo hints;
  struct addrinfo *result;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM; // UDP socket
  hints.ai_family = AF_INET;      // IPv4

  // Initialize connection values
  conn_t client_conn;
  client_conn.ID = 0;
  client_conn.ssthresh = 10000;
  client_conn.cwnd = 512;

  // Get server address info using hints
  // - argv[1]: HOSTNAME-OR-IP
  // - argv[2]: PORT#
  int ret;
  if ((ret = getaddrinfo(argv[1], argv[2], &hints, &result)) != 0)
  {
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
  int fileToTransferFd = open(argv[3], O_RDONLY);
  if (fileToTransferFd == -1)
  {
    cerr << "ERROR: open()" << endl;
    exit(1);
  }

  struct stat fdStat;
  fstat(fileToTransferFd, &fdStat);


  // Initialize packet values
  packet_t packet;
  packet.sequence = htonl(12345);
  packet.acknowledgment = htonl(0);
  packet.connectionID = htons(0);
  packet.flags = htons(SYN);

  // Print out packet sending
  printPacket(packet, &client_conn, false);

  // Send SYN packet
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);

  // Timer variables
  timer_t timerid;
  struct sigevent sev;
  struct itimerspec its;

  // Create the timer
  union sigval arg;
  arg.sival_ptr = &packet;

  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = abortHandler;
  sev.sigev_notify_attributes = NULL;
  sev.sigev_value = arg;
  timer_create(CLOCK_MONOTONIC, &sev, &timerid);

  // Start the 10 seconds timer
  its.it_value.tv_sec = 10;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = 10;
  its.it_interval.tv_nsec = 0;
  timer_settime(timerid, 0, &its, NULL);

  // Recieve SYN_ACK packet
  packet = {0}; // reset values
  int n = recvfrom(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, &serverSockAddrLength);

  if (n > 0)
  {
    // Print out packet received
    client_conn.currentSeq = ntohl(packet.acknowledgment);
    client_conn.currentAck = ntohl(packet.sequence) + 1;
    client_conn.ID = ntohs(packet.connectionID);
    printPacket(packet, &client_conn, true);

    // Disarm timer
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;
    timer_settime(timerid, 0, &its, NULL);
  }

  // Hardcoded response from the client to server, start sending data

  long int size = fdStat.st_size;

  uint8_t fileBuffer[512];
  packet = {0};
  packet.sequence = htonl(client_conn.currentSeq);
  packet.acknowledgment = htonl(client_conn.currentAck);
  packet.connectionID = htons(client_conn.ID);
  packet.flags = htons(ACK);

  size_t bytesRead = read(fileToTransferFd, packet.payload, 512);

  // cerr << "Payload:\n"
  //      << packet.payload << endl;

  // memcpy(packet.payload, fileBuffer, sizeof(fileBuffer));

  printPacket(packet, &client_conn, false);
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);
  client_conn.currentSeq = client_conn.currentSeq + strlen((char *)packet.payload);

  // receive response ack from server
  packet = {0};
  recvfrom(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, &serverSockAddrLength);
  printPacket(packet, &client_conn, true);

  // client_conn.currentAck = packet.sequence + 1;

  // sending FIN packet
  packet = {0};
  packet.sequence = htonl(client_conn.currentSeq);
  packet.acknowledgment = htonl(0); // ACK = 0 to terminate connection
  packet.connectionID = htons(client_conn.ID);
  packet.flags = htons(FIN);
  printPacket(packet, &client_conn, false);
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);
  client_conn.currentSeq = client_conn.currentSeq + 1;

  // receiving FIN-ACK packet from server
  packet = {0};
  recvfrom(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, &serverSockAddrLength);
  printPacket(packet, &client_conn, true);

  // sending final ACK to indicate it has received FIN-ACK and to close connection completely
  packet = {0};
  packet.sequence = htonl(client_conn.currentSeq);
  packet.acknowledgment = htonl(client_conn.currentAck + 1);
  packet.connectionID = htons(client_conn.ID);
  packet.flags = htons(ACK);

  printPacket(packet, &client_conn, false);
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);

  close(serverSockFd);
  close(fileToTransferFd);
  exit(0);
}

// HELPER FUNCTIONS ==========
bool getA(packet_t &packet)
{
  char tmp = ntohs(packet.flags);
  return (tmp & 0x04);
}

bool getS(packet_t &packet)
{
  char tmp = ntohs(packet.flags);
  return (tmp & 0x02);
}

bool getF(packet_t &packet)
{
  char tmp = ntohs(packet.flags);
  return (tmp & 0x01);
}

unsigned int payloadSize(packet_t &packet)
{
  return strlen((char *)packet.payload);
}

// Print packet info
void printPacket(packet_t &packet, conn_t *connection, bool recv)
{
  if (recv)
    cout << "RECV ";
  else
    cout << "SEND ";

  cout << ntohl(packet.sequence) << " "
       << ntohl(packet.acknowledgment) << " "
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