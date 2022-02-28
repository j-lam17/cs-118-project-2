#include <string>
#include <thread>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <netdb.h>
#include <cstring>
#include <vector>
#include <fstream>

using namespace std;

#define MAX_SIZE 524
#define MAX_ACK 102400
#define MIN_CWND 512
#define MAX_CWND 51200
#define RWND 51200
#define INIT_SSTHRESH 10000
#define INIT_SEQ 4321
#define PACKET_SIZE 524

// user defined structs
typedef struct packet_t {
  unsigned int sequence;
  unsigned int acknowledgment;
  unsigned short connectionID;
  char empty;
  char flags;
  char payload[512]; 
} packet_t;

typedef struct conn_t {
  // keeps track of where to send the packet back to 
  struct sockaddr addr;
  // keeping track of packets
  short ID;
  // initialize current sequence number to 4321
  int currentSeq = INIT_SEQ;
  int currentAck; 
  // need to add necessary congestion variables
  int cwnd = MIN_CWND;
  int ssthresh = INIT_SSTHRESH;
  // ofstream to write to
  ofstream *fs;
} conn_t;

bool synPacket(packet_t &incomingPacket);
void ThreeWayHandshake(packet_t &incomingPacket, struct sockaddr &client);
void connToHeader(conn_t *pConn, packet_t &packet);
void setA(packet_t &packet, bool b);
void setS(packet_t &packet, bool b);
void setF(packet_t &packet, bool b);
bool getA(packet_t &packet);
bool getS(packet_t &packet);
bool getF(packet_t &packet);
unsigned int payloadSize(packet_t &packet);
void printPacketServer(packet_t &packet, conn_t *connection, bool recv);

// global variables
// vector to keep track of outstanding connections
vector <conn_t*> connections;
// file directory to save the files to
string file_directory;
unsigned int currentConn = 1;
int server_fd;
socklen_t addr_len = sizeof(struct sockaddr);

// client initiates with SYN, so need to wait to receive a SYN packet
// before sending back SYN/ACK (no payload)
// then, receive ACK from client, and start reading data
// create a connection struct to handle all of the meta data needed

// 1) 3 way handshake implementation via SYN
// single client
// 2) window tracking function
// keeping track of acks, sequences, cwnd, all of the stuff in TCP

int main(int argc, char *argv[])
{
  // incorrect number of arguments provided
  if (argc != 3) {
    cerr << "ERROR: Usage: " << argv[0] <<  " <PORT> <FILE-DIR>" << endl;
    exit(1);
  }

  int port = atoi(argv[1]);
  file_directory = argv[2];

  // struct to store metadata about location of incoming packet
  struct sockaddr client;
  // destination where data is received
  packet_t incomingPacket;
  // # of bytes received
  int recvNum;
  // indicates whether it is the start of a connection
  bool syn;

  // setting up server socket
  // check valid port number
  // within valid range
  // check negative values
  // check for values that are too large
  // 1 - 65535
  if (port < 1 || port > 65535) {
    // print out to stderr an error msg starting with "ERROR:" string
    cerr << "ERROR: Invalid port number inputted\n";
    // exit with non-zero exit code
    exit(1);
  }

  if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("server socket");
    exit(1);
  }
  
  struct addrinfo hints;
  memset(&hints, '\0', sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *myAddrInfo;
  int ret;
  if ((ret = getaddrinfo(NULL, argv[1], &hints, &myAddrInfo)) != 0)
  {
    cerr << "error" << endl;
    exit(1);
  }

  if (bind(server_fd, myAddrInfo->ai_addr, myAddrInfo->ai_addrlen) == -1)
  {
    cerr << "ERROR: bind()" << endl;
    exit(1);
  }

  while (true) {
    // reset contents of incoming packet to be emtpy
    incomingPacket = {0};

    // recvNum = size of datagram read in
    cerr << "Waiting\n";
    recvNum = recvfrom(server_fd, &incomingPacket, PACKET_SIZE, 0, &client, &addr_len); 
    cerr << "Received: " << recvNum << endl;
    
    // function that checks if it's a packet w SYN flag, ACK/FIN = 0
    syn = synPacket(incomingPacket);

    // conditional branch where the packet indicates a new connection
    if (syn) {
      // utilize 3 way handshake here
      ThreeWayHandshake(incomingPacket, client);
    }
    // conditonal branch for a packet with an existing connection
    else {
      cerr << "Didn't enter\n";
    }
  }
}

// checks the status of the SYN flag bit in header
bool synPacket(packet_t &incomingPacket) {
  char t = incomingPacket.flags;
  // want !A S & !F
  return !((t >> 2) & 0x1) && ((t >> 1) & 0x1) && !(t & 0x1);
}

// already know that the current buffer contains a SYN packet
// create a new connection
#include <iomanip>
void ThreeWayHandshake(packet_t &incomingPacket, struct sockaddr &client) {
  cerr << "Entered 3way\n";
  conn_t *newC = new conn_t;

  // indicates new connection
  if (incomingPacket.connectionID == 0) {
    newC->ID = currentConn;
    // now expecting the next byte
    newC->currentAck = incomingPacket.sequence + 1;
    // deep copy of where to send response stored in client
    newC->addr = client;
    currentConn++;
  } 
  // syn packet for an already existing connection
  else {
    // delete newly allocated connection, since it exists
    // potentially send an ACK to them
    delete newC;
    return;
  }

  // storing payload from client
  cerr << "Creating file\n";
  // creating a new file at the corresponding directory
  string fileNum = to_string(newC->ID);
  string path = file_directory + "/" + fileNum + ".file";

  ofstream *myF = new ofstream(path);
  newC->fs = myF;
  *myF << incomingPacket.payload;
  // can only read from the file after closing
  myF->close();

  // printing out recevied packet
  printPacketServer(incomingPacket, newC, true);

  // can now reuse buffer since all information has been extracted
  // clearing previous information for packet
  incomingPacket = {0};

  // sending response SYN-ACK
  connToHeader(newC, incomingPacket);
  setA(incomingPacket, true);
  setS(incomingPacket, true);
  setF(incomingPacket, false);

  sendto(server_fd, &incomingPacket, PACKET_SIZE, 0, &newC->addr, addr_len);

  // printing out sent packet
  printPacketServer(incomingPacket, newC, false);

  // add new connection 
  connections.push_back(newC);
}
// converts a connection struct to corresponding header in packet
void connToHeader(conn_t *pConn, packet_t &packet) {
  packet.sequence = pConn->currentSeq;
  packet.acknowledgment = pConn->currentAck;
  packet.connectionID = pConn->ID;
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

unsigned int payloadSize(packet_t &packet) {
  return strlen(packet.payload);
}

// printing out packets
// need to double check formatting
void printPacketServer(packet_t &packet, conn_t *connection, bool recv) {
  if (recv) {
    cout << "RECV ";
  }
  else {
    cout << "SEND ";
  }
  cout << packet.sequence << " " << packet.acknowledgment
  << " " << connection->ID << " " << connection->cwnd;
  
  if (getA(packet)) {
    cout << " ACK";
  }

  if (getS(packet)) {
    cout << " SYN";
  }

  if (getF(packet)) {
    cout << " FIN";
  }

  // add duplicate check at some point as well

  // end of print statement, append newline
  cout << endl;
}