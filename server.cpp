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
#include <unordered_map>
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
  // keeping track of client ID
  unsigned short ID;
  // initialize current sequence number to 4321
  unsigned int currentSeq = INIT_SEQ;
  unsigned int currentAck; 
  // need to add necessary congestion variables
  unsigned int cwnd = MIN_CWND;
  unsigned int ssthresh = INIT_SSTHRESH;
  // ofstream to write to
  ofstream *fs;
  // waiting for final ACK from client to completely close
  bool waitingForAck = false;
} conn_t;

typedef struct payload_t {
  unsigned int sequence;
  unsigned int length;
  char payload[512];
}

bool synPacket(packet_t &incomingPacket);
bool ackPacket(packet_t &incomingPacket);
bool finPacket(packet_t &incomingPacket);
void ThreeWayHandshake(packet_t &incomingPacket, struct sockaddr &client);
void finHandshake(packet_t &incomingPacket);
void connToHeader(conn_t *connection, packet_t &packet);
void setA(packet_t &packet, bool b);
void setS(packet_t &packet, bool b);
void setF(packet_t &packet, bool b);
bool getA(packet_t &packet);
bool getS(packet_t &packet);
bool getF(packet_t &packet);
unsigned int payloadSize(packet_t &packet);
void printPacketServer(packet_t &packet, conn_t *connection, bool recv);
void dropPacketServer(packet_t &packet);
void appendPayload(packet_t &packet, conn_t *connection);

// global variables
// vector to keep track of outstanding connections
unordered_map<short, conn_t*> connections;
// file directory to save the files to
string file_directory;
unsigned int currentConn = 1;
int server_fd;
socklen_t addr_len = sizeof(struct sockaddr);

//OOO delivery global variables
unsigned int rcvbuf = 0;
unsigned int ACK = 12345;
unsigned int last_byte_read = 12345;
unordered_map<unsigned int, payload_t*> 

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
  bool syn, fin;

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
    client = {0};
    // recvNum = size of datagram read in
    cerr << "Waiting\n";
    recvNum = recvfrom(server_fd, &incomingPacket, PACKET_SIZE, 0, &client, &addr_len); 
    cerr << "Received: " << recvNum << endl;
    
    // function that checks if it's a packet w SYN flag, ACK/FIN = 0
    syn = synPacket(incomingPacket);
    fin = finPacket(incomingPacket);
    // conditional branch where the packet indicates a new connection
    if (syn) {
      // utilize 3 way handshake here
      ThreeWayHandshake(incomingPacket, client);
    }
    else if (fin) {
      // start closing of a connection
      finHandshake(incomingPacket);
    }
    // conditonal branch for a packet without SYN flag
    else {
      // REVISE LATER
      // matching connection found
      if (connections.find(incomingPacket.connectionID) != connections.end()) {
        appendPayload(incomingPacket, connections[incomingPacket.connectionID]);
      }
      // no matching connection for the packet, so discard it
      else {
        // discard
        dropPacketServer(incomingPacket);
      }
    }
  }
}

// checks the status of the SYN flag bit in header
bool synPacket(packet_t &incomingPacket) {
  char t = incomingPacket.flags;
  // want !A S & !F
  return !((t >> 2) & 0x1) && ((t >> 1) & 0x1) && !(t & 0x1);
}

bool finPacket(packet_t &incomingPacket) {
  char t = incomingPacket.flags;
  // want !A !S F
  return !((t >> 2) & 0x1) && !((t >> 1) & 0x1) && (t & 0x1);
}

bool ackPacket(packet_t &incomingPacket) {
  char t = incomingPacket.flags;

  // want A !S !F
  return ((t >> 2) & 0x1) && !((t >> 1) & 0x1) && !(t & 0x1);
}

// already know that the current buffer contains a SYN packet
// create a new connection
void ThreeWayHandshake(packet_t &incomingPacket, struct sockaddr &client) {
  cerr << "Entered 3way\n";
  conn_t *newC = new conn_t;


  // indicates new connection
  if (incomingPacket.connectionID == 0) {
    // printing out received packet
    printPacketServer(incomingPacket, newC, true);
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
    // Drop packet since it has a nonzero connection number
    dropPacketServer(incomingPacket);
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

  // THIS WAS JUST FOR TESTING, no payload in initial SYN packet
  // *myF << incomingPacket.payload;
  // can only read from the file after closing
  // myF->close();

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

  // update next sequence number to be + 1
  newC->currentSeq++;

  // add new connection 
  connections[newC->ID] = newC;
}

// initiate the closing of an existing connection after receiving the FIN packet
void finHandshake(packet_t &incomingPacket) {
  // check that the connection exists
  unsigned short ID = incomingPacket.connectionID;
  // discard packet since no corresponding connection
  if (connections.find(ID) == connections.end()) {
    dropPacketServer(incomingPacket);
    return;
  }
  // need to find the corresponding connection
  // indicate that it's in the process of being closed
  else {
    conn_t *connection = connections[ID];

    // checking that the current sequence matches the last sent ack
    if (connection->currentAck != incomingPacket.sequence) {
      // need to resend previous ack so client knows that the server hasn't
      // received all of the necessary bytes yet
      return;
    }
    
    // set a variable to the connection / start a timer or something
    // whatever is necessary to close the connection
    // indicate that it is waiting for an ACK to close
    // start a timer to potentially resend FIN/ACK packet after 0.5s if it doesn't get ACK from client
    connection->waitingForAck = true;
    // ASSUMING THAT THERE IS NO OUTSTANDING MISSING PACKETS / BYTES
    // NOTHING IN OUT OF ORDER BUFFER LEFT
    
    // assemble response ACK/FIN packet
    incomingPacket = {0};
    // don't need to update seqnum for fin|ack
    // connection->currentSeq = same
    incomingPacket.sequence = connection->currentSeq;
    connection->currentAck = (connection->currentAck + 1) % (MAX_ACK + 1);
    incomingPacket.acknowledgment = connection->currentAck;
    incomingPacket.connectionID = ID;
    setA(incomingPacket, true);
    setS(incomingPacket, false);
    setF(incomingPacket, true);
    
    // send the packet response
    sendto(server_fd, &incomingPacket, PACKET_SIZE, 0, &connection->addr, addr_len);

    // printing out the packet response
    printPacketServer(incomingPacket, connection, false);
  }
}

// converts a connection struct to corresponding header in packet
void connToHeader(conn_t *connection, packet_t &packet) {
  packet.sequence = connection->currentSeq;
  packet.acknowledgment = connection->currentAck;
  packet.connectionID = connection->ID;
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
  cout << "seq#: " << packet.sequence << " ack #: " << packet.acknowledgment
  << " conn_id: " << connection->ID << " cwnd: " << connection->cwnd;
  
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

// print necessary string for dropped packet
void dropPacketServer(packet_t &packet) {
  string msg = "DROP ";
  msg += packet.sequence;
  msg += " ";
  msg += packet.acknowledgment;
  msg += " ";
  msg += packet.connectionID;
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

void appendPayload(packet_t &packet, conn_t *connection) {
  // print out the received packet
  printPacketServer(packet, connection, true);

  int len = payloadSize(packet);

  //handling overflowing rwnd:
  if ((packet.sequence + len) < last_byte_read){
    last_byte_read = packet.sequence + len
  }

  if ((last_byte_read - ACK) > rwnd) {
    dropPacketServer(packet);
    return;
  }

  //check if wrapping occurs:
  if (last_byte_read > MAX_ACK){
    last_byte_read = last_byte_read % MAX_ACK;
  }

  // 1. In Order Delivery:
  if (packet.sequence == connection->currentAck) {
    // add a check to see if it's the FIN-ACK packet to close a connection
    // condition where it's an ACK packet and the connection has been waiting for one

    //enter when client has sent back final FIN-ACK statement
    if (ackPacket(packet) && connection->waitingForAck) {
      // need to close file pointer
      connection->fs->close();
      cerr << "Closed connection " << connection->ID << endl;
      // remove from the connections hash table
      connections.erase(connection->ID);
      // free allocated resources
      delete connection;
      return;
    }
    // append payload to existing file
    *connection->fs << packet.payload;
    // (*connection->fs).close(); // close to just see if it worked
    // update current Seq & Ack Num for the connection
    // seqNum only increments on SYN and FIN, not for ACK
    // connection->currentSeq - same
    connection->currentAck = (connection->currentAck + len) % (MAX_ACK + 1); // change next expected byte
    
    // create packet to send back acknowledgement
    packet = {0};
    packet.sequence = connection->currentSeq;
    packet.acknowledgment = connection->currentAck;
    packet.connectionID = connection->ID;
    setA(packet, true);
    setS(packet, false);
    setF(packet, false);

    // send the packet to the respective client
    sendto(server_fd, &packet, PACKET_SIZE, 0, &connection->addr, addr_len);

    // need to print out packet sent
    printPacketServer(packet, connection, false);
  }
  //2. Out of Order Delivery:
  else {
    
  }
}