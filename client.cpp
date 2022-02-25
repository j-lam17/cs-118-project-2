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

#include <iostream>
#include <string>
#include <list>
#include <sys/stat.h>

using namespace std;

int main(int argc, char **argv)
{
  // Check if received three command arguments
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

  // Open file to transfer from client to server
  // - argv[3]: FILENAME
  int fileToTransferFd = open(argv[3], O_RDONLY);
  if (fileToTransferFd == -1)
  {
    cerr << "ERROR: open()" << endl;
    exit(1);
  }

  struct stat fdStat;
  fstat(fileToTransferFd, &fdStat);
  uint8_t fileBuffer[fdStat.st_size];
  size_t bytesRead = read(fileToTransferFd, fileBuffer, fdStat.st_size);
  cout << bytesRead << " bytes read" << endl;

  sendto(serverSockFd, fileBuffer, bytesRead, MSG_CONFIRM, serverSockAddr, serverSockAddrLength);

  cout << "DATA sent" << endl;

  struct sockaddr addr;
  socklen_t addr_len = sizeof(struct sockaddr);
  memset(fileBuffer, 0, sizeof(fileBuffer));
  ssize_t length = recvfrom(serverSockFd, fileBuffer, fdStat.st_size, 0, &addr, &addr_len);
  string str((char *)fileBuffer);
  cerr << "ACK reveived " << length << " bytes: " << endl
       << str << endl;

  close(fileToTransferFd);
  exit(0);
}