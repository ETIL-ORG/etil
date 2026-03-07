// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/metadata.hpp"
#include "etil/core/metadata_json.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

// ===== MetadataMap =====

TEST(MetadataMapTest, SetAndGet) {
    MetadataMap map;
    map.set("desc", MetadataFormat::Text, "A description");
    auto entry = map.get("desc");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->key, "desc");
    EXPECT_EQ(entry->format, MetadataFormat::Text);
    EXPECT_EQ(entry->content, "A description");
}

TEST(MetadataMapTest, GetNonexistent) {
    MetadataMap map;
    EXPECT_FALSE(map.get("missing").has_value());
}

TEST(MetadataMapTest, Overwrite) {
    MetadataMap map;
    map.set("key", MetadataFormat::Text, "old");
    map.set("key", MetadataFormat::Markdown, "new");
    auto entry = map.get("key");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->format, MetadataFormat::Markdown);
    EXPECT_EQ(entry->content, "new");
    EXPECT_EQ(map.size(), 1u);
}

TEST(MetadataMapTest, Remove) {
    MetadataMap map;
    map.set("key", MetadataFormat::Text, "value");
    EXPECT_TRUE(map.remove("key"));
    EXPECT_FALSE(map.has("key"));
    EXPECT_EQ(map.size(), 0u);
}

TEST(MetadataMapTest, RemoveNonexistent) {
    MetadataMap map;
    EXPECT_FALSE(map.remove("missing"));
}

TEST(MetadataMapTest, Keys) {
    MetadataMap map;
    map.set("a", MetadataFormat::Text, "1");
    map.set("b", MetadataFormat::Json, "2");
    auto keys = map.keys();
    EXPECT_EQ(keys.size(), 2u);
    // Order is not guaranteed; just check both are present
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "a") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "b") != keys.end());
}

TEST(MetadataMapTest, Has) {
    MetadataMap map;
    map.set("x", MetadataFormat::Code, "code");
    EXPECT_TRUE(map.has("x"));
    EXPECT_FALSE(map.has("y"));
}

TEST(MetadataMapTest, SizeAndEmpty) {
    MetadataMap map;
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0u);
    map.set("k", MetadataFormat::Text, "v");
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(map.size(), 1u);
}

// ===== WordImpl metadata =====

TEST(WordImplMetadataTest, SetAndRetrieve) {
    auto* impl = new WordImpl("test", 1);
    WordImplPtr ptr(impl);
    ptr->metadata().set("author", MetadataFormat::Text, "Claude");
    auto entry = ptr->metadata().get("author");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->content, "Claude");
}

// ===== WordConcept metadata =====

TEST(WordConceptMetadataTest, SetAndRetrieve) {
    WordConcept wc;
    wc.name = "dup";
    wc.metadata.set("help", MetadataFormat::Text, "Duplicate top of stack");
    auto entry = wc.metadata.get("help");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->content, "Duplicate top of stack");
}

// ===== Dictionary concept metadata =====

TEST(DictionaryMetadataTest, SetGetRemoveKeys) {
    Dictionary dict;
    auto* impl = new WordImpl("myword", Dictionary::next_id());
    dict.register_word("myword", WordImplPtr(impl));

    EXPECT_TRUE(dict.set_concept_metadata("myword", "desc",
                                          MetadataFormat::Text, "A word"));
    auto entry = dict.get_concept_metadata("myword", "desc");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->content, "A word");

    auto keys = dict.concept_metadata_keys("myword");
    EXPECT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], "desc");

    EXPECT_TRUE(dict.remove_concept_metadata("myword", "desc"));
    EXPECT_FALSE(dict.get_concept_metadata("myword", "desc").has_value());
}

TEST(DictionaryMetadataTest, NonexistentConcept) {
    Dictionary dict;
    EXPECT_FALSE(dict.set_concept_metadata("nope", "k",
                                           MetadataFormat::Text, "v"));
    EXPECT_FALSE(dict.get_concept_metadata("nope", "k").has_value());
    EXPECT_FALSE(dict.remove_concept_metadata("nope", "k"));
    EXPECT_TRUE(dict.concept_metadata_keys("nope").empty());
}

// ===== JSON serialization =====

TEST(MetadataJsonTest, EntryRoundTrip) {
    MetadataEntry entry{"desc", MetadataFormat::Markdown, "# Hello"};
    auto j = to_json(entry);
    auto restored = metadata_entry_from_json(j);
    EXPECT_EQ(restored.key, "desc");
    EXPECT_EQ(restored.format, MetadataFormat::Markdown);
    EXPECT_EQ(restored.content, "# Hello");
}

TEST(MetadataJsonTest, MapRoundTrip) {
    MetadataMap map;
    map.set("a", MetadataFormat::Text, "alpha");
    map.set("b", MetadataFormat::Json, "{\"x\":1}");
    auto j = to_json(map);
    auto restored = metadata_map_from_json(j);
    EXPECT_EQ(restored.size(), 2u);
    auto a = restored.get("a");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->content, "alpha");
    EXPECT_EQ(a->format, MetadataFormat::Text);
    auto b = restored.get("b");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->content, "{\"x\":1}");
    EXPECT_EQ(b->format, MetadataFormat::Json);
}

TEST(MetadataJsonTest, WordImplWithMetadata) {
    auto* impl = new WordImpl("test_word", 42);
    WordImplPtr ptr(impl);
    ptr->set_generation(2);
    ptr->set_weight(0.75);
    ptr->metadata().set("author", MetadataFormat::Text, "Claude");

    auto j = word_impl_to_json(*ptr);
    EXPECT_EQ(j["name"], "test_word");
    EXPECT_EQ(j["id"], 42u);
    EXPECT_EQ(j["generation"], 2u);
    EXPECT_DOUBLE_EQ(j["weight"].get<double>(), 0.75);
    EXPECT_TRUE(j.contains("metadata"));
    EXPECT_TRUE(j["metadata"].contains("author"));
    EXPECT_EQ(j["metadata"]["author"]["content"], "Claude");
}

TEST(MetadataJsonTest, FormatToString) {
    EXPECT_EQ(format_to_string(MetadataFormat::Text), "text");
    EXPECT_EQ(format_to_string(MetadataFormat::Markdown), "markdown");
    EXPECT_EQ(format_to_string(MetadataFormat::Html), "html");
    EXPECT_EQ(format_to_string(MetadataFormat::Code), "code");
    EXPECT_EQ(format_to_string(MetadataFormat::Json), "json");
    EXPECT_EQ(format_to_string(MetadataFormat::Jsonl), "jsonl");
}

TEST(MetadataJsonTest, ParseMetadataFormat) {
    EXPECT_EQ(parse_metadata_format("text"), MetadataFormat::Text);
    EXPECT_EQ(parse_metadata_format("markdown"), MetadataFormat::Markdown);
    EXPECT_EQ(parse_metadata_format("html"), MetadataFormat::Html);
    EXPECT_EQ(parse_metadata_format("code"), MetadataFormat::Code);
    EXPECT_EQ(parse_metadata_format("json"), MetadataFormat::Json);
    EXPECT_EQ(parse_metadata_format("jsonl"), MetadataFormat::Jsonl);
    EXPECT_FALSE(parse_metadata_format("unknown").has_value());
}

// ===== Interpreter metadata words =====

// Helper: define the self-hosted builtins (normally loaded from builtins.til).
static void define_metadata_builtins(Interpreter& interp) {
    interp.interpret_line(": meta! dict-meta-set ;");
    interp.interpret_line(": meta@ dict-meta-get ;");
    interp.interpret_line(": meta-del dict-meta-del ;");
    interp.interpret_line(": meta-keys dict-meta-keys ;");
    interp.interpret_line(": impl-meta! impl-meta-set ;");
    interp.interpret_line(": impl-meta@ impl-meta-get ;");
}

class MetadataInterpreterTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::unique_ptr<Interpreter> interp;

    void SetUp() override {
        register_primitives(dict);
        interp = std::make_unique<Interpreter>(dict, out);
        define_metadata_builtins(*interp);
    }
};

// Helper: pop a Value and assert it exists.
static Value pop_value(Interpreter& interp) {
    auto v = interp.context().data_stack().pop();
    EXPECT_TRUE(v.has_value()) << "Expected a value on the stack";
    return v.value_or(Value(int64_t(0)));
}

// Helper: extract HeapString content from a Value.
static std::string heap_string_content(const Value& v) {
    EXPECT_EQ(v.type, Value::Type::String);
    auto* hs = v.as_string();
    std::string result(hs->view());
    v.release();
    return result;
}

TEST_F(MetadataInterpreterTest, MetaSetAndGet) {
    interp->interpret_line(": myword dup + ;");
    interp->interpret_line(R"(s" myword" s" desc" s" text" s" A word that doubles" meta!)");
    auto set_flag = pop_value(*interp);
    EXPECT_EQ(set_flag.type, Value::Type::Boolean);
    EXPECT_TRUE(set_flag.as_bool());

    interp->interpret_line(R"(s" myword" s" desc" meta@)");
    auto get_flag = pop_value(*interp);
    EXPECT_EQ(get_flag.type, Value::Type::Boolean);
    EXPECT_TRUE(get_flag.as_bool());
    auto content = pop_value(*interp);
    EXPECT_EQ(heap_string_content(content), "A word that doubles");
}

TEST_F(MetadataInterpreterTest, MetaDel) {
    interp->interpret_line(": myword dup + ;");
    interp->interpret_line(R"(s" myword" s" desc" s" text" s" hello" meta!)");
    pop_value(*interp).release(); // discard set flag

    interp->interpret_line(R"(s" myword" s" desc" meta-del)");
    auto del_flag = pop_value(*interp);
    EXPECT_EQ(del_flag.type, Value::Type::Boolean);
    EXPECT_TRUE(del_flag.as_bool());

    interp->interpret_line(R"(s" myword" s" desc" meta@)");
    auto get_flag = pop_value(*interp);
    EXPECT_EQ(get_flag.type, Value::Type::Boolean);
    EXPECT_FALSE(get_flag.as_bool());
}

TEST_F(MetadataInterpreterTest, MetaKeys) {
    interp->interpret_line(": myword dup + ;");
    interp->interpret_line(R"(s" myword" s" desc" s" text" s" hello" meta!)");
    pop_value(*interp).release();
    interp->interpret_line(R"(s" myword" s" author" s" text" s" claude" meta!)");
    pop_value(*interp).release();

    interp->interpret_line(R"(s" myword" meta-keys)");
    auto keys_flag = pop_value(*interp);
    EXPECT_EQ(keys_flag.type, Value::Type::Boolean);
    EXPECT_TRUE(keys_flag.as_bool());

    auto arr_val = pop_value(*interp);
    EXPECT_EQ(arr_val.type, Value::Type::Array);
    auto* arr = arr_val.as_array();
    // 2 user keys (desc, author); definition-type is now impl-level
    EXPECT_EQ(arr->length(), 2u);

    // Collect key names from array
    std::vector<std::string> key_names;
    for (size_t i = 0; i < arr->length(); ++i) {
        Value elem;
        ASSERT_TRUE(arr->get(i, elem));
        key_names.push_back(std::string(elem.as_string()->view()));
        elem.release();
    }
    EXPECT_TRUE(std::find(key_names.begin(), key_names.end(), "desc") != key_names.end());
    EXPECT_TRUE(std::find(key_names.begin(), key_names.end(), "author") != key_names.end());
    arr_val.release();
}

TEST_F(MetadataInterpreterTest, ImplMetaSetAndGet) {
    interp->interpret_line(": myword dup + ;");
    interp->interpret_line(R"(s" myword" s" version" s" text" s" v1.0" impl-meta!)");
    auto set_flag = pop_value(*interp);
    EXPECT_EQ(set_flag.type, Value::Type::Boolean);
    EXPECT_TRUE(set_flag.as_bool());

    interp->interpret_line(R"(s" myword" s" version" impl-meta@)");
    auto get_flag = pop_value(*interp);
    EXPECT_EQ(get_flag.type, Value::Type::Boolean);
    EXPECT_TRUE(get_flag.as_bool());
    auto content = pop_value(*interp);
    EXPECT_EQ(heap_string_content(content), "v1.0");
}

TEST_F(MetadataInterpreterTest, ErrorUnknownFormat) {
    interp->interpret_line(": myword dup + ;");
    interp->interpret_line(R"(s" myword" s" desc" s" badformat" s" hello" meta!)");
    EXPECT_FALSE(pop_value(*interp).as_bool());
}

TEST_F(MetadataInterpreterTest, ErrorUnknownWord) {
    interp->interpret_line(R"(s" nonexistent" s" desc" s" text" s" hello" meta!)");
    EXPECT_FALSE(pop_value(*interp).as_bool());

    interp->interpret_line(R"(s" nonexistent" s" desc" meta@)");
    EXPECT_FALSE(pop_value(*interp).as_bool());

    interp->interpret_line(R"(s" nonexistent" s" desc" s" text" s" hello" impl-meta!)");
    EXPECT_FALSE(pop_value(*interp).as_bool());

    interp->interpret_line(R"(s" nonexistent" s" desc" impl-meta@)");
    EXPECT_FALSE(pop_value(*interp).as_bool());
}

TEST_F(MetadataInterpreterTest, WordsIncludesMetadataWords) {
    out.str("");
    interp->interpret_line("words");
    auto output = out.str();
    EXPECT_TRUE(output.find("meta!") != std::string::npos);
    EXPECT_TRUE(output.find("meta@") != std::string::npos);
    EXPECT_TRUE(output.find("meta-del") != std::string::npos);
    EXPECT_TRUE(output.find("meta-keys") != std::string::npos);
    EXPECT_TRUE(output.find("impl-meta!") != std::string::npos);
    EXPECT_TRUE(output.find("impl-meta@") != std::string::npos);
}
