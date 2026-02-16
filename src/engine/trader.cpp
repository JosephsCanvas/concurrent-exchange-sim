/**
 * @file trader.cpp
 * @brief Implementation of non-template trader components
 * 
 * Most trader logic is in the header template.
 * This file contains template instantiations.
 */

#include <ces/engine/trader.hpp>

namespace ces {

// Template instantiations for common queue sizes

template class Trader<65536>;
template class Trader<16384>;
template class Trader<4096>;

} // namespace ces
