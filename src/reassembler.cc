#include "reassembler.hh"
#include "debug.hh"

#include <cstring>
#include <iterator>
#include <optional>

using namespace std;

// void printf_substrings( const std::map<uint64_t, Reassembler::Substring>& substrings )
// {
//   printf( "Pending substrings:\n" );
//   for ( const auto& [first_index, substring] : substrings ) {
//     printf( "  [%lu, %lu]: \n", substring.first_index, substring.last_index );
//   }
// }

// std::string get_status_string( Reassembler::Status status )
// {
//   switch ( status ) {
//     case Reassembler::Status::DisjointBefore:
//       return "DisjointBefore";
//     case Reassembler::Status::OverlapStart:
//       return "OverlapStart";
//     case Reassembler::Status::StrictlyInside:
//       return "StrictlyInside";
//     case Reassembler::Status::ExactMatch:
//       return "ExactMatch";
//     case Reassembler::Status::StrictlyEncloses:
//       return "StrictlyEncloses";
//     case Reassembler::Status::OverlapEnd:
//       return "OverlapEnd";
//     case Reassembler::Status::DisjointAfter:
//       return "DisjointAfter";
//     default:
//       return "UnknownStatus";
//   }
// }

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  const uint64_t first_unacceptable = next_index_to_write_ + output_.writer().available_capacity();
  const uint64_t original_first_index = first_index;

  if ( is_last_substring ) {
    eof_received_ = true;
    eof_index_ = first_index + data.size();
  }

  first_index = max( first_index, next_index_to_write_ );
  uint64_t last_index = min( original_first_index + data.size(), first_unacceptable );
  if (first_index >= last_index) {
    if (eof_received_ && next_index_to_write_ == eof_index_) {
      output_.writer().close();
    }
    return;
  }

  data = data.substr(first_index - original_first_index, last_index - first_index);
  
  last_index = first_index + data.size() - 1;

  Substring new_substring = { first_index, last_index, data };

  auto it = substrings_.lower_bound( first_index );
  if ( it != substrings_.begin() ) {
    auto previous = std::prev( it );
    if ( previous->first + previous->second.data.size() > first_index ) {
      // Left overlap
      it = previous;
    }
  }

  std::optional<Status> status = std::nullopt;
  std::optional<Substring> current_substrng = std::nullopt;
  
  while ( it != substrings_.end() ) {
    current_substrng = it->second;
    status = check_overlap( new_substring, current_substrng.value() );

    if ( status.value() == Status::OverlapEnd || status.value() == Status::OverlapStart) {
      // merge, remove existing substring, cover new_substring by merged substring, then insert new_substring
      auto merged_substring = merge_substrings( new_substring, current_substrng.value() );
      new_substring = Substring { merged_substring.first_index, merged_substring.last_index, merged_substring.data };
      it = substrings_.erase( it );
    } else if (status.value() == Status::StrictlyEncloses) {
      // remove existing substring
      it = substrings_.erase( it );
    } else if (status.value() == Status::StrictlyInside || status.value() == Status::ExactMatch) {
      // do nothing, don't insert new_substring, return
      // Inside or Exact match means it is impossible to overlap other substrings, no push at all, so we don't need to close
      return;
    } else if (status.value() == Status::DisjointAfter) {
      // then insert new_substring
      break;
    } else {
      it++;
    }
  }

  // insert new substring
  substrings_[new_substring.first_index] = new_substring;

  // check first substring in the head_indices_ map, if it is the next byte to write, write it to the output stream
  while ( !substrings_.empty() && !output_.writer().is_closed() && substrings_.begin()->second.first_index == next_index_to_write_ ) {
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

Reassembler::Status Reassembler::check_overlap( Reassembler::Substring substring_a,
                                                Reassembler::Substring substring_b ) const
{
  uint64_t first_index = substring_a.first_index;
  uint64_t last_index = substring_a.last_index;
  uint64_t other_first_index = substring_b.first_index;
  uint64_t other_last_index = substring_b.last_index;

  if ( last_index < other_first_index ) {
    return Status::DisjointBefore;
  } else if ( first_index > other_last_index ) {
    return Status::DisjointAfter;
  } else if ( first_index == other_first_index && last_index == other_last_index ) {
    return Status::ExactMatch;
  } else if ( first_index <= other_first_index && last_index >= other_last_index ) {
    return Status::StrictlyEncloses;
  } else if ( first_index >= other_first_index && last_index <= other_last_index ) {
    return Status::StrictlyInside;
  } else if ( first_index < other_first_index && last_index < other_last_index ) {
    return Status::OverlapStart;
  } else if ( first_index > other_first_index && last_index > other_last_index ) {
    return Status::OverlapEnd;
  } else {
    throw std::invalid_argument( "Invalid overlap status" );
  }
}

Reassembler::Substring Reassembler::merge_substrings( const Reassembler::Substring& a_substring,
                                                      const Reassembler::Substring& b_substring ) const
{
  auto merged_first = std::min( a_substring.first_index, b_substring.first_index );
  auto merged_last = std::max( a_substring.last_index, b_substring.last_index );

  std::string merged_data( merged_last - merged_first + 1, '\0' );

  std::memcpy(
    &merged_data[a_substring.first_index - merged_first], a_substring.data.data(), a_substring.data.size() );
  std::memcpy(
    &merged_data[b_substring.first_index - merged_first], b_substring.data.data(), b_substring.data.size() );

  return { merged_first, merged_last, std::move( merged_data ) };
}