"""Line classification and argument parsing of protocol version 1."""

import pytest

from tps25751_pd_bench import protocol
from tps25751_pd_bench.protocol import BenchProtocolError, Reply


def test_classify_reply_event_and_log_lines():
    line = protocol.classify("@1234 reg=0x34 len=6 data=d64106009800\r\n")
    assert (line.kind, line.id, line.text) == ("reply", "1234", "reg=0x34 len=6 data=d64106009800")

    assert protocol.classify("@7 OK").text == "OK"
    assert protocol.classify("@7 |").text == "|"
    assert protocol.classify("@7").text == ""

    event = protocol.classify("! READY proto=1")
    assert (event.kind, event.text) == ("event", "READY proto=1")

    # Anything else is log output, including text that merely contains an @.
    for raw in ("[bench] @7 SNAP -> OK (41 ms)", "[1234] WARN: something", "", "!bang",
                "@toolongid123 OK", "@7x-y OK", "user@host"):
        assert protocol.classify(raw).kind == "log", raw


def test_classify_accepts_the_placeholder_id():
    # The console answers "@? ..." when it could not read an id.
    assert protocol.classify("@? ERR SYNTAX expected @<id> <COMMAND>").id == "?"


def test_final_status():
    assert protocol.final_status("OK") == (True, None, None)
    assert protocol.final_status("ERR REFUSED task is not on the allowlist") == (
        False, "REFUSED", "task is not on the allowlist")
    assert protocol.final_status("ERR I2C") == (False, "I2C", "")
    assert protocol.final_status("ERR") == (False, "UNKNOWN", "")
    assert protocol.final_status("OKAY") is None
    assert protocol.final_status("ERRATA follow") is None
    assert protocol.final_status("reg=0x34 len=6 data=00") is None


def test_parse_tokens():
    assert protocol.parse_tokens("reg=0x34 len=6 data=d641 match=1") == {
        "reg": "0x34", "len": "6", "data": "d641", "match": "1"}
    assert protocol.parse_tokens("data=") == {"data": ""}
    assert protocol.parse_tokens("stray reg=0x03 =x") == {"reg": "0x03"}
    assert protocol.parse_tokens("") == {}


def test_format_request():
    assert protocol.format_request("17", "RD 0x34") == b"@17 RD 0x34\n"
    for bad_id in ("", "123456789", "a b", "x!"):
        with pytest.raises(ValueError):
            protocol.format_request(bad_id, "HELLO")
    with pytest.raises(ValueError):
        protocol.format_request("1", "HELLO\n@2 CMD GAID")


def test_reply_separates_tokens_from_decode_text():
    reply = Reply(request="DEC 0x1a", id="1", lines=[
        "reg=0x1a len=5 data=0d00000000",
        "| === Status Register (0x1A) ===",
        "|   Plug Present: Yes",
        "|",
    ])
    assert reply.tokens() == {"reg": "0x1a", "len": "5", "data": "0d00000000"}
    assert reply.decode_text() == ["=== Status Register (0x1A) ===", "  Plug Present: Yes", ""]
    assert reply.data_lines() == ["reg=0x1a len=5 data=0d00000000"]


def test_reply_extras_only_when_present():
    assert Reply(request="RD", id="1").extras() == {}
    reply = Reply(request="RD", id="1", log=["[bench] x"], events=["READY proto=1"])
    assert reply.extras() == {"log": ["[bench] x"], "events": ["READY proto=1"]}


def test_check_version():
    protocol.check_version({"proto": "1"})
    with pytest.raises(BenchProtocolError, match="protocol version 2 and this server speaks version 1"):
        protocol.check_version({"proto": "2"})
    with pytest.raises(BenchProtocolError, match="no proto= token"):
        protocol.check_version({"mode": "APP"})


def test_parse_address():
    assert protocol.parse_address(0x34) == 0x34
    assert protocol.parse_address("0x34") == 0x34
    assert protocol.parse_address(" 0X6a ") == 0x6A
    assert protocol.parse_address("52") == 52          # a bare number is decimal
    for bad in ("zz", "0x100", 256, -1, "", True, "3g"):
        with pytest.raises(ValueError):
            protocol.parse_address(bad)


def test_clean_hex():
    assert protocol.clean_hex("D641 0600_9800") == "d64106009800"
    assert protocol.clean_hex("0x1A000800") == "1a000800"
    assert protocol.clean_hex("") == ""
    for bad in ("abc", "zz", "12 3"):
        with pytest.raises(ValueError):
            protocol.clean_hex(bad)
