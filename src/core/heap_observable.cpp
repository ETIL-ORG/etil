// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/manifold/subject.hpp"

namespace etil::core {

// ---------------------------------------------------------------------------
// HeapObservable implementation
// ---------------------------------------------------------------------------

HeapObservable::HeapObservable(Kind k) : HeapObject(HeapObject::Kind::Observable), obs_kind_(k) {}

HeapObservable::~HeapObservable() {
    if (source_) source_->release();
    if (source_b_) source_b_->release();
    if (operator_xt_) operator_xt_->release();
    if (source_array_) source_array_->release();
    state_.release();
    if (obs_kind_ == Kind::ChannelSubscription && param_ != 0) {
        auto* holder = reinterpret_cast<
            std::shared_ptr<etil::manifold::ChannelSubject>*>(param_);
        if (*holder) (*holder)->close();
        delete holder;
        param_ = 0;
    }
}

void HeapObservable::close_channel_subscription() {
    if (obs_kind_ != Kind::ChannelSubscription || param_ == 0) return;
    auto* holder = reinterpret_cast<
        std::shared_ptr<etil::manifold::ChannelSubject>*>(param_);
    if (*holder) (*holder)->close();
}

const char* HeapObservable::kind_name() const {
    switch (obs_kind_) {
    case Kind::FromArray: return "from-array";
    case Kind::Of:        return "of";
    case Kind::Empty:     return "empty";
    case Kind::Range:     return "range";
    case Kind::Map:       return "map";
    case Kind::MapWith:   return "map-with";
    case Kind::Filter:    return "filter";
    case Kind::FilterWith:return "filter-with";
    case Kind::Scan:      return "scan";
    case Kind::Reduce:    return "reduce";
    case Kind::Take:      return "take";
    case Kind::Skip:      return "skip";
    case Kind::Distinct:  return "distinct";
    case Kind::Merge:     return "merge";
    case Kind::Concat:    return "concat";
    case Kind::Zip:       return "zip";
    // Temporal
    case Kind::Timer:         return "timer";
    case Kind::Delay:         return "delay";
    case Kind::Timestamp:     return "timestamp";
    case Kind::TimeInterval:  return "time-interval";
    case Kind::DebounceTime:  return "debounce-time";
    case Kind::ThrottleTime:  return "throttle-time";
    case Kind::SampleTime:    return "sample-time";
    case Kind::Timeout:       return "timeout";
    case Kind::BufferTime:    return "buffer-time";
    case Kind::TakeUntilTime: return "take-until-time";
    case Kind::DelayEach:     return "delay-each";
    case Kind::AuditTime:     return "audit-time";
    case Kind::RetryDelay:    return "retry-delay";
    case Kind::Buffer:        return "buffer";
    case Kind::BufferWhen:    return "buffer-when";
    case Kind::Window:        return "window";
    case Kind::FlatMap:       return "flat-map";
    case Kind::ReadBytes:     return "read-bytes";
    case Kind::ReadLines:     return "read-lines";
    case Kind::ReadJson:      return "read-json";
    case Kind::ReadCsv:       return "read-csv";
    case Kind::ReadDir:       return "read-dir";
    case Kind::HttpGet:       return "http-get";
    case Kind::HttpPost:      return "http-post";
    case Kind::HttpSse:       return "http-sse";
    case Kind::Tap:           return "tap";
    case Kind::Pairwise:      return "pairwise";
    case Kind::First:         return "first";
    case Kind::Last:          return "last";
    case Kind::TakeWhile:     return "take-while";
    case Kind::DistinctUntil: return "distinct-until";
    case Kind::StartWith:     return "start-with";
    case Kind::Finalize:      return "finalize";
    case Kind::SwitchMap:     return "switch-map";
    case Kind::Catch:         return "catch";
    case Kind::ChannelSubscription: return "channel-subscription";
    case Kind::Channel:             return "channel";
    case Kind::MapWithCancel:       return "map-with-cancel";
    }
    return "unknown";
}

HeapObservable* HeapObservable::channel_subscription(
    std::shared_ptr<etil::manifold::ChannelSubject> subject) {
    auto* o = new HeapObservable(Kind::ChannelSubscription);
    auto* holder = new std::shared_ptr<etil::manifold::ChannelSubject>(std::move(subject));
    o->param_ = reinterpret_cast<int64_t>(holder);
    return o;
}

// --- Factory methods ---

HeapObservable* HeapObservable::from_array(HeapArray* arr) {
    auto* o = new HeapObservable(Kind::FromArray);
    arr->add_ref();
    o->source_array_ = arr;
    return o;
}

HeapObservable* HeapObservable::of(Value val) {
    auto* o = new HeapObservable(Kind::Of);
    value_addref(val);
    o->state_ = val;
    return o;
}

HeapObservable* HeapObservable::empty() {
    return new HeapObservable(Kind::Empty);
}

HeapObservable* HeapObservable::range(int64_t start, int64_t end) {
    auto* o = new HeapObservable(Kind::Range);
    o->state_ = Value(start);
    o->param_ = end;
    return o;
}

HeapObservable* HeapObservable::map(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Map);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::map_with_cancel(HeapObservable* source,
                                                 WordImpl* xt) {
    auto* o = new HeapObservable(Kind::MapWithCancel);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::channel(HeapString* name, ChannelMode mode) {
    auto* o = new HeapObservable(Kind::Channel);
    if (name) {
        name->add_ref();
        o->state_ = Value::from(name);
    }
    o->param_ = static_cast<int64_t>(mode);
    return o;
}

HeapString* HeapObservable::channel_name() const {
    if (obs_kind_ != Kind::Channel) return nullptr;
    if (state_.type != Value::Type::String) return nullptr;
    return state_.as_string();
}

HeapObservable::ChannelMode HeapObservable::channel_mode() const {
    return static_cast<ChannelMode>(param_);
}

HeapObservable* HeapObservable::map_with(HeapObservable* source, WordImpl* xt, Value ctx) {
    auto* o = new HeapObservable(Kind::MapWith);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    value_addref(ctx); o->state_ = ctx;
    return o;
}

HeapObservable* HeapObservable::filter(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Filter);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::filter_with(HeapObservable* source, WordImpl* xt, Value ctx) {
    auto* o = new HeapObservable(Kind::FilterWith);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    value_addref(ctx); o->state_ = ctx;
    return o;
}

HeapObservable* HeapObservable::scan(HeapObservable* source, WordImpl* xt, Value init) {
    auto* o = new HeapObservable(Kind::Scan);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    value_addref(init); o->state_ = init;
    return o;
}

HeapObservable* HeapObservable::take(HeapObservable* source, int64_t n) {
    auto* o = new HeapObservable(Kind::Take);
    source->add_ref(); o->source_ = source;
    o->param_ = n;
    return o;
}

HeapObservable* HeapObservable::skip(HeapObservable* source, int64_t n) {
    auto* o = new HeapObservable(Kind::Skip);
    source->add_ref(); o->source_ = source;
    o->param_ = n;
    return o;
}

HeapObservable* HeapObservable::distinct(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Distinct);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::merge(HeapObservable* a, HeapObservable* b, int64_t max_concurrent) {
    auto* o = new HeapObservable(Kind::Merge);
    a->add_ref(); o->source_ = a;
    b->add_ref(); o->source_b_ = b;
    o->param_ = max_concurrent;
    return o;
}

HeapObservable* HeapObservable::concat(HeapObservable* a, HeapObservable* b) {
    auto* o = new HeapObservable(Kind::Concat);
    a->add_ref(); o->source_ = a;
    b->add_ref(); o->source_b_ = b;
    return o;
}

HeapObservable* HeapObservable::zip(HeapObservable* a, HeapObservable* b) {
    auto* o = new HeapObservable(Kind::Zip);
    a->add_ref(); o->source_ = a;
    b->add_ref(); o->source_b_ = b;
    return o;
}

// --- Temporal factory methods ---

HeapObservable* HeapObservable::timer(int64_t delay_us, int64_t period_us) {
    auto* o = new HeapObservable(Kind::Timer);
    o->state_ = Value(delay_us);
    o->param_ = period_us;
    return o;
}

HeapObservable* HeapObservable::delay(HeapObservable* source, int64_t delay_us) {
    auto* o = new HeapObservable(Kind::Delay);
    source->add_ref(); o->source_ = source;
    o->param_ = delay_us;
    return o;
}

HeapObservable* HeapObservable::timestamp(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Timestamp);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::time_interval(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::TimeInterval);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::delay_each(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::DelayEach);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::debounce_time(HeapObservable* source, int64_t quiet_us) {
    auto* o = new HeapObservable(Kind::DebounceTime);
    source->add_ref(); o->source_ = source;
    o->param_ = quiet_us;
    return o;
}

HeapObservable* HeapObservable::throttle_time(HeapObservable* source, int64_t window_us) {
    auto* o = new HeapObservable(Kind::ThrottleTime);
    source->add_ref(); o->source_ = source;
    o->param_ = window_us;
    return o;
}

HeapObservable* HeapObservable::sample_time(HeapObservable* source, int64_t period_us) {
    auto* o = new HeapObservable(Kind::SampleTime);
    source->add_ref(); o->source_ = source;
    o->param_ = period_us;
    return o;
}

HeapObservable* HeapObservable::timeout(HeapObservable* source, int64_t limit_us) {
    auto* o = new HeapObservable(Kind::Timeout);
    source->add_ref(); o->source_ = source;
    o->param_ = limit_us;
    return o;
}

HeapObservable* HeapObservable::audit_time(HeapObservable* source, int64_t window_us) {
    auto* o = new HeapObservable(Kind::AuditTime);
    source->add_ref(); o->source_ = source;
    o->param_ = window_us;
    return o;
}

HeapObservable* HeapObservable::buffer_time(HeapObservable* source, int64_t window_us) {
    auto* o = new HeapObservable(Kind::BufferTime);
    source->add_ref(); o->source_ = source;
    o->param_ = window_us;
    return o;
}

HeapObservable* HeapObservable::take_until_time(HeapObservable* source, int64_t duration_us) {
    auto* o = new HeapObservable(Kind::TakeUntilTime);
    source->add_ref(); o->source_ = source;
    o->param_ = duration_us;
    return o;
}

HeapObservable* HeapObservable::retry_delay(HeapObservable* source, int64_t delay_us, int64_t max_retries) {
    auto* o = new HeapObservable(Kind::RetryDelay);
    source->add_ref(); o->source_ = source;
    o->state_ = Value(delay_us);
    o->param_ = max_retries;
    return o;
}

// AVO Phase 1 factories

HeapObservable* HeapObservable::buffer(HeapObservable* source, int64_t count) {
    auto* o = new HeapObservable(Kind::Buffer);
    source->add_ref(); o->source_ = source;
    o->param_ = count;
    return o;
}

HeapObservable* HeapObservable::buffer_when(HeapObservable* source, WordImpl* predicate_xt) {
    auto* o = new HeapObservable(Kind::BufferWhen);
    source->add_ref(); o->source_ = source;
    predicate_xt->add_ref(); o->operator_xt_ = predicate_xt;
    return o;
}

HeapObservable* HeapObservable::window(HeapObservable* source, int64_t size) {
    auto* o = new HeapObservable(Kind::Window);
    source->add_ref(); o->source_ = source;
    o->param_ = size;
    return o;
}

HeapObservable* HeapObservable::flat_map(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::FlatMap);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

// AVO Phase 2 factories

HeapObservable* HeapObservable::read_bytes(HeapString* fs_path, int64_t chunk_size) {
    auto* o = new HeapObservable(Kind::ReadBytes);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    o->param_ = chunk_size;
    return o;
}

HeapObservable* HeapObservable::read_lines(HeapString* fs_path) {
    auto* o = new HeapObservable(Kind::ReadLines);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    return o;
}

HeapObservable* HeapObservable::read_json(HeapString* fs_path) {
    auto* o = new HeapObservable(Kind::ReadJson);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    return o;
}

HeapObservable* HeapObservable::read_csv(HeapString* fs_path, HeapString* separator) {
    auto* o = new HeapObservable(Kind::ReadCsv);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    // Store separator in a single-element source_array
    auto* arr = new HeapArray();
    separator->add_ref();
    arr->push_back(Value::from(separator));
    o->source_array_ = arr;
    return o;
}

HeapObservable* HeapObservable::read_dir(HeapString* fs_path) {
    auto* o = new HeapObservable(Kind::ReadDir);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    return o;
}

// AVO Phase 3 factories

HeapObservable* HeapObservable::http_get(HeapArray* url_data) {
    auto* o = new HeapObservable(Kind::HttpGet);
    url_data->add_ref();
    o->source_array_ = url_data;
    return o;
}

HeapObservable* HeapObservable::http_post(HeapArray* url_data, HeapByteArray* body, HeapString* content_type) {
    auto* o = new HeapObservable(Kind::HttpPost);
    url_data->add_ref();
    o->source_array_ = url_data;
    // Store body as Value in state_
    body->add_ref();
    o->state_ = Value::from(body);
    // Store content_type: use param_ as flag, add content_type to url_data
    content_type->add_ref();
    url_data->push_back(Value::from(content_type));  // last element
    return o;
}

HeapObservable* HeapObservable::http_sse(HeapArray* url_data) {
    auto* o = new HeapObservable(Kind::HttpSse);
    url_data->add_ref();
    o->source_array_ = url_data;
    return o;
}

// Gap fill factories

HeapObservable* HeapObservable::tap(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Tap);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::pairwise(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Pairwise);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::first(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::First);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::last(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Last);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::take_while(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::TakeWhile);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::distinct_until(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::DistinctUntil);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::start_with(HeapObservable* source, Value val) {
    auto* o = new HeapObservable(Kind::StartWith);
    source->add_ref(); o->source_ = source;
    value_addref(val);
    o->state_ = val;
    return o;
}

HeapObservable* HeapObservable::finalize(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Finalize);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::switch_map(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::SwitchMap);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::catch_error(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Catch);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

} // namespace etil::core
