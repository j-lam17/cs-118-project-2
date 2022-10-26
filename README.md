# CS118 Project 2

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `tarball` target to create the submission file as well.

You will need to modify the `Makefile` to add your userid for the `.tar.gz` turn-in at the top of the file.

## Provided Files

`server.cpp` and `client.cpp` are the entry points for the server and client part of the project.

## Wireshark dissector

For debugging purposes, you can use the wireshark dissector from `tcp.lua`. The dissector requires
at least version 1.12.6 of Wireshark with LUA support enabled.

To enable the dissector for Wireshark session, use `-X` command line option, specifying the full
path to the `tcp.lua` script:

    wireshark -X lua_script:./confundo.lua

To dissect tcpdump-recorded file, you can use `-r <pcapfile>` option. For example:

    wireshark -X lua_script:./confundo.lua -r confundo.pcap

## PROJECT REPORT

Grace Ma (204881323)
- Focused on server
- Implemented out of order received packets on server end
- Implemented receive window and sequence number wrapping on server end
- Wrote payload to binary file 
- Implemented sighandler
- Overall debug on server side

Jonathan Lam (605415001)
- worked on both client and server
- Implemented 3 way handshake on both client and server
- Implemented FIN sequence for both client and server
- Organized connection struct for each connection to hold metadata individually
- wrote helper functions for dealing with packet structures and connection structures
- wrote printing out packet functions for server side
- Implemented timers for connection timeout, connection close, retransmission
- Implemented control flow for dealing with parallel connections

Desmond Andersen (605391825)

- Focused on client. Implemented host-to-network byte order conversion (and vice versa) and congestion window adjustment, along with RTO and retransmission.
- Also worked on (to a lesser degree of success) the client small file transfer, connection abortion, and responding with ACK for incoming FINs for 2 seconds.


_Design_

Client and server written in C++ using BSD sockets to communicate via UDP datagrams. The client establishes a three-way handshake with the server , then sends a file packaged in one or more UDP datagrams. Additionally the client uses a retransmission timer to resend any lost packets, and utilizes congestion control as to not overload the network. When client finishes sending the file it sends a FIN packet to initiate the shutting-down phase and close the connection. The server listens for incoming connections from clients and manages multiple connections. It saves any files recieved in a local directory. When recieving a FIN, the server will participate in the 4-way handwave to close the connection.

_Problems_

- During the file send process in the client, we kept getting strange ASCII characters at the end of the save files. To solve this, we had to first read the data from the file into a temporary buffer, then copy it into the payload. Also we initially were using `strlen` on the payload instead of `12 + <bytes-read>`.

- In the server we were initially using filestream instead of the binary file reader. We also ran into the issue that a stream has to be closed in order for the buffer to be flushed and the file to be readable.

- Ran into issues with using the autograder on M1 chip Macbooks. Queried Piazza and pulled Xinyu's latest updates to work with arm processors.
