#include "SPECK3D_OMP_C.h"

#include <cassert>
#include <cstring>
#include <numeric>   // std::accumulate()
#include <algorithm> // std::all_of()
#include <omp.h>


void SPECK3D_OMP_C::set_dims( speck::dims_type dims )
{
    m_dims = dims;
}


void SPECK3D_OMP_C::prefer_chunk_dims( speck::dims_type dims )
{
    m_chunk_dims = dims;
}

    
void SPECK3D_OMP_C::set_num_threads( size_t n )
{
    if( n > 0 )
        m_num_threads = n;
}


#ifdef QZ_TERM
void SPECK3D_OMP_C::set_qz_level( int32_t q )
{
    m_qz_lev = q;
}
auto SPECK3D_OMP_C::set_tolerance( double t ) -> RTNType
{
    if( t <= 0.0 )
        return RTNType::InvalidParam;
    else {
        m_tol = t;
        return RTNType::Good;
    } 
}
auto SPECK3D_OMP_C::get_outlier_stats() const -> std::pair<size_t, size_t>
{
    using pair = std::pair<size_t, size_t>;
    pair sum{0, 0};
    auto op  = [](const pair& a, const pair& b) -> pair
               {return {a.first + b.first, a.second + b.second};};
    return std::accumulate( m_outlier_stats.begin(), m_outlier_stats.end(), sum, op );
}
#else
auto SPECK3D_OMP_C::set_bpp( float bpp ) -> RTNType
{
    if( bpp < 0.0 || bpp > 64.0 )
        return RTNType::InvalidParam;
    else {
        m_bpp = bpp;
        return RTNType::Good;
    }
}
#endif


template<typename T>
auto SPECK3D_OMP_C::use_volume( const T* vol, size_t len ) -> RTNType
{
    if( len != m_dims[0] * m_dims[1] * m_dims[2] )
        return RTNType::WrongSize;

    // If preferred chunk size is not set, then use the volume size as chunk size.
    for( size_t i = 0; i < m_chunk_dims.size(); i++ ) {
        if( m_chunk_dims[i] == 0 )
            m_chunk_dims[i] = m_dims[i];
    }

    // Block the volume into smaller chunks
    auto chunks = speck::chunk_volume( m_dims, m_chunk_dims );
    const auto num_chunks = chunks.size();
    m_chunk_buffers.resize( num_chunks );
    std::for_each( m_chunk_buffers.begin(), m_chunk_buffers.end(), [](auto& v){v.clear();} );

    #pragma omp parallel for num_threads(m_num_threads)
    for( size_t i = 0; i < num_chunks; i++ ) {
        m_chunk_buffers[i] = speck::gather_chunk( vol, m_dims, chunks[i] );
    }

    return RTNType::Good;

}
template auto SPECK3D_OMP_C::use_volume( const float* ,  size_t ) -> RTNType;
template auto SPECK3D_OMP_C::use_volume( const double* , size_t ) -> RTNType;


auto SPECK3D_OMP_C::compress() -> RTNType
{
    // Need to make sure that the chunks are ready! 
    auto chunks = speck::chunk_volume( m_dims, m_chunk_dims );
    const auto num_chunks = chunks.size();
    if( m_chunk_buffers.size() != num_chunks )
        return RTNType::Error;
    if(std::any_of( m_chunk_buffers.begin(), m_chunk_buffers.end(), [](auto& v){return v.empty();}))
        return RTNType::Error;

    // Let's prepare some data structures for compression!
    auto compressors = std::vector<SPECK3D_Compressor>( m_num_threads );
    auto chunk_rtn   = std::vector<RTNType>( num_chunks, RTNType::Good );
    m_encoded_streams.resize( num_chunks );
    std::for_each( m_encoded_streams.begin(), m_encoded_streams.end(), [](auto& v){v.clear();} );

#ifdef QZ_TERM
    m_outlier_stats.assign( num_chunks, {0, 0} );
#endif

    // Each thread uses a compressor instance to work on a chunk.
    //
    #pragma omp parallel for num_threads(m_num_threads)
    for( size_t i = 0; i < num_chunks; i++ ) {
        auto& compressor = compressors[ omp_get_thread_num() ];
        const auto buf_len = chunks[i][1] * chunks[i][3] * chunks[i][5];

        // The following few operations have no chance to fail.
        compressor.take_data(std::move(m_chunk_buffers[i]), {chunks[i][1], chunks[i][3], chunks[i][5]});

#ifdef QZ_TERM
        compressor.set_qz_level(  m_qz_lev );
        compressor.set_tolerance( m_tol );
#else
        compressor.set_bpp( m_bpp );
#endif

        // Action items
        chunk_rtn[i]         = compressor.compress();
        m_encoded_streams[i] = compressor.view_encoded_bitstream();

#ifdef QZ_TERM
        m_outlier_stats[i] = compressor.get_outlier_stats();
#endif
    }

    if( std::any_of( chunk_rtn.begin(), chunk_rtn.end(), 
                     [](auto r){return r != RTNType::Good;} ) )
        return RTNType::Error;
    if( std::any_of(m_encoded_streams.begin(), m_encoded_streams.end(), [](auto& s){return s.empty();}))
        return RTNType::Error;

    return RTNType::Good;
}


auto SPECK3D_OMP_C::get_encoded_bitstream() const -> std::vector<uint8_t>
{
    auto header = m_generate_header();
    if(  header.empty() )
        return std::vector<uint8_t>(0);

    auto total_size = std::accumulate(m_encoded_streams.begin(), m_encoded_streams.end(), 
                                      header.size(), [](auto a, auto& b){return a + b.size();});
    auto buf = std::vector<uint8_t>( total_size );

    std::copy( header.begin(), header.end(), buf.begin() );
    auto itr = buf.begin() + header.size();
    for( const auto& s : m_encoded_streams ) {
        std::copy( s.begin(), s.end(), itr );
        itr += s.size();
    }

    return buf;
}


auto SPECK3D_OMP_C::m_generate_header() const -> speck::vec8_type
{
    // The header would contain the following information
    //  -- a version number                     (1 byte)
    //  -- 8 booleans                           (1 byte)
    //  -- volume and chunk dimensions          (4 x 6 = 24 bytes)
    //  -- length of bitstream for each chunk   (4 x num_chunks)

    auto chunks = speck::chunk_volume( m_dims, m_chunk_dims );
    const auto num_chunks  = chunks.size();
    if( num_chunks != m_encoded_streams.size() )
        return std::vector<uint8_t>(0);
    const auto header_size = m_header_magic + num_chunks * 4;
    auto header = std::vector<uint8_t>( header_size );

    // Version number
    header[0] = 10 * SPERR_VERSION_MAJOR + SPERR_VERSION_MINOR;
    size_t loc = 1;

    // 8 booleans: 
    // bool[0]  : if ZSTD is used
    // bool[1]  : if this bitstream is for 3D (true) or 2D (false) data.
    // bool[2-7]: undefined
    bool b[8] = {false, true, false, false, false, false, false, false};
#ifdef USE_ZSTD
    b[0] = true;
#endif
    speck::pack_8_booleans( header[loc], b );
    loc += 1;

    // Volume and chunk dimensions
    uint32_t vcdim[6] = {uint32_t(m_dims[0]), uint32_t(m_dims[1]), uint32_t(m_dims[2]), 
                         uint32_t(m_chunk_dims[0]), uint32_t(m_chunk_dims[1]), uint32_t(m_chunk_dims[2])};
    std::memcpy( &header[loc], vcdim, sizeof(vcdim) );
    loc += sizeof(vcdim);

    // Length of bitstream for each chunk
    // Note that we use uint32_t to keep the length, and we need to make sure
    // that no chunk size is bigger than that.
    for( const auto& stream : m_encoded_streams ) {
        uint32_t len = stream.size();
        std::memcpy( &header[loc], &len, sizeof(len) );
        loc += sizeof(len);
    }
    assert( loc == header_size );

    return std::move(header);
}


