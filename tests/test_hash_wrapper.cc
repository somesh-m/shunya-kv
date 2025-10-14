#include "hash_wrapper.hh"
#include <gtest/gtest.h>
#include <string_view>

using shunyakv::fnv1a64;
using shunyakv::owner_shard;

std::map<std::string, uint64_t> kHashes = {
    {"name", UINT64_C(14176396743819860870)},
    {"age", UINT64_C(16651413216827089244)},
    {"gender", UINT64_C(11674224626475531968)},
    {"address", UINT64_C(1673076945917317811)},
    {"url", UINT64_C(5498485135592963710)},
    {"cache", UINT64_C(2032197906964685637)},
    {"first_name", UINT64_C(4569966122621998145)},
    {"last_name", UINT64_C(8253495473240048185)},
    {"fatherName", UINT64_C(12775831372648624840)},
    {"motherName", UINT64_C(13787813695310249985)},
    {"dob", UINT64_C(14604960393487220226)},
    {"random_key", UINT64_C(3065159885349690590)},
    {"randomKey", UINT64_C(6213936310748362569)},
};

TEST(Wrapper, Vectors) {
    for (const auto &kv : kHashes) {
        EXPECT_EQ(fnv1a64(kv.first), kv.second) << "key=" << kv.first;
    }
}

TEST(OwnerShard, TestOwnerShard) {
    const unsigned smp = 16, first = 1;
    // Check a few hashes map into [1..16)
    for (const auto &hashkey : kHashes) {
        const auto h = hashkey.second; // already the FNV-1a64 of the key
        const unsigned shard = owner_shard(h, smp, first);
        EXPECT_GE(shard, first) << hashkey.first;
        EXPECT_LT(shard, smp) << hashkey.first;
        // TODO : Add test to match exact shard number
    }
}
