#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  uint64_t in_flight = 0;
  for ( const auto& [seqno, msg] : unacked_messages_ ) {
    in_flight += msg.sequence_length();
  }
  return in_flight;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_count_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  while ( window_size_ > sequence_numbers_in_flight() || (window_size_ == 0 && sequence_numbers_in_flight() == 0 )) {
    TCPSenderMessage msg = make_empty_message();
    uint64_t available;

    if ( window_size_ == 0 ) {
      available = 1;
      msg.payload = input_.reader().peek().substr( 0, 1 ); // Send one byte
    } else {
      available = window_size_ - sequence_numbers_in_flight();
      // 给 SYN 留 1 位，剩下空间发 payload 和 FIN
      const uint64_t space_for_payload = syn_sent_ ? available : (available >= 1 ? available - 1 : 0);
      const uint64_t payload_size = std::min( std::min( reader().bytes_buffered() ,
                                              static_cast<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE ) ),
                                    space_for_payload );
      msg.payload = reader().peek().substr( 0, payload_size );
    }

    input_.reader().pop( msg.payload.size() );

    if ( !syn_sent_ ) {
      msg.SYN = syn_sent_ = true;
    }

    if ( !fin_sent_ && reader().is_finished() && available >= msg.payload.size() + msg.SYN + 1) {
      msg.FIN = fin_sent_= true;
    }

    if ( msg.sequence_length() == 0 && !msg.RST ) {
      return; // 没有 SYN/FIN/payload/RST，没什么可发
    }

    if ( !timer_running_ ) {
      // enable timer if not already running
      timer_running_ = true;
      retransmission_passed_time_ = 0;
      consecutive_retransmissions_count_ = 0;
    }

    unacked_messages_.insert( { next_abs_seqno_, msg } );
    transmit( msg );
    next_abs_seqno_ += msg.sequence_length();

    if ( msg.RST ) {
      break;
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage { Wrap32::wrap( next_abs_seqno_, isn_ ), false, "", false, reader().has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    input_.set_error();
    return;
  }

  // Update the window size
  window_size_ = msg.window_size;

  if ( msg.ackno.has_value() ) {
    if ( msg.ackno.value() == isn_ + 1 ) {
      // The SYN has been acknowledged
      syn_sent_ = true;
    }

    if ( msg.ackno.value().unwrap( isn_, next_abs_seqno_ ) > next_abs_seqno_ ) {
      // Error: ackno is beyond the next_abs_seqno_, ignore it
      // Test case: "Impossible ackno (beyond next seqno) is ignored"
      debug( "Received TCPReceiverMessage with ackno beyond next_abs_seqno_, ignoring" );
      return;
    }

    acked_abs_seqno_ = msg.ackno.value().unwrap( isn_, acked_abs_seqno_ );
    // Remove acknowledged messages from unacked_messages_
    auto it = unacked_messages_.begin();
    while ( it != unacked_messages_.end() && it->first + it->second.sequence_length() <= acked_abs_seqno_ ) {
      it = unacked_messages_.erase( it );

      RTO_ms_ = initial_RTO_ms_;              // Reset RTO to initial value
      consecutive_retransmissions_count_ = 0; // Reset consecutive retransmissions count
      retransmission_passed_time_ = 0;        // Reset the timer
      timer_running_ = !unacked_messages_.empty(); // Stop the timer if there are no unacked messages
    }
  } else {
    // If the ackno is not present, we cannot update the next_seqno_ or window_size_
    debug( "Received TCPReceiverMessage without ackno" );
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( timer_running_ ) {
    retransmission_passed_time_ += ms_since_last_tick;
    if ( retransmission_passed_time_ >= RTO_ms_ ) {
      // Retransmit the first unacked message
      retransmission_passed_time_ = 0; // Reset the timer
      if ( !unacked_messages_.empty() ) {
        transmit( unacked_messages_.begin()->second );
      }

      if ( window_size_ > 0 ) {
        RTO_ms_ *= 2; // Exponential backoff
        consecutive_retransmissions_count_++;
      }
    }
  }
}
