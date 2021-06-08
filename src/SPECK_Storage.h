#ifndef SPECK_STORAGE_H
#define SPECK_STORAGE_H

/* 
 * This class implements mechanisms and interfaces to help 2D and 3D SPECK classes 
 * manage storage of coefficients and bitstreams.
 */

#include <vector>
#include <string>
#include <utility> // std::pair<>
#include "speck_helper.h"

namespace speck {

class SPECK_Storage {

public:

    //
    // Memory management: input and output
    //
    template <typename T>
    auto copy_data(const T*, size_t len, size_t dimx, size_t dimy, size_t dimz = 1) -> RTNType;
    auto take_data(vecd_type&&, size_t dimx, size_t dimy, size_t dimz = 1) -> RTNType;
    auto release_data() -> vecd_type;               // Release ownership
    auto view_data() const -> const vecd_type&;     // Keep ownership
    auto get_dims() const -> std::array<size_t, 3>;

    // Get the encoded bitstream.
    // The returned memory block could be written to disk by other programs.
    //
    auto get_encoded_bitstream() const -> vec8_type;

    // Prepare internal states for a decompression operation from an encoded bitstream
    //
    // Note: it takes a raw pointer because it accesses memory provided by others,
    //       and others most likely provide a raw pointer.
    auto parse_encoded_bitstream( const void*, size_t ) -> RTNType;

    // Given a SPECK stream, tell how long the speck stream (including header) is in bytes,
    // and what the volume/slice dimension is in num. of elements.
    // Note: don't need to provide the buffer size because this function
    //       goes to a fixed location to retrieve the stream size.
    auto get_speck_stream_size( const void* ) const -> uint64_t;
    auto get_speck_stream_dims( const void* ) const -> std::array<size_t, 3>;

protected:
    //
    // Member variables
    //
    const size_t    m_header_size    = 24;
    size_t          m_dim_x          = 0;
    size_t          m_dim_y          = 0;
    size_t          m_dim_z          = 0;
    int32_t         m_max_coeff_bits = 0;
    int32_t         m_qz_term_lev    = 0; // At which quantization level does encoding terminate?

    std::vector<double> m_coeff_buf;
    std::vector<bool>   m_bit_buffer;

};

};

#endif
