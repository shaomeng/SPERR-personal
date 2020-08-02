#include "SPECK3D.h"
#include "CDF97.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <chrono>


extern "C"  // C Function calls, and don't include the C header!
{
    int sam_read_n_bytes( const char*, size_t, void* );
    int sam_write_n_doubles( const char*, size_t, const double* );
    int sam_get_statsd( const double* arr1, const double* arr2, size_t len,
                        double* rmse,       double* lmax,   double* psnr,
                        double* arr1min,    double* arr1max            );
}


int main( int argc, char* argv[] )
{

    if( argc != 6 )
    {

#ifdef QZ_TERM
        std::cerr << "Usage: ./a.out input_filename dim_x dim_y dim_z qz_levels" << std::endl;
#else
        std::cerr << "Usage: ./a.out input_filename dim_x dim_y dim_z cratio" << std::endl;
#endif

        return 1;
    }

    const char*   input  = argv[1];
    const char*   output = "sam.tmp";
    const size_t  dim_x  = std::atol( argv[2] );
    const size_t  dim_y  = std::atol( argv[3] );
    const size_t  dim_z  = std::atol( argv[4] );
    const size_t  total_vals = dim_x * dim_y * dim_z;

#ifdef QZ_TERM
    const int     qz_levels = std::atoi( argv[5] );
#else
    const float   cratio = std::atof( argv[5] );
#endif

#ifdef NO_CPP14
    speck::buffer_type_f in_buf( new float[total_vals] );
#else
    speck::buffer_type_f in_buf = std::make_unique<float[]>( total_vals );
#endif

    // Let's read in binaries as 4-byte floats
    if( sam_read_n_bytes( input, sizeof(float) * total_vals, in_buf.get() ) )
    {
        std::cerr << "Input read error!" << std::endl;
        return 1;
    }

    // Take input to go through DWT.
    speck::CDF97 cdf;
    cdf.set_dims( dim_x, dim_y, dim_z );
    cdf.copy_data( in_buf, total_vals );
    const auto startT = std::chrono::high_resolution_clock::now();
    cdf.dwt3d();

    // Do a speck encoding
    speck::SPECK3D encoder;
    encoder.set_dims( dim_x, dim_y, dim_z );
    encoder.set_image_mean( cdf.get_mean() );
    encoder.take_coeffs( cdf.release_data(), total_vals );

#ifdef QZ_TERM
    encoder.set_quantization_iterations( qz_levels );
    //encoder.set_quantization_term_level( qz_levels );
#else
    const size_t total_bits = size_t(32.0f * total_vals / cratio);
    encoder.set_bit_budget( total_bits );
#endif

    encoder.encode();
    encoder.write_to_disk( output );

    // Do a speck decoding
    speck::SPECK3D  decoder;
    decoder.read_from_disk( output );

#ifdef QZ_TERM
    decoder.set_bit_budget( 0 );
#else
    decoder.set_bit_budget( total_bits );
#endif

    decoder.decode();

    // Do an inverse wavelet transform
    speck::CDF97 idwt;
    size_t dim_x_r, dim_y_r, dim_z_r;
    decoder.get_dims( dim_x_r, dim_y_r, dim_z_r );
    idwt.set_dims( dim_x_r, dim_y_r, dim_z_r );
    idwt.set_mean( decoder.get_image_mean() );
    idwt.take_data( decoder.release_coeffs_double(), dim_x_r * dim_y_r * dim_z_r );
    idwt.idwt3d();

    // Finish timer and print timing
    const auto endT   = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> diffT  = endT - startT;
    std::cout << "Time for SPECK in milliseconds: " << diffT.count() * 1000.0f << std::endl;

    // Compare the result with the original input in double precision

#ifdef NO_CPP14
    speck::buffer_type_d in_bufd( new double[ total_vals ] );
#else
    speck::buffer_type_d in_bufd = std::make_unique<double[]>( total_vals );
#endif

    for( size_t i = 0; i < total_vals; i++ )
        in_bufd[i] = in_buf[i];

    double rmse, lmax, psnr, arr1min, arr1max;
    sam_get_statsd( in_bufd.get(), idwt.get_read_only_data().get(), 
                    total_vals, &rmse, &lmax, &psnr, &arr1min, &arr1max );
    printf("Sam: rmse = %f, lmax = %f, psnr = %fdB, orig_min = %f, orig_max = %f\n", 
            rmse, lmax, psnr, arr1min, arr1max );

#ifdef QZ_TERM
    float bpp = float(encoder.get_num_of_bits()) / float(total_vals);
    printf("With %d levels of quantization, average BPP = %f, and qz terminates at level %d\n",
            qz_levels, bpp, encoder.get_quantization_term_level() );
#endif


#ifdef EXPERIMENT
    // Experiment 1: 
    // Sort the differences and then write a tenth of it to disk.
    std::vector<speck::Outlier> LOS( total_vals, speck::Outlier{} );
    for( size_t i = 0; i < total_vals; i++ ) {
        LOS[i].location  = i;
        LOS[i].error = in_buf[i] - float(idwt.get_read_only_data()[i]);
    }
    
    const size_t num_of_outliers = total_vals / 10;
    std::partial_sort( LOS.begin(), LOS.begin() + num_of_outliers, LOS.end(), 
        [](auto& a, auto& b) { return (std::abs(a.error) > std::abs(b.error)); } );

    for( size_t i = 0; i < 10; i++ )
        printf("outliers: (%ld, %f)\n", LOS[i].location, LOS[i].error );

    std::ofstream file( "top_outliers", std::ios::binary );
    if( file.is_open() ) {
        file.write( reinterpret_cast<char*>(LOS.data()), sizeof(speck::Outlier) * num_of_outliers );
        file.close();
    }
#endif

}
