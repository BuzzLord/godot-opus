# Godot Opus
A GDExtension addon for the Godot engine for VOIP and other real-time audio compression using the [Opus audio codec](https://opus-codec.org).

- Bitrates from 6 kb/s to 512 kb/s
- Sampling rates from 8 kHz (narrowband) to 48 kHz (fullband)
- Frame sizes from 2.5 ms to 120 ms
- Support for variable bitrate and constant bitrate
- Audio bandwidth from narrowband to fullband
- Support for speech and music
- Support for mono and stereo
- Good loss robustness and packet loss concealment (PLC)
- Floating point implementation (fixed-point not implemented)

**Note:** Does not support Opus DRED decoder. Does not use Opus Repacketizer or Multistream API.

See the demo project in `samples/godot_opus` for more info on how to use.
