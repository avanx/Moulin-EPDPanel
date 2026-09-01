#include "net/provisioning_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "device/config.h"
#include "device/control.h"
#include "epd/panel.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "net/wifi_manager.h"
#include "util/debug_log.h"

static const char *TAG = "setup_web";
static httpd_handle_t s_server;
static TaskHandle_t s_dns_task;
static volatile bool s_dns_running;
static int s_dns_sock = -1;

static const char *CAPTIVE_PORTAL_URL = "http://192.168.4.1/";

static const char AP_HTML[] =
"<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Moulin-EPDPanel 配网</title><style>"
"*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#17202a;font:15px system-ui,sans-serif}"
".card{max-width:620px;margin:28px auto;background:#fff;padding:26px;border-radius:16px;box-shadow:0 8px 30px #0002}"
"h1{margin:0 0 6px}p{color:#62707d}label{display:block;margin:15px 0 5px;font-weight:600}"
"input,button{width:100%;padding:11px;border:1px solid #ccd4da;border-radius:8px;font:inherit}"
"button{margin-top:18px;background:#18212a;color:#fff;border:0;cursor:pointer}"
".row{display:grid;grid-template-columns:1fr auto;gap:8px}.row button{width:auto;margin:0}"
"#msg{min-height:24px;margin-top:14px}.ok{color:#18794e}.err{color:#c62828}"
"#netlist{list-style:none;margin:10px 0 0;padding:0;border:1px solid #ccd4da;border-radius:8px;overflow:hidden;min-height:48px}"
"#netlist li{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:12px;"
"border-top:1px solid #e8edf1;cursor:pointer;-webkit-tap-highlight-color:transparent}"
"#netlist li:first-child{border-top:0}#netlist li.sel{background:#e8f0fe}"
"#netlist li .meta{color:#62707d;font-size:13px;white-space:nowrap}"
"#netlist .empty{padding:14px;color:#62707d;cursor:default}"
".pager{display:flex;align-items:center;gap:8px;margin-top:8px}"
".pager button{margin:0;width:auto;padding:8px 12px;background:#59636e}"
".pager button:disabled{opacity:.45}.pager span{flex:1;text-align:center;color:#62707d;font-size:13px}"
"</style></head><body><main class=card><h1>连接 WiFi</h1>"
"<p>此页面仅用于设备配网。联网后请通过设备的 Station IP 配置屏幕参数。</p><form id=form>"
"<label>WiFi 网络</label><div class=row>"
"<input id=ssid maxlength=32 placeholder='点选下方网络或手动输入 SSID' required>"
"<button type=button id=scan>重新扫描</button></div>"
"<ul id=netlist><li class=empty>正在扫描…</li></ul>"
"<div class=pager><button type=button id=prev>上一页</button>"
"<span id=pageinfo></span><button type=button id=next>下一页</button></div>"
"<label>WiFi 密码</label><input id=wifi_password type=password maxlength=63 placeholder='留空则保留原密码；开放网络可为空'>"
"<button type=submit>保存并连接</button><div id=msg></div></form></main><script>"
"const q=s=>document.querySelector(s),msg=(t,e=false)=>{q('#msg').textContent=t;q('#msg').className=e?'err':'ok'};"
"const PAGE=10;let nets=[],page=0;"
"function render(){const list=q('#netlist'),pages=Math.max(1,Math.ceil(nets.length/PAGE)||1);"
"if(page>pages-1)page=pages-1;if(page<0)page=0;list.innerHTML='';"
"const slice=nets.slice(page*PAGE,page*PAGE+PAGE);"
"if(!slice.length){const li=document.createElement('li');li.className='empty';"
"li.textContent='未发现网络，请手动输入 SSID';list.append(li)}"
"else slice.forEach(n=>{const li=document.createElement('li');"
"if(q('#ssid').value===n.ssid)li.className='sel';"
"const name=document.createElement('span');name.textContent=n.ssid;"
"const meta=document.createElement('span');meta.className='meta';"
"meta.textContent=n.rssi+' dBm'+(n.open?' · 开放':'');"
"li.append(name,meta);li.onclick=()=>{q('#ssid').value=n.ssid;render()};list.append(li)});"
"q('#pageinfo').textContent=nets.length?(page+1)+'/'+Math.ceil(nets.length/PAGE)+' · 共 '+nets.length+' 个':'—';"
"q('#prev').disabled=!nets.length||page<=0;"
"q('#next').disabled=!nets.length||page>=Math.ceil(nets.length/PAGE)-1}"
"async function scan(){const b=q('#scan'),c=new AbortController(),t=setTimeout(()=>c.abort(),15000);"
"b.disabled=true;q('#netlist').innerHTML='';const loading=document.createElement('li');"
"loading.className='empty';loading.textContent='正在扫描…';q('#netlist').append(loading);"
"try{let a;while(true){const r=await fetch('/api/scan',{signal:c.signal,cache:'no-store'});a=await r.json();"
"if(r.status===202){await new Promise(x=>setTimeout(x,700));continue}if(!r.ok)throw Error(a.error||'扫描失败');break}"
"nets=a.networks||[];page=0;render();msg(nets.length?'发现 '+nets.length+' 个网络':'未发现网络，可手动输入 SSID')}"
"catch(e){nets=[];render();msg(e.name==='AbortError'?'WiFi 扫描超时，可手动输入 SSID':'WiFi 扫描失败，可手动输入 SSID',true)}"
"finally{clearTimeout(t);b.disabled=false}}"
"async function load(){try{const c=await(await fetch('/api/wifi')).json();q('#ssid').value=c.wifi_ssid||'';await scan()}"
"catch(e){msg('读取 WiFi 配置失败',true)}}"
"q('#scan').onclick=scan;q('#prev').onclick=()=>{page--;render()};q('#next').onclick=()=>{page++;render()};"
"q('#form').onsubmit=async e=>{e.preventDefault();msg('正在保存…');"
"const body={wifi_ssid:q('#ssid').value.trim(),wifi_password:q('#wifi_password').value};"
"try{const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
"const j=await r.json();if(!r.ok)throw Error(j.error||'保存失败');msg('保存成功，设备即将重启并连接 WiFi。请从路由器或串口查看 Station IP')}"
"catch(x){msg(x.message,true)}};load();</script></body></html>";
static const char STATION_HTML[] =
"<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'><title>Moulin-EPDPanel 参数</title><style>"
"*{box-sizing:border-box}body{margin:0;background:#f3f5f7;color:#17202a;font:15px system-ui,sans-serif}"
".card{max-width:620px;margin:28px auto;background:#fff;padding:26px;border-radius:16px;box-shadow:0 8px 30px #0002}"
"h1{margin:0 0 6px}p{color:#62707d}label{display:block;margin:15px 0 5px;font-weight:600}"
"input,select,button{width:100%;padding:11px;border:1px solid #ccd4da;border-radius:8px;font:inherit}"
"button{margin-top:12px;background:#18212a;color:#fff;border:0;cursor:pointer}"
".danger{background:#b42318}.secondary{background:#59636e}"
"h2{margin:22px 0 0;font-size:16px}.hint{margin:6px 0 0;font-size:13px;color:#62707d}"
"#msg{min-height:24px;margin-top:14px}.ok{color:#18794e}.err{color:#c62828}"
"</style></head><body><main class=card><h1>屏幕参数</h1><p id=status>正在读取设备状态…</p><form id=form>"
"<h2>屏幕类型</h2><p class=hint>可单独保存，无需服务端参数；用于刷白等本地测试。</p>"
"<label>屏幕类型</label><select id=panel_type></select>"
"<button type=button id=save_panel>保存屏幕类型</button>"
"<h2>服务端拉取</h2><p class=hint>仅在需要从服务器拉画面时填写。</p>"
"<label>服务器地址</label><input id=server_url type=url maxlength=192 placeholder='https://epd.example.com'>"
"<label>Screen ID</label><input id=screen_id maxlength=36 placeholder='UUID'>"
"<label>API Key</label><input id=api_key type=password maxlength=128 placeholder='留空则保留原 Key'>"
"<button type=submit>保存服务参数</button>"
"<h2>操作</h2>"
"<button type=button id=refresh>强制全屏刷新</button>"
"<button type=button id=white class=danger>刷白并暂停更新</button>"
"<button type=button id=restart class=secondary>重启设备</button>"
"<h2>墨色沉积修复</h2>"
"<p class=hint>全黑 5 秒与全白 5 秒交替刷新，用于消除长期残影与墨色沉积。"
"修复期间自动更新暂停，完成后自动恢复并重新拉取画面。</p>"
"<label>循环次数（0 表示持续循环，直到点击停止）</label>"
"<input id=repair_cycles type=number min=0 max=100 step=1 value=5>"
"<button type=button id=repair class=danger>启动修复模式</button>"
"<button type=button id=repair_stop class=secondary>停止修复</button><div id=msg></div></form></main><script>"
"const q=s=>document.querySelector(s),msg=(t,e=false)=>{q('#msg').textContent=t;q('#msg').className=e?'err':'ok'};"
"let hasApiKey=false;"
"async function load(){try{let c=await(await fetch('/api/device')).json();q('#server_url').value=c.server_url||'';"
"q('#screen_id').value=c.screen_id||'';hasApiKey=!!c.has_api_key;"
"if(c.available_panels){let ps=q('#panel_type');ps.innerHTML='';c.available_panels.forEach(p=>{let o=document.createElement('option');o.value=p.type;o.textContent=p.label;ps.append(o)});ps.value=c.panel_type||''}"
"let s=await(await fetch('/api/status')).json();q('#status').textContent=`Station IP：${s.ip}；WiFi：${s.connected?'已连接':'未连接'}`}"
"catch(e){msg('读取设备参数失败',true)}}"
"q('#save_panel').onclick=async()=>{msg('正在保存屏幕类型…');"
"let body={panel_only:true,panel_type:q('#panel_type').value};"
"try{let r=await fetch('/api/device',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
"let j=await r.json();if(!r.ok)throw Error(j.error||'保存失败');msg('屏幕类型已保存，设备即将重启')}"
"catch(x){msg(x.message,true)}};"
"q('#form').onsubmit=async e=>{e.preventDefault();"
"let server=q('#server_url').value.trim(),sid=q('#screen_id').value.trim(),key=q('#api_key').value.trim();"
"if(!server||!sid){msg('保存服务参数需填写服务器地址与 Screen ID',true);return}"
"if(!key&&!hasApiKey){msg('请填写 API Key',true);return}"
"msg('正在保存…');let body={server_url:server,screen_id:sid,api_key:key,panel_type:q('#panel_type').value};"
"try{let r=await fetch('/api/device',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
"let j=await r.json();if(!r.ok)throw Error(j.error||'保存失败');msg('保存成功，设备即将重启并开始轮询')}"
"catch(x){msg(x.message,true)}};"
"q('#refresh').onclick=async()=>{q('#refresh').disabled=true;msg('正在提交全屏刷新请求…');"
"try{let r=await fetch('/api/actions/refresh',{method:'POST'});let j=await r.json();"
"if(!r.ok)throw Error(j.error||'命令失败');msg('全屏刷新请求已提交，设备将立即获取最新画面')}"
"catch(x){msg(x.message,true)}finally{q('#refresh').disabled=false}};"
"q('#white').onclick=async()=>{if(!confirm('确认将屏幕刷成全白并停止自动更新？重启后才会恢复更新。'))return;"
"q('#white').disabled=true;msg('正在刷白，墨水屏刷新可能需要约 30 秒…');try{let r=await fetch('/api/actions/white',{method:'POST'});"
"let j=await r.json();if(!r.ok)throw Error(j.error||'命令失败');for(let i=0;i<70;i++){await new Promise(x=>setTimeout(x,1000));"
"let s=await(await fetch('/api/status',{cache:'no-store'})).json();if(s.control_state==='white'){msg('屏幕已刷白，自动更新已暂停，重启后恢复');return}"
"if(s.control_state==='white_failed')throw Error('屏幕刷白失败，更新仍保持暂停，请重启恢复')}throw Error('等待刷白完成超时')}"
"catch(x){msg(x.message,true)}finally{q('#white').disabled=false}};"
"q('#repair').onclick=async()=>{let n=parseInt(q('#repair_cycles').value,10);"
"if(!Number.isInteger(n)||n<0||n>100){msg('循环次数需为 0-100 的整数',true);return}"
"if(!confirm('确认启动修复模式？屏幕将反复黑白交替刷新，期间自动更新暂停。'))return;"
"q('#repair').disabled=true;msg('正在启动修复模式…');"
"try{let r=await fetch('/api/actions/repair',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({cycles:n})});let j=await r.json();if(!r.ok)throw Error(j.error||'命令失败');"
"while(true){await new Promise(x=>setTimeout(x,2000));"
"let s=await(await fetch('/api/status',{cache:'no-store'})).json();"
"if(s.control_state==='repairing'){msg('修复中…已完成 '+s.repair_cycle+(s.repair_cycles?' / '+s.repair_cycles:'')+' 轮');continue}"
"if(s.control_state==='repair_failed')throw Error('修复失败，自动更新保持暂停，请重启设备');"
"msg('修复完成，共 '+s.repair_cycle+' 轮，已恢复自动更新');return}}"
"catch(x){msg(x.message,true)}finally{q('#repair').disabled=false}};"
"q('#repair_stop').onclick=async()=>{try{let r=await fetch('/api/actions/repair/stop',{method:'POST'});"
"let j=await r.json();if(!r.ok)throw Error(j.error||'命令失败');msg('已请求停止，将在当前一轮刷白后结束')}"
"catch(x){msg(x.message,true)}};"
"q('#restart').onclick=async()=>{if(!confirm('确认立即重启设备？'))return;msg('设备正在重启…');"
"try{await fetch('/api/actions/restart',{method:'POST'})}catch(e){}};load();</script></body></html>";

static void dns_server_task(void *argument)
{
    (void)argument;
    uint8_t buffer[512];
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(53),
    };

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) {
        EPD_LOGE(TAG, "captive DNS socket create failed");
        s_dns_running = false;
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(s_dns_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (bind(s_dns_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        EPD_LOGE(TAG, "captive DNS bind failed");
        close(s_dns_sock);
        s_dns_sock = -1;
        s_dns_running = false;
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    EPD_LOGI(TAG, "captive DNS listening on UDP/53 -> 192.168.4.1");
    while (s_dns_running) {
        struct sockaddr_in source;
        socklen_t source_len = sizeof(source);
        int length = recvfrom(s_dns_sock, buffer, sizeof(buffer), 0,
                              (struct sockaddr *)&source, &source_len);
        if (length < 12) {
            continue;
        }
        /* Ignore responses; only answer standard queries. */
        if (buffer[2] & 0x80) {
            continue;
        }

        int offset = 12;
        while (offset < length && buffer[offset] != 0) {
            offset += buffer[offset] + 1;
        }
        offset++; /* trailing label length 0 */
        if (offset + 4 > length) {
            continue;
        }

        uint16_t qtype = ((uint16_t)buffer[offset] << 8) | buffer[offset + 1];
        int question_end = offset + 4;

        buffer[2] = 0x81; /* QR=1, RD=1 */
        buffer[3] = 0x80; /* RA=1, RCODE=0 */
        buffer[4] = 0x00;
        buffer[5] = 0x01; /* QDCOUNT */
        buffer[6] = 0x00;
        buffer[7] = (qtype == 0x0001) ? 0x01 : 0x00; /* ANCOUNT for A only */
        buffer[8] = 0x00;
        buffer[9] = 0x00;
        buffer[10] = 0x00;
        buffer[11] = 0x00;

        int response_len = question_end;
        if (qtype == 0x0001 && response_len + 16 <= (int)sizeof(buffer)) {
            buffer[response_len++] = 0xC0;
            buffer[response_len++] = 0x0C; /* name pointer to question */
            buffer[response_len++] = 0x00;
            buffer[response_len++] = 0x01; /* TYPE A */
            buffer[response_len++] = 0x00;
            buffer[response_len++] = 0x01; /* CLASS IN */
            buffer[response_len++] = 0x00;
            buffer[response_len++] = 0x00;
            buffer[response_len++] = 0x00;
            buffer[response_len++] = 0x3C; /* TTL 60s */
            buffer[response_len++] = 0x00;
            buffer[response_len++] = 0x04; /* RDLENGTH */
            buffer[response_len++] = 192;
            buffer[response_len++] = 168;
            buffer[response_len++] = 4;
            buffer[response_len++] = 1;
        }
        sendto(s_dns_sock, buffer, response_len, 0, (struct sockaddr *)&source, source_len);
    }

    if (s_dns_sock >= 0) {
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void dns_server_start(void)
{
    if (s_dns_running) {
        return;
    }
    s_dns_running = true;
    BaseType_t created = xTaskCreate(dns_server_task, "captive_dns", 3072, NULL, 3, &s_dns_task);
    if (created != pdPASS) {
        s_dns_running = false;
        s_dns_task = NULL;
        EPD_LOGE(TAG, "captive DNS task create failed");
    }
}

static void dns_server_stop(void)
{
    if (!s_dns_running && s_dns_sock < 0) {
        return;
    }
    s_dns_running = false;
    if (s_dns_sock >= 0) {
        int sock = s_dns_sock;
        s_dns_sock = -1;
        shutdown(sock, 0);
        close(sock);
    }
    for (int i = 0; i < 20 && s_dns_task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static esp_err_t captive_redirect_handler(httpd_req_t *request)
{
    if (!wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Not Found");
    }
    /*
     * Phones probe captive-portal URLs after joining the AP. Redirect every
     * unmatched request to the setup page so the OS opens the config UI.
     */
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", CAPTIVE_PORTAL_URL);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t send_json(httpd_req_t *request, cJSON *json, int status)
{
    char *text = cJSON_PrintUnformatted(json);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(request, "application/json");
    switch (status) {
    case 200:
        break;
    case 202:
        httpd_resp_set_status(request, "202 Accepted");
        break;
    case 400:
        httpd_resp_set_status(request, "400 Bad Request");
        break;
    case 403:
        httpd_resp_set_status(request, "403 Forbidden");
        break;
    default:
        httpd_resp_set_status(request, "500 Internal Server Error");
        break;
    }
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static esp_err_t index_handler(httpd_req_t *request)
{
    bool ap_mode = wifi_manager_is_provisioning();
    const char *page = ap_mode ? AP_HTML : STATION_HTML;
    EPD_LOGI(TAG, "serving %s configuration page", ap_mode ? "AP WiFi" : "Station device");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_get_handler(httpd_req_t *request)
{
    if (!wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Available in AP mode only");
    }
    device_config_t config;
    if (device_config_load(&config) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS error");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    cJSON_AddBoolToObject(root, "has_wifi_password", config.wifi_password[0] != '\0');
    esp_err_t err = send_json(request, root, 200);
    cJSON_Delete(root);
    return err;
}

static esp_err_t device_get_handler(httpd_req_t *request)
{
    if (wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Available in Station mode only");
    }
    device_config_t config;
    if (device_config_load(&config) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS error");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "server_url", config.server_url);
    cJSON_AddStringToObject(root, "screen_id", config.screen_id);
    cJSON_AddStringToObject(root, "panel_type", config.panel_type);
    cJSON_AddBoolToObject(root, "has_api_key", config.api_key[0] != '\0');
    {
        const epd_panel_ops_t *const *panels;
        size_t count = epd_panel_list(&panels);
        cJSON *available = cJSON_AddArrayToObject(root, "available_panels");
        for (size_t i = 0; i < count; i++) {
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "type", panels[i]->type);
            cJSON_AddStringToObject(entry, "label", panels[i]->label);
            cJSON_AddItemToArray(available, entry);
        }
    }
    esp_err_t err = send_json(request, root, 200);
    cJSON_Delete(root);
    return err;
}

static bool copy_json_string(cJSON *root, const char *key, char *dest, size_t size,
                             bool allow_empty)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) >= size || (!allow_empty && item->valuestring[0] == '\0')) {
        return false;
    }
    snprintf(dest, size, "%s", item->valuestring);
    return true;
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static cJSON *receive_json(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > 1024) {
        return NULL;
    }
    char *body = calloc(1, request->content_len + 1);
    if (body == NULL) {
        return NULL;
    }
    int received = httpd_req_recv(request, body, request->content_len);
    if (received <= 0) {
        free(body);
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(body, received);
    free(body);
    return root;
}

static esp_err_t wifi_post_handler(httpd_req_t *request)
{
    if (!wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Available in AP mode only");
    }
    cJSON *root = receive_json(request);
    device_config_t config;
    esp_err_t load_err = device_config_load(&config);
    char old_ssid[DEVICE_WIFI_SSID_MAX + 1];
    snprintf(old_ssid, sizeof(old_ssid), "%s", config.wifi_ssid);
    bool valid = load_err == ESP_OK && root != NULL &&
                 copy_json_string(root, "wifi_ssid", config.wifi_ssid,
                                  sizeof(config.wifi_ssid), false);
    char password[DEVICE_WIFI_PASSWORD_MAX + 1] = {0};
    if (valid) {
        valid = copy_json_string(root, "wifi_password", password, sizeof(password), true);
    }
    if (valid && password[0] != '\0') {
        snprintf(config.wifi_password, sizeof(config.wifi_password), "%s", password);
    } else if (valid && strcmp(old_ssid, config.wifi_ssid) != 0) {
        config.wifi_password[0] = '\0';
    }
    if (valid) {
        valid = device_config_validate_wifi(&config);
    }

    cJSON *response = cJSON_CreateObject();
    esp_err_t result;
    if (!valid) {
        cJSON_AddStringToObject(response, "error", "WiFi 配置无效");
        result = send_json(request, response, 400);
    } else {
        esp_err_t err = device_config_save_wifi(&config);
        if (err != ESP_OK) {
            EPD_LOGE(TAG, "save WiFi failed: %s", esp_err_to_name(err));
            cJSON_AddStringToObject(response, "error", "WiFi 配置保存失败");
            result = send_json(request, response, 500);
        } else {
            cJSON_AddBoolToObject(response, "ok", true);
            result = send_json(request, response, 200);
            xTaskCreate(restart_task, "setup_restart", 2048, NULL, 5, NULL);
        }
    }
    cJSON_Delete(response);
    cJSON_Delete(root);
    return result;
}

static bool panel_type_registered(const char *type)
{
    if (type == NULL || type[0] == '\0') {
        return false;
    }
    const epd_panel_ops_t *const *panels;
    size_t count = epd_panel_list(&panels);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(type, panels[i]->type) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t device_post_handler(httpd_req_t *request)
{
    if (wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Available in Station mode only");
    }
    cJSON *root = receive_json(request);
    device_config_t config;
    esp_err_t load_err = device_config_load(&config);
    bool panel_only = root != NULL &&
                      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "panel_only"));

    cJSON *response = cJSON_CreateObject();
    esp_err_t result;

    if (load_err != ESP_OK || root == NULL) {
        cJSON_AddStringToObject(response, "error", "参数无效");
        result = send_json(request, response, 400);
        cJSON_Delete(response);
        cJSON_Delete(root);
        return result;
    }

    /* Apply panel_type from body when present (both modes). */
    cJSON *pt = cJSON_GetObjectItemCaseSensitive(root, "panel_type");
    if (cJSON_IsString(pt) && pt->valuestring != NULL && pt->valuestring[0] != '\0') {
        snprintf(config.panel_type, sizeof(config.panel_type), "%s", pt->valuestring);
    }

    if (panel_only) {
        if (!panel_type_registered(config.panel_type)) {
            cJSON_AddStringToObject(response, "error", "屏幕类型无效或未编译进固件");
            result = send_json(request, response, 400);
        } else {
            esp_err_t err = device_config_save_panel_type(config.panel_type);
            if (err != ESP_OK) {
                EPD_LOGE(TAG, "save panel type failed: %s", esp_err_to_name(err));
                cJSON_AddStringToObject(response, "error", "屏幕类型保存失败");
                result = send_json(request, response, 500);
            } else {
                cJSON_AddBoolToObject(response, "ok", true);
                result = send_json(request, response, 200);
                xTaskCreate(restart_task, "panel_restart", 2048, NULL, 5, NULL);
            }
        }
        cJSON_Delete(response);
        cJSON_Delete(root);
        return result;
    }

    bool valid = copy_json_string(root, "server_url", config.server_url,
                                  sizeof(config.server_url), false) &&
                 copy_json_string(root, "screen_id", config.screen_id,
                                  sizeof(config.screen_id), false);

    char api_key[DEVICE_API_KEY_MAX + 1] = {0};
    if (valid) {
        valid = copy_json_string(root, "api_key", api_key, sizeof(api_key), true);
    }
    if (valid && api_key[0] != '\0') {
        snprintf(config.api_key, sizeof(config.api_key), "%s", api_key);
    }
    if (valid && config.panel_type[0] != '\0') {
        valid = panel_type_registered(config.panel_type);
    }
    if (valid) {
        valid = device_config_validate_device_settings(&config);
    }

    if (!valid) {
        cJSON_AddStringToObject(response, "error", "参数无效或缺少 API Key");
        result = send_json(request, response, 400);
    } else {
        esp_err_t err = device_config_save_device_settings(&config);
        if (err != ESP_OK) {
            EPD_LOGE(TAG, "save device settings failed: %s", esp_err_to_name(err));
            cJSON_AddStringToObject(response, "error", "参数保存失败");
            result = send_json(request, response, 500);
        } else {
            cJSON_AddBoolToObject(response, "ok", true);
            result = send_json(request, response, 200);
            xTaskCreate(restart_task, "config_restart", 2048, NULL, 5, NULL);
        }
    }
    cJSON_Delete(response);
    cJSON_Delete(root);
    return result;
}

static esp_err_t scan_handler(httpd_req_t *request)
{
    if (!wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Available in AP mode only");
    }
    wifi_ap_record_t records[16];
    uint16_t count = 16;
    esp_err_t scan_err = wifi_manager_scan(records, &count);
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_AddArrayToObject(root, "networks");
    int status = 200;
    if (scan_err == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            if (records[i].ssid[0] == '\0') {
                continue;
            }
            cJSON *network = cJSON_CreateObject();
            cJSON_AddStringToObject(network, "ssid", (const char *)records[i].ssid);
            cJSON_AddNumberToObject(network, "rssi", records[i].rssi);
            cJSON_AddBoolToObject(network, "open", records[i].authmode == WIFI_AUTH_OPEN);
            cJSON_AddItemToArray(networks, network);
        }
        EPD_LOGI(TAG, "returning %u WiFi scan results", (unsigned)count);
    } else if (scan_err == ESP_ERR_NOT_FINISHED) {
        cJSON_AddBoolToObject(root, "scanning", true);
        status = 202;
    } else {
        EPD_LOGE(TAG, "WiFi scan endpoint failed: %s", esp_err_to_name(scan_err));
        cJSON_AddStringToObject(root, "error", esp_err_to_name(scan_err));
        status = 500;
    }
    esp_err_t err = send_json(request, root, status);
    cJSON_Delete(root);
    return err;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    char ip[16];
    char ap_ssid[24];
    wifi_manager_get_ip(ip, sizeof(ip));
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", wifi_manager_is_connected());
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddStringToObject(root, "ap_ssid", ap_ssid);
    cJSON_AddStringToObject(root, "control_state", device_control_state_name());
    cJSON_AddBoolToObject(root, "updates_paused", device_control_updates_paused());
    cJSON_AddNumberToObject(root, "repair_cycle", device_control_repair_cycles_done());
    cJSON_AddNumberToObject(root, "repair_cycles", device_control_repair_cycles_total());
    esp_err_t err = send_json(request, root, 200);
    cJSON_Delete(root);
    return err;
}

static esp_err_t white_action_handler(httpd_req_t *request)
{
    if (wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Available in Station mode only");
    }
    esp_err_t command_err = device_control_request_white();
    cJSON *root = cJSON_CreateObject();
    int status;
    if (command_err == ESP_OK) {
        cJSON_AddBoolToObject(root, "accepted", true);
        status = 202;
    } else {
        cJSON_AddStringToObject(root, "error",
                                device_control_updates_paused()
                                    ? "屏幕更新已暂停，请重启设备后重试"
                                    : "无法启动刷白任务");
        status = 400;
    }
    esp_err_t err = send_json(request, root, status);
    cJSON_Delete(root);
    return err;
}

static esp_err_t repair_action_handler(httpd_req_t *request)
{
    if (wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Available in Station mode only");
    }

    /* An empty body means "use the default cycle count". */
    uint32_t cycles = DEVICE_CONTROL_REPAIR_CYCLES_DEFAULT;
    bool valid = true;
    cJSON *root = receive_json(request);
    if (root != NULL) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "cycles");
        if (item != NULL) {
            if (cJSON_IsNumber(item) && item->valuedouble >= 0 &&
                item->valuedouble <= DEVICE_CONTROL_REPAIR_CYCLES_MAX &&
                item->valuedouble == (double)(uint32_t)item->valuedouble) {
                cycles = (uint32_t)item->valuedouble;
            } else {
                valid = false;
            }
        }
    }
    cJSON_Delete(root);

    esp_err_t command_err = valid ? device_control_request_repair(cycles)
                                  : ESP_ERR_INVALID_ARG;
    cJSON *response = cJSON_CreateObject();
    int status;
    if (command_err == ESP_OK) {
        cJSON_AddBoolToObject(response, "accepted", true);
        cJSON_AddNumberToObject(response, "cycles", cycles);
        status = 202;
        EPD_LOGI(TAG, "ink repair requested from Station page (%u cycles)",
                 (unsigned)cycles);
    } else {
        const char *reason;
        if (command_err == ESP_ERR_INVALID_ARG) {
            reason = "循环次数无效";
        } else if (command_err == ESP_ERR_NOT_SUPPORTED) {
            reason = "当前屏幕不支持修复模式";
        } else if (device_control_updates_paused()) {
            reason = "屏幕更新已暂停或修复正在进行，请稍候或重启设备后重试";
        } else {
            reason = "无法启动修复模式";
        }
        cJSON_AddStringToObject(response, "error", reason);
        status = 400;
    }
    esp_err_t err = send_json(request, response, status);
    cJSON_Delete(response);
    return err;
}

static esp_err_t repair_stop_action_handler(httpd_req_t *request)
{
    if (wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Available in Station mode only");
    }
    esp_err_t command_err = device_control_stop_repair();
    cJSON *root = cJSON_CreateObject();
    int status;
    if (command_err == ESP_OK) {
        cJSON_AddBoolToObject(root, "accepted", true);
        status = 202;
    } else {
        cJSON_AddStringToObject(root, "error", "当前没有正在运行的修复任务");
        status = 400;
    }
    esp_err_t err = send_json(request, root, status);
    cJSON_Delete(root);
    return err;
}

static esp_err_t refresh_action_handler(httpd_req_t *request)
{
    if (wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Available in Station mode only");
    }
    device_config_t config;
    esp_err_t command_err = device_config_load(&config);
    if (command_err == ESP_OK &&
        (!device_config_is_complete(&config) || !device_config_validate(&config))) {
        command_err = ESP_ERR_INVALID_STATE;
    }
    if (command_err == ESP_OK) {
        command_err = device_control_request_full_refresh();
    }
    cJSON *root = cJSON_CreateObject();
    int status;
    if (command_err == ESP_OK) {
        cJSON_AddBoolToObject(root, "accepted", true);
        status = 202;
    } else {
        cJSON_AddStringToObject(root, "error",
                                device_control_updates_paused()
                                    ? "屏幕更新已暂停，请重启设备后重试"
                                    : "无法提交全屏刷新请求");
        status = 400;
    }
    esp_err_t err = send_json(request, root, status);
    cJSON_Delete(root);
    return err;
}

static esp_err_t restart_action_handler(httpd_req_t *request)
{
    if (wifi_manager_is_provisioning()) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN,
                                   "Available in Station mode only");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "accepted", true);
    esp_err_t err = send_json(request, root, 202);
    cJSON_Delete(root);
    if (err == ESP_OK) {
        EPD_LOGI(TAG, "restart requested from Station page");
        xTaskCreate(restart_task, "web_restart", 2048, NULL, 5, NULL);
    }
    return err;
}

esp_err_t provisioning_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 6144;
    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "HTTP server start");

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_get_handler},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler},
        {.uri = "/api/device", .method = HTTP_GET, .handler = device_get_handler},
        {.uri = "/api/device", .method = HTTP_POST, .handler = device_post_handler},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = scan_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/actions/refresh", .method = HTTP_POST,
         .handler = refresh_action_handler},
        {.uri = "/api/actions/white", .method = HTTP_POST, .handler = white_action_handler},
        {.uri = "/api/actions/repair", .method = HTTP_POST, .handler = repair_action_handler},
        {.uri = "/api/actions/repair/stop", .method = HTTP_POST,
         .handler = repair_stop_action_handler},
        {.uri = "/api/actions/restart", .method = HTTP_POST, .handler = restart_action_handler},
        /* Captive-portal probes (Android/iOS/Windows) and any other GET path. */
        {.uri = "/*", .method = HTTP_GET, .handler = captive_redirect_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(s_server, &routes[i]);
        if (err != ESP_OK) {
            provisioning_server_stop();
            return err;
        }
    }
    if (wifi_manager_is_provisioning()) {
        dns_server_start();
    }
    EPD_LOGI(TAG, "configuration HTTP server started in %s mode",
             wifi_manager_is_provisioning() ? "AP" : "Station");
    return ESP_OK;
}

void provisioning_server_stop(void)
{
    dns_server_stop();
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
