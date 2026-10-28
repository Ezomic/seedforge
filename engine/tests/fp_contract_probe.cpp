// Built with physics.cpp's instruction set (FMA available, see tests/CMakeLists.txt) and kept out
// of test_fp_contract.cpp so the compiler cannot fold the calls: a contraction flag fuses these.
namespace ichor::fp_probe {

float mul_add(float a, float b, float c)
{
    return a * b + c;
}

double mul_add(double a, double b, double c)
{
    return a * b + c;
}

}
