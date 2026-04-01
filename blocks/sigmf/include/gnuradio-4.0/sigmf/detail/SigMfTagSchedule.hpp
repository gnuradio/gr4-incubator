#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gnuradio-4.0/Tag.hpp>

#include <gnuradio-4.0/sigmf/SigMfMetadata.hpp>
#include <gnuradio-4.0/sigmf/detail/SigMfDatetime.hpp>
#include <gnuradio-4.0/sigmf/detail/SigMfTagMap.hpp>

namespace gr::incubator::sigmf::detail {

struct SigMfScheduledTag {
    std::size_t offset{};
    property_map map{};
};

// Event offsets are rebased to the selected playback window before publication.
template<typename MakeTagMap>
[[nodiscard]] inline std::vector<SigMfScheduledTag> buildSigMfTagSchedule(const SigMfMetadata& metadata, std::size_t range_start, std::size_t range_end, MakeTagMap&& makeTagMap) {
    std::vector<SigMfScheduledTag> tags;
    tags.reserve(1UZ + metadata.captures.size() + metadata.annotations.size());

    property_map globalTag = makeTagMap("SigMFSource::start");
    // trigger_offset stays at 0 because the schedule rebases the event into playback-relative offsets.
    tag::put(globalTag, tag::TRIGGER_OFFSET, 0.0f);
    tag::put(globalTag, tag::TRIGGER_META_INFO, toTagMetaInfo(metadata.global, {"core:sample_rate"}));
    if (metadata.sample_rate.has_value()) {
        tag::put(globalTag, tag::SAMPLE_RATE, static_cast<float>(*metadata.sample_rate));
    }
    if (const auto trigger_time = tryParseSigMfDatetimeNs(metadata.global)) {
        tag::put(globalTag, tag::TRIGGER_TIME, *trigger_time);
    }
    tags.push_back({0UZ, std::move(globalTag)});

    for (const auto& capture : metadata.captures) {
        if (capture.sample_start < range_start || capture.sample_start >= range_end) {
            continue;
        }

        property_map captureTag = makeTagMap("SigMFSource::capture");
        tag::put(captureTag, tag::TRIGGER_OFFSET, 0.0f);
        tag::put(captureTag, tag::TRIGGER_META_INFO, toTagMetaInfo(capture.metadata));
        if (const auto trigger_time = tryParseSigMfDatetimeNs(capture.metadata)) {
            tag::put(captureTag, tag::TRIGGER_TIME, *trigger_time);
        }
        tags.push_back({capture.sample_start - range_start, std::move(captureTag)});
    }

    for (const auto& annotation : metadata.annotations) {
        if (annotation.sample_start < range_start || annotation.sample_start >= range_end) {
            continue;
        }

        property_map annotationTag = makeTagMap("SigMFSource::annotation");
        tag::put(annotationTag, tag::TRIGGER_OFFSET, 0.0f);
        tag::put(annotationTag, tag::TRIGGER_META_INFO, toTagMetaInfo(annotation.metadata));
        if (const auto trigger_time = tryParseSigMfDatetimeNs(annotation.metadata)) {
            tag::put(annotationTag, tag::TRIGGER_TIME, *trigger_time);
        }
        tags.push_back({annotation.sample_start - range_start, std::move(annotationTag)});
    }

    std::stable_sort(tags.begin(), tags.end(), [](const SigMfScheduledTag& lhs, const SigMfScheduledTag& rhs) { return lhs.offset < rhs.offset; });
    return tags;
}

} // namespace gr::incubator::sigmf::detail
