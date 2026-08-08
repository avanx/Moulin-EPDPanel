# Moulin-EPDPanel

基于 ESP32-S3 的电子墨水屏（E-Paper）显示面板固件。设备周期性从服务端拉取画面并刷新，支持彩色与单色墨水屏。屏幕驱动通过编译宏（`platformio.ini` 的 `build_flags`）按需启用，运行时再通过网页在已编译的屏中选择当前型号。

本项目以 [MIT License](LICENSE) 开源，欢迎贡献驱动、修复与文档。

## 特性

- WiFi 配网（AP 热点 + 网页配置）与 Station 模式参数管理
- HTTPS 拉取 manifest / 帧数据，支持 SHA256 校验与增量（304）
- 统一面板接口 `epd_panel_ops_t`，驱动按宏裁剪编译
- 彩色全刷、单色全刷 / 局刷（视驱动能力）

## 驱动分层

固件按 Bus / Controller / Layout / Product 四层组织，上层只依赖 `epd_panel_ops_t`：

| 层 | 目录 | 职责 |
|---|---|---|
| L0 Bus | `epd/io.*` | SPI bitbang、引脚、BUSY、复位（不含刷新命令） |
| L1 Ctrl | `epd/ctrl/*` | 主控族 init / 写 RAM / 刷新 / 深睡（各族自带刷新序列） |
| L2 Layout | `epd/layout/*` | 拓扑几何：`dual_lr`（左右拼）、`single`（单屏） |
| L3 Product | `epd/drivers/*` + `panel.*` | 型号 cfg + `epd_panel_ops_t` 注册 |

### 局刷能力矩阵

| 屏 | 布局 | 局刷 | 说明 |
|---|---|---|---|
| `qyeg1085_color` | dual-left-right | 否 | `display_partial = NULL`，仅全刷 |
| `qyeg1085_mono` | dual-left-right | 是 | `epd-bw-msb-v1`；宽为 8 倍数；跨缝由 `dual_lr` 拆分（`EPD_MASTER_ONLY_TEST` 时关闭） |
| `hink_e042a13_a0` | single | 否 | `epd-bwr-plane-v1`；仅全刷 |
| `hink_e0213a04` | single | 是 | `epd-bw-msb-v1`；宽为 8 倍数；可见区 122×250 |

### 路线图（TODO）

- [x] 抽象 EPD 驱动层：Bus / Ctrl / Layout / Product 分离
- [x] 局刷能力矩阵与跨 IC region 映射（`dual_lr`）
- [x] 单面板 layout API（`layout/single`，已由 HINK-E042A13-A0 接入）
- [x] 支持 SSD1619 主控族（`ctrl/ssd1619`）
- [x] 支持 IL3895 主控族（`ctrl/il3895`，HINK-E0213A04 / 微雪 2.13 V1 epd2in13）
- [ ] 支持更多主控芯片（SSD16xx 其他型号、UC81xx、IL0xxx 等）
- [ ] 接入其他更多的电子墨水屏幕

## 已支持屏幕

| 状态 | 型号 | 主控 | 分辨率 | 颜色 | 布局 | 编译宏 |
|---|---|---|---|---|---|---|
| ✅ 已支持 | `QYEG1085RYU579` | UC8579cGAC ×2 | 1360×480 | 黑/白/红/黄（4 色, 2 bpp） | 左右双面板 | `CONFIG_EPD_PANEL_QYEG1085_COLOR` |
| ✅ 已支持 | `QYEG1085BNF86BF0` | JD79686AB ×2 | 1360×480 | 黑/白（1 bpp） | 左右双面板 | `CONFIG_EPD_PANEL_QYEG1085_MONO` |
| ✅ 已支持 | `HINK-E042A13-A0` | SSD1619 ×1 | 400×300 | 黑/白/红（BWR） | 单面板 | `CONFIG_EPD_PANEL_HINK_E042A13_A0` |
| ✅ 已支持 | `HINK-E0213A04` | IL3895 ×1 | 128×250（可见 122×250） | 黑/白 | 单面板 | `CONFIG_EPD_PANEL_HINK_E0213A04` |

> 双面板屏共用 L0 `epd_io` 与 L2 `dual_lr`（合计 1360×480）。`HINK-E042A13-A0` 使用 L2 `single` + L1 `ssd1619`（例程见 `reference/GDEH042Z96_Arduino.ino`）。`HINK-E0213A04` 使用 L2 `single` + L1 `il3895`（对齐 `reference/epd2in13`：刷新恒为 `0xC4`，双缓冲写显两次）。新主控加 `ctrl/*`；新拓扑加 `layout/*`；产品文件保持薄粘合。

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
- 黑白红 `HINK-E042A13-A0`：`-DCONFIG_EPD_PANEL_HINK_E042A13_A0=1`
- 单色 `HINK-E0213A04`：`-DCONFIG_EPD_PANEL_HINK_E0213A04=1`

默认配置可同时启用多屏，网页再选当前型号。只保留其一亦可。改完后：

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
密码: 123456789

1. 在手机/电脑上连接设备的热点。
2. 浏览器打开 **http://192.168.4.1**，填写 WiFi 名称与密码（可扫描附近网络）。
3. 保存后设备重启并连接 WiFi。从路由器后台或串口日志查看设备获得的 **Station IP**。

## 屏幕参数配置（Station 模式）

设备联网后，用浏览器访问其 Station IP，在「屏幕参数」页配置：

- **屏幕类型**：可单独保存（不必填服务端参数），用于刷白等本地测试；未配置时使用默认屏。下拉框只列出当前固件已编译的屏。
- **服务器地址**、**Screen ID**、**API Key**：仅在需要从服务端拉画面时填写；「保存服务参数」时校验。

页面还提供操作按钮：

- **强制全屏刷新**：忽略缓存，立即重新拉取并刷新一帧（需已配置服务参数）。
- **刷白并暂停更新**：清屏为全白并暂停自动更新，直到重启（不依赖服务参数）。
- **重启设备**。

保存屏幕类型或服务参数后设备会自动重启；服务参数完整时才会开始轮询服务端。

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
| HINK 黑白红 | `bwr` | `epd-bwr-plane-v1` | `single` | 400 | 300 | 30000 |
| HINK 2.13" 黑白 | `bw` | `epd-bw-msb-v1` | `single` | 128 | 250 | 4000 |

帧数据布局：

- 彩色：2 bpp，每行 170 B（4 像素/字节），双面板。
- 单色：1 bpp，行优先，每行 170 B = 85 B（左面板）+ 85 B（右面板），共 480 行。
- HINK 黑白红：`[BW 15000][R 15000]`，各平面 1 bpp MSB-first（每行 50 B × 300 行）；BW：`0`=黑 / `1`=白；R：`1`=红（驱动写入时按厂商例程取反）。
- HINK 2.13" 黑白：1 bpp MSB-first，每行 16 B × 250 行 = 4000 B；`0`=黑 / `1`=白（可见宽 122，右侧 6 列对控制器无效）。

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
    io.{c,h}                 L0 总线（SPI bitbang / busy / reset）
    panel.{c,h}              L3 统一面板接口与注册表
    layout/
      dual_lr.{c,h}          L2 左右双拼几何与帧切分
      single.{c,h}           L2 单面板几何（待产品接入）
    ctrl/
      uc8579.{c,h}           L1 彩色主控族（含刷新序列）
      jd79686.{c,h}          L1 单色主控族（含局刷与刷新序列）
      ssd1619.{c,h}          L1 SSD1619 BWR 主控族（含刷新序列）
      il3895.{c,h}           L1 IL3895 BW 主控族（微雪 epd2in13 V1，含局刷）
    drivers/
      qyeg1085ryu579.{c,h}   L3 彩色产品（CONFIG_EPD_PANEL_QYEG1085_COLOR）
      qyeg1085bnf86bf0.{c,h} L3 单色产品（CONFIG_EPD_PANEL_QYEG1085_MONO）
      hink_e042a13_a0.{c,h}  L3 BWR 产品（CONFIG_EPD_PANEL_HINK_E042A13_A0）
      hink_e0213a04.{c,h}    L3 BW 产品（CONFIG_EPD_PANEL_HINK_E0213A04）
partitions.csv               分区表
platformio.ini               PlatformIO 构建配置
sdkconfig.defaults           ESP-IDF 默认配置
LICENSE                      MIT 许可证
```

## 添加一块新屏

按差异选择落点（能复用则不复制）：

1. **拓扑**：已有 `dual_lr` / `single` 则复用；否则新建 `epd/layout/<name>.{c,h}` 并加入 `CMakeLists.txt`。
2. **主控族**：已有 `uc8579` / `jd79686` / `ssd1619` 则复用；否则新建 `epd/ctrl/<family>.{c,h}`，刷新序列写在该族内部。
3. **产品**：新建 `src/epd/drivers/<name>.{c,h}`，填 cfg + `epd_panel_ops_t`（分辨率 / `color_mode` / `encoding` / `panel_layout`），`.c` 用 `#if defined(CONFIG_EPD_PANEL_<NAME>) && CONFIG_EPD_PANEL_<NAME>` 守卫。
4. `src/epd/panel.c` 的 `s_registry` 增加 `#if` 条件项。
5. `src/CMakeLists.txt` 的 `app_sources` 增加新产品（及新建的 layout/ctrl）源文件。
6. `platformio.ini` 的 `build_flags` 增加 `-DCONFIG_EPD_PANEL_<NAME>=1`，然后 `pio run`。

### 欢迎贡献

若你手上有其他型号的电子墨水屏资料或可验证硬件，欢迎 PR：优先贡献 layout/ctrl（若需要）+ 薄产品驱动 + 注册表条目 + README「已支持屏幕」表更新。

## 许可证

本项目采用 [MIT License](LICENSE) 发布。
