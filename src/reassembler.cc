#include "reassembler.hh"
#include "debug.hh"

#include <cstring>
#include <iterator>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  const uint64_t first_unacceptable = next_index_to_write_ + output_.writer().available_capacity();
  const uint64_t original_first_index = first_index;
  const uint64_t original_last_index = first_index + data.size();

  if ( is_last_substring ) {
    eof_received_ = true;
    eof_index_ = original_last_index;
  }

  first_index = max( first_index, next_index_to_write_ );
  uint64_t last_index = min( original_last_index, first_unacceptable );
  if (first_index >= last_index) {
    if (eof_received_ && next_index_to_write_ == eof_index_) {
      output_.writer().close();
    }
    return;
  }

  if ( first_index != original_first_index || last_index != original_last_index ) {
    data = data.substr( first_index - original_first_index, last_index - first_index );
  }

  last_index = first_index + data.size() - 1;

  Substring new_substring = { first_index, last_index, std::move( data ) };

  auto it = substrings_.lower_bound( first_index );
  if ( it != substrings_.begin() ) {
    auto previous = std::prev( it );
    if ( previous->first + previous->second.data.size() > first_index ) {
      it = previous;
    }
  }

  while ( it != substrings_.end() ) {
    const auto& current_substring = it->second; // 避免拷贝 Substring 及其包含的 string
    Status status = check_overlap( new_substring, current_substring );

    if ( status == Status::OverlapEnd || status == Status::OverlapStart ) {
      // merge, remove existing substring, cover new_substring by merged substring, then insert new_substring
      new_substring = merge_substrings( new_substring, current_substring );
      it = substrings_.erase( it );
    } else if ( status == Status::StrictlyEncloses ) {
      // remove existing substring
      it = substrings_.erase( it );
    } else if ( status == Status::StrictlyInside || status == Status::ExactMatch ) {
      // do nothing, don't insert new_substring, return
      // Inside or Exact match means it is impossible to overlap other substrings, no push at all, so we don't need to close
      return;
    } else if ( status == Status::DisjointAfter ) {
      // then insert new_substring
      break;
    } else {
      ++it;
    }
  }

  const uint64_t new_first_index = new_substring.first_index;
  substrings_[new_first_index] = std::move( new_substring );

  while ( !substrings_.empty() && !output_.writer().is_closed()
          && substrings_.begin()->second.first_index == next_index_to_write_ ) {
    auto& ready_sub = substrings_.begin()->second;
    output_.writer().push( ready_sub.data );
    next_index_to_write_ += ready_sub.data.size();
    substrings_.erase( substrings_.begin() );
  }

  if ( eof_received_ && next_index_to_write_ == eof_index_ && !output_.writer().is_closed() ) {
    output_.writer().close();
  }
}

uint64_t Reassembler::count_bytes_pending() const
{
  uint64_t total_bytes = 0;
  for ( const auto& [_, substring] : substrings_ ) {
    total_bytes += substring.data.size();
  }
  return total_bytes;
}

Reassembler::Status Reassembler::check_overlap( const Reassembler::Substring& substring_a,
                                                const Reassembler::Substring& substring_b ) const
{
  const uint64_t first_index = substring_a.first_index;
  const uint64_t last_index = substring_a.last_index;
  const uint64_t other_first_index = substring_b.first_index;
  const uint64_t other_last_index = substring_b.last_index;

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
  const auto merged_first = std::min( a_substring.first_index, b_substring.first_index );
  const auto merged_last = std::max( a_substring.last_index, b_substring.last_index );

  std::string merged_data( merged_last - merged_first + 1, '\0' );

  std::memcpy(
    &merged_data[a_substring.first_index - merged_first], a_substring.data.data(), a_substring.data.size() );
  std::memcpy(
    &merged_data[b_substring.first_index - merged_first], b_substring.data.data(), b_substring.data.size() );

  return { merged_first, merged_last, std::move( merged_data ) };
}