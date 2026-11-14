"""KNX regression tests through the real MCP JSON-RPC dispatcher (no hardware)."""

import json
import os
import unittest
from pathlib import Path

if __package__:
    from .mcp_host_test_support import McpHostHarness, ROOT
else:
    from mcp_host_test_support import McpHostHarness, ROOT


PUBLIC_TOOLS = {"self.knx." + name for name in
                ("get_status", "list_objects", "get_object", "read", "write", "set_object")}
IMPORT_TOOLS = {"self.knx.import_configuration", "self.knx.import_configuration_file"}


class KnxMcpServerTest(McpHostHarness, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        component = Path(os.environ.get("ESP_KNX_IP_DIR", ROOT / "managed_components/esp_knx_ip"))
        if not (component / "src/knx_dpt.cpp").is_file():
            raise unittest.SkipTest("requires esp_knx_ip sources; resolve ESP-IDF dependencies first")
        knx = ROOT / "main/knx"
        cls.driver_source = knx / "test/mcp_host_driver.cc"
        cls.extra_sources = [knx / name for name in
                             ("knx_manager.cc", "knx_mcp_tools.cc", "knx_config.cc",
                              "knx_object.cc", "knx_validation.cc")]
        cls.extra_sources += [component / "src/knx_dpt.cpp"]
        cls.extra_include_dirs = [knx, component / "include"]
        cls.extra_compile_flags = ["-DXIAOZHI_KNX_CONFIG_PARSER_ONLY",
                                   "-DCONFIG_XIAOZHI_KNX_IP=1", "-UNDEBUG",
                                   "-include", "sdkconfig.h"]
        # Reuse the KNX platform fakes, but keep the real cJSON and Base64
        # implementations and the shared dispatcher's application scheduler.
        fakes = knx / "test/fakes"
        cls.extra_stubs = {p.relative_to(fakes).as_posix(): p.read_text(encoding="utf-8")
                           for p in fakes.rglob("*.h")
                           if "mbedtls" not in p.parts and p.name not in
                           ("esp_system.h", "esp_log.h")}
        super().setUpClass()

    def request(self, modern, request_id, method="tools/list", params=None):
        if modern:
            return self.modern(request_id, method, params)
        return {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params or {}}

    def knx_call(self, modern, request_id, name, arguments=None):
        return self.request(modern, request_id, "tools/call",
                            {"name": "self.knx." + name, "arguments": arguments or {}})

    def result(self, response, modern):
        self.assertNotIn("error", response)
        result = response["result"]
        if modern:
            self.assertEqual(result["resultType"], "complete")
        else:
            self.assertNotIn("resultType", result)
        return result

    def value(self, response, modern):
        result = self.result(response, modern)
        self.assertIs(result["isError"], False)
        self.assertEqual(len(result["content"]), 1)
        self.assertEqual(result["content"][0]["type"], "text")
        return json.loads(result["content"][0]["text"])

    def tool_error(self, response, modern, message):
        if modern:
            result = self.result(response, True)
            self.assertIs(result["isError"], True)
            self.assertEqual(len(result["content"]), 1)
            self.assertEqual(result["content"][0]["type"], "text")
            error = result["content"][0]["text"]
        else:
            self.assertNotIn("result", response)
            self.assertEqual(response["error"]["code"], -32603)
            error = response["error"]["message"]
        self.assertIn(message, error)
        return error

    @staticmethod
    def configuration():
        return {"media_type": "knx_ip", "physical_address": "2.2.2",
                "media_parameters": {"multicast_address": "224.0.23.12", "udp_port": 3671,
                                     "transport_mode": "routing", "interface_identifier": "test",
                                     "nat": False}, "comObjects": []}

    def test_real_registration_discovery_and_input_schemas(self):
        initialize, discover = self.responses([
            self.legacy_init(), self.modern("discover", "server/discover")])
        self.assertEqual(initialize["result"]["protocolVersion"], "2024-11-05")
        self.assertEqual(discover["result"]["supportedVersions"], ["2026-07-28", "2024-11-05"])
        self.assertEqual(discover["result"]["capabilities"], {"tools": {}})
        for modern in (False, True):
            with self.subTest(modern=modern):
                regular, owner = self.responses([
                    self.request(modern, 1),
                    self.request(modern, 2, params={"withUserTools": True})])
                public = {t["name"]: t for t in self.result(regular, modern)["tools"]}
                all_tools = {t["name"]: t for t in self.result(owner, modern)["tools"]}
                self.assertEqual({n for n in public if n.startswith("self.knx.")}, PUBLIC_TOOLS)
                self.assertEqual({n for n in all_tools if n.startswith("self.knx.")},
                                 PUBLIC_TOOLS | IMPORT_TOOLS)
                for name in IMPORT_TOOLS:
                    self.assertEqual(all_tools[name]["annotations"]["audience"], ["user"])
                for name in ("self.knx.write", "self.knx.set_object"):
                    schema = public[name]["inputSchema"]
                    self.assertEqual(schema["properties"]["value"]["type"], "string")
                    self.assertIn("value", schema["required"])
                schema = all_tools["self.knx.import_configuration"]["inputSchema"]
                self.assertEqual(schema["properties"]["configuration"]["type"], "string")
                limits = public["self.knx.list_objects"]["inputSchema"]["properties"]
                self.assertEqual(limits["offset"]["default"], 0)
                self.assertEqual(limits["limit"],
                                 {"type": "integer", "default": 10, "minimum": 1, "maximum": 20})

    def test_catalog_pagination_preserves_knx_visibility(self):
        for modern in (False, True):
            for with_user in (False, True):
                with self.subTest(modern=modern, with_user=with_user):
                    names, cursors = [], []
                    params = {"withUserTools": with_user}
                    for page in range(12):
                        reply, = self.responses([self.request(modern, page, params=params)], mode="pages")
                        self.assertLessEqual(len(json.dumps(reply, separators=(",", ":")).encode()), 8000)
                        result = self.result(reply, modern)
                        names.extend(t["name"] for t in result["tools"])
                        cursor = result.get("nextCursor")
                        if not cursor:
                            break
                        self.assertNotIn(cursor, cursors)
                        cursors.append(cursor)
                        params["cursor"] = cursor
                    else:
                        self.fail("KNX tool discovery did not finish")
                    self.assertGreater(len(cursors), 0)
                    self.assertEqual(len(names), len(set(names)))
                    self.assertEqual({n for n in names if n.startswith("self.knx.")},
                                     PUBLIC_TOOLS | (IMPORT_TOOLS if with_user else set()))
                    self.assertEqual([n for n in names if n.startswith("page.")],
                                     [f"page.{i}" for i in range(24)])
                    if not with_user:
                        self.assertFalse(IMPORT_TOOLS.intersection(cursors))
                # A guessed hidden-tool cursor must not reveal either import tool.
                reply, = self.responses([self.request(modern, 80, params={
                    "cursor": "self.knx.import_configuration", "withUserTools": False})])
                self.assertFalse(IMPORT_TOOLS.intersection(t["name"] for t in reply["result"]["tools"]))

    def test_object_pagination_defaults_and_boundaries(self):
        for modern in (False, True):
            requests = [self.knx_call(modern, 1, "list_objects")]
            requests += [self.knx_call(modern, i+2, "list_objects", {"offset": o, "limit": 7})
                         for i, o in enumerate((0, 7, 14, 21, 25, 128))]
            replies = self.responses(requests)
            default = self.value(replies[0], modern)
            self.assertEqual((len(default["objects"]), default["next_offset"], default["total"]), (10, 10, 25))
            objects = []
            for reply, offset in zip(replies[1:], (0, 7, 14, 21, 25, 128)):
                result = self.value(reply, modern)
                objects.extend(result["objects"])
                self.assertEqual(result["has_more"], offset + len(result["objects"]) < 25)
                if result["has_more"]:
                    self.assertEqual(result["next_offset"], offset + 7)
                else:
                    self.assertNotIn("next_offset", result)
            self.assertEqual(len(objects), 25)
            self.assertEqual(len({o["id"] for o in objects}), 25)

    def test_dpt_strings_reach_real_codecs_unchanged(self):
        cases = [("switch", "true", [1]), ("switch", "off", [0]),
                 ("temperature", "20.0", [0, 7, 208]), ("mode", "4", [0, 4]),
                 ("rgb", "12,34,56", [0, 12, 34, 56]),
                 ("label", 'a"\\b', [0, 97, 34, 92, 98] + [0] * 10),
                 ("level", "50", [0, 128])]
        for modern in (False, True):
            for tool in ("set_object", "write"):
                with self.subTest(modern=modern, tool=tool):
                    addresses = {"switch": 1, "temperature": 2, "mode": 3, "rgb": 4, "label": 5, "level": 8}
                    requests = [self.knx_call(modern, i+1, tool,
                        {("object_id" if tool == "set_object" else "group_address"):
                         (name if tool == "set_object" else f"1/0/{addresses[name]}"), "value": value})
                        for i, (name, value, _) in enumerate(cases)]
                    *replies, cache, effects = self.responses(requests + [
                        self.knx_call(modern, 20, "get_object", {"object_id": "switch"}), "@snapshot"])
                    for reply in replies:
                        self.assertIs(self.value(reply, modern), True)
                    self.assertIs(self.value(cache, modern)["valid"], False)
                    self.assertEqual(effects["sent"], [
                        {"address": 2048 + addresses[name], "command": 2, "data": data}
                        for name, _, data in cases])

    def test_wrong_argument_types_have_no_side_effects(self):
        for modern in (False, True):
            requests = []
            for value in (True, 1, 20.5, None, [], {}):
                requests += [
                    self.knx_call(modern, len(requests)+1, "set_object", {"object_id": "switch", "value": value}),
                    self.knx_call(modern, len(requests)+2, "write", {"group_address": "1/0/1", "value": value}),
                    self.knx_call(modern, len(requests)+3, "read", {"group_address": value}),
                    self.knx_call(modern, len(requests)+4, "import_configuration", {"configuration": value})]
            requests += [self.knx_call(modern, 100, "set_object", {"object_id": "switch"}),
                         self.knx_call(modern, 101, "read"),
                         self.knx_call(modern, 102, "get_object", {"object_id": 1}),
                         self.knx_call(modern, 103, "import_configuration")]
            *replies, effects = self.responses(requests + ["@snapshot"])
            for reply in replies:
                self.assertNotIn("result", reply)
                self.assertEqual(reply["error"]["code"], -32602)
            self.assertEqual(effects["sent"], [])
            self.assertEqual(effects["storage_writes"], 0)
            self.assertEqual(len(effects["persisted"]["comObjects"]), 25)

    def test_modern_integer_validation_and_legacy_fraction_compatibility(self):
        invalid = [{"limit": 0}, {"limit": 21}, {"offset": -1}, {"offset": 129},
                   {"limit": "2"}, {"limit": True}, {"offset": None}]
        for modern in (False, True):
            for args in invalid + ([{"limit": 1.5}, {"offset": 0.5}, {"limit": 2147483648}] if modern else []):
                with self.subTest(modern=modern, args=args):
                    response, = self.responses([self.knx_call(modern, 1, "list_objects", args)])
                    self.assertEqual(response["error"]["code"], -32602)
        legacy, modern = self.responses([self.knx_call(False, 1, "list_objects", {"limit": 1.5}),
                                        self.knx_call(True, 2, "list_objects", {"limit": 1.0})])
        self.assertEqual(len(self.value(legacy, False)["objects"]), 1)
        self.assertEqual(len(self.value(modern, True)["objects"]), 1)

    def test_reads_are_asynchronous_and_cache_requires_a_response(self):
        for modern in (False, True):
            for sender in (False, True):
                replies = self.responses([
                    self.knx_call(modern, 1, "read", {"group_address": "1/0/6"}),
                    self.knx_call(modern, 2, "get_object", {"object_id": "status"}),
                    "@snapshot", "@receive",
                    self.knx_call(modern, 3, "get_object", {"object_id": "status"}),
                    self.knx_call(modern, 4, "get_status")], custom_sender=sender)
                read, before, effects, _, after, status = replies
                self.assertEqual(self.value(read, modern),
                                 {"requested": True, "group_address": "1/0/6", "completion": "asynchronous"})
                self.assertIs(self.value(before, modern)["valid"], False)
                self.assertEqual(effects["sent"], [{"address": 2054, "command": 0, "data": [0]}])
                obj = self.value(after, modern)
                self.assertIs(obj["valid"], True)
                self.assertIs(obj["value"], True)
                state = self.value(status, modern)
                self.assertEqual((state["state"], state["object_count"], state["valid_object_count"]),
                                 ("running", 25, 1))
                self.assertEqual(state["last_communication_ms"], 2000)

    def test_invalid_values_and_object_permissions_are_tool_failures(self):
        cases = [("get_object", {"object_id": 'missing"\\\n'}, "Unknown KNX object ID"),
                 ("read", {"group_address": "31/7/255"}, "Unknown KNX group address"),
                 ("read", {"group_address": "1/0/7"}, "not readable"),
                 ("set_object", {"object_id": "status", "value": "true"}, "not writable"),
                 ("write", {"group_address": "1/0/6", "value": "true"}, "not writable"),
                 ("write", {"group_address": "1/0/3", "value": "5"}, "DPT-20.102"),
                 ("set_object", {"object_id": "temperature", "value": "20junk"}, "DPT-9.001"),
                 ("set_object", {"object_id": "rgb", "value": "12,34"}, "DPT-232.600"),
                 ("set_object", {"object_id": "level", "value": "101"}, "DPT-5.001")]
        for modern in (False, True):
            *replies, effects = self.responses([
                self.knx_call(modern, i+1, tool, args) for i, (tool, args, _) in enumerate(cases)] + ["@snapshot"])
            for reply, (_, _, error) in zip(replies, cases):
                self.tool_error(reply, modern, error)
            self.assertEqual(effects["sent"], [])

    def test_offline_and_transport_failures_never_report_success(self):
        for modern in (False, True):
            for mode, message, attempts in (("offline", "not running", 0), ("send-error", "ESP_FAIL", 1)):
                for tool, args in (("read", {"group_address": "1/0/1"}),
                                   ("write", {"group_address": "1/0/1", "value": "true"})):
                    with self.subTest(modern=modern, mode=mode, tool=tool):
                        failure, obj, effects = self.responses([
                            self.knx_call(modern, 1, tool, args),
                            self.knx_call(modern, 2, "get_object", {"object_id": "switch"}),
                            "@snapshot"], mode=mode)
                        self.tool_error(failure, modern, message)
                        self.assertIs(self.value(obj, modern)["valid"], False)
                        self.assertEqual(len(effects["sent"]), attempts)

    def test_import_visibility_is_not_call_authorization(self):
        # Current contract: the authenticated backend must authorize owner actions.
        # Neither discovery filters nor request-supplied metadata grant/revoke access.
        for modern in (False, True):
            request = self.knx_call(modern, 2, "import_configuration",
                                    {"configuration": json.dumps(self.configuration())})
            request["params"]["withUserTools"] = False
            hidden, imported, status, effects = self.responses([
                self.request(modern, 1, params={"withUserTools": False}), request,
                self.knx_call(modern, 3, "get_status"), "@snapshot"])
            self.assertFalse(IMPORT_TOOLS.intersection(t["name"] for t in hidden["result"]["tools"]))
            self.assertEqual(self.value(imported, modern),
                             {"imported": True, "persisted": True, "object_count": 0, "apply": "immediate"})
            self.assertEqual(self.value(status, modern)["physical_address"], "2.2.2")
            self.assertEqual(self.value(status, modern)["object_count"], 0)
            self.assertEqual(effects["storage_writes"], 1)
            self.assertEqual(effects["persisted"]["comObjects"], [])

    def test_invalid_and_failed_imports_preserve_registry(self):
        for modern in (False, True):
            for mode, configuration, error in (("", "{", ""), ("", "{}", ""),
                    ("storage-error", json.dumps(self.configuration()), 'injected "storage" failure\nhost')):
                with self.subTest(modern=modern, mode=mode, configuration=configuration):
                    before, failure, after, status = self.responses(["@snapshot",
                        self.knx_call(modern, 1, "import_configuration", {"configuration": configuration}),
                        "@snapshot", self.knx_call(modern, 2, "get_status")], mode=mode)
                    message = self.tool_error(failure, modern, error)
                    self.assertTrue(message)
                    if mode:
                        self.assertEqual(message, error)
                    self.assertEqual(before["persisted"], after["persisted"])
                    self.assertEqual(after["storage_writes"], 1 if mode else 0)
                    self.assertEqual(self.value(status, modern)["object_count"], 25)

    def test_staged_file_import_calls_real_validation_and_storage(self):
        for modern in (False, True):
            for mode in ("", "bad-upload", "storage-error"):
                before, reply, after = self.responses(["@snapshot",
                    self.knx_call(modern, 1, "import_configuration_file"), "@snapshot"], mode=mode)
                self.assertEqual(after["upload_reads"], 1)
                if not mode:
                    self.assertEqual(self.value(reply, modern)["object_count"], 25)
                    self.assertEqual(after["storage_writes"], 1)
                else:
                    self.assertTrue(self.tool_error(reply, modern, ""))
                    self.assertEqual(after["persisted"], before["persisted"])
                    self.assertEqual(after["storage_writes"], int(mode == "storage-error"))

    def test_protocol_errors_and_notifications_do_not_execute_knx(self):
        bad = self.knx_call(True, 1, "set_object", {"object_id": "switch", "value": "true"})
        bad["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "unsupported"
        missing = self.knx_call(True, 2, "import_configuration",
                                {"configuration": json.dumps(self.configuration())})
        del missing["params"]["_meta"]["io.modelcontextprotocol/clientCapabilities"]
        notification = self.knx_call(False, 3, "set_object", {"object_id": "switch", "value": "true"})
        del notification["id"]
        unknown = self.knx_call(False, 4, "unknown")
        version, metadata, missing_tool, effects = self.responses(
            [bad, missing, notification, unknown, "@snapshot"], expected_count=4)
        self.assertEqual([r["error"]["code"] for r in (version, metadata, missing_tool)], [-32022, -32602, -32602])
        self.assertEqual(effects["sent"], [])
        self.assertEqual(effects["storage_writes"], 0)

    def test_deferred_mixed_eras_own_arguments_ids_and_error_semantics(self):
        for sender in (False, True):
            requests = [self.knx_call(True, 'modern"\\id', "set_object",
                                      {"object_id": "temperature", "value": "20.0"}),
                        self.knx_call(False, 2, "set_object", {"object_id": "switch", "value": "off"}),
                        self.knx_call(True, 3, "set_object", {"object_id": "mode", "value": "5"}),
                        self.knx_call(False, 4, "set_object", {"object_id": "mode", "value": "5"})]
            before, modern_ok, legacy_ok, modern_bad, legacy_bad, after = self.responses(
                requests + ["@snapshot", "@drain"], mode="deferred", custom_sender=sender)
            self.assertEqual(before["sent"], [])
            self.assertEqual([r["id"] for r in (modern_ok, legacy_ok, modern_bad, legacy_bad)],
                             ['modern"\\id', 2, 3, 4])
            self.assertIs(self.value(modern_ok, True), True)
            self.assertIs(self.value(legacy_ok, False), True)
            self.tool_error(modern_bad, True, "DPT-20.102")
            self.tool_error(legacy_bad, False, "DPT-20.102")
            self.assertEqual(after["sent"], [{"address": 2050, "command": 2, "data": [0, 7, 208]},
                                             {"address": 2049, "command": 2, "data": [0]}])


if __name__ == "__main__":
    unittest.main()
