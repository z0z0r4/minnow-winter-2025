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

  // printf( "Insert [%lu, %lu] \n", first_index, last_index );

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

  if ( left_sub_string.has_value() ) {
    left_status = check_overlap( left_sub_string.value(), new_substring );
  }

  // printf_substrings( substrings_ );
  // printf( "[%lu, %lu] and [%lu, %lu], Left status: %s\n",
  //         new_substring.first_index,
  //         new_substring.last_index,
  //         left_sub_string.has_value() ? left_sub_string.value().first_index : 0,
  //         left_sub_string.has_value() ? left_sub_string.value().last_index : 0,
  //         left_status.has_value() ? get_status_string( left_status.value() ).c_str() : "None" );

  // As `it` is lower_bound result, only check nearby left substring for overlap is enough
  if ( left_status.has_value() && ( left_status.value() != Status::DisjointBefore ) ) {
    // Handle left overlap
    if ( left_status.value() == Status::ExactMatch || left_status.value() == Status::StrictlyEncloses ) {
      check_and_close();
      return;
    } else if ( left_status.value() == Status::StrictlyInside ) {
      // remove the existing substring, Don't cover the new substring value again!
      substrings_.erase( left_sub_string.value().first_index );
    } else if ( left_status.value() == Status::OverlapStart || left_status.value() == Status::OverlapEnd ) {
      // merge with the left substring
      auto merged_result = merge_substrings( left_sub_string.value(), new_substring );
      substrings_.erase( left_sub_string.value().first_index );
      new_substring = Substring { merged_result.first_index, merged_result.last_index, merged_result.data };
    }
  }

  it = substrings_.lower_bound( new_substring.first_index );

  // printf_substrings( substrings_ );

  while ( it != substrings_.end() ) {
    right_sub_string = it->second;
    // Caution: Different order of parameters, the state action is different from right (StrictlyInside vs
    // StrictlyEncloses)
    right_status = check_overlap( new_substring, right_sub_string.value() );
    // printf( "[%lu, %lu] and [%lu, %lu], Right status: %s\n",
    //         new_substring.first_index,
    //         new_substring.last_index,
    //         right_sub_string.value().first_index,
    //         right_sub_string.value().last_index,
    //         get_status_string( right_status.value() ).c_str() );
    if ( right_status.value() == Status::DisjointBefore ) {
      break;
    } else if ( right_status.value() == Status::ExactMatch || right_status.value() == Status::StrictlyInside ) {
      check_and_close();
      return;
    } else if ( right_status.value() == Status::StrictlyEncloses ) {
      // remove the existing substring, Don't cover the new substring value again!
      it = substrings_.erase( it );
    } else if ( right_status.value() == Status::OverlapStart || right_status.value() == Status::OverlapEnd ) {
      // merge with the right substring
      auto merged_result = merge_substrings( new_substring, right_sub_string.value() );
      it = substrings_.erase( it );
      new_substring = Substring { merged_result.first_index, merged_result.last_index, merged_result.data };
    } else {
      // Get next right substring
      ++it;
    }
  }

  // insert new substring
  substrings_[new_substring.first_index] = new_substring;

  // printf( "Inserted substring [%lu, %lu] \n", new_substring.first_index, new_substring.last_index );

  // printf_substrings( substrings_ );

  // check first substring in the head_indices_ map, if it is the next byte to write, write it to the output stream
  while ( !substrings_.empty() && !output_.writer().is_closed() ) {
    auto& [_, first_substring] = *substrings_.begin();
    if ( first_substring.first_index == next_byte_to_write_ ) {
      // write the substring to the output stream
      output_.writer().push( first_substring.data );
      next_byte_to_write_ += first_substring.data.size();
      // remove the substring from the maps
      substrings_.erase( substrings_.begin() );
    } else {
      break;
    }
  }

  // printf_substrings( substrings_ );

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

void Reassembler::check_and_close()
{
  if ( has_last_ && next_byte_to_write_ == eof_index_ && !output_.writer().is_closed() ) {
    output_.writer().close();
  }
}
