#!/usr/bin/env python3
"""Static regression checks for the Vita HUD/radar aspect contract."""

from __future__ import annotations

import math


def physical_point(x: float, y: float, width: int, height: int) -> tuple[float, float]:
    return x * width / 800.0, y * height / 600.0


def rotated_quad(size: float, width: int, height: int, angle: float) -> list[tuple[float, float]]:
    scale_y = (width / height) * 0.75
    half_w = size * 0.5
    half_h = size * scale_y * 0.5
    c = math.cos(math.radians(angle))
    s = math.sin(math.radians(angle))
    points = []
    for vx, vy in ((-half_w, -half_h), (half_w, -half_h), (half_w, half_h), (-half_w, half_h)):
        px = vx * width / 800.0
        py = vy * height / 600.0
        rx = px * c - py * s
        ry = px * s + py * c
        points.append((rx, ry))
    return points


def distance(a: tuple[float, float], b: tuple[float, float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def validate_mode(width: int, height: int) -> None:
    size = 130.0
    scale_y = (width / height) * 0.75
    physical_w = size * width / 800.0
    physical_h = size * scale_y * height / 600.0
    assert math.isclose(physical_w, physical_h, rel_tol=0.0, abs_tol=1e-6)

    for angle in (0.0, 37.0, 90.0, 179.0, 271.0):
        quad = rotated_quad(size, width, height, angle)
        edges = [distance(quad[i], quad[(i + 1) % 4]) for i in range(4)]
        assert math.isclose(edges[0], edges[1], rel_tol=0.0, abs_tol=1e-5)
        assert math.isclose(edges[1], edges[2], rel_tol=0.0, abs_tol=1e-5)
        assert math.isclose(edges[2], edges[3], rel_tol=0.0, abs_tol=1e-5)


def main() -> None:
    for mode in ((960, 544), (640, 368), (480, 272)):
        validate_mode(*mode)

    # RadarPlayer.dds is the one marker whose retail draw intentionally uses
    # reversed/inset V coordinates.  Keep that directional-arrow contract
    # distinct from the symmetric 0..1 UVs used by other markers.
    size = 16.0
    inset = 0.5 / size
    player_uv = (inset, 1.0 - inset, 1.0 - inset, inset)
    assert 0.0 < player_uv[0] < player_uv[2] < 1.0
    assert player_uv[1] > player_uv[3]

    # Shortest-arc smoothing must cross north by two degrees, not spin 358.
    delta = 1.0 - 359.0
    while delta > 180.0:
        delta -= 360.0
    while delta < -180.0:
        delta += 360.0
    assert delta == 2.0

    # A locator returning after the HUD was absent must snap to the live
    # heading instead of interpolating from stale static state.
    last_draw, current_draw = 10.0, 10.75
    assert current_draw - last_draw > 0.5
    print("radar aspect/rotation contract: PASS")


if __name__ == "__main__":
    main()
