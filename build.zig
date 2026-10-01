const std = @import("std");

pub const MidiApi = enum {
    alsa,
    jack,
    core,
    winmm,
    amidi,
    android_usb,
    dummy,

    pub const defaults = struct {
        pub const macos: []const MidiApi = &.{.core};
        pub const linux: []const MidiApi = &.{ .alsa, .jack };
        pub const windows: []const MidiApi = &.{.winmm};
        pub const ios: []const MidiApi = &.{.core};
        pub const android: []const MidiApi = &.{.android_usb};
    };
};

fn unsupportedOs(os: std.Target.Os.Tag) noreturn {
    std.log.err("unsupported OS: {s}", .{@tagName(os)});
    std.process.exit(1);
}

fn unsupportedMidiApi(os: std.Target.Os.Tag, api: MidiApi) noreturn {
    std.log.err("MIDI API {s} is unsupported on {s}", .{ @tagName(api), @tagName(os) });
    std.process.exit(1);
}

pub fn build(b: *std.Build) !void {
    const rtmidi_cpp_root = b.path(".");

    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const shared = b.option(bool, "shared", "Create shared library instead of static") orelse false;

    const lib_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    if (target.result.abi.isAndroid()) lib_mod.pic = true;
    const lib = b.addLibrary(.{
        .name = "rtmidi",
        .root_module = lib_mod,
        .linkage = if (shared) .dynamic else .static,
    });

    if (b.option(std.Build.LazyPath, "system_include_path", "system include path")) |p| lib_mod.addSystemIncludePath(p);
    if (b.option(std.Build.LazyPath, "system_framework_path", "system framework path")) |p| lib_mod.addSystemFrameworkPath(p);
    if (b.option(std.Build.LazyPath, "library_path", "system library path")) |p| lib_mod.addLibraryPath(p);
    const pkg_config: std.Build.Module.SystemLib.UsePkgConfig = if (target.query.isNative()) .yes else .no;

    lib_mod.addIncludePath(rtmidi_cpp_root);
    lib.installHeadersDirectory(rtmidi_cpp_root, "", .{ .include_extensions = &.{ "rtmidi_c.h", "RtMidi.h" } });

    const t = lib.rootModuleTarget();

    const midi_apis = blk: {
        const maybe_opts = b.option([]const MidiApi, "midi-api", "Enable specific MIDI APIs");
        break :blk if (maybe_opts) |opts| opts else switch (t.os.tag) {
            .macos => MidiApi.defaults.macos,
            .ios => MidiApi.defaults.ios,
            .linux => if (t.abi.isAndroid()) MidiApi.defaults.android else MidiApi.defaults.linux,
            .windows => MidiApi.defaults.windows,
            else => unsupportedOs(t.os.tag),
        };
    };

    var flags = std.ArrayList([]const u8).empty;
    defer flags.deinit(b.allocator);

    switch (t.os.tag) {
        .macos => {
            for (midi_apis) |api| {
                switch (api) {
                    .core => {
                        try flags.append(b.allocator, "-D__MACOSX_CORE__");
                        lib_mod.linkFramework("CoreMIDI", .{});
                        lib_mod.linkFramework("CoreAudio", .{});
                        lib_mod.linkFramework("CoreFoundation", .{});
                    },
                    .dummy => try flags.append(b.allocator, "-D__RTMIDI_DUMMY__"),
                    else => unsupportedMidiApi(t.os.tag, api),
                }
            }
        },
        .ios => {
            for (midi_apis) |api| {
                switch (api) {
                    .core => {
                        try flags.append(b.allocator, "-D__MACOSX_CORE__");
                        lib_mod.linkFramework("CoreMIDI", .{});
                        lib_mod.linkFramework("CoreFoundation", .{});
                    },
                    .dummy => try flags.append(b.allocator, "-D__RTMIDI_DUMMY__"),
                    else => unsupportedMidiApi(t.os.tag, api),
                }
            }
        },
        .linux => if (t.abi.isAndroid()) {
            for (midi_apis) |api| {
                switch (api) {
                    .android_usb => {
                        try flags.append(b.allocator, "-D__ANDROID_USB_MIDI__");
                        lib_mod.addCSourceFiles(.{ .root = rtmidi_cpp_root, .files = &.{"rtmidi_android_usb.c"} });
                    },
                    .amidi => {
                        try flags.append(b.allocator, "-D__AMIDI__");
                        lib_mod.linkSystemLibrary("amidi", .{ .use_pkg_config = .no });
                        lib_mod.linkSystemLibrary("log", .{ .use_pkg_config = .no });
                        lib_mod.linkSystemLibrary("nativehelper", .{ .use_pkg_config = .no });
                    },
                    .dummy => try flags.append(b.allocator, "-D__RTMIDI_DUMMY__"),
                    else => unsupportedMidiApi(t.os.tag, api),
                }
            }
        } else {
            for (midi_apis) |api| {
                switch (api) {
                    .alsa => {
                        try flags.append(b.allocator, "-D__LINUX_ALSA__");
                        lib_mod.linkSystemLibrary("asound", .{ .use_pkg_config = pkg_config });
                        lib_mod.linkSystemLibrary("pthread", .{ .use_pkg_config = pkg_config });
                    },
                    .jack => {
                        try flags.append(b.allocator, "-D__LINUX_JACK__");
                        lib_mod.linkSystemLibrary("jack", .{ .use_pkg_config = pkg_config });
                    },
                    .dummy => try flags.append(b.allocator, "-D__RTMIDI_DUMMY__"),
                    else => unsupportedMidiApi(t.os.tag, api),
                }
            }
        },
        .windows => {
            for (midi_apis) |api| {
                switch (api) {
                    .winmm => {
                        try flags.append(b.allocator, "-D__WINDOWS_MM__");
                        lib_mod.linkSystemLibrary("winmm", .{});
                        lib_mod.linkSystemLibrary("ole32", .{});
                    },
                    .dummy => try flags.append(b.allocator, "-D__RTMIDI_DUMMY__"),
                    else => unsupportedMidiApi(t.os.tag, api),
                }
            }
        },
        else => unsupportedOs(t.os.tag),
    }

    lib_mod.addCSourceFiles(.{
        .root = rtmidi_cpp_root,
        .files = &.{"RtMidi.cpp"},
        .flags = flags.items,
    });
    lib_mod.addCSourceFiles(.{
        .root = rtmidi_cpp_root,
        .files = &.{"rtmidi_c.cpp"},
        .flags = flags.items,
    });

    b.installArtifact(lib);

    const rtmidi_mod = b.addModule("rtmidi", .{
        .root_source_file = b.path("rtmidi.zig"),
        .target = target,
        .optimize = optimize,
    });
    rtmidi_mod.linkLibrary(lib);

    const root_mod_test = b.addTest(.{
        .root_module = rtmidi_mod,
    });
    const root_mod_test_run = b.addRunArtifact(root_mod_test);
    const test_step = b.step("test", "test");
    test_step.dependOn(&root_mod_test_run.step);
}
