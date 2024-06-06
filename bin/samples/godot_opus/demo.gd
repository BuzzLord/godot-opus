extends Node2D

@onready var opus = $GodotOpus
@onready var input = $Input
@onready var output = $Output
@onready var opus_enabled_options = %OpusEnabledOptions
@onready var bit_rate_value_label = %BitRateValueLabel

const CAPTURE_BUFFER_SIZE = 256
const MAX_PACKET_ID = (1 << 31) - 1
const DROPPED_FRAME_THRESHOLD = 10
const RAW_SAMPLE_SIZE = 4

var bus_layout: AudioBusLayout = load("res://samples/godot_opus/demo_bus_layout.tres")
var bus_index: int
var effect: AudioEffectCapture
var playback: AudioStreamGeneratorPlayback

var frame_size: int
var internal_buffer := PackedVector2Array()
var receive_packets := Array()

var send_id: int = 0
var recv_id: int = 0

var use_opus: bool = true
var opus_initialized: bool = false
var drop_rate: float = 0.0
var is_surround: bool = false

var packet_count: int = 0
var packet_threshold: int = 60
var packet_size_avg: float = 0.0
var packet_size_total: float = 0.0
var start_time_msec: int = 0

func _init():
	AudioServer.set_bus_layout(bus_layout)

func _ready():
	_init_opus()
	
	input.bus = &"Capture"
	bus_index = AudioServer.get_bus_index("Capture")
	effect = AudioServer.get_bus_effect(bus_index, 0)
	playback = output.get_stream_playback()
	
	is_surround = AudioServer.get_bus_channels(bus_index) > 1
	start_time_msec = Time.get_ticks_msec()

func _process(_delta):
	if use_opus:
		_process_mic()
		_process_voice()
	else:
		_process_mic_bypass()
		_process_voice_bypass()

## Initialize GodotOpus using the current configuration. Can be called more than once.
func _init_opus():
	opus_initialized = opus.initialize()
	if opus_initialized:
		print("Initialized GodotOpus successfully")
	else:
		push_error("Failed to initialize GodotOpus")
		use_opus = false
		opus_enabled_options.select(1)
		return
	
	packet_count = 0
	packet_size_avg = 0.0
	frame_size = opus.get_frame_size()

## Grab chunk of mic data from Capture, use GodotOpus to encode, then 'send' them to client to process voice
func _process_mic():
	# Internal buffer handling, in case GodotOpus can't accept data for some reason
	var use_internal_buffer = false
	if internal_buffer.size() > 0:
		if opus.can_push_buffer(internal_buffer.size()):
			opus.push_buffer(internal_buffer)
			internal_buffer.clear()
		else:
			use_internal_buffer = true
			
	while effect.get_frames_available() >= CAPTURE_BUFFER_SIZE:
		var stereo_data: PackedVector2Array = effect.get_buffer(CAPTURE_BUFFER_SIZE)
		
		if check_empty_data(stereo_data):
			continue
		
		if use_internal_buffer:
			internal_buffer.append_array(stereo_data)
		elif opus.can_push_buffer(CAPTURE_BUFFER_SIZE):
			opus.push_buffer(stereo_data)
		else:
			internal_buffer.append_array(stereo_data)
	
	while opus.has_encoded_packet():
		var packet: PackedByteArray = opus.get_encoded_packet()
		
		if packet.is_empty():
			push_error("Error encoding packet")
			break
		
		calc_packet_avg(packet.size())
		
		if drop_rate < randf():
			send_data(send_id, packet)
		send_id = inc_id(send_id)

## Send encoded packet to client (would be an rpc in multiplayer project). Include packet id for dropped packet detection.
func send_data(packet_id: int, payload: PackedByteArray):
	if packet_id == recv_id:
		receive_packets.append(payload)
		recv_id = inc_id(recv_id)
	else:
		var delta: int = (packet_id - recv_id) % MAX_PACKET_ID
		if delta < DROPPED_FRAME_THRESHOLD:
			# Probably dropped frames, populate them
			for i in range(delta):
				receive_packets.append(PackedByteArray())
				recv_id = inc_id(recv_id)
		else:
			# delta very large (probably close to MAX_PACKET_ID), 
			# either means old packet(s), or huge drop. ignore it.
			pass
		
		receive_packets.append(payload)
		recv_id = inc_id(recv_id)

## If any packets available, decode them and push them to Generator playback (if possible)
func _process_voice():
	while receive_packets.size() > 0:
		if playback.get_frames_available() < frame_size:
			break
		
		var data: PackedByteArray = receive_packets[0]
		receive_packets.remove_at(0)
		
		var output_data: PackedVector2Array
		if data.size() > 0:
			output_data = opus.decode(data)
		else:
			output_data = opus.decode_dropped(frame_size)
		
		if output_data.is_empty():
			push_error("Error decoding packet")
			continue
		
		playback.push_buffer(output_data)

# ------------------------------------------------------------------------------
# Bypass functions for testing without the GodotOpus codec involved.

## Grab chunk of mic data from Capture, pass to process voice
func _process_mic_bypass():
	while effect.get_frames_available() >= CAPTURE_BUFFER_SIZE:
		var stereo_data: PackedVector2Array = effect.get_buffer(CAPTURE_BUFFER_SIZE)
		
		if check_empty_data(stereo_data):
			continue
		
		if opus.channels == GodotOpus.CHANNELS_MONO:
			for i in range(stereo_data.size()):
				var data = (stereo_data[i][0] + stereo_data[i][1]) * 0.5
				stereo_data[i][0] = data
				stereo_data[i][1] = data
			calc_packet_avg(stereo_data.size() * RAW_SAMPLE_SIZE)
		else:
			calc_packet_avg(stereo_data.size() * 2 * RAW_SAMPLE_SIZE)
		
		if drop_rate < randf():
			receive_packets.append(stereo_data)

## If any packets available, push them to Generator playback (if possible)
func _process_voice_bypass():
	while receive_packets.size() > 0:
		if playback.get_frames_available() < CAPTURE_BUFFER_SIZE:
			break
		
		var data: PackedVector2Array = receive_packets[0]
		receive_packets.remove_at(0)
		
		playback.push_buffer(data)

# ------------------------------------------------------------------------------
# Utility functions

## Increment packet id variable, with overflow wrapping
func inc_id(id: int) -> int:
	return (id + 1) % MAX_PACKET_ID

## Add latest packet size to moving average
func calc_packet_avg(size: int):
	if packet_count == 0:
		packet_size_avg = size
		packet_size_total = size
	else:
		packet_size_avg = packet_size_avg * 0.99 + float(size) * 0.01
		packet_size_total += float(size)
	packet_count += 1
	if packet_count == packet_threshold:
		packet_count = 0
		var end_time_msec = Time.get_ticks_msec()
		var delta_msec: float = end_time_msec - start_time_msec
		start_time_msec = end_time_msec
		var bit_rate = packet_size_total / delta_msec
		packet_size_total = 0.0
		bit_rate_value_label.text = "%.2f KB/sec" % bit_rate

## Check if the data from Capture is completely empty (all zeros); skip processing if so. 
func check_empty_data(data: PackedVector2Array) -> bool:
	# Workaround for a bug in AudioEffectCapture on surround systems:
	# 
	if not is_surround:
		return false
	for i in range(data.size()):
		if data[i][0] != 0.0 or data[i][1] != 0.0:
			return false
	return true

# ------------------------------------------------------------------------------
# UI Signal callback functions

func _on_opus_enabled_options_item_selected(index):
	# 0: Enabled, 1: Disabled
	use_opus = (index == 0)
	if use_opus and not opus_initialized:
		_init_opus()

func _on_channels_options_item_selected(index):
	# 0: Mono, 1: Stereo
	var options = [GodotOpus.CHANNELS_MONO, GodotOpus.CHANNELS_STEREO]
	var new_channels = options[index]
	
	if new_channels != opus.channels:
		opus.channels = new_channels
		_init_opus()

func _on_frame_duration_options_item_selected(index):
	# 0: 2.5 ms, 1: 5 ms, 2: 10 ms, 3: 20 ms, 4: 40 ms,
	# 5: 60 ms, 6: 80 ms, 7: 100 ms, 8: 120 ms
	var options = [[GodotOpus.FRAMESIZE_2_5_MS, 240],
		[GodotOpus.FRAMESIZE_5_MS, 120],
		[GodotOpus.FRAMESIZE_10_MS, 60], 
		[GodotOpus.FRAMESIZE_20_MS, 30],
		[GodotOpus.FRAMESIZE_40_MS, 15], 
		[GodotOpus.FRAMESIZE_60_MS, 10],
		[GodotOpus.FRAMESIZE_80_MS, 8], 
		[GodotOpus.FRAMESIZE_100_MS, 6],
		[GodotOpus.FRAMESIZE_120_MS, 5]]
	var new_duration = options[index][0]
	var new_packet_thresh = options[index][1]
	
	if new_duration != opus.frame_duration:
		opus.frame_duration = new_duration
		packet_threshold = new_packet_thresh
		_init_opus()

func _on_bandwidth_options_item_selected(index):
	# 0: Auto, 1: NB, 2: MB, 3: WB, 4: SWB, 5: FB
	var options = [GodotOpus.BANDWIDTH_AUTO, GodotOpus.BANDWIDTH_NARROWBAND,
		GodotOpus.BANDWIDTH_MEDIUMBAND, GodotOpus.BANDWIDTH_WIDEBAND,
		GodotOpus.BANDWIDTH_SUPERWIDEBAND, GodotOpus.BANDWIDTH_FULLBAND]
	var new_bandwidth = options[index]
	
	if new_bandwidth != opus.bandwidth:
		opus.bandwidth = new_bandwidth
		_init_opus()

func _on_drop_rate_spin_box_value_changed(value):
	drop_rate = value / 100.0

