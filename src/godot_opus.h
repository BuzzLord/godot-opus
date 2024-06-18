#ifndef GODOT_OPUS_H
#define GODOT_OPUS_H

#include <opus.h>
#include <godot_cpp/classes/node.hpp>

#include "ring_buffer.h"

namespace godot {

class GodotOpus : public Node {
	GDCLASS(GodotOpus, Node)

public:
	enum SampleRate {
		SAMPLE_RATE_8000 = 8000,
		SAMPLE_RATE_12000 = 12000,
		SAMPLE_RATE_16000 = 16000,
		SAMPLE_RATE_24000 = 24000,
		SAMPLE_RATE_48000 = 48000
	};

	enum Channels {
		CHANNELS_MONO = 1,
		CHANNELS_STEREO = 2
	};

	enum ApplicationMode {
		APPLICATION_MODE_VOIP = OPUS_APPLICATION_VOIP,
		APPLICATION_MODE_AUDIO = OPUS_APPLICATION_AUDIO,
		APPLICATION_MODE_RESTRICTED_LOWDELAY = OPUS_APPLICATION_RESTRICTED_LOWDELAY
	};

	enum FrameSizeDuration {
		FRAMESIZE_2_5_MS = OPUS_FRAMESIZE_2_5_MS,
		FRAMESIZE_5_MS = OPUS_FRAMESIZE_5_MS,
		FRAMESIZE_10_MS = OPUS_FRAMESIZE_10_MS,
		FRAMESIZE_20_MS = OPUS_FRAMESIZE_20_MS,
		FRAMESIZE_40_MS = OPUS_FRAMESIZE_40_MS,
		FRAMESIZE_60_MS = OPUS_FRAMESIZE_60_MS,
		FRAMESIZE_80_MS = OPUS_FRAMESIZE_80_MS,
		FRAMESIZE_100_MS = OPUS_FRAMESIZE_100_MS,
		FRAMESIZE_120_MS = OPUS_FRAMESIZE_120_MS
	};

	enum Bandwidth {
		BANDWIDTH_NARROWBAND = OPUS_BANDWIDTH_NARROWBAND,
		BANDWIDTH_MEDIUMBAND = OPUS_BANDWIDTH_MEDIUMBAND,
		BANDWIDTH_WIDEBAND = OPUS_BANDWIDTH_WIDEBAND,
		BANDWIDTH_SUPERWIDEBAND = OPUS_BANDWIDTH_SUPERWIDEBAND,
		BANDWIDTH_FULLBAND = OPUS_BANDWIDTH_FULLBAND,
		BANDWIDTH_AUTO = OPUS_AUTO
	};

	enum BitrateMode {
		BITRATE_VARIABLE_AUTO = OPUS_AUTO,
		BITRATE_VARIABLE_BITRATE_MAX = OPUS_BITRATE_MAX,
		BITRATE_VARIABLE_MANUAL = 0,
		BITRATE_CONSTANT = 1
	};

private:
	OpusEncoder *encoder;
	OpusDecoder *decoder;

	bool encoder_enabled;
	bool encoder_initialized;
	bool buffer_initialized;

	bool decoder_enabled;
	bool decoder_initialized;

	RingBuffer<float> encode_buffer;
	PackedByteArray encode_data;
	PackedFloat32Array decode_data;

	int skip_samples;
	int64_t decoded_samples;
	int dropped_sampling_multiple;

	SampleRate sampling_rate;
	Channels channels;
	ApplicationMode application_mode;
	FrameSizeDuration frame_duration;
	Bandwidth bandwidth;
	Bandwidth max_bandwidth;

	int max_payload_bytes;
	int max_frame_size;
	int frame_size;

	BitrateMode bitrate_mode;
	int bitrate_bps;
	int encoder_complexity;
	int packet_loss_perc;

	float buffer_length_seconds;

protected:
	static void _bind_methods();

	void _initialize_buffer();
	int _dropped_frame_size(const int samples) const;
	void _update_frame_size();

public:
	GodotOpus();
	~GodotOpus();

	// void _process(double delta) override;

	bool initialize();

	void clear_buffer();

	// Push onto encode buffer (queue)
	bool can_push_buffer(const int num_samples) const;
	bool push_buffer(const PackedVector2Array data);
	bool push_buffer_raw(const PackedFloat32Array data);

	// Pop and encode packets from encode buffer
	bool has_encoded_packet() const;
	PackedByteArray get_encoded_packet();

	// Decode an encoded packet
	PackedVector2Array decode(const PackedByteArray data);
	PackedFloat32Array decode_raw(const PackedByteArray data);

	// Decode (and inform decoder of) dropped packet, in terms of sample length of the packet
	PackedVector2Array decode_dropped(const int dropped_samples);
	PackedFloat32Array decode_dropped_raw(const int dropped_samples);

	// Resets the decoded samples count, reinitiating the skipped frames. Returns decoded samples before reset.
	int reset_decoder_count();
	int get_decoder_count() const;

	// Property getters/setters

	void set_encoder_enabled(const bool p_encoder_enabled);
	bool is_encoder_enabled() const;

	void set_decoder_enabled(const bool p_decoder_enabled);
	bool is_decoder_enabled() const;

	void set_sampling_rate(const GodotOpus::SampleRate p_sampling_rate);
	GodotOpus::SampleRate get_sampling_rate() const;

	void set_channels(const GodotOpus::Channels p_channels);
	GodotOpus::Channels get_channels() const;

	void set_application_mode(const GodotOpus::ApplicationMode p_application_mode);
	GodotOpus::ApplicationMode get_application_mode() const;

	void set_max_payload_bytes(const int p_max_payload_bytes);
	int get_max_payload_bytes() const;

	void set_buffer_length_seconds(const float p_buffer_length_seconds);
	float get_buffer_length_seconds() const;

	void set_frame_duration(const GodotOpus::FrameSizeDuration p_frame_duration);
	GodotOpus::FrameSizeDuration get_frame_duration() const;

	void set_bandwidth(const GodotOpus::Bandwidth p_bandwidth);
	GodotOpus::Bandwidth get_bandwidth() const;

	void set_max_bandwidth(const GodotOpus::Bandwidth p_bandwidth);
	GodotOpus::Bandwidth get_max_bandwidth() const;

	void set_encoder_complexity(const int p_complexity);
	int get_encoder_complexity() const;

	// Dynamic properties

	void set_bitrate_mode(const GodotOpus::BitrateMode p_mode);
	GodotOpus::BitrateMode get_bitrate_mode() const;

	void set_bitrate(const int p_bitrate);
	int get_bitrate() const;

	void set_packet_loss_perc(const int p_packet_loss_perc);
	int get_packet_loss_perc() const;

	// Not exposed as a property
	int get_frame_size() const;

	void set_skip_samples(const int p_skip_samples);
	int get_skip_samples() const;
};

} //namespace godot

VARIANT_ENUM_CAST(GodotOpus::SampleRate);
VARIANT_ENUM_CAST(GodotOpus::Channels);
VARIANT_ENUM_CAST(GodotOpus::ApplicationMode);
VARIANT_ENUM_CAST(GodotOpus::FrameSizeDuration);
VARIANT_ENUM_CAST(GodotOpus::Bandwidth);
VARIANT_ENUM_CAST(GodotOpus::BitrateMode);

#endif // GODOT_OPUS_H
