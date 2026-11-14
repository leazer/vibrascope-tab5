#include "framework_contract.hpp"
#include "test_support.hpp"

void run_framework_contract_tests()
{
    constexpr auto version = vhm::framework::kLvglVersion;
    EXPECT_TRUE(version.major == 9U);
    EXPECT_TRUE(version.minor == 2U);
    EXPECT_TRUE(version.patch == 2U);
    EXPECT_TRUE(vhm::framework::compiled_platform() == vhm::framework::Platform::Desktop);
    EXPECT_TRUE(vhm::framework::compiled_hal_factory() == vhm::framework::HalFactory::Desktop);
}
