# SigMF blocks

This module currently ships a minimal `SigMFSource` for little-endian `rf32_le` and `cf32_le` recordings.

```cpp
gr::Graph g;
auto& src = g.emplaceBlock<gr::incubator::sigmf::SigMFSource<std::complex<float>>>({
    {"file_name", "/path/to/recording.sigmf-meta"},
    {"repeat", false},
    {"offset", gr::Size_t{0}},
    {"length", gr::Size_t{0}},
});
```

Notes:
- `file_name` points at the `.sigmf-meta` file; the matching `.sigmf-data` file is resolved automatically.
- `offset` and `length` select the playback window in input samples.
- `repeat=true` loops that window and replays its tags.
- Tags use GR4-native keys, including `sample_rate`, `trigger_name`, `trigger_offset`, `trigger_meta_info`, and `trigger_time`.
