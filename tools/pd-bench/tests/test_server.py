"""The MCP server object: tool names, descriptions, and annotations."""

import asyncio

from tps25751_pd_bench import PROTOCOL_VERSION
from tps25751_pd_bench.server import RENEGOTIATE_METHODS, build_server, build_tools


def list_tools(server):
    return asyncio.run(server.list_tools())


def test_server_registers_the_seven_tools(bench):
    server = build_server(build_tools(bench.service))
    tools = {tool.name: tool for tool in list_tools(server)}
    assert sorted(tools) == ["bench_crosscheck", "bench_hello", "bench_read", "bench_renegotiate",
                             "bench_snapshot", "bench_task", "bench_write"]
    assert server.name == "tps25751-bench"
    for tool in tools.values():
        assert tool.description and len(tool.description) > 80


def test_tool_arguments_match_the_plan(bench):
    tools = {tool.name: tool for tool in list_tools(build_server(build_tools(bench.service)))}

    def arguments(name):
        schema = tools[name].inputSchema if hasattr(tools[name], "inputSchema") else tools[name].input_schema
        return set(schema.get("properties", {})), set(schema.get("required", []))

    assert arguments("bench_hello") == (set(), set())
    assert arguments("bench_snapshot") == ({"path"}, set())
    assert arguments("bench_read") == ({"addr", "decode"}, {"addr"})
    assert arguments("bench_task") == ({"code", "data_hex"}, {"code"})
    assert arguments("bench_write") == ({"addr", "hex"}, {"addr", "hex"})
    assert arguments("bench_renegotiate") == ({"method", "max_voltage_mv"}, {"method"})
    assert arguments("bench_crosscheck") == ({"capture_path", "registers_path", "role"},
                                             {"capture_path", "registers_path"})


def test_only_the_reading_tools_are_marked_read_only(bench):
    tools = {tool.name: tool for tool in list_tools(build_server(build_tools(bench.service)))}

    def read_only(name):
        annotations = tools[name].annotations
        return getattr(annotations, "read_only_hint", None) if hasattr(annotations, "read_only_hint") \
            else annotations.readOnlyHint

    assert read_only("bench_hello") is True and read_only("bench_read") is True
    assert read_only("bench_crosscheck") is True
    for name in ("bench_snapshot", "bench_task", "bench_write", "bench_renegotiate"):
        assert read_only(name) is False, name


def test_every_method_is_documented(bench):
    description = build_tools(bench.service).bench_renegotiate.__doc__
    for method in RENEGOTIATE_METHODS:
        assert method in description
    assert PROTOCOL_VERSION == 1
