#include <boost/ut.hpp>

#include <gnuradio-4.0/sigmf/SigMfMetadata.hpp>

#include <filesystem>
#include <string_view>

using namespace boost::ut;
using namespace gr::incubator::sigmf;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path{__FILE__}.parent_path() / "assets";
}

} // namespace

const suite SigMfMetadataTests = [] {
    "fixture_loads_and_validates"_test = [] {
        const auto meta_path = fixtureDir() / "tiny.sigmf-meta";
        const auto loaded    = loadSigMfMetadata(meta_path);

        expect(loaded.has_value()) << (loaded ? "" : loaded.error().message);
        if (!loaded) {
            return;
        }

        const auto& meta = *loaded;
        expect(eq(meta.meta_path, meta_path));
        expect(std::filesystem::exists(meta.data_path));
        expect(meta.datatype == SigMfDatatype::cf32_le);
        expect(std::string_view{datatypeName(meta.datatype)} == std::string_view{"cf32_le"});
        expect(eq(meta.item_size_bytes, 8uz));
        expect(meta.complex_samples);
        expect(meta.sample_rate.has_value());
        if (meta.sample_rate) {
            expect(eq(*meta.sample_rate, 1000000.0));
        }
        expect(eq(meta.captures.size(), 1uz));
        expect(eq(meta.annotations.size(), 1uz));
        expect(eq(meta.data_file_size_bytes, 24uz));

        expect(eq(meta.captures[0].sample_start, 0uz));
        expect(meta.captures[0].metadata.contains("core:frequency"));
        expect(meta.captures[0].metadata.contains("core:datetime"));
        expect(eq(meta.captures[0].metadata.at("core:frequency").get<double>(), 915000000.0));
        expect(meta.captures[0].metadata.at("core:datetime").get<std::string>() == std::string_view{"2026-03-30T00:00:00Z"});

        expect(eq(meta.annotations[0].sample_start, 1uz));
        expect(meta.annotations[0].sample_count.has_value());
        if (meta.annotations[0].sample_count) {
            expect(eq(*meta.annotations[0].sample_count, 2uz));
        }
        expect(meta.annotations[0].metadata.contains("core:label"));
        expect(meta.annotations[0].metadata.contains("core:comment"));
        expect(meta.annotations[0].metadata.at("core:label").get<std::string>() == std::string_view{"burst"});
        expect(meta.annotations[0].metadata.at("core:comment").get<std::string>() == std::string_view{"tiny fixture"});
    };

    "datatype_support_covers_rf32_and_cf32"_test = [] {
        const auto rf32 = parseSigMfDatatype("rf32_le");
        const auto cf32 = parseSigMfDatatype("cf32_le");

        expect(rf32.has_value());
        expect(cf32.has_value());
        if (rf32) {
            expect(*rf32 == SigMfDatatype::rf32_le);
            expect(eq(itemSizeBytes(*rf32), 4uz));
            expect(!isComplexDatatype(*rf32));
        }
        if (cf32) {
            expect(*cf32 == SigMfDatatype::cf32_le);
            expect(eq(itemSizeBytes(*cf32), 8uz));
            expect(isComplexDatatype(*cf32));
        }
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
