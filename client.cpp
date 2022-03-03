#include <sys/types.h>
#include <sys/socket.h>
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

using namespace std;

#define PACKET_SIZE 524

typedef struct packet_t
{
  unsigned int sequence;
  unsigned int acknowledgment;
  unsigned short connectionID;
  char empty;
  char flags;
  char payload[512];
} packet_t;

typedef struct conn_t
{
  // keeps track of where to send the packet back to
  struct sockaddr addr;
  // keeping track of packets
  unsigned short ID;
  // initialize current sequence number to 4321
  unsigned int currentSeq = 4321;
  unsigned int currentAck;
  // need to add necessary congestion variables
  unsigned int cwnd;
  unsigned int ssthresh;
  // ofstream to write to
  ofstream *fs;
} conn_t;

void setA(packet_t &packet, bool b);
void setS(packet_t &packet, bool b);
void setF(packet_t &packet, bool b);
bool getA(packet_t &packet);
bool getS(packet_t &packet);
bool getF(packet_t &packet);
unsigned int payloadSize(packet_t &packet);
void printPacketServer(packet_t &packet, conn_t *connection, bool recv);

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
  // int fileToTransferFd = open(argv[3], O_RDONLY);
  // if (fileToTransferFd == -1)
  // {
  //   cerr << "ERROR: open()" << endl;
  //   exit(1);
  // }

  // struct stat fdStat;
  // fstat(fileToTransferFd, &fdStat);
  // uint8_t fileBuffer[fdStat.st_size];
  // size_t bytesRead = read(fileToTransferFd, fileBuffer, fdStat.st_size);
  // cout << bytesRead << " bytes read" << endl;

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

  packet_t packet;
  packet.sequence = 12345;
  packet.acknowledgment = 0;
  packet.connectionID = 0;
  setA(packet, false);
  setS(packet, true);
  setF(packet, false);

  // create a make shift connection
  conn_t client_conn;
  client_conn.ID = 0;
  client_conn.cwnd = 512;

  // print out packet sending
  printPacketServer(packet, &client_conn, false);

  // sending packet
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);
  packet = {0};
  int n = recvfrom(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, &serverSockAddrLength);
  cerr << "Received: " << n << endl;

  // print out packet received
  client_conn.ID = packet.connectionID;
  client_conn.currentAck = packet.sequence + 1;
  client_conn.currentSeq = packet.acknowledgment;
  printPacketServer(packet, &client_conn, true);

  // manually hardcoding response from the client to server
  packet = {0};
  packet.sequence = client_conn.currentSeq;
  packet.acknowledgment = client_conn.currentAck;
  packet.connectionID = client_conn.ID;
  setA(packet, true);
  setS(packet, false);
  setF(packet, false);
  strcpy(packet.payload, "Hello World\n");
  cerr << "Payload: " << packet.payload;
  printPacketServer(packet, &client_conn, false);
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);
  client_conn.currentSeq = client_conn.currentSeq + strlen(packet.payload);

  // receive response ack from server
  packet = {0};
  recvfrom(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, &serverSockAddrLength);
  printPacketServer(packet, &client_conn, true);

  // client_conn.currentAck = packet.sequence + 1;

  // sending FIN packet
  packet = {0};
  packet.sequence = client_conn.currentSeq;
  packet.acknowledgment = 0; // ACK = 0 to terminate connection
  packet.connectionID = client_conn.ID;
  setA(packet, false);
  setS(packet, false);
  setF(packet, true);
  printPacketServer(packet, &client_conn, false);
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);
  client_conn.currentSeq = client_conn.currentSeq + 1;

  // receiving FIN|ACK packet from server
  packet = {0};
  recvfrom(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, &serverSockAddrLength);
  printPacketServer(packet, &client_conn, true);

  // sending final ACK to indicate it has received SYN|ACK and to close connection completely
  packet = {0};
  packet.sequence = client_conn.currentSeq;
  packet.acknowledgment = client_conn.currentAck + 1;
  packet.connectionID = client_conn.ID;
  setA(packet, true);
  setS(packet, false);
  setF(packet, false);

  printPacketServer(packet, &client_conn, false);
  sendto(serverSockFd, &packet, sizeof(packet_t), 0, serverSockAddr, serverSockAddrLength);

  exit(0);
}

void setA(packet_t &packet, bool b)
{
  char tmp = packet.flags;
  // zero out previous flag
  // 00000011
  tmp &= 0x03;
  // set A flag with value b
  packet.flags = tmp | (b << 2);
}

void setS(packet_t &packet, bool b)
{
  char tmp = packet.flags;
  // zero out previous flag
  // 00000101
  tmp &= 0x05;
  // set S flag with value b
  packet.flags = tmp | (b << 1);
}

void setF(packet_t &packet, bool b)
{
  char tmp = packet.flags;
  // zero out previous flag
  // 00000110
  tmp &= 0x06;
  // set A flag with value b
  packet.flags = tmp | (b);
}

bool getA(packet_t &packet)
{
  char tmp = packet.flags;
  return (tmp & 0x04);
}

bool getS(packet_t &packet)
{
  char tmp = packet.flags;
  return (tmp & 0x02);
}

bool getF(packet_t &packet)
{
  char tmp = packet.flags;
  return (tmp & 0x01);
}

unsigned int payloadSize(packet_t &packet)
{
  return strlen(packet.payload);
}

// printing out packets
// need to double check formatting
void printPacketServer(packet_t &packet, conn_t *connection, bool recv)
{
  if (recv)
  {
    cout << "RECV ";
  }
  else
  {
    cout << "SEND ";
  }
  cout << packet.sequence << " " << packet.acknowledgment
       << " " << connection->ID << " " << connection->cwnd;

  if (getA(packet))
  {
    cout << " ACK";
  }

  if (getS(packet))
  {
    cout << " SYN";
  }

  if (getF(packet))
  {
    cout << " FIN";
  }

  // add duplicate check at some point as well

  // end of print statement, append newline
  cout << endl;
}
