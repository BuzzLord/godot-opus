
#include <godot_cpp/core/class_db.hpp>

#include "godot_opus.h"

using namespace godot;

GodotOpus::GodotOpus() {
	// Constructor, assign defaults here
	encoder = NULL;
	decoder = NULL;

	encoder_enabled = true;
	encoder_initialized = false;
	buffer_initialized = false;
	decoder_enabled = true;
	decoder_initialized = false;

	sampling_rate = SAMPLE_RATE_48000;
	channels = CHANNELS_STEREO;
	application_mode = APPLICATION_MODE_VOIP;
	frame_duration = FRAMESIZE_20_MS;
	bandwidth = BANDWIDTH_AUTO;

	skip_samples = 312; // 48 kHz, VoIP encoder gives 312 skip samples for lookahead
	decoded_samples = 0;
	dropped_sampling_multiple = 120; // 48kHz * 2.5ms

	max_payload_bytes = 1024;
	max_frame_size = 48000 * 2;
	buffer_length_seconds = 0.5;

	_update_frame_size(); // frame_size = 960; // sampling_rate / (1000 / frame_duration) => 48000 / 50
}

GodotOpus::~GodotOpus() {
	// Destructor
	if (encoder != NULL) {
		opus_encoder_destroy(encoder);
		encoder = NULL;
	}

	if (decoder != NULL) {
		opus_decoder_destroy(decoder);
		decoder = NULL;
	}
}

bool GodotOpus::initialize() {
	encoder_initialized = false;
	decoder_initialized = false;

	if (encoder != NULL) {
		opus_encoder_destroy(encoder);
		encoder = NULL;
	}

	if (decoder != NULL) {
		opus_decoder_destroy(decoder);
		decoder = NULL;
	}

	int err;
	if (encoder_enabled) {
		encoder = opus_encoder_create((int)sampling_rate, (int)channels, (int)application_mode, &err);

		ERR_FAIL_COND_V_MSG(err != OPUS_OK, false, opus_strerror(err));

		opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH((int)bandwidth));
		opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&skip_samples));

		// opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate_bps));
		// opus_encoder_ctl(encoder, OPUS_SET_VBR(use_vbr));
		// opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(cvbr));
		// opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(complexity));
		// opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(use_inbandfec));
		// opus_encoder_ctl(encoder, OPUS_SET_FORCE_CHANNELS(forcechannels));
		// opus_encoder_ctl(encoder, OPUS_SET_DTX(use_dtx));
		// opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(packet_loss_perc));

		// opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));
		// opus_encoder_ctl(encoder, OPUS_SET_EXPERT_FRAME_DURATION(variable_duration));

		encode_data.resize(max_payload_bytes);

		_initialize_buffer();

		encoder_initialized = true;
	}

	if (decoder_enabled) {
		decoder = opus_decoder_create((int)sampling_rate, (int)channels, &err);

		ERR_FAIL_COND_V_MSG(err != OPUS_OK, false, opus_strerror(err));

		// opus_decoder_ctl(decoder, OPUS_SET_COMPLEXITY(dec_complexity));

		decoded_samples = 0;
		decode_data.resize(max_frame_size);
		decoder_initialized = true;
	}

	return true;
}

void GodotOpus::_initialize_buffer() {
	if (!buffer_initialized) {
		float target_buffer_size = (int)sampling_rate * (int)channels * buffer_length_seconds;
		ERR_FAIL_COND(target_buffer_size <= 0 || target_buffer_size >= (1 << 27));
		encode_buffer.resize(nearest_shift((int)target_buffer_size));
		buffer_initialized = true;
	}

	clear_buffer();
}

void GodotOpus::clear_buffer() {
	const int32_t data_left = encode_buffer.data_left();
	encode_buffer.advance_read(data_left);
}

bool GodotOpus::can_push_buffer(const int num_samples) const {
	ERR_FAIL_COND_V(!buffer_initialized, false);
	return encode_buffer.space_left() >= (num_samples * (int)channels);
}

bool GodotOpus::push_buffer(const PackedVector2Array data) {
	ERR_FAIL_COND_V_MSG(!buffer_initialized, false, "GodotOpus encode buffer not initialized");
	ERR_FAIL_COND_V_MSG(encode_buffer.space_left() < (data.size() * (int)channels), false, "GodotOpus encode buffer has insuffient space left");

	bool success;
	if (channels == CHANNELS_STEREO) {
		// Interleave the two channels
		for (int i = 0; i < data.size(); i++) {
			success = encode_buffer.write(data[i][0]);
			ERR_FAIL_COND_V_MSG(!success, false, "GodotOpus encode buffer failed to write");

			success = encode_buffer.write(data[i][1]);
			ERR_FAIL_COND_V_MSG(!success, false, "GodotOpus encode buffer failed to write");
		}
	} else {
		// Average the two channels
		for (int i = 0; i < data.size(); i++) {
			success = encode_buffer.write((data[i][0] + data[i][1]) * 0.5);
			ERR_FAIL_COND_V_MSG(!success, false, "GodotOpus encode buffer failed to write");
		}
	}

	return true;
}

bool GodotOpus::push_buffer_raw(const PackedFloat32Array data) {
	ERR_FAIL_COND_V_MSG(!buffer_initialized, false, "GodotOpus encode buffer not initialized");
	ERR_FAIL_COND_V_MSG(encode_buffer.space_left() < data.size(), false, "GodotOpus encode buffer has insuffient space left");

	int written;
	written = encode_buffer.write(data.ptr(), data.size());

	if (written != data.size()) {
		WARN_PRINT("GodotOpus push_buffer_raw did not write all samples to encode_buffer");
		return false;
	}
	return true;
}

bool GodotOpus::has_encoded_packet() const {
	ERR_FAIL_COND_V(!buffer_initialized, false);
	return encode_buffer.data_left() >= frame_size * (int)channels;
}

PackedByteArray GodotOpus::get_encoded_packet() {
	ERR_FAIL_COND_V_MSG(!encoder_initialized, PackedByteArray(), "GodotOpus not initialized with encoder configured");
	if (!has_encoded_packet()) {
		return PackedByteArray();
	}

	PackedFloat32Array pcm;
	pcm.resize(frame_size * (int)channels);
	int len = encode_buffer.read(pcm.ptrw(), frame_size * (int)channels, false);

	ERR_FAIL_COND_V_MSG(len != (frame_size * (int)channels), PackedByteArray(), "Failed to read frame samples from encode_buffer");

	opus_int32 encoded_length = opus_encode_float(encoder, pcm.ptr(), frame_size, encode_data.ptrw(), max_payload_bytes);

	ERR_FAIL_COND_V_MSG(encoded_length < 0, PackedByteArray(), opus_strerror(encoded_length));

	int num_encoded = opus_packet_get_samples_per_frame(encode_data.ptr(), (int)sampling_rate) * opus_packet_get_nb_frames(encode_data.ptr(), encoded_length);

	encode_buffer.advance_read(num_encoded * (int)channels);

	PackedByteArray ret;
	ret.resize(encoded_length);
	for (int i = 0; i < encoded_length; i++) {
		ret[i] = encode_data[i];
	}
	return ret;
}

PackedVector2Array GodotOpus::decode(const PackedByteArray data) {
	ERR_FAIL_COND_V_MSG(!decoder_initialized, PackedVector2Array(), "GodotOpus not initialized with decoder configured");

	opus_int32 output_samples = max_frame_size;
	output_samples = opus_decode_float(decoder, data.ptr(), data.size(), decode_data.ptrw(), output_samples, 0);

	ERR_FAIL_COND_V_MSG(output_samples < 0, PackedVector2Array(), opus_strerror(output_samples));
	decoded_samples += output_samples;

	PackedVector2Array ret;
	if (decoded_samples < skip_samples) {
		int remaining = decoded_samples + output_samples - skip_samples;

		if (remaining > 0) {
			ret.resize(remaining);
			if (channels == CHANNELS_STEREO) {
				// Interleaved stereo case
				for (int i = 0; i < remaining; i++) {
					int idx = i + skip_samples - decoded_samples;
					ret[i] = Vector2(decode_data[idx * 2], decode_data[idx * 2 + 1]);
				}
			} else {
				// Mono case
				for (int i = 0; i < remaining; i++) {
					int idx = i + skip_samples - decoded_samples;
					ret[i] = Vector2(decode_data[idx], decode_data[idx]);
				}
			}
		}
	} else {
		ret.resize(output_samples);
		if (channels == CHANNELS_STEREO) {
			// Interleaved stereo case
			for (int i = 0; i < output_samples; i++) {
				ret[i] = Vector2(decode_data[i * 2], decode_data[i * 2 + 1]);
			}
		} else {
			// Mono case
			for (int i = 0; i < output_samples; i++) {
				ret[i] = Vector2(decode_data[i], decode_data[i]);
			}
		}
	}
	decoded_samples += output_samples;

	return ret;
}

PackedFloat32Array GodotOpus::decode_raw(const PackedByteArray data) {
	ERR_FAIL_COND_V_MSG(!decoder_initialized, PackedFloat32Array(), "GodotOpus not initialized with decoder configured");

	opus_int32 output_samples = max_frame_size;
	output_samples = opus_decode_float(decoder, data.ptr(), data.size(), decode_data.ptrw(), output_samples, 0);

	ERR_FAIL_COND_V_MSG(output_samples < 0, PackedFloat32Array(), opus_strerror(output_samples));

	PackedFloat32Array ret;
	if (decoded_samples < skip_samples) {
		int remaining = decoded_samples + output_samples - skip_samples;
		if (remaining > 0) {
			ret.resize(remaining);
			for (int i = 0; i < remaining; i++) {
				ret[i] = decode_data[i + skip_samples - decoded_samples];
			}
		}
	} else {
		ret.resize(output_samples);
		for (int i = 0; i < output_samples; i++) {
			ret[i] = decode_data[i];
		}
	}
	decoded_samples += output_samples;

	return ret;
}

PackedVector2Array GodotOpus::decode_dropped(const int dropped_samples) {
	ERR_FAIL_COND_V_MSG(!decoder_initialized, PackedVector2Array(), "GodotOpus not initialized with decoder configured");

	opus_int32 output_samples = _dropped_frame_size(dropped_samples);
	output_samples = opus_decode_float(decoder, NULL, 0, decode_data.ptrw(), output_samples, 0);

	ERR_FAIL_COND_V_MSG(output_samples < 0, PackedVector2Array(), opus_strerror(output_samples));

	PackedVector2Array ret;
	ret.resize(output_samples);
	if (channels == CHANNELS_STEREO) {
		// Interleaved stereo case
		for (int i = 0; i < output_samples; i++) {
			ret[i] = Vector2(decode_data[i * 2], decode_data[i * 2 + 1]);
		}
	} else {
		// Mono case
		for (int i = 0; i < output_samples; i++) {
			ret[i] = Vector2(decode_data[i], decode_data[i]);
		}
	}
	return ret;
}

PackedFloat32Array GodotOpus::decode_dropped_raw(const int dropped_samples) {
	ERR_FAIL_COND_V_MSG(!decoder_initialized, PackedFloat32Array(), "GodotOpus not initialized with decoder configured");

	opus_int32 output_samples = _dropped_frame_size(dropped_samples);
	output_samples = opus_decode_float(decoder, NULL, 0, decode_data.ptrw(), output_samples, 0);

	ERR_FAIL_COND_V_MSG(output_samples < 0, PackedFloat32Array(), opus_strerror(output_samples));

	PackedFloat32Array ret;
	ret.resize(output_samples);
	for (int i = 0; i < output_samples; i++) {
		ret[i] = decode_data[i];
	}

	return ret;
}

int GodotOpus::reset_decoder_count() {
	// Can overflow, but shouldn't matter.
	int count = decoded_samples;
	decoded_samples = 0;
	return count;
}

int GodotOpus::get_decoder_count() const {
	// Can overflow; matches what reset does.
	int count = decoded_samples;
	return count;
}

// Protected internal methods ///////////////////////////////////////////////

int GodotOpus::_dropped_frame_size(const int samples) const {
	if (samples % dropped_sampling_multiple == 0) {
		return samples;
	}
	return samples - (samples % dropped_sampling_multiple) + dropped_sampling_multiple;
}

void GodotOpus::_update_frame_size() {
	// frame_size is number of samples in a frame;
	//  = sampling_rate / (1000 / frame_duration)
	int sr = sampling_rate;
	if (frame_duration == FRAMESIZE_2_5_MS) {
		frame_size = sr / 400;
	} else if (frame_duration == FRAMESIZE_5_MS) {
		frame_size = sr / 200;
	} else if (frame_duration == FRAMESIZE_10_MS) {
		frame_size = sr / 100;
	} else if (frame_duration == FRAMESIZE_20_MS) {
		frame_size = sr / 50;
	} else if (frame_duration == FRAMESIZE_40_MS) {
		frame_size = sr / 25;
	} else if (frame_duration == FRAMESIZE_60_MS) {
		frame_size = 3 * sr / 50;
	} else if (frame_duration == FRAMESIZE_80_MS) {
		frame_size = 4 * sr / 50;
	} else if (frame_duration == FRAMESIZE_100_MS) {
		frame_size = 5 * sr / 50;
	} else if (frame_duration == FRAMESIZE_120_MS) {
		frame_size = 6 * sr / 50;
	} else {
		// Default of 20 ms
		frame_size = sr / 50;
	}
}

// Getters and Setters ////////////////////////////////////////////////////////

void GodotOpus::set_skip_samples(const int p_skip_samples) {
	if (encoder_enabled) {
		WARN_PRINT("Manually setting skip_samples when encoder_enabled");
	}
	skip_samples = p_skip_samples;
}

int GodotOpus::get_skip_samples() const {
	return skip_samples;
}

void GodotOpus::set_encoder_enabled(const bool p_encoder_enabled) {
	encoder_enabled = p_encoder_enabled;
}

bool GodotOpus::is_encoder_enabled() const {
	return encoder_enabled;
}

void GodotOpus::set_decoder_enabled(const bool p_decoder_enabled) {
	decoder_enabled = p_decoder_enabled;
}

bool GodotOpus::is_decoder_enabled() const {
	return decoder_enabled;
}

void GodotOpus::set_sampling_rate(const GodotOpus::SampleRate p_sampling_rate) {
	sampling_rate = p_sampling_rate;
	dropped_sampling_multiple = (int)sampling_rate / 400;
	_update_frame_size();
}

GodotOpus::SampleRate GodotOpus::get_sampling_rate() const {
	return sampling_rate;
}

void GodotOpus::set_channels(const GodotOpus::Channels p_channels) {
	channels = p_channels;
}

GodotOpus::Channels GodotOpus::get_channels() const {
	return channels;
}

void GodotOpus::set_application_mode(const GodotOpus::ApplicationMode p_application_mode) {
	application_mode = p_application_mode;
}

GodotOpus::ApplicationMode GodotOpus::get_application_mode() const {
	return application_mode;
}

void GodotOpus::set_max_payload_bytes(const int p_max_payload_bytes) {
	max_payload_bytes = p_max_payload_bytes;
}

int GodotOpus::get_max_payload_bytes() const {
	return max_payload_bytes;
}

void GodotOpus::set_buffer_length_seconds(const float p_buffer_length_seconds) {
	buffer_length_seconds = p_buffer_length_seconds;
}

float GodotOpus::get_buffer_length_seconds() const {
	return buffer_length_seconds;
}

void GodotOpus::set_frame_duration(const GodotOpus::FrameSizeDuration p_frame_duration) {
	frame_duration = p_frame_duration;
	_update_frame_size();
}

int GodotOpus::get_frame_size() const {
	return frame_size;
}

GodotOpus::FrameSizeDuration GodotOpus::get_frame_duration() const {
	return frame_duration;
}

void GodotOpus::set_bandwidth(const GodotOpus::Bandwidth p_bandwidth) {
	bandwidth = p_bandwidth;
}

GodotOpus::Bandwidth GodotOpus::get_bandwidth() const {
	return bandwidth;
}

// Bind methods

void GodotOpus::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize"), &GodotOpus::initialize);
	ClassDB::bind_method(D_METHOD("get_frame_size"), &GodotOpus::get_frame_size);

	ClassDB::bind_method(D_METHOD("clear_buffer"), &GodotOpus::clear_buffer);
	ClassDB::bind_method(D_METHOD("can_push_buffer", "num_samples"), &GodotOpus::can_push_buffer);
	ClassDB::bind_method(D_METHOD("push_buffer", "data"), &GodotOpus::push_buffer);
	ClassDB::bind_method(D_METHOD("push_buffer_raw", "data"), &GodotOpus::push_buffer_raw);
	ClassDB::bind_method(D_METHOD("has_encoded_packet"), &GodotOpus::has_encoded_packet);
	ClassDB::bind_method(D_METHOD("get_encoded_packet"), &GodotOpus::get_encoded_packet);

	ClassDB::bind_method(D_METHOD("decode", "data"), &GodotOpus::decode);
	ClassDB::bind_method(D_METHOD("decode_raw", "data"), &GodotOpus::decode_raw);
	ClassDB::bind_method(D_METHOD("decode_dropped", "dropped_samples"), &GodotOpus::decode_dropped);
	ClassDB::bind_method(D_METHOD("decode_dropped_raw", "dropped_samples"), &GodotOpus::decode_dropped_raw);

	ClassDB::bind_method(D_METHOD("is_encoder_enabled"), &GodotOpus::is_encoder_enabled);
	ClassDB::bind_method(D_METHOD("set_encoder_enabled", "p_encoder_enabled"), &GodotOpus::set_encoder_enabled);
	ClassDB::bind_method(D_METHOD("is_decoder_enabled"), &GodotOpus::is_decoder_enabled);
	ClassDB::bind_method(D_METHOD("set_decoder_enabled", "p_decoder_enabled"), &GodotOpus::set_decoder_enabled);

	ClassDB::bind_method(D_METHOD("get_decoder_count"), &GodotOpus::get_decoder_count);
	ClassDB::bind_method(D_METHOD("reset_decoder_count"), &GodotOpus::reset_decoder_count);
	ClassDB::bind_method(D_METHOD("get_skip_samples"), &GodotOpus::get_skip_samples);
	ClassDB::bind_method(D_METHOD("set_skip_samples", "p_skip_samples"), &GodotOpus::set_skip_samples);

	ClassDB::bind_method(D_METHOD("get_sampling_rate"), &GodotOpus::get_sampling_rate);
	ClassDB::bind_method(D_METHOD("set_sampling_rate", "p_sampling_rate"), &GodotOpus::set_sampling_rate);
	ClassDB::bind_method(D_METHOD("get_channels"), &GodotOpus::get_channels);
	ClassDB::bind_method(D_METHOD("set_channels", "p_channels"), &GodotOpus::set_channels);
	ClassDB::bind_method(D_METHOD("get_application_mode"), &GodotOpus::get_application_mode);
	ClassDB::bind_method(D_METHOD("set_application_mode", "p_application_mode"), &GodotOpus::set_application_mode);

	ClassDB::bind_method(D_METHOD("get_frame_duration"), &GodotOpus::get_frame_duration);
	ClassDB::bind_method(D_METHOD("set_frame_duration", "p_frame_duration"), &GodotOpus::set_frame_duration);
	ClassDB::bind_method(D_METHOD("get_bandwidth"), &GodotOpus::get_bandwidth);
	ClassDB::bind_method(D_METHOD("set_bandwidth", "p_bandwidth"), &GodotOpus::set_bandwidth);

	ClassDB::bind_method(D_METHOD("get_max_payload_bytes"), &GodotOpus::get_max_payload_bytes);
	ClassDB::bind_method(D_METHOD("set_max_payload_bytes", "p_max_payload_bytes"), &GodotOpus::set_max_payload_bytes);
	ClassDB::bind_method(D_METHOD("get_buffer_length_seconds"), &GodotOpus::get_buffer_length_seconds);
	ClassDB::bind_method(D_METHOD("set_buffer_length_seconds", "p_buffer_length_seconds"), &GodotOpus::set_buffer_length_seconds);

	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::BOOL, "encoder_enabled"), "set_encoder_enabled", "is_encoder_enabled");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::BOOL, "decoder_enabled"), "set_decoder_enabled", "is_decoder_enabled");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::INT, "sampling_rate", PROPERTY_HINT_ENUM, "8 kHz:8000,12 kHz:12000,16 kHz:16000,24 kHz:24000,48 kHz:48000"), "set_sampling_rate", "get_sampling_rate");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::INT, "channels", PROPERTY_HINT_ENUM, "Mono:1,Stereo:2"), "set_channels", "get_channels");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::INT, "application_mode", PROPERTY_HINT_ENUM, "VoIP:2048,Audio:2049,Restricted-LowDelay:2051"), "set_application_mode", "get_application_mode");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::INT, "frame_duration", PROPERTY_HINT_ENUM, "2.5 ms:5001,5 ms:5002,10 ms:5003,20 ms:5004,40 ms:5005,60 ms:5006,80 ms:5007,100 ms:5008,120 ms:5009"), "set_frame_duration", "get_frame_duration");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::INT, "bandwidth", PROPERTY_HINT_ENUM, "Narrow Band:1101,Medium Band:1102,Wide Band:1103,Super Wide Band:1004,Full Band:1105,Auto Bandwidth:-1000"), "set_bandwidth", "get_bandwidth");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::INT, "max_payload_bytes", PROPERTY_HINT_RANGE, "16,2048,16,suffix:B"), "set_max_payload_bytes", "get_max_payload_bytes");
	ClassDB::add_property("GodotOpus", PropertyInfo(Variant::FLOAT, "buffer_length_seconds", PROPERTY_HINT_RANGE, "0.01,10,0.01,suffix:s"), "set_buffer_length_seconds", "get_buffer_length_seconds");

	BIND_ENUM_CONSTANT(SAMPLE_RATE_8000);
	BIND_ENUM_CONSTANT(SAMPLE_RATE_12000);
	BIND_ENUM_CONSTANT(SAMPLE_RATE_16000);
	BIND_ENUM_CONSTANT(SAMPLE_RATE_24000);
	BIND_ENUM_CONSTANT(SAMPLE_RATE_48000);

	BIND_ENUM_CONSTANT(CHANNELS_MONO);
	BIND_ENUM_CONSTANT(CHANNELS_STEREO);

	BIND_ENUM_CONSTANT(APPLICATION_MODE_VOIP);
	BIND_ENUM_CONSTANT(APPLICATION_MODE_AUDIO);
	BIND_ENUM_CONSTANT(APPLICATION_MODE_RESTRICTED_LOWDELAY);

	BIND_ENUM_CONSTANT(FRAMESIZE_2_5_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_5_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_10_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_20_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_40_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_60_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_80_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_100_MS);
	BIND_ENUM_CONSTANT(FRAMESIZE_120_MS);

	BIND_ENUM_CONSTANT(BANDWIDTH_NARROWBAND);
	BIND_ENUM_CONSTANT(BANDWIDTH_MEDIUMBAND);
	BIND_ENUM_CONSTANT(BANDWIDTH_WIDEBAND);
	BIND_ENUM_CONSTANT(BANDWIDTH_SUPERWIDEBAND);
	BIND_ENUM_CONSTANT(BANDWIDTH_FULLBAND);
	BIND_ENUM_CONSTANT(BANDWIDTH_AUTO);
}
