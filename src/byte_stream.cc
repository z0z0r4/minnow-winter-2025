#include "byte_stream.hh"

#include <stdexcept>

using namespace std;

ByteStream::ByteStream( uint64_t capacity )
  : capacity_( capacity ),
    error_( false ),
    closed_( false ),
    bytes_pushed_( 0 ),
    bytes_popped_( 0 ),
    buffer_()
{ 
  buffer_ = std::vector<char>();
  buffer_.resize(capacity_);
}

void Writer::push( string data )
{
  if (closed_) {
    return;
  }

  const size_t write_len = min( static_cast<uint64_t>( data.size() ), available_capacity() );
  if ( write_len == 0 ) {
    return;
  }

  for (size_t i = 0; i < write_len; i++) {
    buffer_[(writer_cursor_ + i) % capacity_] = data.data()[i];
  }

  writer_cursor_ = (writer_cursor_ + write_len) % capacity_ ;  
  bytes_pushed_ += write_len;
}

void Writer::close()
{
  closed_ = true;
}

bool Writer::is_closed() const
{
  return closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - (bytes_pushed_ - bytes_popped_);
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

string_view Reader::peek() const
{
  auto len = min(capacity_ - reader_cursor_, bytes_buffered());
  return string_view(buffer_.data() + reader_cursor_, len);
}

void Reader::pop( uint64_t len )
{
  if (bytes_buffered() < len) {
    throw runtime_error("No enough buffered bytes");
  }
  reader_cursor_ = (reader_cursor_ + len) % capacity_;
  bytes_popped_ += len;
}

bool Reader::is_finished() const
{
  return closed_ && bytes_popped_ == bytes_pushed_;
}

uint64_t Reader::bytes_buffered() const
{
  return bytes_pushed_ - bytes_popped_;
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}

