#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

// return relatively seqno
Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + static_cast<uint32_t>( n );
}

// return absolutely seqno
uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint64_t offset = static_cast<uint32_t>(raw_value_ - zero_point.raw_value_);
  uint64_t base = checkpoint & ~(0xFFFFFFFFULL);
  uint64_t candidate = base + offset;

  // Obiously there two candidate around,
  // if |left - c| ≤ 2^31，return left, otherwise return right
  if (candidate > checkpoint && candidate - checkpoint > 0x80000000ULL ) {
    if (candidate >= 0x100000000ULL) {
      candidate -= 0x100000000ULL;
    }
  } else if (candidate < checkpoint && checkpoint - candidate > 0x80000000ULL ) {
    candidate += 0x100000000ULL;
  }
  
  return candidate;
}