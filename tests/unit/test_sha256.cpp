// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/sha256.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace etil::core;

// Helper: convert digest to lowercase hex string for comparison
static std::string to_hex(const std::array<uint8_t, 32>& digest) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (uint8_t b : digest) {
        result.push_back(hex[b >> 4]);
        result.push_back(hex[b & 0x0F]);
    }
    return result;
}

// Reference vectors from FIPS 180-4 and `sha256sum` utility.

TEST(Sha256Test, EmptyString) {
    auto d = sha256(std::string(""));
    EXPECT_EQ(to_hex(d),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256Test, Abc) {
    auto d = sha256(std::string("abc"));
    EXPECT_EQ(to_hex(d),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, Hello) {
    auto d = sha256(std::string("hello"));
    EXPECT_EQ(to_hex(d),
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

// Padding edge cases — these exercise the block boundary logic.

TEST(Sha256Test, Length55Bytes) {
    // 55 bytes: fits in one block with padding byte + 8-byte length
    auto d = sha256(std::string(55, 'x'));
    EXPECT_EQ(to_hex(d),
        "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
}

TEST(Sha256Test, Length56Bytes) {
    // 56 bytes: padding byte doesn't fit before length -> requires 2 blocks
    auto d = sha256(std::string(56, 'x'));
    EXPECT_EQ(to_hex(d),
        "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
}

TEST(Sha256Test, Length63Bytes) {
    auto d = sha256(std::string(63, 'x'));
    EXPECT_EQ(to_hex(d),
        "75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2");
}

TEST(Sha256Test, Length64Bytes) {
    // Exact block boundary
    auto d = sha256(std::string(64, 'x'));
    EXPECT_EQ(to_hex(d),
        "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
}

TEST(Sha256Test, MultiBlock) {
    // 90 bytes: spans 2 blocks, data crosses boundary
    std::string s = "The quick brown fox jumps over the lazy dog. "
                    "The quick brown fox jumps over the lazy dog.";
    auto d = sha256(s);
    EXPECT_EQ(to_hex(d),
        "635241ac823ee4a81fbb410c92be616b0a89191083d8d7b5d232c823dc8df4f5");
}

TEST(Sha256Test, ByteVectorOverload) {
    std::vector<uint8_t> v = {'a', 'b', 'c'};
    auto d = sha256(v);
    EXPECT_EQ(to_hex(d),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, RawPointerOverload) {
    const uint8_t data[] = {'a', 'b', 'c'};
    auto d = sha256(data, 3);
    EXPECT_EQ(to_hex(d),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256Test, LongInput) {
    // 1000 'a' characters — reference from `python3 -c "print('a'*1000, end='')" | sha256sum`
    auto d = sha256(std::string(1000, 'a'));
    EXPECT_EQ(to_hex(d),
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}
