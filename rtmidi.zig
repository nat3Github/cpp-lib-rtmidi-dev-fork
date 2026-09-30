const std = @import("std");
const builtin = @import("builtin");
const assert = std.debug.assert;
const Allocator = std.mem.Allocator;

pub const c = struct {
    pub const struct_RtMidiWrapper = extern struct {
        ptr: ?*anyopaque = null,
        callback_proxy: ?*anyopaque = null,
        error_callback_proxy: ?*anyopaque = null,
        ok: bool = false,
        msg: [*c]u8 = null,
    };
    pub const RtMidiWrapper = struct_RtMidiWrapper;
    pub const RtMidiPtr = [*c]struct_RtMidiWrapper;
    pub const RtMidiInPtr = [*c]struct_RtMidiWrapper;
    pub const RtMidiOutPtr = [*c]struct_RtMidiWrapper;

    pub const RTMIDI_API_UNSPECIFIED: c_int = 0;
    pub const RTMIDI_API_MACOSX_CORE: c_int = 1;
    pub const RTMIDI_API_LINUX_ALSA: c_int = 2;
    pub const RTMIDI_API_UNIX_JACK: c_int = 3;
    pub const RTMIDI_API_WINDOWS_MM: c_int = 4;
    pub const RTMIDI_API_RTMIDI_DUMMY: c_int = 5;
    pub const RTMIDI_API_WEB_MIDI_API: c_int = 6;
    pub const RTMIDI_API_WINDOWS_UWP: c_int = 7;
    pub const RTMIDI_API_ANDROID: c_int = 8;

    pub const enum_RtMidiApi = c_uint;
    pub const RTMIDI_ERROR_WARNING: c_int = 0;
    pub const RTMIDI_ERROR_DEBUG_WARNING: c_int = 1;
    pub const RTMIDI_ERROR_UNSPECIFIED: c_int = 2;
    pub const RTMIDI_ERROR_NO_DEVICES_FOUND: c_int = 3;
    pub const RTMIDI_ERROR_INVALID_DEVICE: c_int = 4;
    pub const RTMIDI_ERROR_MEMORY_ERROR: c_int = 5;
    pub const RTMIDI_ERROR_INVALID_PARAMETER: c_int = 6;
    pub const RTMIDI_ERROR_INVALID_USE: c_int = 7;
    pub const RTMIDI_ERROR_DRIVER_ERROR: c_int = 8;
    pub const RTMIDI_ERROR_SYSTEM_ERROR: c_int = 9;
    pub const RTMIDI_ERROR_THREAD_ERROR: c_int = 10;
    pub const enum_RtMidiErrorType = c_uint;

    pub const RtMidiCCallback = ?*const fn (timeStamp: f64, message: [*c]const u8, messageSize: usize, userData: ?*anyopaque) callconv(.c) void;
    pub const RtMidiErrorCCallback = ?*const fn (@"type": enum_RtMidiErrorType, errorText: [*c]const u8, userData: ?*anyopaque) callconv(.c) void;

    pub extern fn rtmidi_get_version() [*c]const u8;
    pub extern fn rtmidi_get_compiled_api(apis: [*c]enum_RtMidiApi, apis_size: c_uint) c_int;
    pub extern fn rtmidi_api_name(api: enum_RtMidiApi) [*c]const u8;
    pub extern fn rtmidi_api_display_name(api: enum_RtMidiApi) [*c]const u8;
    pub extern fn rtmidi_compiled_api_by_name(name: [*c]const u8) enum_RtMidiApi;
    pub extern fn rtmidi_open_port(device: RtMidiPtr, portNumber: c_uint, portName: [*c]const u8) void;
    pub extern fn rtmidi_open_virtual_port(device: RtMidiPtr, portName: [*c]const u8) void;
    pub extern fn rtmidi_close_port(device: RtMidiPtr) void;
    pub extern fn rtmidi_get_port_count(device: RtMidiPtr) c_uint;
    pub extern fn rtmidi_get_port_name(device: RtMidiPtr, portNumber: c_uint, bufOut: [*c]u8, bufLen: [*c]c_int) c_int;
    pub extern fn rtmidi_in_create(api: enum_RtMidiApi, clientName: [*c]const u8, queueSizeLimit: c_uint) RtMidiInPtr;
    pub extern fn rtmidi_in_free(device: RtMidiInPtr) void;
    pub extern fn rtmidi_in_get_current_api(device: RtMidiPtr) enum_RtMidiApi;
    pub extern fn rtmidi_in_set_callback(device: RtMidiInPtr, callback: RtMidiCCallback, userData: ?*anyopaque) void;
    pub extern fn rtmidi_in_cancel_callback(device: RtMidiInPtr) void;
    pub extern fn rtmidi_in_ignore_types(device: RtMidiInPtr, midiSysex: bool, midiTime: bool, midiSense: bool) void;
    pub extern fn rtmidi_in_get_message(device: RtMidiInPtr, message: [*c]u8, size: [*c]usize) f64;
    pub extern fn rtmidi_out_create(api: enum_RtMidiApi, clientName: [*c]const u8) RtMidiOutPtr;
    pub extern fn rtmidi_out_free(device: RtMidiOutPtr) void;
    pub extern fn rtmidi_out_get_current_api(device: RtMidiPtr) enum_RtMidiApi;
    pub extern fn rtmidi_out_send_message(device: RtMidiOutPtr, message: [*c]const u8, length: c_int) c_int;
    pub extern fn rtmidi_set_error_callback(device: RtMidiPtr, callback: RtMidiErrorCCallback, userData: ?*anyopaque) void;
};

pub const MidiApi = enum(c.enum_RtMidiApi) {
    unspecified = c.RTMIDI_API_UNSPECIFIED,
    macosx_core = c.RTMIDI_API_MACOSX_CORE,
    linux_alsa = c.RTMIDI_API_LINUX_ALSA,
    unix_jack = c.RTMIDI_API_UNIX_JACK,
    windows_mm = c.RTMIDI_API_WINDOWS_MM,
    rtmidi_dummy = c.RTMIDI_API_RTMIDI_DUMMY,
    web_midi_api = c.RTMIDI_API_WEB_MIDI_API,
    windows_uwp = c.RTMIDI_API_WINDOWS_UWP,
    android = c.RTMIDI_API_ANDROID,
};

pub const ErrorType = enum(c.enum_RtMidiErrorType) {
    warning = c.RTMIDI_ERROR_WARNING,
    debug_warning = c.RTMIDI_ERROR_DEBUG_WARNING,
    unspecified = c.RTMIDI_ERROR_UNSPECIFIED,
    no_devices_found = c.RTMIDI_ERROR_NO_DEVICES_FOUND,
    invalid_device = c.RTMIDI_ERROR_INVALID_DEVICE,
    memory_error = c.RTMIDI_ERROR_MEMORY_ERROR,
    invalid_parameter = c.RTMIDI_ERROR_INVALID_PARAMETER,
    invalid_use = c.RTMIDI_ERROR_INVALID_USE,
    driver_error = c.RTMIDI_ERROR_DRIVER_ERROR,
    system_error = c.RTMIDI_ERROR_SYSTEM_ERROR,
    thread_error = c.RTMIDI_ERROR_THREAD_ERROR,
};

pub const Error = error{
    RtMidiError,
};

pub fn getVersion() [:0]const u8 {
    return std.mem.span(c.rtmidi_get_version());
}

pub fn getCompiledApis(allocator: Allocator) ![]MidiApi {
    const count = c.rtmidi_get_compiled_api(null, 0);
    if (count < 0) return error.RtMidiError;
    const apis = try allocator.alloc(c.enum_RtMidiApi, @intCast(count));
    defer allocator.free(apis);

    const actual_count = c.rtmidi_get_compiled_api(apis.ptr, @intCast(count));
    if (actual_count < 0) return error.RtMidiError;

    const result = try allocator.alloc(MidiApi, @intCast(actual_count));
    for (apis[0..@intCast(actual_count)], 0..) |api, i| {
        result[i] = @enumFromInt(api);
    }
    return result;
}

pub fn getApiName(api: MidiApi) [:0]const u8 {
    return std.mem.span(c.rtmidi_api_name(@intFromEnum(api)));
}

pub fn getApiDisplayName(api: MidiApi) [:0]const u8 {
    return std.mem.span(c.rtmidi_api_display_name(@intFromEnum(api)));
}

pub fn getCompiledApiByName(name: [:0]const u8) MidiApi {
    return @enumFromInt(c.rtmidi_compiled_api_by_name(name.ptr));
}

fn check(ok: bool, msg: [*c]const u8) !void {
    if (!ok) {
        if (msg != null) {
            std.log.err("RtMidi error: {s}", .{msg});
        }
        return error.RtMidiError;
    }
}

pub const MidiMessage = packed union {
    raw: [3]u8,
    data: packed struct {
        channel: u4,
        status: u4,
        byte1: u8,
        byte2: u8,
    },
};

pub const MidiIn = struct {
    pub const Config = struct {
        queue_size_limit: u32 = 1024,
        api: MidiApi = switch (builtin.os.tag) {
            .windows => MidiApi.windows_uwp,
            .macos => MidiApi.macosx_core,
            .linux => MidiApi.linux_alsa,
            .wasi => MidiApi.web_midi_api,
            else => MidiApi.unspecified,
        },
        midi_sysex: bool = false,
        midi_time: bool = false,
        midi_sense: bool = false,
    };
    ptr: c.RtMidiInPtr,

    pub fn init(self: *@This(), client_name: [:0]const u8, cfg: Config) !void {
        const ptr = c.rtmidi_in_create(@intCast(@intFromEnum(cfg.api)), client_name.ptr, cfg.queue_size_limit);
        if (ptr == null) return error.RtMidiError;
        try check(ptr.*.ok, ptr.*.msg);
        c.rtmidi_in_ignore_types(ptr, !cfg.midi_sysex, !cfg.midi_time, !cfg.midi_sense);
        self.* = MidiIn{ .ptr = ptr };
    }

    pub fn deinit(self: *MidiIn) void {
        c.rtmidi_in_free(self.ptr);
    }

    pub fn openPort(self: *MidiIn, port_number: u32, name: [:0]const u8) !void {
        c.rtmidi_open_port(self.ptr, port_number, name.ptr);
        try check(self.ptr.*.ok, self.ptr.*.msg);
    }

    pub fn openVirtualPort(self: *MidiIn, name: [:0]const u8) !void {
        c.rtmidi_open_virtual_port(self.ptr, name.ptr);
        try check(self.ptr.*.ok, self.ptr.*.msg);
    }

    pub fn closePort(self: *MidiIn) void {
        c.rtmidi_close_port(self.ptr);
    }

    pub fn getPortCount(self: *MidiIn) u32 {
        return c.rtmidi_get_port_count(self.ptr);
    }

    pub fn getPortName(self: *MidiIn, port_number: u32, allocator: Allocator) ![]u8 {
        var len: i32 = 0;
        _ = c.rtmidi_get_port_name(self.ptr, port_number, null, &len);
        if (len <= 0) return error.RtMidiError;

        const buf = try allocator.alloc(u8, @intCast(len));
        errdefer allocator.free(buf);

        const actual_len = c.rtmidi_get_port_name(self.ptr, port_number, buf.ptr, &len);
        if (actual_len < 0) return error.RtMidiError;

        return buf[0..@intCast(actual_len)];
    }

    pub fn setCallback(self: *MidiIn, callback: c.RtMidiCCallback, user_data: ?*anyopaque) !void {
        c.rtmidi_in_set_callback(self.ptr, callback, user_data);
        try check(self.ptr.*.ok, self.ptr.*.msg);
    }

    pub fn cancelCallback(self: *MidiIn) void {
        c.rtmidi_in_cancel_callback(self.ptr);
    }

    pub fn ignoreTypes(self: *MidiIn, midi_sysex: bool, midi_time: bool, midi_sense: bool) void {
        c.rtmidi_in_ignore_types(self.ptr, midi_sysex, midi_time, midi_sense);
    }

    pub fn getCurrentApi(self: *MidiIn) MidiApi {
        return @enumFromInt(c.rtmidi_in_get_current_api(self.ptr));
    }

    pub fn getMessage(self: *MidiIn, buff: []u8) ?struct { []const u8, f64 } {
        var size: usize = buff.len;
        const delta = c.rtmidi_in_get_message(self.ptr, buff.ptr, &size);
        if (size == 0) return null;
        return .{ buff[0..size], delta };
    }
};

pub const MidiOut = struct {
    ptr: c.RtMidiOutPtr,

    pub fn init(api: MidiApi, client_name: [:0]const u8) !MidiOut {
        const ptr = c.rtmidi_out_create(@intCast(@intFromEnum(api)), client_name.ptr);
        if (ptr == null) return error.RtMidiError;
        try check(ptr.*.ok, ptr.*.msg);
        return MidiOut{ .ptr = ptr.? };
    }

    pub fn deinit(self: *MidiOut) void {
        c.rtmidi_out_free(self.ptr);
    }

    pub fn openPort(self: *MidiOut, port_number: u32, name: [:0]const u8) !void {
        c.rtmidi_open_port(self.ptr, port_number, name.ptr);
        try check(self.ptr.*.ok, self.ptr.*.msg);
    }

    pub fn openVirtualPort(self: *MidiOut, name: [:0]const u8) !void {
        c.rtmidi_open_virtual_port(self.ptr, name.ptr);
        try check(self.ptr.*.ok, self.ptr.*.msg);
    }

    pub fn closePort(self: *MidiOut) void {
        c.rtmidi_close_port(self.ptr);
    }

    pub fn getPortCount(self: *MidiOut) u32 {
        return c.rtmidi_get_port_count(self.ptr);
    }

    pub fn getPortName(self: *MidiOut, port_number: u32, allocator: Allocator) ![]u8 {
        var len: i32 = 0;
        _ = c.rtmidi_get_port_name(self.ptr, port_number, null, &len);
        if (len <= 0) return error.RtMidiError;

        const buf = try allocator.alloc(u8, @intCast(len));
        errdefer allocator.free(buf);

        const actual_len = c.rtmidi_get_port_name(self.ptr, port_number, buf.ptr, &len);
        if (actual_len < 0) return error.RtMidiError;

        return buf[0..@intCast(actual_len)];
    }

    pub fn sendMessage(self: *MidiOut, message: []const u8) !void {
        const ret = c.rtmidi_out_send_message(self.ptr, message.ptr, @intCast(message.len));
        if (ret < 0) try check(self.ptr.*.ok, self.ptr.*.msg);
    }

    pub fn getCurrentApi(self: *MidiOut) MidiApi {
        return @enumFromInt(c.rtmidi_out_get_current_api(self.ptr));
    }
};

test "midi out create" {
    var midi_out = try MidiOut.init(.unspecified, "ZigTest");
    defer midi_out.deinit();
    const count = midi_out.getPortCount();
    std.debug.print("MidiOut ports: {}\n", .{count});
}

test "midi in create" {
    var midi_in: MidiIn = undefined;
    try MidiIn.init(&midi_in, "ZigTest", .{});
    defer midi_in.deinit();
    const count = midi_in.getPortCount();
    std.debug.print("MidiIn ports: {}\n", .{count});
}

test "global functions" {
    const version = getVersion();
    std.debug.print("RtMidi version: {s}\n", .{version});
    assert(version.len > 0);

    const apis = try getCompiledApis(std.testing.allocator);
    defer std.testing.allocator.free(apis);
    std.debug.print("Compiled APIs: {}\n", .{apis.len});
    assert(apis.len > 0);

    for (apis) |api| {
        const name = getApiName(api);
        const display_name = getApiDisplayName(api);
        std.debug.print("  API: {s} ({s})\n", .{ name, display_name });
    }
}

test "get current api" {
    var midi_out = try MidiOut.init(.unspecified, "ZigTest");
    defer midi_out.deinit();
    const api = midi_out.getCurrentApi();
    std.debug.print("MidiOut current API: {}\n", .{api});
    assert(api != .unspecified);
}
