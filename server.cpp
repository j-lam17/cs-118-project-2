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
#include <chrono>

using namespace std;
using namespace std::this_thread;
using namespace std::chrono;

#define MAX_ACK 102400
#define MIN_CWND 512
#define MAX_CWND 51200
#define RWND 51200
#define INIT_SSTHRESH 10000
#define INIT_SEQ 4321
#define PACKET_SIZE 524
#define CLOCKID CLOCK_MONOTONIC
#define SIG SIGRTMIN

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
  // indicate that you have received data and to deactivate corresponding timer
  // 10s window always active for life of the connection, not just starting connection
  // bool data = false;
  // timer variables, only one timer at a time
  timer_t *ptrTimerid;
  struct sigevent *ptrSev;
  struct itimerspec *ptrIts;
} conn_t;

typedef struct payload_t {
  unsigned int sequence;
  unsigned int length;
  char payload[512];
} payload_t; 

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
// function for closing a connection after 10s without receiving data
static void activeDataHandler(union sigval val);
static void finalAckHandler(union sigval val);
static void sigHandler(int signum);
void recvPacket(packet_t &packet);
void sendPacket(packet_t &packet);

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
unordered_map<unsigned int, unsigned int> bytes_recieved; //key = seq#, val = packet length

unordered_map<unsigned int, payload_t*> payloads; //key = seq#, val = payload struct


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



  // intializing signal handlers
  signal(SIGQUIT, sigHandler);
  signal(SIGTERM, sigHandler);


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
    int recvNum = recvfrom(server_fd, &incomingPacket, PACKET_SIZE, 0, &client, &addr_len); 
    // cerr << "Received: " << recvNum << endl;

    // process packet
    recvPacket(incomingPacket);
    
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
  
  // Creating client's timer for 10s to close if no data is received
  newC->ptrTimerid = new timer_t;
  newC->ptrSev = new struct sigevent;
  newC->ptrIts = new struct itimerspec;

  // Create the timer
  union sigval arg;
  arg.sival_int = newC->ID;

  // initializing what to do when the signal occurs
  newC->ptrSev->sigev_notify = SIGEV_THREAD;
  newC->ptrSev->sigev_notify_function = activeDataHandler;
  newC->ptrSev->sigev_notify_attributes=NULL;
  newC->ptrSev->sigev_value = arg;
  timer_create(CLOCKID, newC->ptrSev, newC->ptrTimerid);
      
  newC->ptrIts->it_value.tv_sec = 10;
  newC->ptrIts->it_value.tv_nsec = 0;
  newC->ptrIts->it_interval.tv_sec = 0;
  newC->ptrIts->it_interval.tv_nsec = 0;

  // arming the timer
  timer_settime(*newC->ptrTimerid, 0, newC->ptrIts, NULL);

  // storing payload from client
  cerr << "Creating file\n";
  // creating a new file at the corresponding directory
  string fileNum = to_string(newC->ID);
  string path = file_directory + "/" + fileNum + ".file";

  ofstream *myF = new ofstream(path);
  newC->fs = myF;

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

  // processing to send
  sendPacket(incomingPacket);
  
  // change response length to only be 12
  sendto(server_fd, &incomingPacket, 12, 0, &newC->addr, addr_len);

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
    
    // indicate that it is waiting for an ACK to close
    connection->waitingForAck = true;
    // ASSUMING THAT THERE IS NO OUTSTANDING MISSING PACKETS / BYTES
    // NOTHING IN OUT OF ORDER BUFFER LEFT
    
    // assemble response ACK/FIN packet
    incomingPacket = {0};
    // don't need to update seqnum for fin|ack
    connection->currentAck = (connection->currentAck + 1) % (MAX_ACK + 1);
    connToHeader(connection, incomingPacket);
    setA(incomingPacket, true);
    setS(incomingPacket, false);
    setF(incomingPacket, true);

    // printing out the packet response
    printPacketServer(incomingPacket, connection, false);

    // processing to send
    sendPacket(incomingPacket);
    
    // send the packet response
    sendto(server_fd, &incomingPacket, PACKET_SIZE, 0, &connection->addr, addr_len);
    
    // Setting necessary timer to wait for final ACK from client
    // need to delete previous timer for 10s data
    timer_delete(*connection->ptrTimerid);
    // updating handler function
    connection->ptrSev->sigev_notify_function = finalAckHandler;
    // create a new timer object
    timer_create(CLOCKID, connection->ptrSev, connection->ptrTimerid);
    // arm the timer
    connection->ptrIts->it_value.tv_sec = 2;
    connection->ptrIts->it_value.tv_nsec = 0;
    connection->ptrIts->it_interval.tv_sec = 2;
    connection->ptrIts->it_interval.tv_nsec = 0;

    timer_settime(*connection->ptrTimerid, 0, connection->ptrIts, NULL);

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
  string msg = "";
  if (recv) {
    msg += "RECV ";
  }
  else {
    msg += "SEND ";
  }
  msg += to_string(packet.sequence);
  msg += " ";
  msg += to_string(packet.acknowledgment);
  msg += " ";
  msg += to_string(connection->ID);
  /* Not needed for server side
  msg += " ";
  msg += to_string(connection->cwnd);
  */
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

  // add duplicate check at some point as well

  // end of print statement, append newline
  cout << msg;
}

// print necessary string for dropped packet
void dropPacketServer(packet_t &packet) {
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

void appendPayload(packet_t &packet, conn_t *connection) {
  // print out the received packet
  // printPacketServer(packet, connection, true);

  int len = payloadSize(packet);

  //handling overflowing rwnd:
  unsigned int last_byte = packet.sequence + len;

  if ((last_byte - ACK) > RWND) {
    cerr << "OVERFLOW" << endl;
    dropPacketServer(packet);

    // create packet to send back acknowledgement
    //since dropped packet, nothing happens
    packet = {0};
    connToHeader(connection, packet);
    setA(packet, true);
    setS(packet, false);
    setF(packet, false);

    // need to print out packet sent
    printPacketServer(packet, connection, false);

    // processing to send
    sendPacket(packet);

    // send the packet to the respective client
    sendto(server_fd, &packet, PACKET_SIZE, 0, &connection->addr, addr_len);

    
    return;
  }
  else if (last_byte > last_byte_read){//not overflow
    last_byte_read = last_byte;

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
      // free ofstream
      delete connection->fs;
      // free timer
      timer_delete(*connection->ptrTimerid);
      delete connection->ptrTimerid;
      delete connection->ptrSev;
      delete connection->ptrIts;
      // free connection
      delete connection;
      return;
    }
    // need to update this to reset timer whenever a packet is received
    // reset the timer delay to 10s
    timer_settime(*connection->ptrTimerid, 0, connection->ptrIts, NULL);
    
    // append payload to existing file
    *connection->fs << packet.payload;

    // update current ACK, seqNum only increments on SYN and FIN, not for ACK
    connection->currentAck = (connection->currentAck + len) % (MAX_ACK + 1); // change next expected byte
    
    //check if in order packet fills a gap
    while (payloads.find(connection->currentAck) != payloads.end()){ //while there's an OOO packet that is now ready
      int packet_sequence = connection->currentAck;
      int packet_length = bytes_recieved[packet_sequence];

      connection->currentAck = (connection->currentAck + packet_length) % (MAX_ACK + 1);

      rcvbuf -= packet_length;

      char payload_to_fill[512];

      strcpy(payload_to_fill, payloads[packet_sequence]->payload);

      *connection->fs << payload_to_fill;

      //remove entry from bytes_to_read:
      bytes_recieved.erase(packet_sequence);

      // remote entry from payloads:
      payloads.erase(packet_sequence);

    }
    // create packet to send back acknowledgement
    packet = {0};
    connToHeader(connection, packet);
    setA(packet, true);
    setS(packet, false);
    setF(packet, false);

    // need to print out packet sent
    printPacketServer(packet, connection, false);

    // processing to send
    sendPacket(packet);

    // send the packet to the respective client
    sendto(server_fd, &packet, 12, 0, &connection->addr, addr_len);
  }
  //2. Out of Order Delivery:
  else {

    cerr << "OOO delivery! " << packet.sequence << endl;

    payload_t *newPayload = new payload_t;
    newPayload -> sequence = packet.sequence;
    newPayload -> length = len;
    strcpy(newPayload->payload, packet.payload);

    //add OOO bytes interval to dictionary
    bytes_recieved[newPayload -> sequence] = newPayload -> length;

    //add payload to payload dictionary
    payloads[newPayload -> sequence] = newPayload;

    //send back duplicate ack
    packet = {0};
    connToHeader(connection, packet);
    setA(packet, true);
    setS(packet, false);
    setF(packet, false);

    // need to print out packet sent
    printPacketServer(packet, connection, false);

    // processing to send
    sendPacket(packet);

    // send the packet to the respective client
    sendto(server_fd, &packet, 12, 0, &connection->addr, addr_len);
    
  }
}

static void
activeDataHandler(union sigval val) {
  cerr << "In data handler\n";
  // find the existing connection
  conn_t *connection = connections[val.sival_int];
  // write single ERROR string to the file stream 
  *connection->fs << "ERROR";
  // close the file stream
  connection->fs->close();
  // remove the connection from the unordered_map
  connections.erase(connection->ID);
  // free ofstream
  delete connection->fs;
  // destroy timer
  timer_delete(*connection->ptrTimerid);
  // free objects
  delete connection->ptrTimerid;
  delete connection->ptrSev;
  delete connection->ptrIts;
  // free memory
  delete connection;
}

static void
finalAckHandler(union sigval val) {
  cerr << "In ack handler\n";
  // find the existing connection
  conn_t *connection = connections[val.sival_int];
  // need to retransmit the previous fin packet
  packet_t packet;
  connToHeader(connection, packet);
  setA(packet, true);
  setS(packet, false);
  setF(packet, true);
  // print out the packet
  printPacketServer(packet, connection, false);
}

static void sigHandler(int signum) {
  cerr << "Caught signal\n";
  exit(0);
}

void recvPacket(packet_t &packet) {
  packet.sequence = ntohl(packet.sequence);
  packet.acknowledgment = ntohl(packet.acknowledgment);
  packet.connectionID = ntohs(packet.connectionID);
}
void sendPacket(packet_t &packet) {
  packet.sequence = htonl(packet.sequence);
  packet.acknowledgment = htonl(packet.acknowledgment);
  packet.connectionID = htons(packet.connectionID);
}