#include <boost/ut.hpp>

#include <gnuradio-4.0/sigmf/SigMfSource.hpp>

#include <chrono>
#include <complex>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <optional>
#include <string_view>
#include <vector>

using namespace boost::ut;
using namespace gr::incubator::sigmf;
using namespace std::string_view_literals;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path{__FILE__}.parent_path() / "assets";
}

template<typename T>
void processSamples(SigMFSource<T>& blk, std::size_t n_samples) {
    {
        auto span = blk.out.template tryReserve<gr::SpanReleasePolicy::ProcessAll>(n_samples);
        const auto st = blk.processBulk(span);
        (void)st;
    }
}

template<typename T>
std::vector<T> toVector(auto&& range) {
    return std::vector<T>(range.begin(), range.end());
}

template<typename T>
struct PublishedChunk {
    std::vector<T>   data;
    std::vector<gr::Tag> tags;
};

template<typename T>
PublishedChunk<T> collectPublished(auto& reader, auto& tagReader) {
    auto data = reader.get();
    auto tags = tagReader.get();
    return {toVector<T>(data), std::vector<gr::Tag>(tags.begin(), tags.end())};
}

[[nodiscard]] const gr::property_map* tagMetaInfo(const gr::Tag& tag) {
    const auto it = tag.map.find(std::pmr::string("trigger_meta_info"));
    if (it == tag.map.end()) {
        return nullptr;
    }
    return it->second.get_if<gr::property_map>();
}

[[nodiscard]] std::string_view triggerName(const gr::Tag& tag) {
    const auto it = tag.map.find(std::pmr::string("trigger_name"));
    if (it == tag.map.end()) {
        return {};
    }
    return it->second.value_or(std::string_view{});
}

[[nodiscard]] std::optional<std::uint64_t> triggerTime(const gr::Tag& tag) {
    const auto it = tag.map.find(std::pmr::string("trigger_time"));
    if (it == tag.map.end()) {
        return std::nullopt;
    }
    return it->second.value_or(std::uint64_t{0});
}

[[nodiscard]] std::uint64_t nsFromDate(std::chrono::year y, std::chrono::month m, std::chrono::day d) {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<nanoseconds>(sys_days{year_month_day{y, m, d}}.time_since_epoch()).count());
}

} // namespace

const suite SigMfSourceTests = [] {
    "emits_global_capture_and_annotation_tags"_test = [] {
        const auto expectedNs = nsFromDate(std::chrono::year{2026}, std::chrono::month{3}, std::chrono::day{30});

        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<std::complex<float>>>({
            {"file_name", (fixtureDir() / "tiny.sigmf-meta").string()},
            {"repeat", false},
            {"offset", gr::Size_t{0}},
            {"length", gr::Size_t{0}},
        });
        blk.start();

        auto reader    = blk.out.buffer().streamBuffer.new_reader();
        auto tagReader = blk.out.buffer().tagBuffer.new_reader();
        processSamples(blk, 3UZ);

        auto published = collectPublished<std::complex<float>>(reader, tagReader);
        auto& data     = published.data;
        auto& tags     = published.tags;

        expect(eq(data.size(), 3uz));
        expect(data[0] == std::complex<float>{1.0f, 2.0f});
        expect(data[1] == std::complex<float>{3.0f, 4.0f});
        expect(data[2] == std::complex<float>{5.0f, 6.0f});

        expect(eq(tags.size(), 3uz));
        expect(eq(tags[0].index, 0UZ));
        expect(eq(tags[1].index, 0UZ));
        expect(eq(tags[2].index, 1UZ));
        expect(triggerName(tags[0]) == "SigMFSource::start"sv);
        expect(triggerName(tags[1]) == "SigMFSource::capture"sv);
        expect(triggerName(tags[2]) == "SigMFSource::annotation"sv);
        expect(triggerTime(tags[0]).has_value());
        expect(triggerTime(tags[1]).has_value());
        expect(!triggerTime(tags[2]).has_value());
        if (triggerTime(tags[0])) {
            expect(eq(*triggerTime(tags[0]), expectedNs));
        }
        if (triggerTime(tags[1])) {
            expect(eq(*triggerTime(tags[1]), expectedNs));
        }
        expect(tags[0].map.contains(std::pmr::string("sample_rate")));
        expect(eq(tags[0].map.at(std::pmr::string("sample_rate")).value_or(0.f), 1'000'000.f));
        expect(tagMetaInfo(tags[0]) != nullptr);
        expect(tagMetaInfo(tags[1]) != nullptr);
        expect(tagMetaInfo(tags[2]) != nullptr);
        expect(tagMetaInfo(tags[0])->contains(std::pmr::string("core:datatype")));
        expect(tagMetaInfo(tags[0])->contains(std::pmr::string("core:version")));
        expect(tagMetaInfo(tags[1])->contains(std::pmr::string("core:frequency")));
        expect(tagMetaInfo(tags[1])->contains(std::pmr::string("core:datetime")));
        expect(tagMetaInfo(tags[2])->contains(std::pmr::string("core:label")));
        expect(tagMetaInfo(tags[2])->contains(std::pmr::string("core:comment")));
    };

    "offset_and_length_rebase_tags"_test = [] {
        const auto expectedNs = nsFromDate(std::chrono::year{2026}, std::chrono::month{3}, std::chrono::day{30});

        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<std::complex<float>>>({
            {"file_name", (fixtureDir() / "tiny.sigmf-meta").string()},
            {"repeat", false},
            {"offset", gr::Size_t{1}},
            {"length", gr::Size_t{1}},
        });
        blk.start();

        auto reader    = blk.out.buffer().streamBuffer.new_reader();
        auto tagReader = blk.out.buffer().tagBuffer.new_reader();
        processSamples(blk, 1UZ);

        auto published = collectPublished<std::complex<float>>(reader, tagReader);
        auto& data     = published.data;
        auto& tags     = published.tags;

        expect(eq(data.size(), 1uz));
        expect(data[0] == std::complex<float>{3.0f, 4.0f});

        expect(eq(tags.size(), 2uz));
        expect(eq(tags[0].index, 0UZ));
        expect(eq(tags[1].index, 0UZ));
        expect(triggerName(tags[0]) == "SigMFSource::start"sv);
        expect(triggerName(tags[1]) == "SigMFSource::annotation"sv);
        expect(triggerTime(tags[0]).has_value());
        if (triggerTime(tags[0])) {
            expect(eq(*triggerTime(tags[0]), expectedNs));
        }
        expect(!triggerTime(tags[1]).has_value());
        expect(tags[0].map.contains(std::pmr::string("sample_rate")));
        expect(tagMetaInfo(tags[1]) != nullptr);
        expect(tagMetaInfo(tags[1])->contains(std::pmr::string("core:label")));
        expect(tagMetaInfo(tags[1])->contains(std::pmr::string("core:comment")));
    };

    "repeat_replays_tags_on_loop"_test = [] {
        const auto expectedNs = nsFromDate(std::chrono::year{2026}, std::chrono::month{3}, std::chrono::day{30});

        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<std::complex<float>>>({
            {"file_name", (fixtureDir() / "tiny.sigmf-meta").string()},
            {"repeat", true},
            {"offset", gr::Size_t{0}},
            {"length", gr::Size_t{2}},
        });
        blk.start();

        auto reader    = blk.out.buffer().streamBuffer.new_reader();
        auto tagReader = blk.out.buffer().tagBuffer.new_reader();
        processSamples(blk, 4UZ);

        auto published = collectPublished<std::complex<float>>(reader, tagReader);
        auto& data     = published.data;
        auto& tags     = published.tags;

        expect(eq(data.size(), 4uz));
        expect(data[0] == std::complex<float>{1.0f, 2.0f});
        expect(data[1] == std::complex<float>{3.0f, 4.0f});
        expect(data[2] == std::complex<float>{1.0f, 2.0f});
        expect(data[3] == std::complex<float>{3.0f, 4.0f});

        expect(eq(tags.size(), 6uz));
        expect(eq(tags[0].index, 0UZ));
        expect(eq(tags[1].index, 0UZ));
        expect(eq(tags[2].index, 1UZ));
        expect(eq(tags[3].index, 2UZ));
        expect(eq(tags[4].index, 2UZ));
        expect(eq(tags[5].index, 3UZ));
        expect(triggerName(tags[0]) == "SigMFSource::start"sv);
        expect(triggerName(tags[1]) == "SigMFSource::capture"sv);
        expect(triggerName(tags[2]) == "SigMFSource::annotation"sv);
        expect(triggerName(tags[3]) == "SigMFSource::start"sv);
        expect(triggerName(tags[4]) == "SigMFSource::capture"sv);
        expect(triggerName(tags[5]) == "SigMFSource::annotation"sv);
        expect(triggerTime(tags[0]).has_value());
        expect(triggerTime(tags[1]).has_value());
        expect(triggerTime(tags[3]).has_value());
        expect(triggerTime(tags[4]).has_value());
        if (triggerTime(tags[0])) {
            expect(eq(*triggerTime(tags[0]), expectedNs));
        }
        if (triggerTime(tags[1])) {
            expect(eq(*triggerTime(tags[1]), expectedNs));
        }
        if (triggerTime(tags[3])) {
            expect(eq(*triggerTime(tags[3]), expectedNs));
        }
        if (triggerTime(tags[4])) {
            expect(eq(*triggerTime(tags[4]), expectedNs));
        }
    };

    "rf32_emits_float_samples_and_tags"_test = [] {
        const auto expectedNs = nsFromDate(std::chrono::year{2026}, std::chrono::month{3}, std::chrono::day{30});

        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<float>>({
            {"file_name", (fixtureDir() / "tiny_rf32.sigmf-meta").string()},
            {"repeat", false},
            {"offset", gr::Size_t{0}},
            {"length", gr::Size_t{0}},
        });
        blk.start();

        auto reader    = blk.out.buffer().streamBuffer.new_reader();
        auto tagReader = blk.out.buffer().tagBuffer.new_reader();
        processSamples(blk, 3UZ);

        auto published = collectPublished<float>(reader, tagReader);
        auto& data     = published.data;
        auto& tags     = published.tags;

        expect(eq(data.size(), 3uz));
        expect(data[0] == 1.0f);
        expect(data[1] == 2.0f);
        expect(data[2] == 3.0f);

        expect(eq(tags.size(), 3uz));
        expect(eq(tags[0].index, 0UZ));
        expect(eq(tags[1].index, 0UZ));
        expect(eq(tags[2].index, 1UZ));
        expect(triggerName(tags[0]) == "SigMFSource::start"sv);
        expect(triggerName(tags[1]) == "SigMFSource::capture"sv);
        expect(triggerName(tags[2]) == "SigMFSource::annotation"sv);
        expect(triggerTime(tags[0]).has_value());
        expect(triggerTime(tags[1]).has_value());
        expect(!triggerTime(tags[2]).has_value());
        if (triggerTime(tags[0])) {
            expect(eq(*triggerTime(tags[0]), expectedNs));
        }
        if (triggerTime(tags[1])) {
            expect(eq(*triggerTime(tags[1]), expectedNs));
        }
        expect(tags[0].map.contains(std::pmr::string("sample_rate")));
        expect(eq(tags[0].map.at(std::pmr::string("sample_rate")).value_or(0.f), 1'000'000.f));
        expect(tagMetaInfo(tags[0]) != nullptr);
        expect(tagMetaInfo(tags[1]) != nullptr);
        expect(tagMetaInfo(tags[2]) != nullptr);
        expect(tagMetaInfo(tags[0])->contains(std::pmr::string("core:datatype")));
        expect(tagMetaInfo(tags[0])->contains(std::pmr::string("core:version")));
        expect(tagMetaInfo(tags[1])->contains(std::pmr::string("core:frequency")));
        expect(tagMetaInfo(tags[1])->contains(std::pmr::string("core:datetime")));
        expect(tagMetaInfo(tags[2])->contains(std::pmr::string("core:label")));
        expect(tagMetaInfo(tags[2])->contains(std::pmr::string("core:comment")));
    };

    "rf32_offset_and_repeat_work"_test = [] {
        const auto expectedNs = nsFromDate(std::chrono::year{2026}, std::chrono::month{3}, std::chrono::day{30});

        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<float>>({
            {"file_name", (fixtureDir() / "tiny_rf32.sigmf-meta").string()},
            {"repeat", true},
            {"offset", gr::Size_t{1}},
            {"length", gr::Size_t{1}},
        });
        blk.start();

        auto reader    = blk.out.buffer().streamBuffer.new_reader();
        auto tagReader = blk.out.buffer().tagBuffer.new_reader();
        processSamples(blk, 2UZ);

        auto published = collectPublished<float>(reader, tagReader);
        auto& data     = published.data;
        auto& tags     = published.tags;

        expect(eq(data.size(), 2uz));
        expect(data[0] == 2.0f);
        expect(data[1] == 2.0f);

        expect(eq(tags.size(), 4uz));
        expect(eq(tags[0].index, 0UZ));
        expect(eq(tags[1].index, 0UZ));
        expect(eq(tags[2].index, 1UZ));
        expect(eq(tags[3].index, 1UZ));
        expect(triggerName(tags[0]) == "SigMFSource::start"sv);
        expect(triggerName(tags[1]) == "SigMFSource::annotation"sv);
        expect(triggerName(tags[2]) == "SigMFSource::start"sv);
        expect(triggerName(tags[3]) == "SigMFSource::annotation"sv);
        expect(triggerTime(tags[0]).has_value());
        expect(triggerTime(tags[2]).has_value());
        if (triggerTime(tags[0])) {
            expect(eq(*triggerTime(tags[0]), expectedNs));
        }
        if (triggerTime(tags[2])) {
            expect(eq(*triggerTime(tags[2]), expectedNs));
        }
    };

    "invalid_capture_datetime_omits_trigger_time"_test = [] {
        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<std::complex<float>>>({
            {"file_name", (fixtureDir() / "invalid_capture_datetime.sigmf-meta").string()},
            {"repeat", false},
            {"offset", gr::Size_t{0}},
            {"length", gr::Size_t{1}},
        });
        blk.start();

        auto reader    = blk.out.buffer().streamBuffer.new_reader();
        auto tagReader = blk.out.buffer().tagBuffer.new_reader();
        processSamples(blk, 1UZ);

        auto published = collectPublished<std::complex<float>>(reader, tagReader);
        auto& tags     = published.tags;

        expect(eq(tags.size(), 2uz));
        expect(triggerTime(tags[0]).has_value());
        expect(!triggerTime(tags[1]).has_value());
        expect(tagMetaInfo(tags[1]) != nullptr);
        expect(tagMetaInfo(tags[1])->at(std::pmr::string("core:datetime")).value_or(std::string_view{}) == "not-a-valid-datetime"sv);
    };

    "wrong_datatype_fails_cleanly"_test = [] {
        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<std::complex<float>>>({
            {"file_name", (fixtureDir() / "wrong_dtype.sigmf-meta").string()},
            {"repeat", false},
            {"offset", gr::Size_t{0}},
            {"length", gr::Size_t{0}},
        });

        expect(throws<gr::exception>([&] { blk.start(); }));
    };

    "float_wrong_datatype_fails_cleanly"_test = [] {
        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<float>>({
            {"file_name", (fixtureDir() / "tiny.sigmf-meta").string()},
            {"repeat", false},
            {"offset", gr::Size_t{0}},
            {"length", gr::Size_t{0}},
        });

        expect(throws<gr::exception>([&] { blk.start(); }));
    };

    "complex_wrong_datatype_fails_cleanly"_test = [] {
        gr::Graph g;
        auto& blk = g.emplaceBlock<SigMFSource<std::complex<float>>>({
            {"file_name", (fixtureDir() / "tiny_rf32.sigmf-meta").string()},
            {"repeat", false},
            {"offset", gr::Size_t{0}},
            {"length", gr::Size_t{0}},
        });

        expect(throws<gr::exception>([&] { blk.start(); }));
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
