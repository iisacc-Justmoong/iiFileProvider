#include "CTestSupport.h"

#include <sstream>
#include <stdexcept>
#include <string>

int main()
{
    std::ostringstream errors;
    std::ostringstream output;
    auto* previous = std::cerr.rdbuf(errors.rdbuf());
    auto* previousOutput = std::cout.rdbuf(output.rdbuf());
    provider_test::CTestRunner empty;
    const int emptyResult = empty.result();
    provider_test::CTestRunner failure;
    bool continued = false;
    failure.run("bad-row", [] { CTEST_CHECK(false); });
    failure.run("next-row", [&] { continued = true; });
    const int failureResult = failure.result();
    std::cerr.rdbuf(previous);
    std::cout.rdbuf(previousOutput);
    if (emptyResult == 0 || failureResult == 0 || !continued ||
        errors.str().find("No test cases") == std::string::npos ||
        errors.str().find("bad-row") == std::string::npos ||
        errors.str().find("ctest_support.cpp:") == std::string::npos)
        return 1;

    provider_test::CTestRunner success;
    success.run("checks", [] {
        int evaluations = 0;
        CTEST_CHECK(true);
        CTEST_COMPARE(++evaluations, 1);
        CTEST_COMPARE(evaluations, 1);
        CTEST_THROWS(std::runtime_error, throw std::runtime_error("expected"));
    });
    return success.result();
}
