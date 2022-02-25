#include <string>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>
#include <inttypes.h>
#include <unordered_map>
#include <cstring>

using namespace std;

int main(int argc, char **argv)
{

  // check arguments
  if (argc != 2)
  {
    cerr << "ERROR: Usage: " << argv[0] << " <PORT> " << endl;
    exit(1);
  }

  // UDP socket
  int serverSockFd = socket(AF_INET, SOCK_DGRAM, 0);

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

  if (bind(serverSockFd, myAddrInfo->ai_addr, myAddrInfo->ai_addrlen) == -1)
  {
    cerr << "ERROR: bind()" << endl;
    exit(1);
  }

  while (1)
  {
    char buf[1024]; // extra space to be safe
    struct sockaddr addr;
    socklen_t addr_len = sizeof(struct sockaddr);

    ssize_t length = recvfrom(serverSockFd, buf, 1024, 0, &addr, &addr_len);
    // string str(buf);
    cerr << "DATA received " << length << " bytes from : " << inet_ntoa(((struct sockaddr_in *)&addr)->sin_addr) << endl;

    length = sendto(serverSockFd, "ACK", strlen("ACK"), MSG_CONFIRM, &addr, addr_len);
    cout << length << " bytes ACK sent" << endl;
  }

  exit(0);
}