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
  client_conn.ssthresh = 1000;
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

  sockaddr *serverSockAddr = result->ai_addr;
  socklen_t serverSockAddrLength = result->ai_addrlen;

  // Create a UDP socket
  // - int socket(int domain, int type, int protocol)
  int serverSockFd = socket(AF_INET, SOCK_DGRAM, 0);

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

  // sendto(serverSockFd, fileBuffer, bytesRead, MSG_CONFIRM, serverSockAddr, serverSockAddrLength);

  // cout << "DATA sent" << endl;

  // struct sockaddr addr;
  // socklen_t addr_len = sizeof(struct sockaddr);
  // memset(fileBuffer, 0, sizeof(fileBuffer));
  // ssize_t length = recvfrom(serverSockFd, fileBuffer, fdStat.st_size, 0, &addr, &addr_len);
  // string str((char *)fileBuffer);
  // cerr << "ACK reveived " << length << " bytes: " << endl
  //      << str << endl;

  // close(fileToTransferFd);

  // // Print binary
  // for (long unsigned i = 0; i < sizeof(packet); i += 1)
  // {
  //   uint8_t x = (uint8_t)((char *)&packet)[i];
  //   cout << bitset<8>(x) << endl;
  // }

  // exit(0);

  // Initialize packet values
  packet_t packet;
  packet.sequence = htonl(12345);
  packet.acknowledgment = htonl(0);
  packet.connectionID = htons(0);
  packet.flags = htons(SYN);

  // Print out packet sending
  printPacket(packet, &client_conn, false);

  // Send packet
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);

  // Recieve packet
  packet = {0}; // reset values
  int n = recvfrom(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, &serverSockAddrLength);
  // cerr << "Received " << n << " bytes!" << endl;

  // Print out packet received
  client_conn.currentSeq = ntohl(packet.acknowledgment);
  client_conn.currentAck = ntohl(packet.sequence) + 1;
  client_conn.ID = ntohs(packet.connectionID);
  printPacket(packet, &client_conn, true);

  // Hardcoded response from the client to server

  long int size = fdStat.st_size;

  uint8_t fileBuffer[512];
  packet = {0};
  packet.sequence = htonl(client_conn.currentSeq);
  packet.acknowledgment = htonl(client_conn.currentAck);
  packet.connectionID = htons(client_conn.ID);
  packet.flags = htons(ACK);

  size_t bytesRead = read(fileToTransferFd, fileBuffer, 512);
  memcpy(packet.payload, fileBuffer, sizeof(fileBuffer));

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
       << connection->ssthresh << " "
       << connection->cwnd;

  if (getA(packet))
    cout << " ACK";

  if (getS(packet))
    cout << " SYN";

  if (getF(packet))
    cout << " FIN";

  // TODO: add duplicate check

  cout << endl;
}