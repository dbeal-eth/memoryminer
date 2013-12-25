#include "bignum.h"

namespace mc {
	std::vector< std::pair<uint32_t,uint32_t> > momentum_search( uint256 midHash, char* scratchpad, int totalThreads );
	bool momentum_verify( uint256 midHash, uint32_t a, uint32_t b );
}

