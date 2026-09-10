#include "reassembler.hh"
#include "debug.hh"

#include <cstring>
#include <iostream>
#include <iterator>
#include <optional>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  const uint64_t original_first_index = first_index;
  // exclusive last index [first_index, end_index)
  const uint64_t original_end_index = first_index + data.size();

  if ( is_last_substring ) {
    eof_received_ = true;
    eof_index_ = first_index + data.size();
  }

  first_index = max( first_index, next_index_to_write_ );
  const uint64_t end_index
    = min( original_end_index, next_index_to_write_ + output_.writer().available_capacity() );

  if ( first_index >= end_index ) {
    if ( eof_received_ && next_index_to_write_ == eof_index_ ) {
      output_.writer().close();
    }
    return;
  }

  data = data.substr( first_index - original_first_index, end_index - first_index );

  const uint64_t last_index = end_index - 1; // inclusive last index

  Substring new_substring = { first_index, last_index, data };

  auto it = substrings_.lower_bound( first_index );
  if ( it != substrings_.begin() ) {
    auto previous = std::prev( it );
    if ( previous->first + previous->second.data.size() > first_index ) {
      // Left overlap
      it = previous;
    }
  }

  uint64_t cursor = first_index;
  while ( cursor <= last_index ) {
    if ( it == substrings_.end() || it->first > last_index ) {
      // No more overlapping substrings, insert remaining substring
      substrings_[cursor] = Substring { cursor, last_index, data.substr( cursor - first_index ) };
      break;
    }

    if ( cursor >= it->second.first_index && cursor <= it->second.last_index ) {
      cursor = it->second.last_index + 1;
      it++;
      continue;
    }

    uint64_t insert_end = min( last_index, it->second.first_index - 1 );
    if ( cursor <= insert_end ) {
      // insert non-overlapping part
      substrings_[cursor]
        = Substring { cursor, insert_end, data.substr( cursor - first_index, insert_end - cursor + 1 ) };
      cursor = it->second.last_index + 1; // Move cursor to the end of the current overlapping substring
      it++;
      continue;
    }
  }

  while ( !substrings_.empty() && !output_.writer().is_closed()
          && substrings_.begin()->second.first_index == next_index_to_write_ ) {
    // write the substring to the output stream
    output_.writer().push( substrings_.begin()->second.data );
    next_index_to_write_ += substrings_.begin()->second.data.size();
    // remove the substring from the maps
    substrings_.erase( substrings_.begin() );
  }

  if ( eof_received_ && next_index_to_write_ == eof_index_ && !output_.writer().is_closed() ) {
    output_.writer().close();
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  uint64_t total_bytes = 0;
  for ( const auto& [_, substring] : substrings_ ) {
    total_bytes += substring.data.size();
  }
  return total_bytes;
}