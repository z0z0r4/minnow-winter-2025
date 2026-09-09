#include "reassembler.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  const uint64_t original_first_index = first_index;
  const uint64_t first_unacceptable = next_byte_to_write_ + output_.writer().available_capacity();
  const uint64_t last_index = first_index + data.size();

  if ( is_last_substring ) {
    eof_received_ = true;
    eof_index_ = last_index;
  }

  // Keep only bytes that can still enter the output stream.
  first_index = max( first_index, next_byte_to_write_ );
  const uint64_t last = min( last_index, first_unacceptable );
  if ( first_index >= last ) {
    if ( eof_received_ && next_byte_to_write_ == eof_index_ ) {
      output_.writer().close();
    }
    return;
  }
  data = data.substr( first_index - original_first_index, last - first_index );

  // Add only gaps not already represented in the map. Stored ranges never overlap.
  auto it = substrings_.lower_bound( first_index );
  if ( it != substrings_.begin() ) {
    auto previous = prev( it );
    if ( previous->first + previous->second.size() > first_index ) {
      it = previous;
    }
  }

  uint64_t cursor = first_index;
  while ( cursor < last ) {
    if ( it == substrings_.end() || it->first >= last ) {
      substrings_.emplace( cursor, data.substr( cursor - first_index, last - cursor ) );
      break;
    }

    if ( it->first > cursor ) {
      const uint64_t gap_end = min( last, it->first );
      substrings_.emplace( cursor, data.substr( cursor - first_index, gap_end - cursor ) );
      cursor = gap_end;
      continue;
    }

    cursor = max( cursor, it->first + it->second.size() );
    ++it;
  }

  while ( !substrings_.empty() && substrings_.begin()->first == next_byte_to_write_ ) {
    auto current = substrings_.begin();
    output_.writer().push( current->second );
    next_byte_to_write_ += current->second.size();
    substrings_.erase( current );
  }

  if ( eof_received_ && next_byte_to_write_ == eof_index_ ) {
    output_.writer().close();
  }
}

uint64_t Reassembler::count_bytes_pending() const
{
  uint64_t bytes_pending = 0;
  for ( const auto& [_, data] : substrings_ ) {
    bytes_pending += data.size();
  }
  return bytes_pending;
}
