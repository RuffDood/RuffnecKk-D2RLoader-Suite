const std = @import("std");
const drlg = @import("d2-drlg");
const render = @import("d2-render");

fn performanceCounter() u64 {
    var value: std.os.windows.LARGE_INTEGER = undefined;
    std.debug.assert(std.os.windows.ntdll.RtlQueryPerformanceCounter(&value).toBool());
    return @bitCast(value);
}

fn performanceFrequency() u64 {
    var value: std.os.windows.LARGE_INTEGER = undefined;
    std.debug.assert(std.os.windows.ntdll.RtlQueryPerformanceFrequency(&value).toBool());
    return @bitCast(value);
}

fn elapsedMilliseconds(start: u64, end: u64, frequency: u64) f64 {
    return @as(f64, @floatFromInt(end - start)) * 1000.0 / @as(f64, @floatFromInt(frequency));
}

fn parseU32(text: []const u8) !u32 {
    return std.fmt.parseUnsigned(u32, text, 10);
}

const MaximumDataRoots: usize = 4;
const MaximumTableBytes: usize = 64 * 1024 * 1024;
const MaximumDataPathBytes: usize = 32 * 1024;

const DataOptions = struct {
    excel_roots: [MaximumDataRoots][]const u8 = .{ "", "", "", "" },
    excel_root_count: usize = 0,
    tiles_roots: [MaximumDataRoots][]const u8 = .{ "", "", "", "" },
    tiles_root_count: usize = 0,

    fn isAbsolutePath(path: []const u8) bool {
        if (path.len >= 3 and std.ascii.isAlphabetic(path[0]) and path[1] == ':' and (path[2] == '/' or path[2] == '\\')) {
            return true;
        }
        return path.len >= 2 and ((path[0] == '\\' and path[1] == '\\') or (path[0] == '/' and path[1] == '/'));
    }

    fn appendRoot(
        roots: *[MaximumDataRoots][]const u8,
        count: *usize,
        value: []const u8,
    ) !void {
        if (value.len == 0 or value.len >= MaximumDataPathBytes or std.mem.indexOfScalar(u8, value, 0) != null or !isAbsolutePath(value)) {
            return error.InvalidDataRoot;
        }
        for (roots[0..count.*]) |existing| {
            if (std.ascii.eqlIgnoreCase(existing, value)) return;
        }
        if (count.* >= roots.len) return error.TooManyDataRoots;
        roots[count.*] = value;
        count.* += 1;
    }

    fn parse(args: anytype) !DataOptions {
        var result: DataOptions = .{};
        while (args.next()) |flag| {
            const value = args.next() orelse return error.MissingDataRoot;
            if (std.mem.eql(u8, flag, "--excel-root")) {
                try appendRoot(
                    &result.excel_roots,
                    &result.excel_root_count,
                    value,
                );
            } else if (std.mem.eql(u8, flag, "--tiles-root")) {
                try appendRoot(
                    &result.tiles_roots,
                    &result.tiles_root_count,
                    value,
                );
            } else {
                return error.UnknownDataOption;
            }
        }
        return result;
    }
};

const LoadedInputs = struct {
    owned: [7]?[]u8 = .{ null, null, null, null, null, null, null },
    fingerprint: u64 = 14695981039346656037,

    const filenames = [_][]const u8{
        "levels.txt",
        "lvlprest.txt",
        "lvltypes.txt",
        "lvlmaze.txt",
        "lvlsub.txt",
        "lvlwarp.txt",
        "objects.txt",
    };

    fn loadOne(
        allocator: std.mem.Allocator,
        roots: []const []const u8,
        filename: []const u8,
    ) !?[]u8 {
        var selected: ?[]u8 = null;
        errdefer if (selected) |bytes| allocator.free(bytes);
        var threaded = std.Io.Threaded.init_single_threaded;
        const io = threaded.io();
        for (roots) |root| {
            var pathbuf: [MaximumDataPathBytes]u8 = undefined;
            const path = std.fmt.bufPrint(&pathbuf, "{s}/{s}", .{ root, filename }) catch return error.DataPathTooLong;
            std.Io.Dir.cwd().access(io, path, .{}) catch continue;
            const bytes = std.Io.Dir.cwd().readFileAlloc(
                io,
                path,
                allocator,
                .limited(MaximumTableBytes),
            ) catch return error.ActiveTableReadFailed;
            if (bytes.len == 0) {
                allocator.free(bytes);
                return error.ActiveTableEmpty;
            }
            if (selected) |current| {
                if (!std.mem.eql(u8, current, bytes)) {
                    allocator.free(bytes);
                    return error.ConflictingActiveTables;
                }
                allocator.free(bytes);
            } else {
                selected = bytes;
            }
        }
        return selected;
    }

    fn load(
        allocator: std.mem.Allocator,
        options: DataOptions,
    ) !LoadedInputs {
        var result: LoadedInputs = .{};
        errdefer result.deinit(allocator);
        const roots = options.excel_roots[0..options.excel_root_count];
        for (filenames, 0..) |filename, index| {
            result.owned[index] = try loadOne(allocator, roots, filename);
            digestBytes(&result.fingerprint, filename);
            if (result.owned[index]) |bytes| {
                digestBytes(&result.fingerprint, bytes);
            } else {
                digestInt(&result.fingerprint, @as(u8, 0));
            }
        }
        return result;
    }

    fn deinit(self: *LoadedInputs, allocator: std.mem.Allocator) void {
        for (&self.owned) |*entry| {
            if (entry.*) |bytes| allocator.free(bytes);
            entry.* = null;
        }
    }

    fn context(
        self: *const LoadedInputs,
        allocator: std.mem.Allocator,
        options: DataOptions,
    ) !drlg.Ctx {
        return drlg.Ctx.initFromBuffers(allocator, .{
            .levels = self.owned[0],
            .lvl_prest = self.owned[1],
            .lvl_types = self.owned[2],
            .lvl_maze = self.owned[3],
            .lvl_sub = self.owned[4],
            .lvl_warp = self.owned[5],
            .objects = self.owned[6],
            .ds1_roots = options.tiles_roots[0..options.tiles_root_count],
        });
    }
};

fn digestInt(digest: *u64, value: anytype) void {
    const bytes = std.mem.asBytes(&value);
    for (bytes) |byte| {
        digest.* ^= byte;
        digest.* *%= 1099511628211;
    }
}

fn isStandardCampaignLevel(act_no: i32, level_id: i32) bool {
    return switch (act_no) {
        // The Cow Level is portal-gated in game, but libd2 generates its
        // seed-exact geometry together with Act I.  The label protocol also
        // publishes level 39, so excluding it here invalidates the complete
        // Act I coordinate snapshot.
        0 => level_id >= 1 and level_id <= 39,
        1 => level_id >= 40 and level_id <= 74,
        2 => level_id >= 75 and level_id <= 102,
        3 => level_id >= 103 and level_id <= 108,
        4 => level_id >= 109 and level_id <= 132, // Uber branches 133-136.
        else => false,
    };
}

fn emitGeometryAtlas(
    allocator: std.mem.Allocator,
    inputs: *const LoadedInputs,
    data_options: DataOptions,
    seed: u32,
    difficulty_value: u32,
    requested_act: ?i32,
    emit_cells: bool,
    campaign_only: bool,
) !void {
    const difficulty: drlg.Difficulty = switch (difficulty_value) {
        0 => .normal,
        1 => .nightmare,
        2 => .hell,
        else => return error.InvalidDifficulty,
    };
    if (requested_act) |act| {
        if (act < 0 or act >= 5) return error.InvalidGeometryRequest;
    }

    const started = performanceCounter();
    const frequency = performanceFrequency();
    var ctx = try inputs.context(allocator, data_options);
    defer ctx.deinit();

    var total_levels: usize = 0;
    var total_cells: usize = 0;
    var total_walls: usize = 0;
    var atlas_digest: u64 = 14695981039346656037;
    var act_no: i32 = 0;
    while (act_no < 5) : (act_no += 1) {
        if (requested_act != null and requested_act.? != act_no) continue;
        const act_started = performanceCounter();
        var result = try render.generateActAutomapGeometry(
            &ctx,
            allocator,
            act_no,
            seed,
            difficulty,
        );
        defer result.deinit(allocator);

        var act_cells: usize = 0;
        var act_walls: usize = 0;
        var act_levels: usize = 0;
        var act_digest: u64 = 14695981039346656037;
        for (result.levels) |level| {
            if (campaign_only and !isStandardCampaignLevel(act_no, level.level_id)) continue;
            var min_x: i32 = std.math.maxInt(i32);
            var min_y: i32 = std.math.maxInt(i32);
            var max_x: i32 = std.math.minInt(i32);
            var max_y: i32 = std.math.minInt(i32);
            var level_walls: usize = 0;
            var level_digest: u64 = 14695981039346656037;
            digestInt(&level_digest, level.level_id);
            digestInt(&level_digest, level.layer);
            for (level.cells) |cell| {
                min_x = @min(min_x, cell.tx);
                min_y = @min(min_y, cell.ty);
                max_x = @max(max_x, cell.tx);
                max_y = @max(max_y, cell.ty);
                if (cell.wall_tree) level_walls += 1;
                digestInt(&level_digest, cell.frame);
                digestInt(&level_digest, cell.tx);
                digestInt(&level_digest, cell.ty);
                digestInt(&level_digest, cell.wall_tree);
                digestInt(&level_digest, cell.raised);
                if (emit_cells) {
                    std.debug.print(
                        "MG1 C {d} {d} {d} {d} {d} {d} {d}\n",
                        .{
                            level.level_id,
                            level.layer,
                            cell.frame,
                            cell.tx,
                            cell.ty,
                            @intFromBool(cell.wall_tree),
                            @intFromBool(cell.raised),
                        },
                    );
                }
            }
            if (level.cells.len == 0) {
                min_x = 0;
                min_y = 0;
                max_x = 0;
                max_y = 0;
            }
            std.debug.print(
                "MG1 L {d} {d} {d} {d} {d} {d} {d} {d} {x}\n",
                .{ level.level_id, level.layer, level.cells.len, level_walls, min_x, min_y, max_x, max_y, level_digest },
            );
            digestInt(&act_digest, level.level_id);
            digestInt(&act_digest, level_digest);
            act_levels += 1;
            act_cells += level.cells.len;
            act_walls += level_walls;
        }
        std.debug.print(
            "MG1 A {d} {d} {d} {d} {x} {d:.3}\n",
            .{
                act_no,
                act_levels,
                act_cells,
                act_walls,
                act_digest,
                elapsedMilliseconds(act_started, performanceCounter(), frequency),
            },
        );
        digestInt(&atlas_digest, act_no);
        digestInt(&atlas_digest, act_digest);
        total_levels += act_levels;
        total_cells += act_cells;
        total_walls += act_walls;
    }
    std.debug.print(
        "MG1 Z {d} {d} {d} {d} {d} {x} {d:.3}\n",
        .{
            seed,
            difficulty_value,
            total_levels,
            total_cells,
            total_walls,
            atlas_digest,
            elapsedMilliseconds(started, performanceCounter(), frequency),
        },
    );
}

fn smokeLegacyAutomap(
    allocator: std.mem.Allocator,
    seed: u32,
    difficulty_value: u32,
    act_no: i32,
) !void {
    const difficulty: drlg.Difficulty = switch (difficulty_value) {
        0 => .normal,
        1 => .nightmare,
        2 => .hell,
        else => return error.InvalidDifficulty,
    };
    if (act_no < 0 or act_no >= 5) return error.InvalidGeometryRequest;
    var ctx = try drlg.Ctx.init(allocator);
    defer ctx.deinit();
    var result = try render.generateActAutomap(&ctx, allocator, act_no, seed, difficulty);
    defer result.deinit(allocator);
    var cells: usize = 0;
    for (result.levels) |level| cells += level.cells.len;
    std.debug.print("MG1 LEGACY {d} {d} {d}\n", .{ act_no, result.levels.len, cells });
}

fn appendUnsignedLe(
    comptime T: type,
    output: *std.ArrayListUnmanaged(u8),
    allocator: std.mem.Allocator,
    value: T,
) !void {
    var bytes: [@sizeOf(T)]u8 = undefined;
    std.mem.writeInt(T, &bytes, value, .little);
    try output.appendSlice(allocator, &bytes);
}

fn appendSignedLe(
    comptime T: type,
    output: *std.ArrayListUnmanaged(u8),
    allocator: std.mem.Allocator,
    value: T,
) !void {
    const U = std.meta.Int(.unsigned, @bitSizeOf(T));
    try appendUnsignedLe(U, output, allocator, @bitCast(value));
}

fn writeGeometryBinaryWithContext(
    allocator: std.mem.Allocator,
    ctx: *drlg.Ctx,
    seed: u32,
    difficulty_value: u32,
    act_no: i32,
    output_path: []const u8,
) !void {
    const difficulty: drlg.Difficulty = switch (difficulty_value) {
        0 => .normal,
        1 => .nightmare,
        2 => .hell,
        else => return error.InvalidDifficulty,
    };
    if (act_no < 0 or act_no >= 5 or output_path.len == 0) {
        return error.InvalidGeometryRequest;
    }
    const started = performanceCounter();
    const frequency = performanceFrequency();
    var result = try render.generateActAutomapGeometry(
        ctx,
        allocator,
        act_no,
        seed,
        difficulty,
    );
    defer result.deinit(allocator);

    var level_count: u32 = 0;
    var cell_count: u32 = 0;
    var digest: u64 = 14695981039346656037;
    for (result.levels) |level| {
        if (!isStandardCampaignLevel(act_no, level.level_id)) continue;
        level_count += 1;
        cell_count = std.math.add(
            u32,
            cell_count,
            @intCast(level.cells.len),
        ) catch return error.GeometryTooLarge;
        digestInt(&digest, level.level_id);
        digestInt(&digest, level.layer);
        for (level.cells) |cell| {
            digestInt(&digest, cell.frame);
            digestInt(&digest, cell.tx);
            digestInt(&digest, cell.ty);
            digestInt(&digest, cell.wall_tree);
            digestInt(&digest, cell.raised);
        }
    }

    const total_size = std.math.add(
        usize,
        32,
        std.math.add(
            usize,
            @as(usize, level_count) * 12,
            @as(usize, cell_count) * 16,
        ) catch return error.GeometryTooLarge,
    ) catch return error.GeometryTooLarge;
    if (total_size > 16 * 1_024 * 1_024) return error.GeometryTooLarge;
    var output: std.ArrayListUnmanaged(u8) = .empty;
    defer output.deinit(allocator);
    try output.ensureTotalCapacity(allocator, total_size);
    try output.appendSlice(allocator, "MSA1");
    try appendUnsignedLe(u16, &output, allocator, 2); // protocol version
    try appendUnsignedLe(u16, &output, allocator, 1); // standard-campaign filter
    try appendUnsignedLe(u32, &output, allocator, seed);
    try output.append(allocator, @intCast(difficulty_value));
    try output.append(allocator, @intCast(act_no));
    try appendUnsignedLe(u16, &output, allocator, 0);
    try appendUnsignedLe(u32, &output, allocator, level_count);
    try appendUnsignedLe(u32, &output, allocator, cell_count);
    try appendUnsignedLe(u64, &output, allocator, digest);
    for (result.levels) |level| {
        if (!isStandardCampaignLevel(act_no, level.level_id)) continue;
        try appendSignedLe(i32, &output, allocator, level.level_id);
        try output.append(allocator, level.layer);
        try output.appendSlice(allocator, &[_]u8{ 0, 0, 0 });
        try appendUnsignedLe(u32, &output, allocator, @intCast(level.cells.len));
        for (level.cells) |cell| {
            try appendSignedLe(i32, &output, allocator, cell.frame);
            try appendSignedLe(i32, &output, allocator, cell.tx);
            try appendSignedLe(i32, &output, allocator, cell.ty);
            try output.append(allocator, @intFromBool(cell.wall_tree));
            try output.append(allocator, @intFromBool(cell.raised));
            try output.appendSlice(allocator, &[_]u8{ 0, 0 });
        }
    }
    if (output.items.len != total_size) return error.GeometrySizeMismatch;
    var threaded = std.Io.Threaded.init_single_threaded;
    const io = threaded.io();
    try std.Io.Dir.cwd().writeFile(io, .{
        .sub_path = output_path,
        .data = output.items,
    });
    std.debug.print(
        "MSA1 seed={d} difficulty={d} act={d} levels={d} cells={d} bytes={d} digest={x} elapsed_ms={d:.3}\n",
        .{
            seed,
            difficulty_value,
            act_no,
            level_count,
            cell_count,
            output.items.len,
            digest,
            elapsedMilliseconds(started, performanceCounter(), frequency),
        },
    );
}

fn writeGeometryBinary(
    allocator: std.mem.Allocator,
    inputs: *const LoadedInputs,
    data_options: DataOptions,
    seed: u32,
    difficulty_value: u32,
    act_no: i32,
    output_path: []const u8,
) !void {
    var ctx = try inputs.context(allocator, data_options);
    defer ctx.deinit();
    try writeGeometryBinaryWithContext(
        allocator,
        &ctx,
        seed,
        difficulty_value,
        act_no,
        output_path,
    );
}

const BoundarySide = enum(u8) {
    left,
    right,
    top,
    bottom,
};

const PhysicalBoundary = struct {
    side: BoundarySide,
    fixed: i32,
    start: i32,
    end: i32,
};

const PhysicalOpening = struct {
    x: i32,
    y: i32,
    span: i32,
};

fn makePhysicalBoundary(
    source_x: i32,
    source_y: i32,
    source_w: i32,
    source_h: i32,
    target_x: i32,
    target_y: i32,
    target_w: i32,
    target_h: i32,
) ?PhysicalBoundary {
    if (source_x < 0 or source_y < 0 or source_w <= 0 or source_h <= 0 or target_x < 0 or target_y < 0 or target_w <= 0 or target_h <= 0) return null;
    const sx0: i64 = source_x;
    const sy0: i64 = source_y;
    const sx1 = sx0 + source_w;
    const sy1 = sy0 + source_h;
    const tx0: i64 = target_x;
    const ty0: i64 = target_y;
    const tx1 = tx0 + target_w;
    const ty1 = ty0 + target_h;
    const vertical_start = @max(sy0, ty0);
    const vertical_end = @min(sy1, ty1);
    const horizontal_start = @max(sx0, tx0);
    const horizontal_end = @min(sx1, tx1);
    const side: BoundarySide = if (tx1 == sx0 and vertical_end > vertical_start)
        .left
    else if (tx0 == sx1 and vertical_end > vertical_start)
        .right
    else if (ty1 == sy0 and horizontal_end > horizontal_start)
        .top
    else if (ty0 == sy1 and horizontal_end > horizontal_start)
        .bottom
    else
        return null;
    const fixed_tile = switch (side) {
        .left, .right => if (side == .left) sx0 else sx1,
        .top, .bottom => if (side == .top) sy0 else sy1,
    };
    const start_tile = switch (side) {
        .left, .right => vertical_start,
        .top, .bottom => horizontal_start,
    };
    const end_tile = switch (side) {
        .left, .right => vertical_end,
        .top, .bottom => horizontal_end,
    };
    const maximum: i64 = std.math.maxInt(i32);
    const fixed = fixed_tile * 5;
    const start = start_tile * 5;
    const end = end_tile * 5;
    if (fixed < 0 or start < 0 or end <= start or fixed > maximum or start > maximum or end > maximum) return null;
    return .{
        .side = side,
        .fixed = @intCast(fixed),
        .start = @intCast(start),
        .end = @intCast(end),
    };
}

fn collisionAt(level: *const drlg.LevelFull, world_x: i32, world_y: i32) ?u16 {
    if (level.coll_w <= 0 or level.coll_h <= 0 or world_x < 0 or world_y < 0) return null;
    const origin_x = @as(i64, level.meta.origin_x) * 5;
    const origin_y = @as(i64, level.meta.origin_y) * 5;
    const local_x = @as(i64, world_x) - origin_x;
    const local_y = @as(i64, world_y) - origin_y;
    if (local_x < 0 or local_y < 0 or local_x >= level.coll_w or local_y >= level.coll_h) return null;
    const width: usize = @intCast(level.coll_w);
    const height: usize = @intCast(level.coll_h);
    const cells = std.math.mul(usize, width, height) catch return null;
    const expected_bytes = std.math.mul(usize, cells, 2) catch return null;
    if (level.raw.len != expected_bytes) return null;
    const index = @as(usize, @intCast(local_y)) * width + @as(usize, @intCast(local_x));
    const offset = index * 2;
    return @as(u16, level.raw[offset]) | (@as(u16, level.raw[offset + 1]) << 8);
}

fn playerPathOpen(level: *const drlg.LevelFull, x: i32, y: i32) bool {
    const value = collisionAt(level, x, y) orelse return false;
    return value & 0x1c09 == 0;
}

fn updatePhysicalOpening(
    boundary: PhysicalBoundary,
    run_start: i32,
    run_end: i32,
    best: *?PhysicalOpening,
) void {
    const span = run_end - run_start;
    if (run_start < 0 or span < 3) return;
    const midpoint = run_start + @divTrunc(span, 2);
    const candidate = PhysicalOpening{
        .x = switch (boundary.side) {
            .left => boundary.fixed,
            .right => boundary.fixed - 1,
            .top, .bottom => midpoint,
        },
        .y = switch (boundary.side) {
            .top => boundary.fixed,
            .bottom => boundary.fixed - 1,
            .left, .right => midpoint,
        },
        .span = span,
    };
    if (candidate.x < 0 or candidate.y < 0) return;
    if (best.* == null or candidate.x < best.*.?.x or (candidate.x == best.*.?.x and candidate.y < best.*.?.y) or (candidate.x == best.*.?.x and candidate.y == best.*.?.y and candidate.span > best.*.?.span)) {
        best.* = candidate;
    }
}

fn scanPhysicalBoundary(
    source: *const drlg.LevelFull,
    target: *const drlg.LevelFull,
    boundary: PhysicalBoundary,
    best: *?PhysicalOpening,
) void {
    const open_at = struct {
        fn check(
            a: *const drlg.LevelFull,
            b: *const drlg.LevelFull,
            side: BoundarySide,
            fixed: i32,
            position: i32,
        ) bool {
            return switch (side) {
                .left => playerPathOpen(a, fixed, position) and playerPathOpen(a, fixed + 1, position) and playerPathOpen(b, fixed - 1, position) and playerPathOpen(b, fixed - 2, position),
                .right => playerPathOpen(a, fixed - 1, position) and playerPathOpen(a, fixed - 2, position) and playerPathOpen(b, fixed, position) and playerPathOpen(b, fixed + 1, position),
                .top => playerPathOpen(a, position, fixed) and playerPathOpen(a, position, fixed + 1) and playerPathOpen(b, position, fixed - 1) and playerPathOpen(b, position, fixed - 2),
                .bottom => playerPathOpen(a, position, fixed - 1) and playerPathOpen(a, position, fixed - 2) and playerPathOpen(b, position, fixed) and playerPathOpen(b, position, fixed + 1),
            };
        }
    }.check;
    var run_start: i32 = -1;
    var position = boundary.start;
    while (position < boundary.end) : (position += 1) {
        if (open_at(source, target, boundary.side, boundary.fixed, position)) {
            if (run_start < 0) run_start = position;
        } else if (run_start >= 0) {
            updatePhysicalOpening(boundary, run_start, position, best);
            run_start = -1;
        }
    }
    if (run_start >= 0) {
        updatePhysicalOpening(boundary, run_start, boundary.end, best);
    }
}

fn findFullLevel(result: *const drlg.ActFullResult, level_id: i32) ?usize {
    for (result.levels, 0..) |level, index| {
        if (level.meta.level_id == level_id) return index;
    }
    return null;
}

const PermanentPortalRule = struct {
    source_level_id: i32,
    target_level_id: i32,
};

// These are red-portal destinations that Levels Vis/Warp does not encode.
// Their coordinates remain data-driven: Act I uses the generated Cairn Stone
// Alpha preset plus D2Game's proven (+4,+4) quest-portal offset; Act V uses
// D2Common's generated permanent-portal DT1 tiles. Town/cow/player portals are
// intentionally excluded because their location is not seed-static.
const permanent_portal_rules = [_]PermanentPortalRule{
    .{ .source_level_id = 4, .target_level_id = 38 },
    .{ .source_level_id = 111, .target_level_id = 125 },
    .{ .source_level_id = 112, .target_level_id = 126 },
    .{ .source_level_id = 117, .target_level_id = 127 },
};

fn permanentPortalTarget(level_id: i32) ?i32 {
    for (permanent_portal_rules) |rule| {
        if (rule.source_level_id == level_id) return rule.target_level_id;
    }
    return null;
}

fn findPhysicalSeamOpening(
    result: *const drlg.ActFullResult,
    source_index: usize,
    target_level_id: i32,
) ?PhysicalOpening {
    if (source_index >= result.levels.len) return null;
    const target_index = findFullLevel(result, target_level_id) orelse return null;
    const source = &result.levels[source_index];
    const target = &result.levels[target_index];
    var best: ?PhysicalOpening = null;
    for (source.room_links) |link| {
        if (link.to_level != target_level_id or link.from_room >= source.meta.rooms.len or link.to_room >= target.meta.rooms.len) continue;
        const source_room = source.meta.rooms[link.from_room];
        const target_room = target.meta.rooms[link.to_room];
        const boundary = makePhysicalBoundary(
            source_room.x,
            source_room.y,
            source_room.w,
            source_room.h,
            target_room.x,
            target_room.y,
            target_room.w,
            target_room.h,
        ) orelse continue;
        scanPhysicalBoundary(source, target, boundary, &best);
    }
    if (best != null) return best;

    // A few stitched outdoor pairs do not expose an exact cross-level room
    // identity even though their level rectangles share a boundary. Collision
    // remains authoritative: the fallback scans that shared boundary and still
    // refuses to invent an arithmetic midpoint when no player-width path exists.
    const boundary = makePhysicalBoundary(
        source.meta.origin_x,
        source.meta.origin_y,
        source.meta.width,
        source.meta.height,
        target.meta.origin_x,
        target.meta.origin_y,
        target.meta.width,
        target.meta.height,
    ) orelse return null;
    scanPhysicalBoundary(source, target, boundary, &best);
    return best;
}

fn findPresetFacadeOpening(
    result: *const drlg.ActFullResult,
    source_level_id: i32,
    target_level_id: i32,
) ?PhysicalOpening {
    // D2Common's DRLGROOMTILE_AddTilePresetUnits maps the generated
    // Monastery double-door tile (style 6/sequence 0/right-door) to the real
    // doorway object. This is the authoritative Tamoe <-> Monastery facade;
    // a collision opening elsewhere on the long shared level boundary is not.
    const facade_level_id: i32 = 26;
    const outside_level_id: i32 = 7;
    if (!((source_level_id == facade_level_id
            and target_level_id == outside_level_id)
        or (source_level_id == outside_level_id
            and target_level_id == facade_level_id))) return null;
    const facade_index = findFullLevel(result, facade_level_id) orelse return null;
    const facade = &result.levels[facade_index];
    var anchor: ?PhysicalOpening = null;
    for (facade.special_tiles) |tile| {
        if (tile.main != 6 or tile.sub != 0 or tile.orientation != 9) continue;
        const candidate = PhysicalOpening{
            .x = facade.meta.origin_x * 5 + tile.x + 5,
            .y = facade.meta.origin_y * 5 + tile.y - 2,
            .span = 1,
        };
        if (anchor != null) return null;
        anchor = candidate;
    }
    if (anchor == null or source_level_id == facade_level_id) return anchor;

    // Keep reciprocal definitions one subtile apart as the native automap
    // contract expects. Move the outside-owned sample one subtile from the
    // facade object toward the generated facade level's centre.
    const center_x = facade.meta.origin_x * 5
        + @divTrunc(facade.meta.width * 5, 2);
    const center_y = facade.meta.origin_y * 5
        + @divTrunc(facade.meta.height * 5, 2);
    const dx = center_x - anchor.?.x;
    const dy = center_y - anchor.?.y;
    if (@abs(dx) >= @abs(dy)) {
        anchor.?.x += if (dx < 0) -1 else 1;
    } else {
        anchor.?.y += if (dy < 0) -1 else 1;
    }
    return anchor;
}

fn emitLabelAtlasWithContext(
    allocator: std.mem.Allocator,
    ctx: *drlg.Ctx,
    seed: u32,
    difficulty_value: u32,
    act_no: i32,
    current_level: i32,
) !void {
    const difficulty: drlg.Difficulty = switch (difficulty_value) {
        0 => .normal,
        1 => .nightmare,
        2 => .hell,
        else => return error.InvalidDifficulty,
    };
    if (act_no < 0 or act_no >= 5 or current_level <= 0) {
        return error.InvalidLabelRequest;
    }

    const started = performanceCounter();
    const frequency = performanceFrequency();
    var result = try drlg.generateActLabels(
        ctx,
        allocator,
        act_no,
        seed,
        difficulty,
    );
    defer result.deinit(allocator);
    var physical = try drlg.generateActFull(
        ctx,
        allocator,
        act_no,
        seed,
        difficulty,
        .{ .room_links = true, .raw_collision = true },
    );
    defer physical.deinit(allocator);
    var object_table = try drlg.gen.objects.loadFrom(
        allocator,
        ctx.objects_txt,
    );
    defer object_table.deinit();
    const permanent_portal_class_id = object_table.permanentPortalClassId();

    std.debug.print(
        "MS1 H 3 {d} {d} {d} {d}\n",
        .{ seed, difficulty_value, act_no, current_level },
    );
    var exit_count: usize = 0;
    var waypoint_count: usize = 0;
    var portal_count: usize = 0;
    var room_witness_count: usize = 0;
    for (result.levels) |level| {
        const origin_subtile_x = level.meta.origin_x * 5;
        const origin_subtile_y = level.meta.origin_y * 5;
        var minimum_x = level.meta.origin_x;
        var minimum_y = level.meta.origin_y;
        var maximum_x = level.meta.origin_x + level.meta.width;
        var maximum_y = level.meta.origin_y + level.meta.height;
        if (level.meta.rooms.len != 0) {
            minimum_x = level.meta.rooms[0].x;
            minimum_y = level.meta.rooms[0].y;
            maximum_x = level.meta.rooms[0].x + level.meta.rooms[0].w;
            maximum_y = level.meta.rooms[0].y + level.meta.rooms[0].h;
            for (level.meta.rooms[1..]) |room| {
                minimum_x = @min(minimum_x, room.x);
                minimum_y = @min(minimum_y, room.y);
                maximum_x = @max(maximum_x, room.x + room.w);
                maximum_y = @max(maximum_y, room.y + room.h);
            }
        }
        const anchor_x = minimum_x * 5 + @divTrunc((maximum_x - minimum_x) * 5, 2);
        const anchor_y = minimum_y * 5 + @divTrunc((maximum_y - minimum_y) * 5, 2);
        std.debug.print(
            "MS1 L {d} {d} {d} {d} {d} {d}\n",
            .{
                level.meta.level_id,
                origin_subtile_x,
                origin_subtile_y,
                anchor_x,
                anchor_y,
                level.meta.rooms.len,
            },
        );
        if (level.meta.level_id == current_level) {
            for (level.meta.rooms) |room| {
                std.debug.print(
                    "MS1 R {d} {d} {d} {d} {d}\n",
                    .{ level.meta.level_id, room.x, room.y, room.w, room.h },
                );
                room_witness_count += 1;
            }
        }
        var seam_targets: std.ArrayListUnmanaged(i32) = .empty;
        defer seam_targets.deinit(allocator);
        for (level.exits) |exit| {
            if (exit.kind == .seam) {
                if (std.mem.indexOfScalar(i32, seam_targets.items, exit.dest_level_id) != null) continue;
                try seam_targets.append(allocator, exit.dest_level_id);
                const level_index = findFullLevel(
                    &physical,
                    level.meta.level_id,
                ) orelse continue;
                const opening = findPresetFacadeOpening(
                    &physical,
                    level.meta.level_id,
                    exit.dest_level_id,
                ) orelse findPhysicalSeamOpening(
                    &physical,
                    level_index,
                    exit.dest_level_id,
                ) orelse continue;
                std.debug.print(
                    "MS1 E {d} {d} {d} {d} {d}\n",
                    .{
                        level.meta.level_id,
                        exit.dest_level_id,
                        opening.x,
                        opening.y,
                        @intFromEnum(exit.kind),
                    },
                );
                exit_count += 1;
                continue;
            }
            std.debug.print(
                "MS1 E {d} {d} {d} {d} {d}\n",
                .{
                    level.meta.level_id,
                    exit.dest_level_id,
                    origin_subtile_x + exit.x,
                    origin_subtile_y + exit.y,
                    @intFromEnum(exit.kind),
                },
            );
            exit_count += 1;
        }
        for (level.waypoints) |waypoint| {
            if (!waypoint.exact) continue;
            std.debug.print(
                "MS1 W {d} {d} {d} {d}\n",
                .{
                    level.meta.level_id,
                    origin_subtile_x + waypoint.x,
                    origin_subtile_y + waypoint.y,
                    waypoint.class_id,
                },
            );
            waypoint_count += 1;
        }
        if (permanentPortalTarget(level.meta.level_id)) |target_level_id| {
            if (findFullLevel(&physical, target_level_id) != null
                and permanent_portal_class_id != null) {
                const physical_level_index = findFullLevel(
                    &physical,
                    level.meta.level_id,
                ) orelse continue;
                const physical_level = physical.levels[physical_level_index];
                var portal_min_x: i32 = std.math.maxInt(i32);
                var portal_min_y: i32 = std.math.maxInt(i32);
                var portal_max_x: i32 = std.math.minInt(i32);
                var portal_max_y: i32 = std.math.minInt(i32);
                if (level.meta.level_id == 4) {
                    // D2MOO ACT1Q4_OpenPortalToTristram proves that D2Game
                    // creates object 60 at Stone Alpha (object 17) +4,+4.
                    for (physical_level.presets) |preset| {
                        if (preset.etype != 2 or preset.txt_file_no != 17) {
                            continue;
                        }
                        const portal_x = origin_subtile_x + preset.x + 4;
                        const portal_y = origin_subtile_y + preset.y + 4;
                        portal_min_x = @min(portal_min_x, portal_x);
                        portal_min_y = @min(portal_min_y, portal_y);
                        portal_max_x = @max(portal_max_x, portal_x);
                        portal_max_y = @max(portal_max_y, portal_y);
                    }
                } else {
                    for (physical_level.special_tiles) |tile| {
                        if (tile.main != 29 or tile.sub != 0) continue;
                        const offset_x: i32 = if (tile.orientation == 9) 2 else 0;
                        const offset_y: i32 = if (tile.orientation == 9) 0 else 2;
                        const portal_x = origin_subtile_x + tile.x + offset_x;
                        const portal_y = origin_subtile_y + tile.y + offset_y;
                        portal_min_x = @min(portal_min_x, portal_x);
                        portal_min_y = @min(portal_min_y, portal_y);
                        portal_max_x = @max(portal_max_x, portal_x);
                        portal_max_y = @max(portal_max_y, portal_y);
                    }
                }
                if (portal_min_x != std.math.maxInt(i32)) {
                    std.debug.print(
                        "MS1 P {d} {d} {d} {d} {d}\n",
                        .{
                            level.meta.level_id,
                            target_level_id,
                            portal_min_x + @divTrunc(
                                portal_max_x - portal_min_x,
                                2,
                            ),
                            portal_min_y + @divTrunc(
                                portal_max_y - portal_min_y,
                                2,
                            ),
                            permanent_portal_class_id.?,
                        },
                    );
                    portal_count += 1;
                }
            }
        }
    }
    std.debug.print(
        "MS1 Z {d} {d} {d} {d} {d} {d:.3}\n",
        .{
            result.levels.len,
            exit_count,
            waypoint_count,
            portal_count,
            room_witness_count,
            elapsedMilliseconds(started, performanceCounter(), frequency),
        },
    );
}

fn emitLabelAtlas(
    allocator: std.mem.Allocator,
    inputs: *const LoadedInputs,
    data_options: DataOptions,
    seed: u32,
    difficulty_value: u32,
    act_no: i32,
    current_level: i32,
) !void {
    var ctx = try inputs.context(allocator, data_options);
    defer ctx.deinit();
    try emitLabelAtlasWithContext(
        allocator,
        &ctx,
        seed,
        difficulty_value,
        act_no,
        current_level,
    );
}

fn writePrimaryAtlasBinary(
    allocator: std.mem.Allocator,
    inputs: *const LoadedInputs,
    data_options: DataOptions,
    seed: u32,
    difficulty_value: u32,
    act_no: i32,
    current_level: i32,
    output_path: []const u8,
) !void {
    var ctx = try inputs.context(allocator, data_options);
    defer ctx.deinit();
    try emitLabelAtlasWithContext(
        allocator,
        &ctx,
        seed,
        difficulty_value,
        act_no,
        current_level,
    );
    try writeGeometryBinaryWithContext(
        allocator,
        &ctx,
        seed,
        difficulty_value,
        act_no,
        output_path,
    );
}

fn emitAutomapSpriteStats(allocator: std.mem.Allocator) !void {
    var stats = try render.loadAutomapSpriteStats(allocator);
    defer stats.deinit();
    var maximum_width: u32 = 0;
    var maximum_height: u32 = 0;
    var total_area: u64 = 0;
    var total_opaque: u64 = 0;
    for (stats.frames, 0..) |frame, index| {
        maximum_width = @max(maximum_width, frame.width);
        maximum_height = @max(maximum_height, frame.height);
        total_area += @as(u64, frame.width) * frame.height;
        total_opaque += frame.opaque_pixels;
        std.debug.print(
            "MSS1 F {d} {d} {d} {d} {d} {d}\n",
            .{ index, frame.width, frame.height, frame.offset_x, frame.offset_y, frame.opaque_pixels },
        );
    }
    std.debug.print(
        "MSS1 Z {d} {d} {d} {d} {d}\n",
        .{ stats.frames.len, maximum_width, maximum_height, total_area, total_opaque },
    );
}

fn digestBytes(digest: *u64, bytes: []const u8) void {
    for (bytes) |byte| {
        digest.* ^= byte;
        digest.* *%= 1099511628211;
    }
}

fn writeAutomapSpritePackage(
    allocator: std.mem.Allocator,
    output_path: []const u8,
) !void {
    if (output_path.len == 0) return error.MissingOutputPath;
    var atlas = try render.loadAutomapSpriteAtlas(allocator);
    defer atlas.deinit();
    if (atlas.palettes.len != 5 * 768 or atlas.indices.len != @as(usize, atlas.atlas_width) * atlas.atlas_height) {
        return error.InvalidAutomapSpriteAtlas;
    }

    var digest: u64 = 14695981039346656037;
    digestInt(&digest, atlas.frame_count);
    digestInt(&digest, atlas.frame_width);
    digestInt(&digest, atlas.frame_height);
    digestInt(&digest, atlas.columns);
    digestInt(&digest, atlas.rows);
    digestInt(&digest, atlas.atlas_width);
    digestInt(&digest, atlas.atlas_height);
    digestBytes(&digest, atlas.palettes);
    digestBytes(&digest, atlas.indices);

    var output: std.ArrayListUnmanaged(u8) = .empty;
    defer output.deinit(allocator);
    try output.ensureTotalCapacity(
        allocator,
        48 + atlas.palettes.len + atlas.indices.len,
    );
    try output.appendSlice(allocator, "MSP1");
    try appendUnsignedLe(u16, &output, allocator, 1);
    try appendUnsignedLe(u16, &output, allocator, 1); // palette-index payload
    try appendUnsignedLe(u32, &output, allocator, atlas.frame_count);
    try appendUnsignedLe(u16, &output, allocator, atlas.frame_width);
    try appendUnsignedLe(u16, &output, allocator, atlas.frame_height);
    try appendUnsignedLe(u16, &output, allocator, atlas.columns);
    try appendUnsignedLe(u16, &output, allocator, atlas.rows);
    try output.append(allocator, 5);
    try output.appendSlice(allocator, &[_]u8{ 0, 0, 0 });
    try appendUnsignedLe(u32, &output, allocator, atlas.atlas_width);
    try appendUnsignedLe(u32, &output, allocator, atlas.atlas_height);
    try appendUnsignedLe(u32, &output, allocator, @intCast(atlas.indices.len));
    try appendUnsignedLe(u32, &output, allocator, @intCast(atlas.palettes.len));
    try appendUnsignedLe(u64, &output, allocator, digest);
    try output.appendSlice(allocator, atlas.palettes);
    try output.appendSlice(allocator, atlas.indices);
    if (output.items.len != 48 + atlas.palettes.len + atlas.indices.len) {
        return error.AutomapSpritePackageSizeMismatch;
    }
    var threaded = std.Io.Threaded.init_single_threaded;
    const io = threaded.io();
    try std.Io.Dir.cwd().writeFile(io, .{
        .sub_path = output_path,
        .data = output.items,
    });
    std.debug.print(
        "MSP1 frames={d} atlas={d}x{d} palettes=5 bytes={d} digest={x}\n",
        .{
            atlas.frame_count,
            atlas.atlas_width,
            atlas.atlas_height,
            output.items.len,
            digest,
        },
    );
}

pub fn main(init: std.process.Init.Minimal) !void {
    const allocator = std.heap.smp_allocator;
    var args = try std.process.Args.Iterator.initAllocator(init.args, allocator);
    defer args.deinit();
    _ = args.next();
    const first = args.next();
    if (first != null and (std.mem.eql(u8, first.?, "geometry") or std.mem.eql(u8, first.?, "geometry-cells") or std.mem.eql(u8, first.?, "geometry-raw") or std.mem.eql(u8, first.?, "geometry-raw-cells"))) {
        const emit_cells = std.mem.eql(u8, first.?, "geometry-cells") or std.mem.eql(u8, first.?, "geometry-raw-cells");
        const campaign_only = std.mem.eql(u8, first.?, "geometry") or std.mem.eql(u8, first.?, "geometry-cells");
        const seed = try parseU32(args.next() orelse return error.MissingSeed);
        const difficulty_value = try parseU32(args.next() orelse return error.MissingDifficulty);
        const act_text = args.next();
        const requested_act: ?i32 = if (act_text == null or std.mem.eql(u8, act_text.?, "all"))
            null
        else
            @intCast(try parseU32(act_text.?));
        const data_options = try DataOptions.parse(&args);
        var inputs = try LoadedInputs.load(allocator, data_options);
        defer inputs.deinit(allocator);
        try emitGeometryAtlas(
            allocator,
            &inputs,
            data_options,
            seed,
            difficulty_value,
            requested_act,
            emit_cells,
            campaign_only,
        );
        return;
    }
    if (first != null and std.mem.eql(u8, first.?, "legacy-smoke")) {
        const seed = try parseU32(args.next() orelse return error.MissingSeed);
        const difficulty_value = try parseU32(args.next() orelse return error.MissingDifficulty);
        const act_no: i32 = @intCast(try parseU32(args.next() orelse return error.MissingAct));
        try smokeLegacyAutomap(allocator, seed, difficulty_value, act_no);
        return;
    }
    if (first != null and std.mem.eql(u8, first.?, "atlas-binary")) {
        const seed = try parseU32(args.next() orelse return error.MissingSeed);
        const difficulty_value = try parseU32(args.next() orelse return error.MissingDifficulty);
        const act_no: i32 = @intCast(try parseU32(args.next() orelse return error.MissingAct));
        const current_level: i32 = @intCast(try parseU32(args.next() orelse return error.MissingCurrentLevel));
        const output_path = args.next() orelse return error.MissingOutputPath;
        const data_options = try DataOptions.parse(&args);
        var inputs = try LoadedInputs.load(allocator, data_options);
        defer inputs.deinit(allocator);
        try writePrimaryAtlasBinary(
            allocator,
            &inputs,
            data_options,
            seed,
            difficulty_value,
            act_no,
            current_level,
            output_path,
        );
        return;
    }
    if (first != null and std.mem.eql(u8, first.?, "geometry-binary")) {
        const seed = try parseU32(args.next() orelse return error.MissingSeed);
        const difficulty_value = try parseU32(args.next() orelse return error.MissingDifficulty);
        const act_no: i32 = @intCast(try parseU32(args.next() orelse return error.MissingAct));
        const output_path = args.next() orelse return error.MissingOutputPath;
        const data_options = try DataOptions.parse(&args);
        var inputs = try LoadedInputs.load(allocator, data_options);
        defer inputs.deinit(allocator);
        try writeGeometryBinary(
            allocator,
            &inputs,
            data_options,
            seed,
            difficulty_value,
            act_no,
            output_path,
        );
        return;
    }
    if (first != null and std.mem.eql(u8, first.?, "labels")) {
        const seed = try parseU32(args.next() orelse return error.MissingSeed);
        const difficulty_value = try parseU32(args.next() orelse return error.MissingDifficulty);
        const act_no: i32 = @intCast(try parseU32(args.next() orelse return error.MissingAct));
        const current_level: i32 = @intCast(try parseU32(args.next() orelse return error.MissingCurrentLevel));
        const data_options = try DataOptions.parse(&args);
        var inputs = try LoadedInputs.load(allocator, data_options);
        defer inputs.deinit(allocator);
        try emitLabelAtlas(
            allocator,
            &inputs,
            data_options,
            seed,
            difficulty_value,
            act_no,
            current_level,
        );
        return;
    }
    if (first != null and std.mem.eql(u8, first.?, "sprite-stats")) {
        try emitAutomapSpriteStats(allocator);
        return;
    }
    if (first != null and std.mem.eql(u8, first.?, "sprite-package")) {
        try writeAutomapSpritePackage(
            allocator,
            args.next() orelse return error.MissingOutputPath,
        );
        return;
    }
    const seed = if (first) |value| try parseU32(value) else 1337;
    const difficulty_value = if (args.next()) |value| try parseU32(value) else 0;
    const difficulty: drlg.Difficulty = switch (difficulty_value) {
        0 => .normal,
        1 => .nightmare,
        2 => .hell,
        else => return error.InvalidDifficulty,
    };

    const frequency = performanceFrequency();
    const total_started = performanceCounter();
    var ctx = try drlg.Ctx.init(allocator);
    defer ctx.deinit();
    const init_finished = performanceCounter();

    var total_levels: usize = 0;
    var total_rooms: usize = 0;
    var total_exits: usize = 0;
    var total_waypoints: usize = 0;
    var digest: u64 = 14695981039346656037;
    var act_no: i32 = 0;
    while (act_no < 5) : (act_no += 1) {
        const act_started = performanceCounter();
        var result = try drlg.generateActLabels(
            &ctx,
            allocator,
            act_no,
            seed,
            difficulty,
        );
        const act_finished = performanceCounter();
        defer result.deinit(allocator);

        var rooms: usize = 0;
        var exits: usize = 0;
        var exact_waypoints: usize = 0;
        var inexact_waypoints: usize = 0;
        for (result.levels) |level| {
            rooms += level.meta.rooms.len;
            exits += level.exits.len;
            digestInt(&digest, level.meta.level_id);
            digestInt(&digest, level.meta.origin_x);
            digestInt(&digest, level.meta.origin_y);
            for (level.meta.rooms) |room| {
                digestInt(&digest, room.x);
                digestInt(&digest, room.y);
                digestInt(&digest, room.w);
                digestInt(&digest, room.h);
                digestInt(&digest, room.map_prest_id);
            }
            for (level.exits) |exit| {
                digestInt(&digest, exit.dest_level_id);
                digestInt(&digest, exit.x);
                digestInt(&digest, exit.y);
            }
            for (level.waypoints) |waypoint| {
                if (waypoint.exact) {
                    exact_waypoints += 1;
                } else {
                    inexact_waypoints += 1;
                }
                digestInt(&digest, waypoint.x);
                digestInt(&digest, waypoint.y);
                digestInt(&digest, waypoint.class_id);
            }
        }
        total_levels += result.levels.len;
        total_rooms += rooms;
        total_exits += exits;
        total_waypoints += exact_waypoints + inexact_waypoints;
        std.debug.print(
            "act={d} elapsed_ms={d:.3} levels={d} rooms={d} exits={d} exact_waypoints={d} inexact_waypoints={d}\n",
            .{ act_no + 1, elapsedMilliseconds(act_started, act_finished, frequency), result.levels.len, rooms, exits, exact_waypoints, inexact_waypoints },
        );
    }

    std.debug.print(
        "seed={d} difficulty={d} init_ms={d:.3} total_ms={d:.3} levels={d} rooms={d} exits={d} waypoints={d} digest={x}\n",
        .{
            seed,
            difficulty_value,
            elapsedMilliseconds(total_started, init_finished, frequency),
            elapsedMilliseconds(total_started, performanceCounter(), frequency),
            total_levels,
            total_rooms,
            total_exits,
            total_waypoints,
            digest,
        },
    );
}
