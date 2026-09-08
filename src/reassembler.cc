#include "reassembler.hh"
#include "debug.hh"

#include <iterator>
#include <optional>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  const uint64_t first_unassembled = next_byte_to_write_;
  const uint64_t available_cap = output_.writer().available_capacity();
  const uint64_t first_unacceptable = first_unassembled + available_cap;

  if ( is_last_substring ) {
    has_last_ = true;
    eof_index_ = first_index + data.size();
  }

  if ( first_index + data.size() <= first_unassembled || first_index >= first_unacceptable ) {
    check_and_close();
    return;
  }

  if ( first_index + data.size() > first_unacceptable ) {
    data = data.substr( 0, first_unacceptable - first_index );
  }

  if ( first_index < first_unassembled ) {
    data = data.substr( first_unassembled - first_index );
    first_index = first_unassembled;
  }

  if ( data.empty() ) {
    check_and_close();
    return;
  }

  auto last_index = first_index + data.size() - 1;
  Substring new_substring = { first_index, last_index, data };

  auto it = substrings_.lower_bound( first_index );
  std::optional<Substring> right_sub_string = std::nullopt;
  std::optional<Substring> left_sub_string = std::nullopt;
  if ( it != substrings_.end() ) {
    right_sub_string = it->second;
  }

  if ( it != substrings_.begin() ) {
    left_sub_string = std::prev( it )->second;
  }

  std::optional<Status> right_status = std::nullopt;
  std::optional<Status> left_status = std::nullopt;

  if ( right_sub_string.has_value() ) {
    right_status = check_overlap(
      first_index, last_index, right_sub_string.value().first_index, right_sub_string.value().last_index );
  }
  if ( left_sub_string.has_value() ) {
    left_status = check_overlap(
      left_sub_string.value().first_index, left_sub_string.value().last_index, first_index, last_index );
  }

  if ( left_status.has_value() && ( left_status.value() != Status::DisjointBefore ) ) {
    // Handle left overlap
    if ( left_status.value() == Status::ExactMatch || left_status.value() == Status::StrictlyEncloses ) {
      check_and_close();
      return;
    } else if ( left_status.value() == Status::StrictlyInside ) {
      // remove the existing substring and try insert the new substring again, avoid missing any overlap
      substrings_.erase( left_sub_string.value().first_index );
      insert( first_index, data, is_last_substring );
      check_and_close();
      return;
    } else if ( left_status.value() == Status::OverlapStart || left_status.value() == Status::OverlapEnd ) {
      // merge with the left substring
      auto merged_result = merge_substrings( left_sub_string.value(), new_substring, left_status.value() );
      substrings_.erase( left_sub_string.value().first_index );
      insert( merged_result.first_index, merged_result.data, is_last_substring );
      check_and_close();
      return;
    }
  }

  if ( right_status.has_value() && ( right_status.value() != Status::DisjointAfter ) ) {
    // Handle right overlap
    if ( right_status.value() == Status::ExactMatch || right_status.value() == Status::StrictlyInside ) {
      check_and_close();
      return;
    } else if ( right_status.value() == Status::StrictlyEncloses ) {
      // remove the existing substring and try insert the new substring again, avoid missing any overlap
      substrings_.erase( right_sub_string.value().first_index );
      insert( first_index, data, is_last_substring );
      check_and_close();
      return;
    } else if ( right_status.value() == Status::OverlapStart || right_status.value() == Status::OverlapEnd ) {
      // merge with the right substring
      auto merged_result = merge_substrings( new_substring, right_sub_string.value(), right_status.value() );
      substrings_.erase( right_sub_string.value().first_index );
      insert( merged_result.first_index, merged_result.data, is_last_substring );
      check_and_close();
      return;
    }
  }

  // insert new substring
  substrings_[first_index] = new_substring;

  // check first substring in the head_indices_ map, if it is the next byte to write, write it to the output stream
  while ( !substrings_.empty() && !output_.writer().is_closed() ) {
    auto& [_, first_substring] = *substrings_.begin();
    if ( first_substring.first_index == next_byte_to_write_ ) {
      // write the substring to the output stream
      output_.writer().push( first_substring.data );
      next_byte_to_write_ += first_substring.data.size();
      // remove the substring from the maps
      substrings_.erase( first_substring.first_index );
    } else {
      break;
    }
  }

  check_and_close();
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

Reassembler::Status Reassembler::check_overlap( uint64_t first_index,
                                                uint64_t last_index,
                                                uint64_t other_first_index,
                                                uint64_t other_last_index ) const
{
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
                                                      const Reassembler::Substring& b_substring,
                                                      Status overlap_status ) const
{
  auto a_first = a_substring.first_index;
  auto a_last = a_substring.last_index;
  auto b_first = b_substring.first_index;
  auto b_last = b_substring.last_index;

  switch ( overlap_status ) {
    case Status::ExactMatch:
      return { a_substring.first_index, a_substring.last_index, a_substring.data };
    case Status::StrictlyEncloses:
      return { a_substring.first_index, a_substring.last_index, a_substring.data };
    case Status::StrictlyInside:
      return { b_substring.first_index, b_substring.last_index, b_substring.data };
    case Status::OverlapStart: {
      auto merged_first = a_first;
      auto merged_last = b_last;
      auto merged_data = a_substring.data + b_substring.data.substr( a_last - b_first + 1 );
      return { merged_first, merged_last, merged_data };
    }
    case Status::OverlapEnd: {
      auto merged_first = b_first;
      auto merged_last = a_last;
      auto merged_data = b_substring.data + a_substring.data.substr( b_last - a_first + 1 );
      return { merged_first, merged_last, merged_data };
    }
    default:
      throw std::invalid_argument( "Invalid overlap status for merging substrings" );
  }
}

void Reassembler::check_and_close()
{
  if ( has_last_ && next_byte_to_write_ == eof_index_ && !output_.writer().is_closed() ) {
    output_.writer().close();
  }
}
