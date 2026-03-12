# HTTP Runtime Blocks (Incubator)

This module provides runtime HTTP-oriented sink blocks for lightweight visualization/data extraction workflows.

## Block: `HttpTimeSeriesSink<T>`

Supported sample types:
- `float`
- `std::complex<float>`

Namespace:
- `gr::incubator::http::HttpTimeSeriesSink`

### Behavior

- Input is interpreted as interleaved multi-channel samples.
- The block keeps a fixed-size latest-window ring buffer (`window_size` samples per channel).
- The block serves one endpoint:
  - `GET /snapshot` (default path)
- Returned samples are oldest-to-newest in the current retained window.

### Parameters (v1)

- `bind_host` (default: `127.0.0.1`)
- `bind_port` (default: `8080`)
- `window_size` (default: `1024`)
- `channels` (default: `1`)
- `snapshot_path` (default: `/snapshot`)

### Snapshot JSON

For `float`:

```json
{
  "sample_type": "float32",
  "channels": 2,
  "samples_per_channel": 3,
  "layout": "channels_first",
  "data": [
    [0.1, 0.2, 0.3],
    [1.1, 1.2, 1.3]
  ]
}
```

For `std::complex<float>`:

```json
{
  "sample_type": "complex64",
  "channels": 2,
  "samples_per_channel": 3,
  "layout": "channels_first_interleaved_complex",
  "data": [
    [0.1, -0.1, 0.2, -0.2, 0.3, -0.3],
    [1.1, -1.1, 1.2, -1.2, 1.3, -1.3]
  ]
}
```

### Minimal usage (conceptual)

```cpp
auto& sink = fg.emplaceBlock<gr::incubator::http::HttpTimeSeriesSink<float>>({
  {"bind_host", "127.0.0.1"},
  {"bind_port", static_cast<uint16_t>(18080)},
  {"channels", static_cast<gr::Size_t>(2)},
  {"window_size", static_cast<gr::Size_t>(2048)},
  {"snapshot_path", "/snapshot"}
});
```

Then query:

```bash
curl http://127.0.0.1:18080/snapshot
```
