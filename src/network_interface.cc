#include <iostream>

#include "address.hh"
#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "ipv4_datagram.hh"
#include "network_interface.hh"
#include "parser.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // IP -> ethernet frame
  auto arp_entry = arp_cache_.find( next_hop.ipv4_numeric() );
  if ( arp_entry == arp_cache_.end() ) {
    // find retry table entry
    auto retry_entry = arp_request_time_.find( next_hop.ipv4_numeric() );
    if ( retry_entry == arp_request_time_.end() || now - retry_entry->second >= 5000 ) {
      // ARP Cache miss, send ARP request
      send_arp_request( next_hop );
      arp_request_time_[next_hop.ipv4_numeric()] = now;
    }

    // push into queue
    ip_frames_out_.push( { dgram, next_hop, now } );
    return;
  }

  // ARP Cache hit, send the datagram
  send_eth_datagram( dgram, arp_entry->second.address );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST ) {
    return; // Drop
  }

  if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    // Handle ARP frame
    Parser parser { frame.payload };
    ARPMessage arp_message;
    arp_message.parse( parser );

    if ( arp_message.opcode == ARPMessage::OPCODE_REQUEST ) {
      if ( arp_message.target_ip_address != ip_address_.ipv4_numeric() ) {
        return; // Drop
      }
      // Learn the sender's mapping
      arp_cache_[arp_message.sender_ip_address] = { arp_message.sender_ethernet_address, now };

      // Send ARP reply
      send_arp_reply( arp_message.sender_ethernet_address,
                      Address::from_ipv4_numeric( arp_message.sender_ip_address ) );
    } else if ( arp_message.opcode == ARPMessage::OPCODE_REPLY ) {
      if ( arp_message.target_ip_address != ip_address_.ipv4_numeric()
           || arp_message.target_ethernet_address != ethernet_address_ ) {
        return;
      }

      // Learn the sender's mapping
      arp_cache_[arp_message.sender_ip_address] = { arp_message.sender_ethernet_address, now };
      auto arp_request_time_entry = arp_request_time_.find( arp_message.sender_ip_address );
      if ( arp_request_time_entry != arp_request_time_.end() ) {
        arp_request_time_.erase( arp_request_time_entry );
      }

      // Process queued datagrams waiting for ARP resolution
      size_t queue_size = ip_frames_out_.size();
      for ( size_t i = 0; i < queue_size; ++i ) {
        auto [dgram, next_hop, enqueue_time] = ip_frames_out_.front();
        if ( next_hop.ipv4_numeric() == arp_message.sender_ip_address ) {
          // ARP Cache hit, send the datagram
          send_eth_datagram( dgram, arp_message.sender_ethernet_address );
        } else {
          // ARP Cache miss, keep it in the queue
          if ( now - enqueue_time <= 5000 ) {
            ip_frames_out_.push( { dgram, next_hop, enqueue_time } );
          }
        }
        ip_frames_out_.pop();
      }
    }
  } else if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    if ( frame.header.dst != ethernet_address_ ) {
      return;
    }

    // Handle IPv4 frame
    Parser parser( frame.payload );
    InternetDatagram internet_datagram;
    internet_datagram.parse( parser );
    datagrams_received_.push( internet_datagram );
  } else {
    debug( "Ignored received unknown type frame." );
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  now += ms_since_last_tick;

  // Update ARP request timers
  for ( auto& entry : arp_request_time_ ) {
    if ( now - entry.second >= 5000 ) {
      // Send ARP request again if it has been more than 5 seconds since the last request
      send_arp_request( Address::from_ipv4_numeric( entry.first ) );
      entry.second = now; // reset timer for this ARP request
    }
  }

  for ( auto it = arp_cache_.begin(); it != arp_cache_.end(); ) {
    if ( now - it->second.cache_time >= 30000 ) {
      it = arp_cache_.erase( it );
    } else {
      ++it;
    }
  }

  // Process queued datagrams waiting for ARP resolution
  std::queue<PendingDatagram> keep;
  while ( !ip_frames_out_.empty() ) {
    auto item = ip_frames_out_.front();
    ip_frames_out_.pop();
    if ( now - item.enqueue_time <= 5000 ) {
      keep.push( std::move( item ) );
    }
  }
  ip_frames_out_ = std::move( keep );
}

void NetworkInterface::send_arp_request( const Address& next_hop )
{
  ARPMessage arp_request;
  arp_request.opcode = ARPMessage::OPCODE_REQUEST;
  arp_request.sender_ethernet_address = ethernet_address_;
  arp_request.sender_ip_address = ip_address_.ipv4_numeric();
  // Default 00:00:00:00:00:00
  // arp_request.target_ethernet_address = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
  arp_request.target_ip_address = next_hop.ipv4_numeric();

  // send ARP request
  Serializer arp_serializer;
  arp_request.serialize( arp_serializer );
  EthernetFrame arp_frame;
  arp_frame.header.src = ethernet_address_;
  arp_frame.header.dst = ETHERNET_BROADCAST;
  arp_frame.header.type = EthernetHeader::TYPE_ARP;
  arp_frame.payload = arp_serializer.finish();

  transmit( arp_frame );
}

void NetworkInterface::send_arp_reply( const EthernetAddress& target_eth_address, const Address& target_ip_address )
{
  ARPMessage arp_request;
  arp_request.opcode = ARPMessage::OPCODE_REPLY;
  arp_request.sender_ethernet_address = ethernet_address_;
  arp_request.sender_ip_address = ip_address_.ipv4_numeric();
  arp_request.target_ethernet_address = target_eth_address;
  arp_request.target_ip_address = target_ip_address.ipv4_numeric();

  // send ARP reply
  Serializer serializer;
  arp_request.serialize( serializer );
  EthernetFrame eth_frame;
  eth_frame.header.src = ethernet_address_;
  eth_frame.header.dst = target_eth_address;
  eth_frame.header.type = EthernetHeader::TYPE_ARP;
  eth_frame.payload = serializer.finish();

  transmit( eth_frame );
}

void NetworkInterface::send_eth_datagram( const InternetDatagram& dgram,
                                          const EthernetAddress& dst_ethernet_address )
{
  EthernetFrame frame;
  frame.header.src = ethernet_address_;
  frame.header.type = EthernetHeader::TYPE_IPv4;
  frame.header.src = ethernet_address_;
  frame.header.dst = dst_ethernet_address;

  Serializer serializer {};
  dgram.serialize( serializer );
  frame.payload = serializer.finish();

  transmit( frame );
}