# Examples

Put example flowgraphs in here that combine blocks from multiple modules.

## fm_demodulator

Demonstrates the basic signal processing chain for FM Demodulation in a statically compiled flowgraph. Uses RT Audio Sink and file/zmq/soapy as possible input sources. The RF path follows the Studio mono FM graph shape: RF source -> rotator -> FirDecimator -> quadrature demod -> deemphasis -> audio resampler -> RtAudioSink.

```
./examples/fm_demodulator     --source=soapy     --soapy-driver=hackrf     --soapy-freq=99200000     --station-freq=99100000     --soapy-bw=200000     --soapy-gain=60     --soapy-channel=0     --rate 2000000     --quad-rate 400000     --audio-rate 32000     --audio-api alsa
```

## fm_demodulator_cli

Extends the `fm_demodulator` example to allow runtime setting of parameters through the block settings interface

## fm_demodulator_imgui

Demonstrates the `DataSink` block usage which is library agnostic and be extended to any in-process graphing library (ImGui, QT, etc.) for basic plotting applications.
