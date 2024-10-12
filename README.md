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

## Installation
To install from Github, go to the [Godot Opus Releases](https://github.com/BuzzLord/godot-opus/releases), and download the `Godot_Opus.zip` asset file for the latest release. From inside the Godot Editor AssetLib, click on the Import button and select the `Godot_Opus.zip` file (check the 'ignore asset root' box), then import it into your project. It will add new `godot_opus` folders to `res://addons` and `res://samples`. The addons folder contains the core libraries, while the samples contains a small demo project showing off the use of the `GodotOpus` node (this can be deleted without affecting the addon). Note since Godot Opus is not a plugin, nothing needs to be enabled in the project to use it; you just need to create a `GodotOpus` node to get started.

## Basic Usage
Add a `GodotOpus` node to a scene that will be encoding or decoding an audio stream. Configure the properties as necessary (from the editor or a script), then in a script call the `initialize` method before using it.

### Encoding
For input, add a `Capture` AudioEffect to an audio bus to be used to capture sound from a source stream (e.g. a microphone). Add an `AudioStreamPlayer` to the scene to act as the input sound stream, set to use the bus with the `Capture`, and an appropriate stream. In the scene script, get the capture bus effect from the `AudioServer` in `_ready` for later use. Then in `_process`, loop while the capture effect has enough frames (using `get_frames_available`), check that the `GodotOpus` can handle the frames with `can_push_buffer`, and if it can, pop the frames off the capture effect buffer with `get_buffer`, then push the data onto the `GodotOpus` encode buffer with `push_buffer`.

Once all data from the input stream has been put on the `GodotOpus` encode buffer, loop and check if it has an encoded packet ready with `has_encoded_packet`. If so, grab it with `get_encoded_packet` (which returns a `PackedByteArray`), which will vary in size depending on the encoding parameters and the input audio stream itself. Note: the encoding itself only occurs when `get_encoded_packet` is called; no background thread is running to do the encoding behind the scenes. Send the byte array to the target client (using an rpc or some other mechanism) that has `GodotOpus` setup for decoding.

### Decoding
On the output side (assuming the scene is different than the input scene), a `GodotOpus` node should be added, configured the same as the encoder side, and initialized similar to the input side. An `AudioStreamPlayer` should be added to the scene with a `Generator` stream. In the scene script, in `_process`, receive the `PackedByteArray` data from the encoder side. Start by checking that the output stream playback has enough frames available (with `get_frames_available`) before trying to decode the data. The `frame_size` can be retrieved from `GodotOpus` with `get_frame_size`. If playback has room, pass the byte array data into `GodotOpus` `decode`, which will return a `PackedVector2Array` of audio frame data. Add the audio data to the playback buffer with `push_buffer`, which should result in the audio playing.

### Usage Notes
* Both encoder and decoder `GodotOpus` nodes should share the same configuration of properties, except possibly the `encoder_enabled` and `decoder_enabled`. These can be configured once and left as fixed for the application, or made to be configurable by a user; if configured (on the encode side), the property values should be communicated to all clients that need to decode the stream.
* Most of the properties of the `GodotOpus` node cannot be changed dynamically, but require a call to `initialize` to re-initialize the state with the new property values. Exceptions to this requirement are:
    * `bitrate_mode`: Change between variable bitrate (VBR) Auto (let `GodotOpus` decide bitrate), VBR Max (use the highest bitrate possible), VBR Manual (use configured `bitrate`), or constant bitrate (CBR, use configured `bitrate`).
    * `bitrate`: Used as bitrate when `bitrate_mode` is VBR Manual or CBR.
    * `packet_loss`: Packet loss percentage, in the range 0-100. Higher values trigger progressively more loss resistant behavior in the encoder at the expense of quality at a given bitrate in the absence of packet loss, but greater quality under loss.
* Each encoder and decoder should be used for a single stream of audio data. For example don't re-use a decoder for multiple clients, as the node calculates and stores internal state that would get corrupted if multiple input streams were passed in. Likewise, the encoder builds state of the stream that it can use to better encode data (specifically to include some redundancy in the case of dropped packets).
* Setting `channels` to `Mono` will result in `GodotOpus` averaging the stereo frames into a single channel before encoding, then decoding to stereo frames with identical left/right channels. This is useful for microphones, which typically don't need stereo sound, and reduces the amount of data that needs to be processed and transmitted.
* `GodotOpus` supports Packet Loss Concealment using the function `decode_dropped`: if an application enumerates the packets sent, and keeps track of when packets are skipped before decoding, `GodotOpus` can generate (extrapolate based on internal state) missed audio data by passing the missed number of frames into `decode_dropped`. See the sample demo in `samples/godot_opus` for an example.
