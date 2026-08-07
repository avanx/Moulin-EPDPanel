# Moulin-EPDPanel

基于 ESP32-S3 的电子墨水屏（E-Paper）显示面板固件。设备周期性从服务端拉取画面并刷新，支持彩色与单色墨水屏。屏幕驱动通过编译宏（`platformio.ini` 的 `build_flags`）按需启用，运行时再通过网页在已编译的屏中选择当前型号。

本项目以 [MIT License](LICENSE) 开源，欢迎贡献驱动、修复与文档。

## 特性

- WiFi 配网（AP 热点 + 网页配置）与 Station 模式参数管理
- HTTPS 拉取 manifest / 帧数据，支持 SHA256 校验与增量（304）
- 统一面板接口 `epd_panel_ops_t`，驱动按宏裁剪编译
- 彩色全刷、单色全刷 / 局刷（视驱动能力）

## 路线图（TODO）

当前驱动仍与具体型号、双面板布局耦合较深。后续重点是**抽象更多 EPD 驱动**，以覆盖不同分辨率、主控芯片与面板拓扑。

### 驱动抽象与多屏支持

- [ ] 进一步抽象 EPD 驱动层：将初始化序列、LUT、像素打包、面板拓扑（单屏 / 左右拼 / 上下拼）从型号实现中剥离
- [ ] 支持更多主控芯片（如 SSD16xx、UC81xx 其他型号、IL0xxx 等常见 EPD controller）
- [ ] 支持更多分辨率与单面板布局（非 1360×480 双拼）
- [ ] 按主控族复用通用传输与刷新管线，型号仅保留 LUT / 时序差异
- [ ] 完善局刷能力矩阵（哪些屏支持、region 坐标映射规则）

## 已支持屏幕

| 状态 | 型号 | 主控 | 分辨率 | 颜色 | 布局 | 编译宏 |
|---|---|---|---|---|---|---|
| ✅ 已支持 | `QYEG1085RYU579` | UC8579cGAC ×2 | 1360×480 | 黑/白/红/黄（4 色, 2 bpp） | 左右双面板 | `CONFIG_EPD_PANEL_QYEG1085_COLOR` |
| ✅ 已支持 | `QYEG1085BNF86BF0` | JD79686AB ×2 | 1360×480 | 黑/白（1 bpp） | 左右双面板 | `CONFIG_EPD_PANEL_QYEG1085_MONO` |

> 当前两款屏共用 `epd_io` 时序与引脚约定，均为「左右双面板拼接、合计 1360×480」。更多分辨率 / 主控需通过抽象驱动层扩展。

## 硬件

- 主控：ESP32-S3-DevKitM-1 模组 N16R8（16 MB Flash + 8 MB OPI PSRAM）
- 默认引脚（可在 `src/epd/io.h` 改）：

  | 信号 | GPIO | 信号 | GPIO |
  |---|---|---|---|
  | BUSY | 4 | CS2（Slave/右） | 8 |
  | RST  | 5 | CLK  | 9 |
  | DC   | 6 | MOSI | 10 |
  | CS（Master/左） | 7 | | |

## 开发环境

- PlatformIO，框架 `espidf`
- 目标环境：`esp32-s3-devkitm-1`（见 `platformio.ini`）

## 构建与屏幕驱动选择

屏幕驱动通过编译宏按需启用，未启用的驱动编译为空翻译单元（不占 Flash/RAM）。在 `platformio.ini` 的 `build_flags` 里用 `-DCONFIG_EPD_PANEL_*=1` 选择（**至少启用一个**）：

- 彩色 `QYEG1085RYU579`（默认）：`-DCONFIG_EPD_PANEL_QYEG1085_COLOR=1`
- 单色 `QYEG1085BNF86BF0`：`-DCONFIG_EPD_PANEL_QYEG1085_MONO=1`

默认配置可同时启用两屏，网页再选当前型号。只保留其一亦可。改完后：

```
pio run -e esp32-s3-devkitm-1
```

> 切换驱动宏后，建议清一次构建缓存再编译：`rm -rf .pio/build/esp32-s3-devkitm-1`。
> 两个宏都不启用时，`epd/panel.c` 会 `#error` 报错中止（防呆）。

## 烧录与串口

```
pio run -t upload          # 烧录（自动检测串口）
pio device monitor         # 串口监视器，115200
```

如需指定端口：`pio run -t upload --upload-port COM3`。

## 首次配网（AP 模式）

设备首次启动且没有 WiFi 配置时，会自动开启配置热点：

SSID: EPDPanel-<mac>
密码: 123123

1. 在手机/电脑上连接设备的热点。
2. 浏览器打开 **http://192.168.4.1**，填写 WiFi 名称与密码（可扫描附近网络）。
3. 保存后设备重启并连接 WiFi。从路由器后台或串口日志查看设备获得的 **Station IP**。

## 屏幕参数配置（Station 模式）

设备联网后，用浏览器访问其 Station IP，在「屏幕参数」页配置：

- **服务器地址**、**Screen ID**、**API Key**：由你的服务端提供。
- **屏幕类型**：下拉框，只列出当前固件**已编译进去**的屏（由 `platformio.ini` 的 `build_flags` 决定）。选择后持久化到 NVS，重启生效；未配置时使用默认屏。

页面还提供三个操作按钮：

- **强制全屏刷新**：忽略缓存，立即重新拉取并刷新一帧。
- **刷白并暂停更新**：清屏为全白并暂停自动更新，直到重启。
- **重启设备**。

保存参数后设备会自动重启并开始轮询服务端。

## 服务端接口约定

设备通过 HTTPS 拉取 manifest 与帧数据，请求头带 `X-API-Key`。

- Manifest：`GET {server_url}/api/v1/device/screens/{screen_id}/manifest`
  （可选 `?base_snapshot_id=...` 做增量；未变化时服务端返回 304）
- 帧数据：manifest 中的 `frame_url`，二进制，校验 `sha256` 与 `X-Content-SHA256`。

manifest 中与屏幕相关的字段必须与当前屏匹配，否则设备判定为不兼容并重试：

| 屏 | `color_mode` | `encoding` | `panel_layout` | `width` | `height` | `byte_size` |
|---|---|---|---|---|---|---|
| 彩色 | `bwry` | `edp-bwry-v1` | `dual-panel-left-right` | 1360 | 480 | 163200 |
| 单色 | `bw` | `epd-bw-msb-v1` | `dual-panel-left-right` | 1360 | 480 | 81600 |

帧数据布局：

- 彩色：2 bpp，每行 170 B（4 像素/字节），双面板。
- 单色：1 bpp，行优先，每行 170 B = 85 B（左面板）+ 85 B（右面板），共 480 行。

## 目录结构

```
src/                         固件源码
  main.c                     入口与轮询
  util/debug_log.h           日志宏
  device/
    config.{c,h}             NVS 配置持久化
    control.{c,h}            设备控制（全刷 / 刷白 / 暂停）
  net/
    wifi_manager.{c,h}       WiFi Station / AP
    provisioning_server.{c,h} 网页配置服务
    display_client.{c,h}     服务端拉取 / 校验 / 刷新
  epd/
    io.{c,h}                 共享 IO 层（SPI bitbang / busy / 刷新序列）
    panel.{c,h}              统一面板接口与注册表
    drivers/
      qyeg1085ryu579.{c,h}   彩色屏 QYEG1085RYU579（CONFIG_EPD_PANEL_QYEG1085_COLOR）
      qyeg1085bnf86bf0.{c,h} 单色屏 QYEG1085BNF86BF0（CONFIG_EPD_PANEL_QYEG1085_MONO）
partitions.csv               分区表
platformio.ini               PlatformIO 构建配置
sdkconfig.defaults           ESP-IDF 默认配置
LICENSE                      MIT 许可证
```

## 添加一块新屏

1. 新建 `src/epd/drivers/<name>.{c,h}`：实现 `init` / `display_image` / `fill_white`，定义 `epd_panel_ops_t` 实例（填自己的分辨率 / 序列 / `color_mode` / `encoding` / `panel_layout`）。IO 时序相同时复用 `epd/io.h`，不同则自带。`.c` 文件用 `#if defined(CONFIG_EPD_PANEL_<NAME>) && CONFIG_EPD_PANEL_<NAME>` 守卫。
2. `src/epd/panel.c` 的 `s_registry` 增加 `#if` 条件项（参照现有 color / mono）。
3. `src/CMakeLists.txt` 的 `app_sources` 增加 `epd/drivers/<name>.c`。
4. `platformio.ini` 的 `build_flags` 增加 `-DCONFIG_EPD_PANEL_<NAME>=1`，然后 `pio run`。

### 欢迎贡献

若你手上有其他型号的电子墨水屏资料或可验证硬件，欢迎 PR：优先贡献独立驱动文件 + 注册表条目 + README「已支持屏幕」表更新。

## 许可证

本项目采用 [MIT License](LICENSE) 发布。
