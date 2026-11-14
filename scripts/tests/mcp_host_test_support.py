"""Shared host harness compiling the real MCP dispatcher and JSON implementation."""

import json
import os
import shlex
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class McpHostHarness:
    """Platform-only fakes; subclasses can link production tool implementations."""

    extra_sources = ()
    extra_include_dirs = ()
    extra_compile_flags = ()
    extra_stubs = {}
    driver_source = None

    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not compiler or shutil.which(compiler[0]) is None:
            raise unittest.SkipTest("requires a host C++23 compiler")
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        build = Path(cls.directory.name)
        # Only platform dependencies are replaced. Compile mcp_server.cc and the
        # press-to-talk tool unchanged, using the real cJSON parser/serializer.
        stubs = {
            "platform.h": r'''
                #pragma once
                #define _APPLICATION_H_
                #define SETTINGS_H
                #define BOARD_NAME "host-test"
                #include <cstdlib>
                #include <functional>
                #include <iostream>
                #include <string>
                #include <utility>
                #include <vector>
                class Application {
                public:
                    std::vector<std::function<void()>> pending;
                    static Application& GetInstance() {
                        static Application app;
                        return app;
                    }
                    void Schedule(std::function<void()> fn) {
                        pending.push_back(std::move(fn));
                    }
                    void Drain() {
                        while (!pending.empty()) {
                            auto tasks = std::move(pending);
                            pending.clear();
                            for (auto& task : tasks) task();
                        }
                    }
                    void SendMcpMessage(const std::string& payload) {
                        std::cout << "default\t" << payload << '\n';
                    }
                    void Reboot() { std::abort(); }
                    bool UpgradeFirmware(const std::string&) { std::abort(); }
                };
                class Settings {
                public:
                    Settings(const std::string&, bool = false) {}
                    int GetInt(const std::string&, int fallback) { return fallback; }
                    void SetInt(const std::string&, int) {}
                    void SetString(const std::string&, const std::string&) {}
                };
                inline void vTaskDelay(int) { std::abort(); }
                #define pdMS_TO_TICKS(ms) (ms)
            ''',
            "esp_system.h": r'''
                #pragma once
                #include <cstdlib>
                [[noreturn]] inline void esp_system_abort(const char*) { std::abort(); }
            ''',
            "esp_log.h": "\n".join(
                f"#define ESP_LOG{level}(...) ((void)0)" for level in "EWID"
            ),
            "esp_pthread.h": "",
            "esp_app_desc.h": r'''
                struct esp_app_desc_t { const char* version; };
                inline const esp_app_desc_t* esp_app_get_description() {
                    static esp_app_desc_t desc{"test"};
                    return &desc;
                }
            ''',
            "board.h": r'''
                #pragma once
                #include <cstdint>
                #include <cstdlib>
                #include <string>
                struct AudioCodec { void SetOutputVolume(int) { std::abort(); } };
                struct Backlight { void SetBrightness(uint8_t, bool) { std::abort(); } };
                #include "camera.h"
                struct HostCamera : Camera {
                    std::string legacy_url, legacy_token;
                    int captures = 0;
                    void SetExplainUrl(const std::string& url, const std::string& token) override {
                        legacy_url = url; legacy_token = token;
                    }
                    bool Capture() override { ++captures; return true; }
                    bool SetHMirror(bool) override { return true; }
                    bool SetVFlip(bool) override { return true; }
                    std::expected<std::string, std::string> Explain(const std::string& question) override {
                        return Explain(question, legacy_url, legacy_token);
                    }
                    std::expected<std::string, std::string> Explain(
                        const std::string& question, const std::string& url,
                        const std::string& token) override {
                        if (url == "https://fail.example/") {
                            return std::unexpected("Upload failed");
                        }
                        return url + "|" + token + "|" + question;
                    }
                };
                class Board {
                public:
                    static Board& GetInstance() { static Board board; return board; }
                    std::string GetDeviceStatusJson() { std::abort(); }
                    std::string GetSystemInfoJson() { std::abort(); }
                    AudioCodec* GetAudioCodec() { std::abort(); }
                    Backlight* GetBacklight() { return nullptr; }
                    HostCamera* GetCamera() {
                        static HostCamera camera;
                        return &camera;
                    }
                };
            ''',
            "display.h": "",
            "lvgl_image.h": "",
            "lvgl_theme.h": "",
            "lvgl_display.h": "",
        }
        stubs.update(cls.extra_stubs)
        for name, contents in stubs.items():
            (build / name).parent.mkdir(parents=True, exist_ok=True)
            (build / name).write_text(textwrap.dedent(contents), encoding="utf-8")
        driver = build / "driver.cc"
        driver.write_text(textwrap.dedent(r'''
            #include "mcp_server.h"
            #include "press_to_talk_mcp_tool.h"
            #include "board.h"
            int main(int argc, char** argv) {
                auto& server = McpServer::GetInstance();
                const std::string mode = argc > 2 ? argv[2] : "";
                PressToTalkMcpTool press_to_talk;
                if (mode != "empty" && mode != "user-only") {
                    press_to_talk.Initialize();
                    server.AddTool("test.error", "Return a tool error",
                        PropertyList({Property("message", kPropertyTypeString)}),
                        [](const PropertyList& args) -> ToolResult {
                            return std::unexpected(args["message"].value<std::string>());
                        });
                    server.AddTool("test.integer", "Return integer",
                        PropertyList({Property("value", kPropertyTypeInteger)}),
                        [](const PropertyList& args) -> ToolResult {
                            return args["value"].value<int>();
                        });
                    server.AddTool("test.image", "Return image", PropertyList(),
                        [](const PropertyList&) -> ToolResult {
                            return new ImageContent("image/jpeg", "abc");
                        });
                    server.AddTool("test.captures", "Capture count", PropertyList(),
                        [](const PropertyList&) -> ToolResult {
                            return Board::GetInstance().GetCamera()->captures;
                        });
                    server.AddTool("self.camera.take_photo", "Take photo",
                        PropertyList({Property("question", kPropertyTypeString)}),
                        [](const PropertyList& args, const McpRequestContext& context) -> ToolResult {
                            auto camera = Board::GetInstance().GetCamera();
                            camera->Capture();
                            auto question = args["question"].value<std::string>();
                            auto value = context.vision
                                ? camera->Explain(question, context.vision->url, context.vision->token)
                                : camera->Explain(question);
                            if (!value) return std::unexpected(value.error());
                            return *value;
                        });
                }
                if (mode != "empty") {
                    server.AddUserOnlyTool("test.user", "User-only", PropertyList(),
                        [](const PropertyList&) -> ToolResult { return true; });
                }
                if (mode == "pages") {
                    for (int i = 0; i < 40; ++i) {
                        server.AddTool("page." + std::to_string(i), std::string(700, 'x'), PropertyList(),
                            [](const PropertyList&) -> ToolResult { return true; });
                    }
                }
                if (mode == "oversize") {
                    server.AddTool("oversize", std::string(9000, 'x'), PropertyList(),
                        [](const PropertyList&) -> ToolResult { return true; });
                }
                McpServer::ResponseSender sender;
                if (argc > 1 && std::string(argv[1]) == "custom-sender") {
                    sender = [](const std::string& payload) {
                        std::cout << "custom\t" << payload << '\n';
                    };
                }
                std::string request;
                while (std::getline(std::cin, request)) {
                    server.ParseMessage(request, sender);
                    if (mode != "deferred") Application::GetInstance().Drain();
                }
                Application::GetInstance().Drain();
            }
        '''), encoding="utf-8")
        if cls.driver_source is not None:
            driver.write_text(cls.driver_source.read_text(encoding="utf-8"), encoding="utf-8")
        # Allow non-system installations without fetching dependencies in tests.
        cjson_flags = shlex.split(os.environ.get("MCP_CJSON_CFLAGS", "-I/usr/include/cjson"))
        libs = shlex.split(os.environ.get("MCP_CJSON_LIBS", "-lcjson -lmbedcrypto"))
        probe = subprocess.run(
            compiler + ["-std=c++23", *cjson_flags, "-x", "c++", "-", *libs,
                        "-o", str(build / "probe")],
            input="#include <expected>\n#include <cJSON.h>\n#include <mbedtls/base64.h>\n"
                  "int main() { std::expected<int, int> v; cJSON_Delete(cJSON_CreateObject()); "
                  "size_t n; return mbedtls_base64_encode(nullptr, 0, &n, nullptr, 0); }",
            text=True, capture_output=True,
        )
        if probe.returncode:
            raise unittest.SkipTest("requires C++23, cJSON and mbedTLS development headers: "
                                    + probe.stderr)
        cls.executable = build / "mcp_test"
        compiled = subprocess.run(
            compiler + ["-std=c++23", "-fno-exceptions", *cjson_flags,
                        *cls.extra_compile_flags,
                        *(f"-I{path}" for path in cls.extra_include_dirs),
                        f"-I{build}", f"-I{ROOT / 'main'}",
                        f"-I{ROOT / 'main/boards/common'}",
                        "-include", str(build / "platform.h"), str(driver),
                        str(ROOT / "main/mcp_server.cc"),
                        str(ROOT / "main/boards/common/press_to_talk_mcp_tool.cc"),
                        *(str(path) for path in cls.extra_sources),
                        *libs, "-o", str(cls.executable)],
            capture_output=True, text=True,
        )
        if compiled.returncode:
            raise RuntimeError("MCP host compile failed:\\n" + compiled.stderr)

    def responses(self, requests, custom_sender=True, mode="", expected_count=None):
        process = subprocess.run(
            [str(self.executable), "custom-sender" if custom_sender else "default", mode],
            input="".join((request if isinstance(request, str) else json.dumps(request)) + "\n"
                          for request in requests),
            text=True, encoding="utf-8", capture_output=True, check=True,
        )
        responses = []
        try:
            for line in process.stdout.splitlines():
                route, _, payload = line.partition("\t")
                self.assertEqual(route, "custom" if custom_sender else "default")
                responses.append(json.loads(payload))
        except json.JSONDecodeError as error:
            self.fail(f"MCP response is not valid JSON: {process.stdout!r}: {error}")
        self.assertEqual(len(responses), len(requests) if expected_count is None else expected_count)
        return responses

    @staticmethod
    def call(request_id, name, arguments):
        return {"jsonrpc": "2.0", "id": request_id, "method": "tools/call",
                "params": {"name": name, "arguments": arguments}}

    def assert_error(self, response, request_id, code, message):
        self.assertEqual(response, {"jsonrpc": "2.0", "id": request_id,
                                    "error": {"code": code, "message": message}})

    @staticmethod
    def modern(request_id, method="tools/list", params=None, capabilities=None):
        return {"jsonrpc": "2.0", "id": request_id, "method": method, "params": {
            **(params or {}),
            "_meta": {
                "io.modelcontextprotocol/protocolVersion": "2026-07-28",
                "io.modelcontextprotocol/clientInfo": {"name": "host", "version": "1"},
                "io.modelcontextprotocol/clientCapabilities": capabilities or {},
            },
        }}

    @classmethod
    def photo(cls, request_id, vision=None):
        caps = {} if vision is None else {"extensions": {"me.xiaozhi/vision": vision}}
        return cls.modern(request_id, "tools/call",
                          {"name": "self.camera.take_photo", "arguments": {"question": "look"}},
                          caps)

    @staticmethod
    def legacy_init(url="https://legacy.example/", token="legacy-secret"):
        return {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "capabilities": {"vision": {"url": url, "token": token}}}}
