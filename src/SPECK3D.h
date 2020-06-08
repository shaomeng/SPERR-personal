#ifndef SPECK3D_H
#define SPECK3D_H

#include "SPECK_Storage.h"

namespace speck {

//
// Auxiliary class to hold a SPECK Set
//
class SPECKSet3D {
public:
    //
    // Member data
    //
    uint32_t start_x  = 0;
    uint32_t start_y  = 0;
    uint32_t start_z  = 0;
    uint32_t length_x = 0;
    uint32_t length_y = 0;
    uint32_t length_z = 0;
    // which partition level is this set at (starting from zero, in all 3 directions).
    // This data member is the sum of all 3 partition levels.
    uint16_t     part_level = 0;
    Significance signif     = Significance::Insig;
    SetType      type       = SetType::TypeS; // This field is only used to indicate garbage status

public:
    //
    // Member functions
    //
    auto is_pixel() const -> bool;
    auto is_empty() const -> bool;
#ifdef PRINT
    void print() const;
#endif
};

//
// Main SPECK3D class
//
class SPECK3D : public SPECK_Storage {
public:
    // trivial input
    void set_dims(size_t, size_t, size_t); // Accepts volume dimension
    void set_max_coeff_bits(int32_t);      // (Useful for reconstruction)
    void set_bit_budget(size_t);           // How many bits does speck process?

    // trivial output
    void get_dims(size_t&, size_t&, size_t&) const;

    // core operations
    auto encode() -> int;
    auto decode() -> int;
    auto write_to_disk(const std::string& filename) const -> int override;
    auto read_from_disk(const std::string& filename) -> int override;

private:
    //
    // Note: for methods returning an integer, 0 means normal execution, and
    // 1 means bit budget met.
    //
    auto m_ready_to_encode() const -> bool;
    auto m_ready_to_decode() const -> bool;
    // How many partition operation could we perform in each direction?
    void m_num_of_partitions(std::array<size_t, 3>&) const;
    void m_clean_LIS(); // Clean garbage sets from m_LIS if too much garbage exists.
    void m_initialize_sets_lists();
    auto m_sorting_pass_encode() -> int;
    auto m_sorting_pass_decode() -> int;
    auto m_refinement_pass_encode() -> int;
    auto m_refinement_pass_decode() -> int;

    // For the following 5 methods, indices are used to locate which set to process from m_LIS,
    auto m_process_S_encode(size_t idx1, size_t idx2) -> int;
    auto m_process_S_decode(size_t idx1, size_t idx2) -> int;
    auto m_code_S(size_t idx1, size_t idx2) -> int;
    auto m_process_P_encode(size_t idx) -> int; // Same functionality as process_S, but specifically
    auto m_process_P_decode(size_t idx) -> int; // designed for sets that are essentially pixels.

    void m_partition_S_XYZ(const SPECKSet3D& set, std::array<SPECKSet3D, 8>& subsets) const;
    void m_partition_S_XY(const SPECKSet3D& set, std::array<SPECKSet3D, 4>& subsets) const;
    void m_partition_S_Z(const SPECKSet3D& set, std::array<SPECKSet3D, 2>& subsets) const;

    //
    // Private data members
    //
    double  m_threshold      = 0.0; // Threshold that's used for quantization
    size_t  m_budget         = 0;   // What's the budget for num of bits?
    size_t  m_bit_idx        = 0;   // Used for decode. Which bit we're at?
    size_t  m_dim_x          = 0;   // 3D volume dims
    size_t  m_dim_y          = 0;
    size_t  m_dim_z          = 0;
    bool    m_encode_mode    = true; // Encode (true) or Decode (false) mode?
    int32_t m_max_coeff_bits = 0;    // = log2(max_coefficient)

    std::vector<std::vector<SPECKSet3D>> m_LIS;
    std::vector<size_t>                  m_LIS_garbage_cnt;

    std::vector<bool> m_significance_map; // only used when encoding.
    std::vector<bool> m_sign_array;

    std::vector<size_t> m_LSP;       // Records locations of significant pixels
    std::vector<bool>   m_LSP_newly; // Records if this pixel is newly significant or not.

    // Now we use a vector of indices to serve the same funcationality of the last LIS,
    // which would contain all insignificant pixels.
    std::vector<size_t> m_LIP;         // List of insignificant pixels.
    std::vector<bool>   m_LIP_garbage; // If this insignificant pixel is considered garbage.
    size_t              m_LIP_garbage_cnt = 0;
};

};

#endif
