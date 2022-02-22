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

using namespace std;

#define MAX_SIZE 524
#define MAX_ACK 102400
#define MIN_CWND 512
#define MAX_CWND 51200
#define RWND 51200
#define Init_SSTHRESH 10000

bool socket_setup(int port);

int main(int argc, char *argv[])
{
  int port = atoi(argv[1]);
  socket_setup(port);
}

bool socket_setup(int port) {
  cout << "port num: " << port << endl;
  return true;
}
