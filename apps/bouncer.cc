#include "socket.hh"
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <span>
#include <string>

using namespace std;

void bouncer( int port )
{
  UDPSocket s_sock;
  UDPSocket c_sock;
  s_sock.bind( Address { "0.0.0.0", to_string( port ) } );
  c_sock.bind( Address { "0.0.0.0", to_string( port + 1 ) } );

  s_sock.set_blocking( false );
  c_sock.set_blocking( false );

  // 缓存两个方向的对端地址
  bool have_s = false;
  bool have_c = false;
  Address s_addr { "0.0.0.0", "0" };
  Address c_addr { "0.0.0.0", "0" };

  cerr << "[*] Local Bouncer ready on ports " << port << " & " << port + 1 << "\n";

  std::array<pollfd, 2> fds {};
  fds[0].fd = s_sock.fd_num();
  fds[0].events = POLLIN;
  fds[1].fd = c_sock.fd_num();
  fds[1].events = POLLIN;

  while ( true ) {
    poll( fds.data(), 2, -1 ); // 阻塞直到有 socket 可读

    if ( (fds[0].revents & POLLIN) != 0 ) {
      Address src { "0.0.0.0", "0" };
      string data;
      s_sock.recv( src, data );
      s_addr = src;
      have_s = true;
      if ( have_c && !data.empty() ) {
        c_sock.sendto( c_addr, data );
      }
    }

    if ( (fds[1].revents & POLLIN) != 0 ) {
      Address src { "0.0.0.0", "0" };
      string data;
      c_sock.recv( src, data );
      c_addr = src;
      have_c = true;
      if ( have_s && !data.empty() ) {
        s_sock.sendto( s_addr, data );
      }
    }
  }
}

auto main( int argc, char* argv[] ) -> int
{
  try {
    if ( argc <= 0 ) {
      abort();
    }
    auto args = span( argv, argc );

    if ( argc > 2 ) {
      cerr << "Usage: " << args.front() << " [port]\n";
      cerr << "Default port is 3000 (server); client uses 3001.\n";
      return EXIT_FAILURE;
    }

    const int port = ( argc == 2 ) ? stoi( args[1] ) : 3000;
    bouncer( port );
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}