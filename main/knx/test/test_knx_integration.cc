#include "knx_config.h"
#include "knx_manager.h"
#include "knx_mcp_tools.h"
#include "mcp_server.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <map>

namespace {
std::string persisted, upload;
bool storage_failure = false;
unsigned sends = 0;
std::vector<uint8_t> sent;
int64_t now = 1000000;
std::map<knx_address_t, std::pair<esp_knx_ip_telegram_callback_t, void*>> callbacks;
std::map<std::string, std::unique_ptr<McpTool>> tools;

std::string Configuration(const std::string& dpt = "DPT-20.102",
                          const std::string& address = "1.1.1") {
    return R"({"media_type":"knx_ip","media_parameters":{"multicast_address":"224.0.23.12","udp_port":3671,"transport_mode":"routing","interface_identifier":"test","nat":false},"physical_address":")" +
        address + R"(","communication_objects":[{"id":"mode","name":"Mode","group_address":"1/0/1","datapoint_type":")" +
        dpt + R"(","readable":true,"writable":true}]})";
}

void Receive(std::initializer_list<uint8_t> data) {
    knx_telegram_t telegram{};
    telegram.command = KNX_COMMAND_RESPONSE;
    telegram.destination = knx_group_address(1,0,1);
    telegram.data_length = data.size();
    std::copy(data.begin(),data.end(),telegram.data);
    auto [callback, context] = callbacks.at(telegram.destination);
    callback(&telegram,context);
}
}

// Only platform, transport and storage are faked. Parsing, codecs, manager and
// registered MCP callbacks (including McpTool::Call serialization) are production.
struct KnxManagerTestAccess {
    static bool Start(KnxManager& m) { return m.StartTransport(); }
    static bool Reload(KnxManager& m) { return m.LoadConfiguration(); }
};
int64_t esp_timer_get_time() { return now; }
struct esp_knx_ip_context { esp_knx_ip_state_t state = ESP_KNX_IP_STATE_INITIALIZED; };
extern "C" esp_err_t esp_knx_ip_create(const esp_knx_ip_config_t*, esp_knx_ip_handle_t* h) {
    *h = new esp_knx_ip_context; return ESP_OK;
}
extern "C" esp_err_t esp_knx_ip_destroy(esp_knx_ip_handle_t h) { delete h; callbacks.clear(); return ESP_OK; }
extern "C" esp_err_t esp_knx_ip_start(esp_knx_ip_handle_t h) { h->state=ESP_KNX_IP_STATE_RUNNING; return ESP_OK; }
extern "C" esp_err_t esp_knx_ip_stop(esp_knx_ip_handle_t h) { h->state=ESP_KNX_IP_STATE_STOPPED; return ESP_OK; }
extern "C" esp_err_t esp_knx_ip_get_state(esp_knx_ip_handle_t h, esp_knx_ip_state_t* s) { *s=h->state; return ESP_OK; }
extern "C" esp_err_t esp_knx_ip_register_callback(esp_knx_ip_handle_t, knx_address_t address,
    esp_knx_ip_telegram_callback_t callback, void* context) { callbacks[address]={callback,context}; return ESP_OK; }
extern "C" esp_err_t esp_knx_ip_send(esp_knx_ip_handle_t, knx_address_t, knx_command_t,
    const uint8_t* data, size_t length) { ++sends; sent.assign(data,data+length); return ESP_OK; }
bool KnxLoadRuntimeConfiguration(std::string& json, bool& found, std::string&) {
    found=!persisted.empty(); json=persisted; return true;
}
bool KnxLoadLegacyConfiguration(std::string&, bool& found, std::string&) { found=false; return true; }
bool KnxLoadRuntimeConfigurationUpload(std::string& json, std::string&) { json=upload; return true; }
bool KnxWriteRuntimeConfiguration(const std::string& json, std::string& error) {
    if (storage_failure) { error="injected storage failure"; return false; }
    persisted=json; return true;
}
McpServer::McpServer() = default;
McpServer::~McpServer() = default;
void McpServer::AddTool(const std::string& name, const std::string& description,
                        const PropertyList& properties, ToolCallback callback) {
    tools[name]=std::make_unique<McpTool>(name,description,properties,std::move(callback));
}
void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description,
                                const PropertyList& properties, ToolCallback callback) {
    AddTool(name,description,properties,std::move(callback)); tools[name]->set_user_only(true);
}

int main() {
    auto& manager=KnxManager::GetInstance();
    assert(manager.Initialize());
    std::string error;
    size_t count=0;
    assert(manager.ImportConfiguration(Configuration(),count,error) && count==1);
    esp_netif_t netif;
    manager.OnNetworkConnected(&netif);
    assert(KnxManagerTestAccess::Start(manager));
    RegisterKnxMcpTools(McpServer::GetInstance());
    RegisterKnxUserOnlyMcpTools(McpServer::GetInstance());
    auto call=[&](const char* tool,const char* key,const std::string& value) {
        return tools.at(tool)->Call(PropertyList({Property(key,kPropertyTypeString,value)}));
    };
    auto write=[&](const char* tool,const char* key,const char* id,const char* value) {
        return tools.at(tool)->Call(PropertyList({Property(key,kPropertyTypeString,std::string(id)),
            Property("value",kPropertyTypeString,std::string(value))}));
    };
    assert(write("self.knx.set_object","object_id","mode","4"));
    assert(sends==1 && (sent==std::vector<uint8_t>{0,4}));
    assert(write("self.knx.write","group_address","1/0/1","0"));
    auto invalid=write("self.knx.set_object","object_id","mode","5");
    assert(!invalid && invalid.error().find("mode")!=std::string::npos &&
        invalid.error().find("DPT-20.102")!=std::string::npos);
    assert(!write("self.knx.write","group_address","1/0/1","255"));
    assert(sends==2);
    KnxCommunicationObject object;
    assert(manager.GetCommunicationObject("mode",object) && !object.valid);
    Receive({0,3});
    assert(manager.GetCommunicationObject("mode",object) && object.valid);
    assert(std::get<uint8_t>(object.current_value)==3 && object.last_update_ms==1000);
    now=2000000;
    Receive({0,5});
    Receive({0,4,0});
    assert(manager.GetCommunicationObject("mode",object));
    assert(object.valid && std::get<uint8_t>(object.current_value)==3 && object.last_update_ms==1000);
    const auto saved=persisted;
    assert(!call("self.knx.import_configuration","configuration",Configuration("DPT-20.999","2.2.2")));
    assert(persisted==saved && manager.GetPhysicalAddress()=="1.1.1");
    assert(manager.GetCommunicationObject("mode",object) && object.last_update_ms==1000);
    storage_failure=true;
    assert(!call("self.knx.import_configuration","configuration",Configuration("DPT-1.001","3.3.3")));
    assert(persisted==saved && manager.GetPhysicalAddress()=="1.1.1");
    storage_failure=false;
    upload=Configuration("DPT-20.999","4.4.4");
    assert(!tools.at("self.knx.import_configuration_file")->Call(PropertyList()));
    assert(persisted==saved);
    object.id="direct"; object.parsed_group_address=1; object.datapoint_type={20,999,true};
    assert(!manager.RegisterCommunicationObject(object,error));
    assert(KnxManagerTestAccess::Reload(manager));
    assert(manager.GetCommunicationObject("mode",object) && !object.valid);
    assert((object.datapoint_type==KnxDpt{20,102,true}));
    assert(call("self.knx.import_configuration","configuration",Configuration("DPT-9.001")));
    assert(KnxManagerTestAccess::Start(manager));
    Receive({0,0x07,0xd0}); // 20.00 degrees, independently specified bytes.
    assert(manager.GetCommunicationObject("mode",object) && std::get<float>(object.current_value)==20);
    now=3000000;
    Receive({0,0x7f,0xff}); // KNX invalid data sentinel.
    assert(manager.GetCommunicationObject("mode",object) && object.valid && object.last_update_ms==2000);
    assert(std::get<float>(object.current_value)==20);
    assert(call("self.knx.get_object","object_id","mode"));
    std::cout<<"KNX manager/MCP integration tests passed\n";
}
