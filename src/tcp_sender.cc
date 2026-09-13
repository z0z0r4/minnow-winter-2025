#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // debug( "unimplemented sequence_numbers_in_flight() called" );
  uint64_t in_flight = 0;
  for ( const auto& [seqno, msg] : unacked_messages_ ) {
    in_flight += msg.sequence_length();
  }
  return in_flight;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  // debug( "unimplemented consecutive_retransmissions() called" );
  return consecutive_retransmissions_count_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // debug( "unimplemented push() called" );
  // (void)transmit;
  if ( window_size_ == 0 ) {
    // If the window size is zero, we can only send a single byte to probe the window
    if ( sequence_numbers_in_flight() == 0 ) {
      TCPSenderMessage msg = make_empty_message();
      msg.seqno = Wrap32::wrap( next_abs_seqno_, isn_ );
      msg.payload = input_.reader().peek().substr( 0, 1 ); // Send one byte
      input_.reader().pop( msg.payload.size() );
      msg.FIN = input_.reader().is_finished() && !fin_sent_ && msg.payload.size() == 0;
      fin_sent_ = fin_sent_ || msg.FIN;

      if ( msg.sequence_length() == 0 && !msg.RST ) {
        return; // 没东西可发
      }

      if ( !timer_running_ ) {
        timer_running_ = true;
        retransmission_passed_time_ = 0;
        consecutive_retransmissions_count_ = 0;
      }

      add_unacked_message( msg );
      transmit( msg );
      next_abs_seqno_ += msg.sequence_length();
    }
    return;
  }

  while ( window_size_ > sequence_numbers_in_flight() ) {
    TCPSenderMessage msg = make_empty_message();
    // printf(
    //   "push: window_size_=%lu, sequence_numbers_in_flight()=%lu\n", window_size_, sequence_numbers_in_flight() );
    if ( reader().has_error() && syn_sent_ ) {
      msg.seqno = Wrap32::wrap( next_abs_seqno_, isn_ );
      msg.SYN = false;
      msg.FIN = false;
    } else if ( !syn_sent_ ) {
      msg.SYN = true;
      msg.seqno = isn_;
      msg.RST = reader().has_error();

      // 给 SYN 留 1 位，剩下空间发 payload
      const uint64_t available
        = ( window_size_ == 0 )
            ? 1
            : ( sequence_numbers_in_flight() >= window_size_ ? 0 : window_size_ - sequence_numbers_in_flight() );
      const uint64_t space_for_payload = ( available >= 1 ) ? available - 1 : 0;
      const uint64_t n = std::min( std::min( static_cast<uint64_t>( reader().bytes_buffered() ),
                                             static_cast<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE ) ),
                                   space_for_payload );
      msg.payload = reader().peek().substr( 0, n );
      input_.reader().pop( msg.payload.size() );
      msg.FIN = reader().is_finished() && !fin_sent_ && ( available - 1 - n ) >= 1;
      syn_sent_ = true;
      fin_sent_ = fin_sent_ || msg.FIN;
    } else {
      // Send payload
      const uint64_t payload_size
        = std::min( std::min( reader().bytes_buffered(), static_cast<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE ) ),
                    window_size_ - sequence_numbers_in_flight() );
      msg.SYN = false;
      msg.seqno = Wrap32::wrap( next_abs_seqno_, isn_ );
      msg.payload = reader().peek().substr( 0, payload_size );
      input_.reader().pop( msg.payload.size() );
      msg.FIN
        = reader().is_finished() && window_size_ >= sequence_numbers_in_flight() + payload_size + 1 && !fin_sent_;
      fin_sent_ = fin_sent_ || msg.FIN;
      if ( msg.sequence_length() == 0 && !msg.RST ) {
        return; // 没有 SYN/FIN/payload/RST，没什么可发
      }
    }
    if ( msg.sequence_length() != 0 && !timer_running_ ) {
      // enable timer if not already running
      timer_running_ = true;
      retransmission_passed_time_ = 0;
      consecutive_retransmissions_count_ = 0;
    }
    // printf("push: adding msg seq=%lu SYN=%d FIN=%d payload=%zu\n",
    //      next_abs_seqno_, msg.SYN, msg.FIN, msg.payload.size());
    add_unacked_message( msg );
    transmit( msg );
    // printf( "transmit: seqno: %lu, SYN: %d, payload size: %lu, FIN: %d, RST: %d\n",
    //         msg.seqno.unwrap( isn_, next_abs_seqno_ ), msg.SYN, msg.payload.size(), msg.FIN, msg.RST );
    next_abs_seqno_ += msg.sequence_length();

    if ( msg.RST ) {
      break;
    }
    // printf( "next_abs_seqno_: %lu\n", next_abs_seqno_ );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  // debug( "unimplemented make_empty_message() called" );
  // return {};
  return TCPSenderMessage { Wrap32::wrap( next_abs_seqno_, isn_ ), false, "", false, reader().has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // debug( "unimplemented receive() called" );
  // (void)msg;
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
    while ( it != unacked_messages_.end() ) {
      //   printf( "Checking unacked message: seqno_last: %lu, acked_abs_seqno_: %lu\n",
      //           it->first + it->second.sequence_length(), acked_abs_seqno_ );
      if ( it->first + it->second.sequence_length() <= acked_abs_seqno_ ) {
        // printf( "Removing acknowledged message: seqno: %lu, SYN: %d, payload size: %lu, FIN: %d, RST: %d\n",
        //         it->second.seqno.unwrap( isn_, next_abs_seqno_ ),
        //         it->second.SYN,
        //         it->second.payload.size(),
        //         it->second.FIN,
        //         it->second.RST );
        it = unacked_messages_.erase( it );
        RTO_ms_ = initial_RTO_ms_;              // Reset RTO to initial value
        consecutive_retransmissions_count_ = 0; // Reset consecutive retransmissions count
        retransmission_passed_time_ = 0;        // Reset the timer
        if ( unacked_messages_.empty() ) {
          timer_running_ = false; // Stop the timer if no unacked messages
        }
      } else {
        ++it;
      }
    }
    // printf("After removal, unacked size=%zu\n", unacked_messages_.size());
  } else {
    // If the ackno is not present, we cannot update the next_seqno_ or window_size_
    debug( "Received TCPReceiverMessage without ackno" );
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // debug( "unimplemented tick({}, ...) called", ms_since_last_tick );
  // (void)transmit;
  if ( timer_running_ ) {
    retransmission_passed_time_ += ms_since_last_tick;
    // printf( "tick: retransmission_passed_time_=%lu, RTO_ms_=%lu\n", retransmission_passed_time_, RTO_ms_ );
    if ( retransmission_passed_time_ >= RTO_ms_ ) {
      // Retransmit the first unacked message
      retransmission_passed_time_ = 0; // Reset the timer
      if ( !unacked_messages_.empty() ) {
        transmit( unacked_messages_.begin()->second );
        // printf( "Retransmit: seqno: %lu, SYN: %d, payload size: %lu, FIN: %d, RST: %d\n",
        //         unacked_messages_.begin()->second.seqno.unwrap( isn_, next_abs_seqno_ ),
        //         unacked_messages_.begin()->second.SYN,
        //         unacked_messages_.begin()->second.payload.size(),
        //         unacked_messages_.begin()->second.FIN,
        //         unacked_messages_.begin()->second.RST );
      }

      if ( window_size_ > 0 ) {
        RTO_ms_ *= 2; // Exponential backoff
        consecutive_retransmissions_count_++;
      }
    }
  }
}

void TCPSender::add_unacked_message( const TCPSenderMessage& msg )
{
  if ( msg.sequence_length() == 0 ) {
    return; // Don't add empty messages to unacked_messages_
  }
  unacked_messages_.insert( { msg.seqno.unwrap( isn_, next_abs_seqno_ ), msg } );
}
