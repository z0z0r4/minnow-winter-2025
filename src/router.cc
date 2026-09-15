#include "router.hh"
#include "debug.hh"

#include <iostream>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  routing_table_.push_back( { route_prefix, prefix_length, next_hop, interface_num } );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  for ( size_t i = 0; i < interfaces_.size(); i++ ) {
    // Check interface
    auto interface = interfaces_[i];
    while ( !interface->datagrams_received().empty() ) {
      auto dgram = interface->datagrams_received().front();
      interface->datagrams_received().pop();

      // Find the best route for the datagram
      uint32_t dest_ip = dgram.header.dst;

      // TTL check
      if ( dgram.header.ttl <= 1 ) {
        continue;
      }
      dgram.header.ttl--;

      // Check routing table for the best match
      optional<tuple<uint32_t, uint8_t, optional<Address>, size_t>> best_rule;
      for ( const auto& rule : routing_table_ ) {
        uint8_t prefix_length = std::get<1>( rule );
        if ( !is_rule_match( rule, dest_ip ) ) {
          continue;
        }

        if ( best_rule.has_value() ) {
          // Compare prefix lengths to find the longest match
          if ( prefix_length <= std::get<1>( *best_rule ) ) {
            continue;
          }
        }

        // Replace best_rule
        best_rule = rule;
      }

      // Send to the next hop if a best rule was found
      if ( best_rule.has_value() ) {
        auto next_hop = std::get<2>( *best_rule );
        auto interface_num = std::get<3>( *best_rule );
        if ( interface_num >= interfaces_.size() ) {
          continue;
        }

        auto out_interface = interfaces_[interface_num];
        if ( next_hop.has_value() ) {
          out_interface->send_datagram( dgram, *next_hop );
        } else {
          out_interface->send_datagram( dgram, Address::from_ipv4_numeric( dest_ip ) );
        }
      } else {
        cerr << "DEBUG: No route found for datagram with destination IP "
             << Address::from_ipv4_numeric( dest_ip ).ip() << "\n";
      }
    }
  }
}

bool Router::is_rule_match( const tuple<uint32_t, uint8_t, optional<Address>, size_t>& rule,
                            uint32_t dest_ip ) const
{
  uint32_t route_prefix = std::get<0>( rule );
  uint8_t prefix_length = std::get<1>( rule );

  // Create a mask for the prefix length
  uint32_t mask = ( prefix_length == 0 ) ? 0 : ( ~0u << ( 32 - prefix_length ) );

  // Check if the destination IP matches the route prefix
  return ( dest_ip & mask ) == ( route_prefix & mask );
}