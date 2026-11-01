const std = @import("std");
const pf = @import("d2-pathfinding");
const data = @import("d2-data");
const gps = @import("gps_routing.zig");
const t = std.testing;

const Fixture = struct {
    world: pf.World,
    router: pf.Router,
    lv: *pf.Level,

    fn create() !*Fixture {
        return createSized(40, 30);
    }

    fn createSized(w: i32, h: i32) !*Fixture {
        const self = try t.allocator.create(Fixture);
        errdefer t.allocator.destroy(self);
        self.world = pf.World.init(t.allocator, 1, .normal);
        errdefer self.world.by_id.deinit(t.allocator);
        errdefer self.world.levels.deinit(t.allocator);
        self.lv = try t.allocator.create(pf.Level);
        errdefer t.allocator.destroy(self.lv);
        const cells = try t.allocator.alloc(u16, @intCast(w * h));
        @memset(cells, 0);
        self.lv.* = pf.Level.initBare(t.allocator, w, h, cells) catch |err| {
            t.allocator.free(cells);
            return err;
        };
        errdefer self.lv.deinit();
        self.lv.id = 42;
        self.lv.rooms = try pf.rooms.build(t.allocator, &.{.{ .x = 0, .y = 0, .w = @divTrunc(w, 5), .h = @divTrunc(h, 5) }}, @divTrunc(w, 5), @divTrunc(h, 5));
        try self.world.by_id.put(t.allocator, 42, 0);
        try self.world.levels.append(t.allocator, self.lv);
        self.router = pf.Router.init(t.allocator, &self.world);
        return self;
    }

    fn destroy(self: *Fixture) void {
        self.router.deinit();
        self.world.deinit();
        t.allocator.destroy(self);
    }

    fn query(self: *Fixture, from: pf.Point, to: pf.Point, mode: gps.Mode) !pf.Route {
        return gps.route(&self.router, .{ .level = 42, .x = from.x, .y = from.y }, .{ .level = 42, .x = to.x, .y = to.y }, mode);
    }
};

fn crossClear(lv: *const pf.Level, p: pf.Move) bool {
    for ([_][2]i32{ .{ 0, 0 }, .{ -1, 0 }, .{ 1, 0 }, .{ 0, -1 }, .{ 0, 1 } }) |d| {
        if ((lv.liveAt(p.x + d[0], p.y + d[1]) & 0x1c09) != 0) return false;
    }
    return true;
}

fn readLittle(comptime T: type, bytes: []const u8, offset: usize) T {
    return std.mem.readInt(T, bytes[offset..][0..@sizeOf(T)], .little);
}

test "MSR2 walking follow grid uses the qualified small-player pass map" {
    const f = try Fixture.create();
    defer f.destroy();
    const follow = (try gps.followGrid(&f.router, f.lv, .walk)).?;
    try t.expectEqual(@as(i32, 0), follow.layout.origin_x);
    try t.expectEqual(@as(i32, 0), follow.layout.origin_y);
    try t.expectEqual(@as(u32, 40), follow.layout.width);
    try t.expectEqual(@as(u32, 30), follow.layout.height);
    try t.expectEqual(@as(u32, 150), follow.layout.byte_count);

    var bytes: std.ArrayListUnmanaged(u8) = .empty;
    defer bytes.deinit(t.allocator);
    try gps.appendFollowGridBinary(t.allocator, &bytes, follow);
    try t.expectEqual(@as(usize, 170), bytes.items.len);
    try t.expectEqual(@as(i32, 0), readLittle(i32, bytes.items, 0));
    try t.expectEqual(@as(i32, 0), readLittle(i32, bytes.items, 4));
    try t.expectEqual(@as(u32, 40), readLittle(u32, bytes.items, 8));
    try t.expectEqual(@as(u32, 30), readLittle(u32, bytes.items, 12));
    try t.expectEqual(@as(u32, 150), readLittle(u32, bytes.items, 16));
    const blocked_cell: usize = 0;
    const open_cell: usize = 5 * 40 + 5;
    try t.expect(bytes.items[20 + blocked_cell / 8] &
        (@as(u8, 1) << @intCast(blocked_cell % 8)) == 0);
    try t.expect(bytes.items[20 + open_cell / 8] &
        (@as(u8, 1) << @intCast(open_cell % 8)) != 0);
}

test "MSR2 Teleport follow grid is the twenty-byte zero sentinel" {
    const f = try Fixture.create();
    defer f.destroy();
    try t.expect((try gps.followGrid(&f.router, f.lv, .teleport)) == null);
    var bytes: std.ArrayListUnmanaged(u8) = .empty;
    defer bytes.deinit(t.allocator);
    try gps.appendFollowGridBinary(t.allocator, &bytes, null);
    try t.expectEqual(@as(usize, 20), bytes.items.len);
    for (bytes.items) |byte| try t.expectEqual(@as(u8, 0), byte);
}

test "MSR2 follow grid bounds dimensions cells and world coordinates" {
    _ = try gps.followGridLayout(0, 0, 65_536, 1);
    try t.expectError(
        error.InvalidFollowGridDimensions,
        gps.followGridLayout(0, 0, 65_537, 1),
    );
    try t.expectError(
        error.FollowGridTooLarge,
        gps.followGridLayout(0, 0, 1_025, 1_024),
    );
    try t.expectError(
        error.FollowGridOutsideWorldBounds,
        gps.followGridLayout(-1, 0, 40, 30),
    );
    try t.expectError(
        error.FollowGridOutsideWorldBounds,
        gps.followGridLayout(13_100, 0, 40, 30),
    );
}

test "GPS rejects a point-only corridor and refreshes clearance when it opens" {
    const f = try Fixture.create();
    defer f.destroy();
    _ = f.lv.editTerrain(.{ .x0 = 20, .y0 = 0, .x1 = 20, .y1 = 29 }, .{ .add = 1 });
    _ = f.lv.editTerrain(.{ .x0 = 20, .y0 = 10, .x1 = 20, .y1 = 10 }, .{ .remove = 1 });
    const a: pf.Pos = .{ .level = 42, .x = 5, .y = 10 };
    const b: pf.Pos = .{ .level = 42, .x = 35, .y = 10 };
    var point = try f.router.route(a, b, .{ .teleport = false, .snap_radius = 0 });
    defer point.deinit();
    try t.expectError(error.Unreachable, gps.route(&f.router, a, b, .walk));
    _ = f.lv.editTerrain(.{ .x0 = 20, .y0 = 9, .x1 = 20, .y1 = 11 }, .{ .remove = 1 });
    var opened = try gps.route(&f.router, a, b, .walk);
    defer opened.deinit();
    try t.expect(opened.moveCount() > 1);
    for (opened.legs[0].moves) |m| try t.expect(crossClear(f.lv, m));
}

test "GPS gated Teleport traces the segment and reroutes after a door opens" {
    const f = try Fixture.create();
    defer f.destroy();
    const a: pf.Point = .{ .x = 5, .y = 7 };
    const b: pf.Point = .{ .x = 35, .y = 7 };
    _ = f.lv.editTerrain(.{ .x0 = 20, .y0 = 0, .x1 = 20, .y1 = 29 }, .{ .add = 0x805 });
    var ordinary = try f.query(a, b, .teleport);
    defer ordinary.deinit();
    try t.expectEqual(@as(usize, 2), ordinary.moveCount());
    f.lv.teleport = .gated;
    try t.expectError(error.Unreachable, f.query(a, b, .teleport));
    _ = f.lv.editTerrain(.{ .x0 = 20, .y0 = 14, .x1 = 20, .y1 = 18 }, .{ .remove = 0x805 });
    var detour = try f.query(a, b, .teleport);
    defer detour.deinit();
    try t.expect(detour.moveCount() > 2);
    for (detour.legs[0].moves) |m| try t.expect(crossClear(f.lv, m));
}

test "GPS keeps forbidden Teleport as explicit walking and rejects cross-level requests" {
    const f = try Fixture.create();
    defer f.destroy();
    f.lv.teleport = .forbidden;
    var result = try f.query(.{ .x = 5, .y = 5 }, .{ .x = 35, .y = 25 }, .teleport);
    defer result.deinit();
    for (result.legs[0].moves) |m| try t.expectEqual(pf.Move.Kind.walk, m.kind);
    try t.expectError(error.CurrentLevelOnly, gps.route(&f.router, .{ .level = 42, .x = 5, .y = 5 }, .{ .level = 43, .x = 5, .y = 5 }, .walk));
    try t.expectError(error.StartBlocked, f.query(.{ .x = 0, .y = 0 }, .{ .x = 5, .y = 5 }, .walk));
}

test "GPS snaps a blocked destination but keeps the player origin exact" {
    const f = try Fixture.create();
    defer f.destroy();
    const nav = try f.router.navFor(f.lv);
    const point_reps = try nav.tileReps(0x1c09);
    const player_reps = try nav.tileRepsFor(0x1c09, .small);
    try t.expect(point_reps.ptr != player_reps.ptr);
    const a: pf.Point = .{ .x = 5, .y = 5 };
    const b: pf.Point = .{ .x = 35, .y = 25 };
    var first = try f.query(a, b, .teleport);
    defer first.deinit();
    try f.lv.addUnit(17, b, .{ .stamp = .{ .width = .small }, .flag = 0x1000 });
    var snapped = try f.query(a, b, .teleport);
    defer snapped.deinit();
    try t.expectEqual(a, pf.Point{
        .x = snapped.legs[0].moves[0].x,
        .y = snapped.legs[0].moves[0].y,
    });
    const snapped_goal = snapped.legs[0].moves[snapped.legs[0].moves.len - 1];
    try t.expect(snapped_goal.x != b.x or snapped_goal.y != b.y);
    try t.expect(@abs(snapped_goal.x - b.x) <= gps.goal_snap_radius);
    try t.expect(@abs(snapped_goal.y - b.y) <= gps.goal_snap_radius);
    f.lv.removeUnit(17);
    var freed = try f.query(a, b, .teleport);
    defer freed.deinit();
    for (freed.legs[0].moves) |m| try t.expect(crossClear(f.lv, m));
}

test "coordinate trace preserves directional raster and size-two endpoint rules" {
    const f = try Fixture.create();
    defer f.destroy();
    _ = f.lv.editTerrain(.{ .x0 = 2, .y0 = 1, .x1 = 2, .y1 = 1 }, .{ .add = 0x804 });
    try t.expect(f.lv.trace(.{ .x = 1, .y = 1 }, .{ .x = 5, .y = 3 }, 0x804).blocked);
    try t.expect(!f.lv.trace(.{ .x = 5, .y = 3 }, .{ .x = 1, .y = 1 }, 0x804).blocked);
    _ = f.lv.editTerrain(.{ .x0 = 0, .y0 = 0, .x1 = 39, .y1 = 29 }, .{ .remove = 0xffff });
    _ = f.lv.editTerrain(.{ .x0 = 6, .y0 = 5, .x1 = 6, .y1 = 5 }, .{ .add = 0x804 });
    // A barrier inside the skipped source extent is not a trace hit.
    try t.expect(pf.cast.unitsCanReach(f.lv, 0x804, .{ .x = 5, .y = 5 }, 2, .{ .x = 15, .y = 5 }, 2));
    _ = f.lv.editTerrain(.{ .x0 = 10, .y0 = 5, .x1 = 10, .y1 = 5 }, .{ .add = 0x804 });
    try t.expect(!pf.cast.unitsCanReach(f.lv, 0x804, .{ .x = 5, .y = 5 }, 2, .{ .x = 15, .y = 5 }, 2));
    try t.expect(pf.cast.unitsCanReach(f.lv, 0x804, .{ .x = 5, .y = 5 }, 2, .{ .x = 7, .y = 5 }, 2));
}

test "GPS replaces an occupied intermediate landing from a warm tile cache" {
    const f = try Fixture.createSized(120, 40);
    defer f.destroy();
    const a: pf.Point = .{ .x = 5, .y = 5 };
    const b: pf.Point = .{ .x = 115, .y = 35 };
    var first = try f.query(a, b, .teleport);
    defer first.deinit();
    try t.expect(first.moveCount() >= 4);
    const chosen = first.legs[0].moves[1];
    try f.lv.addUnit(18, .{ .x = chosen.x, .y = chosen.y }, .{ .stamp = .{ .width = .small }, .flag = 0x1000 });
    var second = try f.query(a, b, .teleport);
    defer second.deinit();
    for (second.legs[0].moves) |m| {
        try t.expect(crossClear(f.lv, m));
        try t.expect(m.x != chosen.x or m.y != chosen.y);
    }
}

test "GPS refuses an unqualified pad instead of inventing a walking connection" {
    const f = try Fixture.create();
    defer f.destroy();
    _ = f.lv.editTerrain(.{ .x0 = 20, .y0 = 0, .x1 = 20, .y1 = 29 }, .{ .add = 1 });
    f.lv.pads = try t.allocator.dupe(pf.level.Pad, &.{.{ .at = .{ .x = 5, .y = 5 }, .to = .{ .x = 35, .y = 25 }, .class_id = 733 }});
    var legacy = try f.router.route(.{ .level = 42, .x = 5, .y = 5 }, .{ .level = 42, .x = 35, .y = 25 }, .{ .teleport = false });
    defer legacy.deinit();
    var has_pad = false;
    for (legacy.legs[0].moves) |m| has_pad = has_pad or m.kind == .pad;
    try t.expect(has_pad);
    try t.expectError(error.Unreachable, f.query(.{ .x = 5, .y = 5 }, .{ .x = 35, .y = 25 }, .walk));
}

test "GPS metadata uses active Levels and Objects including custom IDs" {
    var world = try pf.World.initFromBuffers(t.allocator, 1, .normal, .{
        .levels = "Id\tTeleport\r\n0\t0\r\n2\t0\r\n3\t2\r\n733\t1\r\n",
        .objects = "Name\tId\tOperateFn\r\ndoor\t733\t29\r\nCustom Pad\t734\t27\r\n",
    });
    defer world.deinit();
    try t.expectEqual(pf.TeleportRule.forbidden, world.teleport_rule[2]);
    try t.expectEqual(pf.TeleportRule.gated, world.teleport_rule[3]);
    try t.expectEqual(pf.TeleportRule.forbidden, world.teleport_rule[732]);
    try t.expectEqual(pf.TeleportRule.allowed, world.teleport_rule[733]);
    try t.expectEqual(@as(i16, 29), world.door_fn[733]);
    try t.expect(world.pad_class[734]);
    try t.expect(!world.pad_class[733]);
}

test "GPS refuses malformed or ambiguous metadata instead of embedded fallback" {
    try t.expectError(error.DuplicateNavigationId, pf.World.initFromBuffers(t.allocator, 1, .normal, .{ .levels = "Id\tTeleport\n2\t1\n2\t0\n" }));
    try t.expectError(error.InvalidNavigationTable, pf.World.initFromBuffers(t.allocator, 1, .normal, .{ .levels = "Id\tTeleport\n2\tmaybe\n" }));
    try t.expectError(error.InvalidNavigationTable, pf.World.initFromBuffers(t.allocator, 1, .normal, .{ .levels = "Id\tTeleport\n2\n" }));
    try t.expectError(error.NavigationTableTooSparse, pf.World.initFromBuffers(t.allocator, 1, .normal, .{ .levels = "Id\tTeleport\n1000001\t1\n" }));
    try t.expectError(error.InvalidNavigationTable, pf.World.initFromBuffers(t.allocator, 1, .normal, .{ .objects = "Name\tId\tOperateFn\ndoor\t733\tbroken\n" }));
    try t.expectError(error.InvalidNavigationTable, pf.World.initFromBuffers(t.allocator, 1, .normal, .{ .objects = "Name\t*ID\tOperateFn\ndoor\t733\n" }));
    try t.expectError(error.InvalidNavigationTable, pf.World.initFromBuffers(t.allocator, 1, .normal, .{ .objects = "Name\t*ID\tOperateFn\tIsDoor\ndoor\t733\t8\n" }));
}

test "GPS Objects honors D2R ID and IsDoor plus legacy Expansion replacement" {
    var d2r = try pf.World.initFromBuffers(t.allocator, 1, .normal, .{
        .objects = "Class\tName\t*ID\tIsDoor\tOperateFn\nGate\tLocalizedKey\t733\t1\t8\n",
    });
    defer d2r.deinit();
    try t.expectEqual(@as(i16, 8), d2r.door_fn[733]);
    var legacy = try pf.World.initFromBuffers(t.allocator, 1, .normal, .{
        .objects = "Name\tId\tOperateFn\nPad\t408\t27\ndoor\t409\t8\nExpansion\t\t\nDummy\t408\t0\nDummy\t409\t0\n",
    });
    defer legacy.deinit();
    try t.expect(!legacy.pad_class[408]);
    try t.expectEqual(@as(i16, -1), legacy.door_fn[409]);
}

// Change one synthetic buffer field while preserving every other byte.
fn overrideTeleport(id: []const u8, value: []const u8) ![]u8 {
    const source = data.file("Levels");
    var lines = std.mem.splitScalar(u8, source, '\n');
    var headers = std.mem.splitScalar(u8, lines.next().?, '\t');
    var idc: usize = 0;
    var telec: usize = 0;
    var column: usize = 0;
    while (headers.next()) |h| : (column += 1) {
        if (std.mem.eql(u8, h, "Id")) idc = column;
        if (std.mem.eql(u8, h, "Teleport")) telec = column;
    }
    while (lines.next()) |line| {
        var fields = std.mem.splitScalar(u8, line, '\t');
        var matches = false;
        var target: ?[]const u8 = null;
        column = 0;
        while (fields.next()) |v| : (column += 1) {
            if (column == idc) matches = std.mem.eql(u8, v, id);
            if (column == telec) target = v;
        }
        if (!matches) continue;
        const field = target orelse return error.MissingFixtureField;
        const offset = @intFromPtr(field.ptr) - @intFromPtr(source.ptr);
        return std.mem.concat(t.allocator, u8, &.{ source[0..offset], value, source[offset + field.len ..] });
    }
    return error.MissingFixtureLevel;
}

test "GPS context and loaded terrain share the effective table buffers" {
    const table = try overrideTeleport("2", "0");
    defer t.allocator.free(table);
    const environment = try gps.Environment.create(t.allocator, 1337, .hell, .{ .levels = table });
    defer environment.destroy();
    try environment.loadAct(0);
    const count = environment.world.levels.items.len;
    try environment.loadAct(0);
    try t.expectEqual(count, environment.world.levels.items.len);
    const level = environment.world.level(2) orelse return error.MissingFixtureLevel;
    try t.expectEqual(pf.TeleportRule.forbidden, level.teleport);
    try t.expect(level.cells.len > 0);
}

test "GPS walking approaches a shortcut from the reachable side of its entrance" {
    const f = try Fixture.createSized(120, 40);
    defer f.destroy();
    // The entrance marker is in a wall. The closest clear cell is on the
    // disconnected side; the player's usable approach is slightly farther.
    _ = f.lv.editTerrain(.{ .x0 = 60, .y0 = 0, .x1 = 63, .y1 = 39 }, .{ .add = 1 });
    var route = try f.query(.{ .x = 10, .y = 20 }, .{ .x = 63, .y = 20 }, .walk);
    defer route.deinit();
    const moves = route.legs[0].moves;
    try t.expectEqual(@as(i32, 10), moves[0].x);
    try t.expect(moves[moves.len - 1].x < 60);
    for (moves) |move| try t.expect(crossClear(f.lv, move));
}

test "GPS walking keeps a clear but disconnected exact destination unreachable" {
    const f = try Fixture.createSized(120, 40);
    defer f.destroy();
    _ = f.lv.editTerrain(.{ .x0 = 60, .y0 = 0, .x1 = 63, .y1 = 39 }, .{ .add = 1 });
    try t.expectError(error.Unreachable, f.query(.{ .x = 10, .y = 20 }, .{ .x = 66, .y = 20 }, .walk));
}

test "GPS walking bounds an entrance approach outside the current level" {
    const f = try Fixture.createSized(120, 40);
    defer f.destroy();
    var route = try f.query(.{ .x = 10, .y = 20 }, .{ .x = -1, .y = 20 }, .walk);
    defer route.deinit();
    const last = route.legs[0].moves[route.legs[0].moves.len - 1];
    try t.expect(last.x >= 0 and last.x < 10);
    try t.expect(crossClear(f.lv, last));
    try t.expectError(error.GoalBlocked, f.query(.{ .x = 10, .y = 20 }, .{ .x = -49, .y = 20 }, .walk));
    try t.expectError(error.GoalBlocked, f.query(.{ .x = 10, .y = 20 }, .{ .x = std.math.maxInt(i32), .y = std.math.maxInt(i32) }, .walk));
    try t.expectError(error.GoalBlocked, f.query(.{ .x = 10, .y = 20 }, .{ .x = std.math.minInt(i32), .y = std.math.minInt(i32) }, .teleport));
}

fn overrideLevelField(source: []const u8, id: []const u8, name: []const u8, value: []const u8) ![]u8 {
    var lines = std.mem.splitScalar(u8, source, '\n');
    var headers = std.mem.splitScalar(u8, lines.next().?, '\t');
    var id_column: ?usize = null;
    var field_column: ?usize = null;
    var column: usize = 0;
    while (headers.next()) |header| : (column += 1) {
        if (std.mem.eql(u8, header, "Id")) id_column = column;
        if (std.mem.eql(u8, header, name)) field_column = column;
    }
    const key_column = id_column orelse return error.MissingFixtureColumn;
    const target_column = field_column orelse return error.MissingFixtureColumn;
    while (lines.next()) |line| {
        var fields = std.mem.splitScalar(u8, line, '\t');
        var matches = false;
        var target: ?[]const u8 = null;
        column = 0;
        while (fields.next()) |field| : (column += 1) {
            if (column == key_column) matches = std.mem.eql(u8, field, id);
            if (column == target_column) target = field;
        }
        if (!matches) continue;
        const field = target orelse return error.MissingFixtureField;
        const offset = @intFromPtr(field.ptr) - @intFromPtr(source.ptr);
        return std.mem.concat(t.allocator, u8, &.{ source[0..offset], value, source[offset + field.len ..] });
    }
    return error.MissingFixtureLevel;
}

test "GPS generates a QoL Stony Field shortcut with disconnected skipped floors" {
    var table = try t.allocator.dupe(u8, data.file("Levels"));
    defer t.allocator.free(table);
    // Exact a1_stony_shortcut table changes recorded from QoL Map & Waypoint
    // 9.1: entrances point directly to Dark Wood and the old cave is detached.
    const changes = [_][3][]const u8{
        .{ "4", "Vis3", "5" },  .{ "4", "Vis4", "5" },
        .{ "4", "Vis5", "5" },  .{ "4", "Vis6", "5" },
        .{ "5", "Vis3", "4" },  .{ "5", "Vis4", "4" },
        .{ "5", "Vis5", "4" },  .{ "5", "Vis6", "4" },
        .{ "10", "Vis0", "0" }, .{ "10", "Vis1", "0" },
        .{ "14", "Vis1", "0" },
    };
    for (changes) |change| {
        const updated = try overrideLevelField(table, change[0], change[1], change[2]);
        t.allocator.free(table);
        table = updated;
    }
    const environment = try gps.Environment.create(t.allocator, 1337, .hell, .{ .levels = table });
    defer environment.destroy();
    try environment.loadAct(0);
    const level = environment.world.level(4) orelse return error.MissingFixtureLevel;
    var shortcut_count: usize = 0;
    var routed = false;
    const pass = try (try environment.router.navFor(level)).passMapFor(gps.options(.walk).mask, .small);
    const components = try pass.components(t.allocator);
    for (level.exits) |exit| {
        if (exit.to_level == 5) shortcut_count += 1;
        try t.expect(exit.to_level != 10);
        if (exit.to_level != 5 or routed) continue;
        const goal = pf.grid.nearestPassable(pass, exit.x, exit.y, gps.goal_snap_radius) orelse continue;
        const component = components[pass.index(goal.x, goal.y)];
        for (components, 0..) |candidate_component, index| {
            if (candidate_component != component) continue;
            const x: i32 = @intCast(index % @as(usize, @intCast(level.w)));
            const y: i32 = @intCast(index / @as(usize, @intCast(level.w)));
            if (@abs(x - goal.x) + @abs(y - goal.y) < 100) continue;
            var route = try gps.route(&environment.router, .{ .level = 4, .x = x, .y = y }, .{ .level = 4, .x = exit.x, .y = exit.y }, .walk);
            defer route.deinit();
            try t.expect(route.moveCount() >= 2);
            for (route.legs[0].moves) |move| try t.expect(crossClear(level, move));
            const follow = (try gps.followGrid(&environment.router, level, .walk)).?;
            try t.expect(follow.layout.width == @as(u32, @intCast(level.w)));
            routed = true;
            break;
        }
    }
    try t.expect(shortcut_count > 0);
    try t.expect(routed);
}
