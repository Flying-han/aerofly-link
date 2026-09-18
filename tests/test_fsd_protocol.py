# -*- coding: utf-8 -*-
"""
FSD 协议层纯函数单元测试
=========================
覆盖 core/fsd_protocol.py 的位打包、坐标解析与工具函数。
无 Qt / 网络依赖，可独立快速运行。
"""
import math

import pytest

from core.fsd_protocol import (
    pack_pbh,
    unpack_pbh,
    xpdr_mode_to_fsd_letter,
    parse_coord,
    decimal_to_packed_coord,
    distance_nm,
    DEFAULT_RATING,
)


class TestPBH:
    """Pitch/Bank/Heading 32 位打包与解包（与 Swift packPBH 一致）"""

    @pytest.mark.parametrize("heading", [0, 45, 90, 135, 180, 225, 270, 315, 359])
    def test_heading_roundtrip(self, heading):
        pbh = pack_pbh(0.0, 0.0, heading, False)
        _, _, decoded, on_ground = unpack_pbh(pbh)
        err = abs((decoded - heading + 180) % 360 - 180)
        assert err < 1.0
        assert on_ground is False

    def test_heading_90_expected_pbh(self):
        # 与 Swift 基准值对齐：朝向 90° → PBH = 1024
        assert pack_pbh(0.0, 0.0, 90.0, False) == 1024

    def test_on_ground_flag(self):
        pbh = pack_pbh(0.0, 0.0, 0.0, True)
        assert (pbh >> 1) & 1 == 1

    def test_pitch_bank_roundtrip(self):
        for pitch, bank in [(5.0, -10.0), (-3.0, 25.0), (0.0, 0.0)]:
            pbh = pack_pbh(pitch, bank, 180.0, False)
            p, b, h, _ = unpack_pbh(pbh)
            assert abs(p - pitch) <= 1.0
            assert abs(b - bank) <= 1.0
            assert abs(h - 180.0) <= 1.0


class TestXpdrLetters:
    """应答机模式 → FSD serializer 字母映射"""

    @pytest.mark.parametrize("mode,letter", [
        ("STBY", "S"),
        ("stby", "S"),
        ("ALT", "N"),
        ("ModeC", "N"),
        ("IDENT", "Y"),
        ("", "N"),       # 未知值按正常报告处理
        ("XXXX", "N"),
    ])
    def test_mapping(self, mode, letter):
        assert xpdr_mode_to_fsd_letter(mode) == letter


class TestCoords:
    """坐标解析（packed / decimal 双格式兼容）"""

    def test_parse_decimal(self):
        assert parse_coord("31.1434") == pytest.approx(31.1434)
        assert parse_coord("-0.4614", is_lon=True) == pytest.approx(-0.4614)

    def test_parse_packed_latitude(self):
        # 3938.17 → 39°38.17' = 39.636167
        assert parse_coord("3938.17") == pytest.approx(39 + 38.17 / 60, abs=1e-4)

    def test_parse_packed_longitude(self):
        # 11623.29 → 116°23.29'（经度 > 180 走 packed 分支）
        assert parse_coord("11623.29", is_lon=True) == pytest.approx(116 + 23.29 / 60, abs=1e-4)

    def test_decimal_to_packed_lat(self):
        packed = decimal_to_packed_coord(39.6362)
        assert packed == "3938.172"
        assert parse_coord(packed) == pytest.approx(39.6362, abs=1e-3)

    def test_decimal_to_packed_lon_east(self):
        packed = decimal_to_packed_coord(116.5882, is_lon=True)
        assert packed == "11635.292"

    def test_decimal_to_packed_lon_west_no_sign(self):
        # 西经转为 0-360 表示，无负号
        packed = decimal_to_packed_coord(-0.4543, is_lon=True)
        assert not packed.startswith("-")
        assert packed == "35932.742"

    def test_negative_lat_keeps_sign(self):
        packed = decimal_to_packed_coord(-33.9461)
        assert packed.startswith("-")


class TestDistance:
    """大圆距离（海里）"""

    def test_known_pair(self):
        # 上海浦东 → 北京首都 约 594 海里
        d = distance_nm(31.1434, 121.8082, 40.0801, 116.5846)
        assert 560 < d < 630

    def test_zero_distance(self):
        assert distance_nm(31.0, 121.0, 31.0, 121.0) == pytest.approx(0.0, abs=1e-9)

    def test_symmetry(self):
        d1 = distance_nm(10.0, 20.0, 30.0, 40.0)
        d2 = distance_nm(30.0, 40.0, 10.0, 20.0)
        assert d1 == pytest.approx(d2)


def test_default_rating_is_student_pilot():
    # 回归：默认等级必须是 S1(2)。OBS(1) 无法被部分服务器纳入广播列表。
    assert DEFAULT_RATING == 2
