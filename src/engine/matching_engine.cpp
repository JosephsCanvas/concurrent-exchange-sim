/**
 * @file matching_engine.cpp
 * @brief Implementation of non-template matching engine components
 * 
 * Most matching engine logic is in the header template.
 * This file contains any non-template utilities.
 */

#include <ces/engine/matching_engine.hpp>

namespace ces {

// Template instantiations for common queue sizes
// This helps reduce compile times for common configurations

// 64K capacity
template class MatchingEngine<65536>;

// 16K capacity
template class MatchingEngine<16384>;

// 4K capacity  
template class MatchingEngine<4096>;

} // namespace ces
