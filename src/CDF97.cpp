#include "CDF97.h"

#include <type_traits>
#include <cassert>
#include <cstring>  // for std::memcpy()

template< typename T >
int speck::CDF97::assign_data( const T* data, long x, long y, long z )
{
    static_assert( std::is_floating_point<T>::value, 
                   "!! Only floating point values are supported !!" );
    m_dim_x = x;
    m_dim_y = y;
    m_dim_z = z;
    long num_of_vals = m_dim_x * m_dim_y * m_dim_z;
    m_data_buf = std::make_unique<double[]>( num_of_vals );
    for( long i = 0; i < num_of_vals; i++ )
        m_data_buf[i] = data[i];

    return 0;
}
template int speck::CDF97::assign_data( const float*  data, long x, long y, long z );
template int speck::CDF97::assign_data( const double* data, long x, long y, long z );


int speck::CDF97::dwt2d()
{
    //
    // Pre-process data
    //
    m_calc_mean();
    long num_of_vals = m_dim_x * m_dim_y * m_dim_z;
    for( long i = 0; i < num_of_vals; i++ )
        m_data_buf[i] -= m_data_mean;

    const auto num_level_xy = m_num_of_levels_xy();
    for( long lev = 0; lev < num_level_xy; lev++ )
    {
        long len_x = m_calc_approx_len( m_dim_x, lev );
        long len_y = m_calc_approx_len( m_dim_y, lev );
        m_dwt2d_one_level( m_data_buf.get(), len_x, len_y );
    }

    return 0;
}


int speck::CDF97::idwt2d()
{
    const auto num_level_xy = m_num_of_levels_xy();

    for( long lev = num_level_xy - 1; lev >= 0; lev-- )
    {
        long len_x = m_calc_approx_len( m_dim_x, lev );
        long len_y = m_calc_approx_len( m_dim_y, lev );
        m_idwt2d_one_level( m_data_buf.get(), len_x, len_y );
    }

    long num_of_vals = m_dim_x * m_dim_y * m_dim_z;
    for( long i = 0; i < num_of_vals; i++ )
        m_data_buf[i] += m_data_mean;

    return 0;
}


double* speck::CDF97::release_buffer( double& mean, long& dim_x, long& dim_y, long& dim_z )
{
    mean  = m_data_mean;    m_data_mean = 0.0;
    dim_x = m_dim_x;        m_dim_x     = 0;     
    dim_y = m_dim_y;        m_dim_y     = 0;     
    dim_z = m_dim_z;        m_dim_z     = 0;     
    return m_data_buf.release();
}

    
//
// Private Methods
//
void speck::CDF97::m_calc_mean()
{
    assert( m_dim_x > 0 && m_dim_y > 0 && m_dim_z > 0 );

    //
    // Here we calculate mean row by row to avoid too big numbers.
    // (Not using kahan summation because that's hard to vectorize.)
    //
    buffer_type row_means = std::make_unique<double[]>( m_dim_y * m_dim_z );
    const double dim_x1 = 1.0 / double(m_dim_x);
    long counter1 = 0, counter2 = 0;
    for( long z = 0; z < m_dim_z; z++ )
        for( long y = 0; y < m_dim_y; y++ )
        {
            double sum = 0.0;
            for( long x = 0; x < m_dim_x; x++ )
                sum += m_data_buf[ counter1++ ];
            row_means[ counter2++ ] = sum * dim_x1;
        }

    buffer_type layer_means = std::make_unique<double[]>( m_dim_z );
    const double dim_y1 = 1.0 / double(m_dim_y);
    counter1 = 0; counter2 = 0;
    for( long z = 0; z < m_dim_z; z++ )
    {
        double sum = 0.0;
        for( long y = 0; y < m_dim_y; y++ )
            sum += row_means[ counter1++ ];
        layer_means[ counter2++ ] = sum * dim_y1;
    }

    double sum = 0.0;
    for( long z = 0; z < m_dim_z; z++ )
        sum += layer_means[ z ];

    m_data_mean = sum / double(m_dim_z);
}

    
void speck::CDF97::m_dwt2d_one_level( double* plane, long len_x, long len_y )
{
    assert( len_x <= m_dim_x && len_y <= m_dim_y );

    // Create temporary buffers to work on
    long len_xy = std::max( len_x, len_y );
    buffer_type buffer = std::make_unique<double[]>( len_xy * 2 );
    double *const buf_ptr  = buffer.get();       // First half of the array
    double *const buf_ptr2 = buf_ptr + len_xy;   // Second half of the array

    // First, perform DWT along X for every row
    if( len_x % 2 == 0 )    // Even length
    {
        for( long i = 0; i < len_y; i++ )
        {
            auto* pos = plane + i * m_dim_x;
            std::memcpy( buf_ptr, pos, sizeof(double) * len_x );
            this->QccWAVCDF97AnalysisSymmetricEvenEven( buf_ptr, len_x );
            // pub back the resluts in low-pass and high-pass groups
            m_gather_even( pos, buf_ptr, len_x );
        }
    }
    else                    // Odd length
    {
        for( long i = 0; i < len_y; i++ )
        {
            auto* pos = plane + i * m_dim_x;
            std::memcpy( buf_ptr, pos, sizeof(double) * len_x );
            this->QccWAVCDF97AnalysisSymmetricOddEven( buf_ptr, len_x );
            // pub back the resluts in low-pass and high-pass groups
            m_gather_odd( pos, buf_ptr, len_x );
        }
    }

    // Second, perform DWT along Y for every column
    if( len_y % 2 == 0 )    // Even length
    {
        for( long x = 0; x < len_x; x++ )
        {
            for( long y = 0; y < len_y; y++ )
                buf_ptr[y] = plane[ y * m_dim_x + x ];
            this->QccWAVCDF97AnalysisSymmetricEvenEven( buf_ptr, len_y );
            // Re-organize the resluts in low-pass and high-pass groups
            m_gather_even( buf_ptr2, buf_ptr, len_y );
            for( long y = 0; y < len_y; y++ )
                plane[ y * m_dim_x + x ] = buf_ptr2[y];
        }
    }
    else                    // Odd length
    {
        for( long x = 0; x < len_x; x++ )
        {
            for( long y = 0; y < len_y; y++ )
                buf_ptr[y] = plane[ y * m_dim_x + x ];
            this->QccWAVCDF97AnalysisSymmetricOddEven( buf_ptr, len_y );
            // Re-organize the resluts in low-pass and high-pass groups
            m_gather_odd( buf_ptr2, buf_ptr, len_y );
            for( long y = 0; y < len_y; y++ )
                plane[ y * m_dim_x + x ] = buf_ptr2[y];
        }
    }
}
    

void speck::CDF97::m_idwt2d_one_level( double* plane, long len_x, long len_y )
{
    assert( len_x <= m_dim_x && len_y <= m_dim_y );

    // Create temporary buffers to work on
    long len_xy = std::max( len_x, len_y );
    buffer_type buffer = std::make_unique<double[]>( len_xy * 2 );
    double *const buf_ptr  = buffer.get();       // First half of the array
    double *const buf_ptr2 = buf_ptr + len_xy;   // Second half of the array

    // First, perform IDWT along Y for every column
    if( len_y % 2 == 0 )    // Even length
    {
        for( long x = 0; x < len_x; x++ )
        {
            for( long y = 0; y < len_y; y++ )
                buf_ptr[y] = plane[ y * m_dim_x + x ];
            // Re-organize the coefficients as interleaved low-pass and high-pass ones
            m_scatter_even( buf_ptr2, buf_ptr, len_y );
            this->QccWAVCDF97SynthesisSymmetricEvenEven( buf_ptr2, len_y );
            for( long y = 0; y < len_y; y++ )
                plane[ y * m_dim_x + x ] = buf_ptr2[y];
        }
    }
    else                    // Odd length
    {
        for( long x = 0; x < len_x; x++ )
        {
            for( long y = 0; y < len_y; y++ )
                buf_ptr[y] = plane[ y * m_dim_x + x ];
            // Re-organize the coefficients as interleaved low-pass and high-pass ones
            m_scatter_odd( buf_ptr2, buf_ptr, len_y );
            this->QccWAVCDF97SynthesisSymmetricOddEven( buf_ptr2, len_y );
            for( long y = 0; y < len_y; y++ )
                plane[ y * m_dim_x + x ] = buf_ptr2[y];
        }
    }

    // Second, perform IDWT along X for every row
    if( len_x % 2 == 0 )    // Even length
    {
        for( long i = 0; i < len_y; i++ )
        {
            auto* pos = plane + i * m_dim_x;
            // Re-organize the coefficients as interleaved low-pass and high-pass ones
            m_scatter_even( buf_ptr, pos, len_x );
            this->QccWAVCDF97SynthesisSymmetricEvenEven( buf_ptr, len_x );
            std::memcpy( pos, buf_ptr, sizeof(double) * len_x );
        }
    }
    else                    // Odd length
    {
        for( long i = 0; i < len_y; i++ )
        {
            auto* pos = plane + i * m_dim_x;
            // Re-organize the coefficients as interleaved low-pass and high-pass ones
            m_scatter_odd( buf_ptr, pos, len_x );
            this->QccWAVCDF97SynthesisSymmetricOddEven( buf_ptr, len_x );
            std::memcpy( pos, buf_ptr, sizeof(double) * len_x );
        }
    }
}

long speck::CDF97::m_num_of_levels_xy() const
{
    assert( m_dim_x > 0 && m_dim_y > 0 );
    const auto min_xy = std::min( m_dim_x, m_dim_y );
    float f     = std::log2(float(min_xy) / 9.0f);  // 9.0f for CDF 9/7 kernel
    long num_level_xy = f < 0.0f ? 0 : long(f) + 1;

    // Treat this special case which would occur with power of 2 lengths
    if( m_calc_approx_len( min_xy, num_level_xy - 1 ) == 8 )
        num_level_xy++;

    return num_level_xy;
}


long speck::CDF97::m_num_of_levels_z() const
{
    assert( m_dim_z > 0 );
    float f      = std::log2( float(m_dim_z) / 9.0f ); // 9.0f for CDF 9/7 kernel
    long num_level_z = f < 0.0f ? 0 : long(f) + 1;

    // Treat this special case which would occur with power of 2 lengths
    if( m_calc_approx_len( m_dim_z, num_level_z - 1 ) == 8 )
        num_level_z++;

    return num_level_z;
}


long speck::CDF97::m_calc_approx_len( long orig_len, long lev ) const
{
    assert( lev >= 0 );
    long low_len = orig_len;
    for( long i = 0; i < lev; i++ )
        low_len = low_len % 2 == 0 ? low_len / 2 : (low_len + 1) / 2;
    
    return low_len;
}


void speck::CDF97::m_gather_even( double* dest, const double* orig, long len ) const
{
    assert( len % 2 == 0 ); // This function specifically for even length input
    long low_count = len / 2, high_count = len / 2; 
                            // How many low-pass and high-pass elements?
    long counter = 0;
    for( long i = 0; i < low_count; i++ )
        dest[counter++] = orig[i*2];
    for( long i = 0; i < high_count; i++ )
        dest[counter++] = orig[i*2+1];
}
void speck::CDF97::m_gather_odd( double* dest, const double* orig, long len ) const
{
    assert( len % 2 == 1 ); // This function specifically for odd length input
    long low_count = len / 2 + 1, high_count = len / 2; 
                            // How many low-pass and high-pass elements?
    long counter = 0;
    for( long i = 0; i < low_count; i++ )
        dest[counter++] = orig[i*2];
    for( long i = 0; i < high_count; i++ )
        dest[counter++] = orig[i*2+1];
}


void speck::CDF97::m_scatter_even( double* dest, const double* orig, long len ) const
{
    assert( len % 2 == 0 ); // This function specifically for even length input
    long low_count = len / 2, high_count = len / 2;
                            // How many low-pass and high-pass elements?
    long counter = 0;
    for( long i = 0; i < low_count; i++ )
        dest[i*2]   = orig[counter++];
    for( long i = 0; i < high_count; i++ )
        dest[i*2+1] = orig[counter++];
}
void speck::CDF97::m_scatter_odd( double* dest, const double* orig, long len ) const
{
    assert( len % 2 == 1 ); // This function specifically for odd length input
    long low_count = len / 2 + 1, high_count = len / 2;
                            // How many low-pass and high-pass elements?
    long counter = 0;
    for( long i = 0; i < low_count; i++ )
        dest[i*2]   = orig[counter++];
    for( long i = 0; i < high_count; i++ )
        dest[i*2+1] = orig[counter++];
}


//
// Methods from QccPack
//
void speck::CDF97::QccWAVCDF97AnalysisSymmetricEvenEven( double* signal, 
                                                         long signal_length)
{
    long index;

    for (index = 1; index < signal_length - 2; index += 2)
        signal[index] += ALPHA * (signal[index - 1] + signal[index + 1]);

    signal[signal_length - 1] += 2.0 * ALPHA * signal[signal_length - 2];
    signal[0] += 2.0 * BETA * signal[1];

    for (index = 2; index < signal_length; index += 2)
        signal[index] += BETA * (signal[index + 1] + signal[index - 1]);

    for (index = 1; index < signal_length - 2; index += 2)
        signal[index] +=  GAMMA * (signal[index - 1] + signal[index + 1]);

    signal[signal_length - 1] += 2.0 * GAMMA * signal[signal_length - 2];
    signal[0] = EPSILON * (signal[0] + 2.0 * DELTA * signal[1]);

    for (index = 2; index < signal_length; index += 2)
        signal[index] =  EPSILON * (signal[index] + 
                         DELTA * (signal[index + 1] + signal[index - 1]));

    for (index = 1; index < signal_length; index += 2)
        signal[index] /= (-EPSILON);
}


void speck::CDF97::QccWAVCDF97SynthesisSymmetricEvenEven( double* signal, 
                                                          long signal_length)
{
    long index;

    for (index = 1; index < signal_length; index += 2)
        signal[index] *= (-EPSILON);

    signal[0] = signal[0]/EPSILON - 2.0 * DELTA * signal[1];

    for (index = 2; index < signal_length; index += 2)
        signal[index] = signal[index]/EPSILON - 
                        DELTA * (signal[index + 1] + signal[index - 1]);

    for (index = 1; index < signal_length - 2; index += 2)
        signal[index] -= GAMMA * (signal[index - 1] + signal[index + 1]);

    signal[signal_length - 1] -= 2.0 * GAMMA * signal[signal_length - 2];
    signal[0] -= 2.0 * BETA * signal[1];

    for (index = 2; index < signal_length; index += 2)
        signal[index] -= BETA * (signal[index + 1] + signal[index - 1]);

    for (index = 1; index < signal_length - 2; index += 2)
        signal[index] -= ALPHA * (signal[index - 1] + signal[index + 1]);

    signal[signal_length - 1] -= 2.0 * ALPHA * signal[signal_length - 2];
}


void speck::CDF97::QccWAVCDF97SynthesisSymmetricOddEven( double* signal,
                                                         long signal_length)
{
  long index;
  
  for (index = 1; index < signal_length - 1; index += 2)
    signal[index] *= (-EPSILON);

  signal[0] = signal[0] / EPSILON - 2.0 * DELTA * signal[1];

  for (index = 2; index < signal_length - 2; index += 2)
    signal[index] = signal[index] / EPSILON - 
                    DELTA * (signal[index + 1] + signal[index - 1]);

  signal[signal_length - 1] = signal[signal_length - 1] / EPSILON - 
                              2.0 * DELTA * signal[signal_length - 2];

  for (index = 1; index < signal_length - 1; index += 2)
    signal[index] -= GAMMA * (signal[index - 1] + signal[index + 1]);

  signal[0] -= 2.0 * BETA * signal[1];

  for (index = 2; index < signal_length - 2; index += 2)
    signal[index] -= BETA * (signal[index + 1] + signal[index - 1]);

  signal[signal_length - 1] -= 2.0 * BETA * signal[signal_length - 2];

  for (index = 1; index < signal_length - 1; index += 2)
    signal[index] -= ALPHA * (signal[index - 1] + signal[index + 1]);
}


void speck::CDF97::QccWAVCDF97AnalysisSymmetricOddEven(double*  signal,
                                                       long signal_length)
{
  long index;

  for (index = 1; index < signal_length - 1; index += 2)
    signal[index] += ALPHA * (signal[index - 1] + signal[index + 1]);
  
  signal[0] += 2.0 * BETA * signal[1];

  for (index = 2; index < signal_length - 2; index += 2)
    signal[index] += BETA * (signal[index + 1] + signal[index - 1]);

  signal[signal_length - 1] += 2.0 * BETA * signal[signal_length - 2];

  for (index = 1; index < signal_length - 1; index += 2)
    signal[index] += GAMMA * (signal[index - 1] + signal[index + 1]);
  
  signal[0] = EPSILON * (signal[0] + 2.0 * DELTA * signal[1]);

  for (index = 2; index < signal_length - 2; index += 2)
    signal[index] = EPSILON * (signal[index] + 
                    DELTA * (signal[index + 1] + signal[index - 1]));

  signal[signal_length - 1] = EPSILON * (signal[signal_length - 1] +
                              2 * DELTA * signal[signal_length - 2]);

  for (index = 1; index < signal_length - 1; index += 2)
    signal[index] /= (-EPSILON);
}


//
// For debug only 
//
const double* speck::CDF97::get_data() const
{
    return m_data_buf.get();
}
double speck::CDF97::get_mean() const
{
    return m_data_mean;
}
