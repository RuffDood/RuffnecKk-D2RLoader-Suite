//! MapSense GPS policy over the pinned generator and pathfinder.
//! Owns no live D2R pointers, hooks, input or renderer state.
const std = @import("std");
const drlg = @import("d2-drlg");
const pf = @import("d2-pathfinding");

pub const Mode = enum { walk, teleport };

/// Map-derived destinations can identify an object or room centre rather than
/// a standable subtile. One standard room is 40 subtiles across, so this bound
/// reaches the usable approach without allowing an unbounded replacement.
pub const goal_snap_radius: i32 = 48;
pub const maximum_follow_grid_cells: usize = 1_048_576;
pub const maximum_follow_grid_dimension: i32 = 65_536;
pub const maximum_follow_world_coordinate: i64 = 65_535;

pub const FollowGridLayout = struct {
    origin_x: i32,
    origin_y: i32,
    width: u32,
    height: u32,
    byte_count: u32,
};

pub const FollowGrid = struct {
    layout: FollowGridLayout,
    pass: *const pf.PassMap,
};

pub fn options(mode: Mode) pf.Options {
    return .{
        .teleport = mode == .teleport,
        .teleport_across_levels = false,
        .teleport_max_cast = 50,
        .teleport_gated_trace = true,
        .footprint = .small,
        .allow_pads = false,
        .snap_radius = 0,
        .exit_snap_radius = 0,
    };
}

pub fn route(router: *pf.Router, from: pf.Pos, to: pf.Pos, mode: Mode) !pf.Route {
    if (from.level != to.level) return error.CurrentLevelOnly;
    const opts = options(mode);
    const level = router.level(from.level) orelse return error.UnknownLevel;
    const nav = try router.navFor(level);
    const pass = try nav.passMapFor(opts.mask, opts.footprint);
    if (!pass.passable(from.x, from.y)) return error.StartBlocked;
    // Reject distant coordinates before the bounded ring search adds offsets.
    // This also keeps malformed CLI requests away from signed overflow.
    if (@as(i64, to.x) < -goal_snap_radius or @as(i64, to.y) < -goal_snap_radius or
        @as(i64, to.x) > @as(i64, level.w) - 1 + goal_snap_radius or
        @as(i64, to.y) > @as(i64, level.h) - 1 + goal_snap_radius)
        return error.GoalBlocked;
    const goal = if (mode == .walk and !pass.passable(to.x, to.y))
        try nearestWalkingApproach(router.alloc, pass, from, to)
    else
        pf.grid.nearestPassable(pass, to.x, to.y, goal_snap_radius);
    const resolved = goal orelse return error.GoalBlocked;
    return router.route(
        from,
        .{ .level = to.level, .x = resolved.x, .y = resolved.y },
        opts,
    );
}

/// An entrance marker can lie inside a wall or just outside the level's grid.
/// Choose its nearest player-width approach in the origin's terrain component,
/// rather than snapping onto a disconnected floor on the other side. A clear
/// exact goal keeps its identity and is never moved to hide an unreachable path.
fn nearestWalkingApproach(allocator: std.mem.Allocator, pass: *pf.PassMap, from: pf.Pos, to: pf.Pos) !?pf.Point {
    const components = try pass.components(allocator);
    const origin_component = components[pass.index(from.x, from.y)];
    const Search = struct {
        pass: *const pf.PassMap,
        components: []const u32,
        origin_component: u32,

        fn accepts(self: @This(), x: i32, y: i32) bool {
            return self.pass.passable(x, y) and
                self.components[self.pass.index(x, y)] == self.origin_component;
        }
    };
    return pf.world.ringSearch(
        Search{ .pass = pass, .components = components, .origin_component = origin_component },
        to.x,
        to.y,
        goal_snap_radius,
        Search.accepts,
    );
}

pub fn followGridLayout(origin_tile_x: i32, origin_tile_y: i32, width: i32, height: i32) !FollowGridLayout {
    if (width <= 0 or height <= 0 or
        width > maximum_follow_grid_dimension or height > maximum_follow_grid_dimension)
    {
        return error.InvalidFollowGridDimensions;
    }
    const origin_x = @as(i64, origin_tile_x) * pf.SUBTILES_PER_TILE;
    const origin_y = @as(i64, origin_tile_y) * pf.SUBTILES_PER_TILE;
    const maximum_x = origin_x + @as(i64, width) - 1;
    const maximum_y = origin_y + @as(i64, height) - 1;
    if (origin_x < 0 or origin_y < 0 or
        maximum_x > maximum_follow_world_coordinate or
        maximum_y > maximum_follow_world_coordinate)
    {
        return error.FollowGridOutsideWorldBounds;
    }
    const cell_count = std.math.mul(
        usize,
        @intCast(width),
        @intCast(height),
    ) catch return error.FollowGridTooLarge;
    if (cell_count > maximum_follow_grid_cells) return error.FollowGridTooLarge;
    const byte_count = std.math.divCeil(usize, cell_count, 8) catch unreachable;
    return .{
        .origin_x = @intCast(origin_x),
        .origin_y = @intCast(origin_y),
        .width = @intCast(width),
        .height = @intCast(height),
        .byte_count = @intCast(byte_count),
    };
}

/// Protocol 2 exposes only the immutable walking terrain view. Teleport needs
/// room adjacency and a distinct gated trace, so its grid trailer stays empty.
pub fn followGrid(router: *pf.Router, level: *pf.Level, mode: Mode) !?FollowGrid {
    if (mode != .walk) return null;
    const layout = try followGridLayout(level.origin_x, level.origin_y, level.w, level.h);
    const opts = options(mode);
    const pass = try (try router.navFor(level)).passMapFor(opts.mask, opts.footprint);
    if (pass.w != level.w or pass.h != level.h) return error.FollowGridSizeMismatch;
    return .{ .layout = layout, .pass = pass };
}

fn appendInteger(
    comptime T: type,
    output: *std.ArrayListUnmanaged(u8),
    allocator: std.mem.Allocator,
    value: T,
) !void {
    var bytes: [@sizeOf(T)]u8 = undefined;
    std.mem.writeInt(T, &bytes, value, .little);
    try output.appendSlice(allocator, &bytes);
}

/// MSR2's trailing grid header is always present. A null grid writes twenty
/// zero bytes, which is the only admitted Teleport representation.
pub fn appendFollowGridBinary(
    allocator: std.mem.Allocator,
    output: *std.ArrayListUnmanaged(u8),
    follow_grid: ?FollowGrid,
) !void {
    const grid = follow_grid orelse {
        try output.appendSlice(allocator, &([_]u8{0} ** 20));
        return;
    };
    try appendInteger(i32, output, allocator, grid.layout.origin_x);
    try appendInteger(i32, output, allocator, grid.layout.origin_y);
    try appendInteger(u32, output, allocator, grid.layout.width);
    try appendInteger(u32, output, allocator, grid.layout.height);
    try appendInteger(u32, output, allocator, grid.layout.byte_count);

    const cell_count = @as(usize, grid.layout.width) * @as(usize, grid.layout.height);
    var cell: usize = 0;
    while (cell < cell_count) {
        var packed_byte: u8 = 0;
        var bit: u4 = 0;
        while (bit < 8 and cell < cell_count) : ({
            bit += 1;
            cell += 1;
        }) {
            if (grid.pass.staticPassableAt(cell)) {
                packed_byte |= @as(u8, 1) << @intCast(bit);
            }
        }
        try output.append(allocator, packed_byte);
    }
}

/// Heap-stable because Router borrows World. Ctx and metadata receive the same
/// immutable TableSet; callers retain borrowed input buffers until destroy().
/// Replace this whole environment on seed, difficulty or dataset changes.
pub const Environment = struct {
    allocator: std.mem.Allocator,
    context: drlg.Ctx,
    world: pf.World,
    router: pf.Router,
    loaded_acts: u8,

    pub fn create(allocator: std.mem.Allocator, seed: u32, difficulty: drlg.Difficulty, input: drlg.TableSet) !*Environment {
        const self = try allocator.create(Environment);
        errdefer allocator.destroy(self);
        self.allocator = allocator;
        self.context = try drlg.Ctx.initFromBuffers(allocator, input);
        errdefer self.context.deinit();
        self.world = try pf.World.initFromBuffers(allocator, seed, difficulty, input);
        self.router = pf.Router.init(allocator, &self.world);
        self.loaded_acts = 0;
        return self;
    }

    pub fn destroy(self: *Environment) void {
        const allocator = self.allocator;
        self.router.deinit();
        self.world.deinit();
        self.context.deinit();
        allocator.destroy(self);
    }

    pub fn loadAct(self: *Environment, act: i32) !void {
        if (act < 0 or act > 4) return error.InvalidAct;
        const bit = @as(u8, 1) << @as(u3, @intCast(act));
        if (self.loaded_acts & bit != 0) return;
        try self.world.loadAct(&self.context, act);
        self.loaded_acts |= bit;
    }
};
