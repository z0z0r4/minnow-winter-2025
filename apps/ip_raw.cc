#include "socket.hh"

#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <iostream>

using namespace std;

class RawSocket : public DatagramSocket
{
public:
  RawSocket() : DatagramSocket( AF_INET, SOCK_RAW, IPPROTO_RAW ) {}
};

uint64_t calculate_checksum( const uint8_t* data, size_t length )
{
  uint64_t sum = 0;

  // Calculate the checksum
  for (size_t i = 0; i < length; i += 2) {
    sum += (data[i] << 8) + (i + 1 < length ? data[i + 1] : 0);
  }

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  sum = ~sum;
  return sum;
}

int main()
{
  // construct an Internet or user datagram here, and send using the RawSocket as in the Jan. 10 lecture
  auto source ="10.0.0.1";
  auto destination = "10.0.0.2";
  Address source_addr( source );
  Address dest_addr ( destination );

  std::cout << "Sending raw socket message from " << source << " to " << destination << std::endl;
  
  auto raw_socket = RawSocket();

  // protocol number 5
  std::string payload = "Hello, this is a raw socket message!";

  std::cout << "Protocol number: 5" << std::endl;
  std::cout << "Payload: " << payload << std::endl;

  auto packet_size = sizeof( iphdr ) + payload.size();
  std::vector<uint8_t> packet(packet_size);
  iphdr *ip_header = reinterpret_cast<iphdr*>( packet.data() );

  // Fill in the IP header
  ip_header->version = 4; // IPv4
  ip_header->ihl = sizeof( iphdr ) / 4; // Header length in
  ip_header->tos = 0; // Type of service
  ip_header->tot_len = htons( packet_size ); // Total length
  ip_header->id = htons( 6657 ); // Identification
  ip_header->frag_off = 0;
  ip_header->ttl = 64; // Time to live
  ip_header->protocol = 5; // Protocol number
  ip_header->check = 0; // Checksum (will be calculated later)
  ip_header->saddr = inet_addr( source ); // Source IP address
  ip_header->daddr = inet_addr( destination ); // Destination IP address

  auto checksum = calculate_checksum( reinterpret_cast<const uint8_t*>(ip_header), sizeof( iphdr ) );

  ip_header->check = static_cast<uint16_t>(checksum);
  
  memcpy( packet.data() + sizeof( iphdr ), payload.data(), payload.size() );

  std::cout << "Sending raw socket message from " << source << " to " << destination << std::endl;
  raw_socket.sendto( dest_addr, std::string_view(reinterpret_cast<const char*>(packet.data()), packet.size()) );
  std::cout << "Raw socket message sent successfully!" << std::endl;

  // protocol number 17
  std::string udp_payload = "Hello UDP via raw socket!";
  
  std::cout << "Protocol number: 17" << std::endl;
  std::cout << "Payload: " << udp_payload << std::endl;

  auto udp_len = sizeof(udphdr) + udp_payload.size();
  auto packet17_size = sizeof(iphdr) + udp_len;
  std::vector<uint8_t> packet17(packet17_size);

  iphdr *ip_header17 = reinterpret_cast<iphdr*>( packet17.data() );
  udphdr *udp_header = reinterpret_cast<udphdr*>( packet17.data() + sizeof(iphdr) );

  // Fill in the IP header for protocol 17
  ip_header17->version = 4; // IPv4
  ip_header17->ihl = sizeof( iphdr ) / 4; // Header length in
  ip_header17->tos = 0; // Type of service
  ip_header17->tot_len = htons( packet17_size ); // Total length
  ip_header17->id = htons( 6657 ); // Identification
  ip_header17->frag_off = 0;
  ip_header17->ttl = 64; // Time to live
  ip_header17->protocol = 17; // Protocol number
  ip_header17->check = 0; // Checksum (will be calculated later)
  ip_header17->saddr = inet_addr( source ); // Source IP address
  ip_header17->daddr = inet_addr( destination ); // Destination IP address

  // Calculate the checksum for the IP header
  auto checksum17 = calculate_checksum( reinterpret_cast<const uint8_t*>(ip_header17), sizeof( iphdr ) );
  ip_header17->check = static_cast<uint16_t>(checksum17);

  // Fill in the UDP header
  udp_header->source = htons( 12345 ); // Source port
  udp_header->dest = htons( 54321 ); // Destination port
  udp_header->len = htons( udp_len ); // UDP length
  udp_header->check = 0; // Checksum (optional for UDP, set to 0)
  
  memcpy( packet17.data() + sizeof( iphdr ) + sizeof( udphdr ), udp_payload.data(), udp_payload.size() );

  std::cout << "Sending raw socket message from " << source << " to " << destination << std::endl;
  raw_socket.sendto( dest_addr, std::string_view(reinterpret_cast<const char*>(packet17.data()), packet17.size()) );
  std::cout << "Raw socket message sent successfully!" << std::endl;

  return 0;
}
