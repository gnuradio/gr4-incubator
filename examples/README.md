# Examples

Put example flowgraphs in here that combine blocks from multiple modules.

## fm_demodulator

Demonstrates the basic signal processing chain for FM Demodulation in a statically compiled flowgraph.  Uses RT Audio Sink and file/zmq/soapy as possible input sources.

```
./examples/fm_demodulator     --source=soapy     --soapy-driver=hackrf     --soapy-freq=96900000     --soapy-bw=400000     --soapy-gain=60     --soapy-channel=0     --rate 400000
```

## fm_demodulator_cli

Extends the `fm_demodulator` example to allow runtime setting of parameters through the block settings interface

## fm_demodulator_imgui

Demonstrates the `DataSink` block usage which is library agnostic and be extended to any in-process graphing library (ImGui, QT, etc.) for basic plotting applications.
