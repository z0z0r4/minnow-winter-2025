#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  if ( state_ == State::CLOSED ) {
    return;
  }

  if ( message.RST || reassembler_.has_error() ) {
    state_ = State::CLOSED;
    reassembler_.set_error();
    return;
  }

  if ( state_ == State::LISTEN ) {
    if ( message.SYN ) {
      ins_ = message.seqno;
      state_ = State::SYN_RECEIVED;
    } else {
      // Drop
      return;
    }
  }

  uint64_t checkpoint = reassembler_.writer().bytes_pushed();
  uint64_t abs_seqno = message.seqno.unwrap( ins_, checkpoint );
  uint64_t first_index = abs_seqno;
  if ( !message.SYN ) {
    first_index -= 1; // The first abs_seqno occupied by the SYN flag
  }

  reassembler_.insert( first_index, message.payload, message.FIN );

  if ( message.FIN ) {
    state_ = State::FIN_RECEIVED;
  }
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.
  if ( state_ == State::CLOSED || reassembler_.has_error() ) {
    return { std::nullopt, 0, true };
  }

  uint64_t capacity = reassembler_.writer().available_capacity();
  uint16_t window_size = static_cast<uint16_t>( std::min( capacity, static_cast<uint64_t>( UINT16_MAX ) ) );

  // Nothing to acknowledge yet
  if ( state_ == State::LISTEN ) {
    return { std::nullopt, window_size, false };
  }

  uint64_t next_abs_seqno = 1 + reassembler_.writer().bytes_pushed();

  // All Done!
  if ( state_ == State::FIN_RECEIVED && reassembler_.writer().is_closed()) {
    next_abs_seqno += 1;
  }

  Wrap32 ackno = Wrap32::wrap( next_abs_seqno, ins_ );
  return { ackno, window_size, false };
}
