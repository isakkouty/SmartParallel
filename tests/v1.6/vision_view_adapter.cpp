#include <smart/vision/data_view_adapter.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main()
{
    std::vector<unsigned char> pixels(4 * 24, 0);
    smart::vision::ImageView<unsigned char> image{pixels.data(), 5, 4, 24, 3};
    auto matrix = smart::vision::as_element_matrix_view(image);
    require(matrix.extent(0) == 4 && matrix.extent(1) == 15
                && matrix.stride(0) == 24 && matrix.stride(1) == 1,
            "ImageView adapter shape mismatch");
    matrix(2, 7) = 42;
    require(pixels[2 * 24 + 7] == 42, "ImageView adapter indexing mismatch");

    auto roundtrip = smart::vision::as_image_view(matrix, 5, 3);
    require(roundtrip.data == image.data && roundtrip.width == image.width
                && roundtrip.height == image.height
                && roundtrip.stride_bytes == image.stride_bytes
                && roundtrip.channels == image.channels,
            "ImageView adapter roundtrip mismatch");

    smart::vision::ImageView<const unsigned char> const_image{
        pixels.data(), 5, 4, 24, 3};
    auto const_matrix = smart::vision::as_element_matrix_view(const_image);
    require(const_matrix(2, 7) == 42 && const_matrix.size() == 60,
            "const ImageView adapter mismatch");

    bool byte_alignment_threw = false;
    try
    {
        std::vector<std::uint16_t> words(32, 0);
        smart::vision::ImageView<std::uint16_t> invalid{
            words.data(), 3, 2, 7, 1};
        (void)smart::vision::as_element_matrix_view(invalid);
    }
    catch (const std::invalid_argument&) { byte_alignment_threw = true; }
    require(byte_alignment_threw, "misaligned ImageView byte stride was accepted");

    bool column_stride_threw = false;
    try
    {
        auto non_unit = smart::data::MatrixView<unsigned char>(
            pixels.data(), {4, 5}, {24, 2});
        (void)smart::vision::as_image_view(non_unit, 5, 1);
    }
    catch (const std::invalid_argument&) { column_stride_threw = true; }
    require(column_stride_threw, "non-unit matrix column stride was accepted as ImageView");

    bool overlapping_rows_threw = false;
    try
    {
        auto overlapping = smart::data::MatrixView<unsigned char>(
            pixels.data(), {4, 15}, {10, 1});
        (void)smart::vision::as_image_view(overlapping, 5, 3);
    }
    catch (const std::invalid_argument&) { overlapping_rows_threw = true; }
    require(overlapping_rows_threw, "overlapping matrix rows were accepted as ImageView");

    return 0;
}
