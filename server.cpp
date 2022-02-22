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
#include <cstring>

using namespace std;

#define MAX_SIZE 524
#define MAX_ACK 102400
#define MIN_CWND 512
#define MAX_CWND 51200
#define RWND 51200
#define INIT_SSTHRESH 10000
#define INIT_SEQ 4321
#define INIT_ACK 0

// user defined structs
typedef struct header_t {
  unsigned int seqNum;
  unsigned int ackNum;
  unsigned short connID;
  bool A;
  bool S;
  bool F;
} header_t;

void socket_setup(int port, int &server_fd, struct sockaddr_in &server, int len);
void assemble_header(header_t *h, char *header);
void parse_header(header_t *h, char *buf);
void ThreeWayHandshake();
// client initiates with SYN, so need to wait to receive a SYN packet
// before sending back SYN/ACK (no payload)
// then, receive ACK from client, and start reading data
// create a connection struct to handle all of the meta data needed

// 1) 3 way handshake implementation via SYN
// single client
// 2) window tracking function
// keeping track of acks, sequences, cwnd, all of the stuff in TCP
// 
int main(int argc, char *argv[])
{
  int port = atoi(argv[1]);

  int server_fd;
  struct sockaddr_in server;
  int len = sizeof(struct sockaddr_in);
  socket_setup(port, server_fd, server, len);
}

// always assume the folder is correct
void socket_setup(int port, int &server_fd, struct sockaddr_in &server,
  int len) {
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

  if ((server_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("server socket");
    exit(1);
  }
  
  // INADDR_ANY allows server to accept connections from any interface
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_family = AF_INET;
  server.sin_port = htons(port); // desired port is given from input variable
  memset(&server.sin_zero, '\0', sizeof(server.sin_zero));

  // binding server to socket
  if (bind(server_fd, (struct sockaddr *) &server, sizeof(server)) == -1) {
    perror("server socket bind");
    exit(1);
  }

}

void assemble_header(header_t *h, char *header) {
    // clear any previous header data
    // always 12 bytes of data
    memset(header, 0, 12);

    // formatting sequence #
    unsigned int seqNum = h->seqNum;
    header[0] = (seqNum >> 24) & 0xFF;
    header[1] = (seqNum >> 16) & 0xFF;
    header[2] = (seqNum >> 8) & 0xFF;
    header[3] = (seqNum) & 0xFF;

    // formatting ack #
    unsigned int ackNum = h->ackNum;
    header[4] = (ackNum >> 24) & 0xFF;
    header[5] = (ackNum >> 16) & 0xFF;
    header[6] = (ackNum >> 8) & 0xFF;
    header[7] = (ackNum) & 0xFF;

    // formatting the connection ID
    unsigned short connID = h->connID;
    header[8] = (connID >> 8) & 0xFF;
    header[9] = (connID) & 0xFF;

    // formatting flag bits
    // byte 10 was already zerod out from initialzation
    char flag = 0;
    bool A = h->A;
    bool S = h->S;
    bool F = h->F;
    flag |= (A << 2);
    flag |= (S << 1);
    flag |= (F);
    header[11] = flag;

    // No need to return anything, since update happens in place
  }

  void parse_header(header_t *h, char *buf) {
    // assuming that the first 12 bytes of received message contain header
    unsigned int tmp = 0;
    // store seqNum
    tmp |= (buf[0] << 24);
    tmp |= (buf[1] << 16);
    tmp |= (buf[2] << 8);
    tmp |= (buf[3]);
    h->seqNum = tmp;

    // store ackNum
    tmp = 0;
    tmp |= (buf[4] << 24);
    tmp |= (buf[5] << 16);
    tmp |= (buf[6] << 8);
    tmp |= (buf[7]);
    h->ackNum = tmp;

    // store connID
    tmp = 0;
    tmp |= (buf[8] << 8);
    tmp |= (buf[9]);
    h->connID = tmp;

    // storing flags
    tmp = buf[11];
    h->A = tmp & 0x04;
    h->S = tmp & 0x02;
    h->F = tmp & 0x01;
    
    // No need to return anything, since update happens in place
  }