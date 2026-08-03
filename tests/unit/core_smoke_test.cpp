#include <array>
#include <span>

#include <gtest/gtest.h>

TEST(Stage2SmokeTest, UsesCxx20StandardLibrary) {
    static constexpr std::array values{2, 4, 6};
    constexpr std::span view(values);

    static_assert(view.size() == 3);
    EXPECT_EQ(view.back(), 6);
}
