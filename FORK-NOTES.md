# FORK-NOTES.md — 本 fork 相对上游的改动

上游：<https://github.com/sotwat/openutau-logic-au>
本 fork：<https://github.com/irorange27/openutau-logic-au>
基点：上游 `a4d5db9`（2026-10-09）

---

## 改了什么（3 个 commit）

| commit | 主题 | 文件 | 规模 |
|---|---|---|---|
| `31042b1` | 声明 note 输入端口，让宿主送 MIDI | `src/plugin.cpp` | +61 −2 |
| `592c9b3` | macOS 上无条件走 relay（修沙箱路径） | `src/session.cpp` | +10 −5 |
| `d5849d4` | 捕获音符事件并上报 | `src/session.cpp`, `src/session.h` | +65 |

合计 **+136 −7**，只碰 3 个文件，不动音频渲染路径。

---

## ① `31042b1` — 声明 note 输入端口

**问题**：插件注册了 `audio_ports / params / state / render / gui`，但**没有** `note_ports`。
clap-wrapper 的 AUv2 `setupMIDIBusses` 读这个扩展来决定 AU 是否接受 MIDI ——
没声明 → 宿主认为插件不收音符 → **MIDI 根本不会送达**，`ApplyEvents` 永远看不到 note 事件。

**改动**：加 `CLAP_EXT_NOTE_PORTS`，一个输入端口。同时广告 `CLAP_NOTE_DIALECT_CLAP`
（首选）和 `CLAP_NOTE_DIALECT_MIDI` —— 能用 CLAP 原生音符的宿主会给出完整数据
（key / velocity / channel / 采样偏移），省掉自己解析 MIDI 1.0 running status。

**验证**：`nm` 看到 `kNotePorts` / `NotePortsGet` / `NotePortsCount` 三个符号。

---

## ② `592c9b3` — 修 relay 沙箱路径（**这个是连不上 OpenUtau 的真正原因**）

**问题**：Logic 把 AU 跑在 `AUHostingServiceXPC` 沙箱里，沙箱**改写了 `TMPDIR`**：

| 谁 | 解析出的目录 |
|---|---|
| AU（沙箱内） | `.../T/**AUHostingService**/OpenUtau/PluginServers` |
| OpenUtau（未沙箱） | `.../T/OpenUtau/PluginServers` |

两个目录**永远对不上** → 插件把"我在这里"写到了没人看的地方 → 桥接窗口一直 `Not connected`。

**为什么原代码没兜住**：relay 本来就是为了这种情况准备的，但**只在 `listener_.Start()` 失败时**才进入。
沙箱**允许** `listen()`，于是插件走了"直连"那条路 —— 在 macOS 上这条路不是更快，而是**看不见**。

**改动**：Apple 平台上**无条件**进入 relay 模式。relay 以登录代理身份跑在沙箱外，
所以它能写在 OpenUtau 会扫描的目录里，并转发给插件。

> ⚠️ 这一条**与上游的架构意图有冲突**：上游把 relay 当"兜底"，本 fork 把它当"唯一可行路径"。
> 如果将来上游修好了沙箱路径问题（比如用共享容器目录），这条应该回退。

---

## ③ `d5849d4` — 捕获音符事件

**改动**：在 `ApplyEvents` 里捕获音符。两种 dialect 都处理：
`CLAP_EVENT_NOTE_ON/OFF`（首选）和原始 `CLAP_EVENT_MIDI` 字节
（含 MIDI 1.0 惯例：velocity 0 的 note-on 即 note-off）。

**线程模型**（遵循本代码库已有的约定）：
`process()` 跑在音频线程，**日志是被禁止的**（`LogMessage` 会加锁 + 格式化）。
所以 `ObserveNote` 只做无锁 relaxed store + 一次 release 递增计数，
由 worker 线程在 `Run()` 里通过 `DrainNotes()` 取走并打印。
字段先写、计数后写；worker 只在看到包含该音符的计数后才读字段 —— 半个音符不会被上报。
每次 drain 打一行而不是每个音符一行，否则一个和弦小节就会刷爆宿主日志。

**注意**：**这个改动不会把音符转发给 OpenUtau。**
`PROTOCOL.md §6.3` 把那个方向标为 reserved（未实现），本 fork 也没做。
渲染路径完全没动。

---

## 验证（在本机 macOS 15.7.7 / arm64 / Logic 12.4 上实测）

```
$ auval -v aumu OuBg OuBr
[OpenUtau Bridge] info: note: off C4 vel=0.000 offset=0 (2 since last report)
AU VALIDATION SUCCEEDED.
```

**A/B 对照**（证明因果，不是巧合）：

| 版本 | note-ports 符号 | auval | 收到 note 日志 |
|---|---|---|---|
| 上游 `a4d5db9` | 0 个 | ✅ SUCCEEDED | ❌ 完全没有 |
| 本 fork | 3 个 | ✅ SUCCEEDED | ✅ 收到 C4 note-off |

两版 auval 都通过，但**只有声明了 note port 的那版收到 MIDI**。
所以「加 note port」确实是让宿主送 MIDI 进来的关键 —— 这是实测结论，不是推测。

`auval` 的 "Test MIDI" 阶段会真的送音符进插件，因此**可以完全绕开 Logic 的 GUI 来验证 MIDI 通路**。

---

## 怎么构建

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --target openutau-vst-bridge_auv2 -j 8
```

产物：`build/assets/OpenUtau Bridge.component`

安装（**必须重新签名**，否则 Logic 可能不加载）：

```sh
C="$HOME/Library/Audio/Plug-Ins/Components/OpenUtau Bridge.component"
rm -rf "$C"
cp -R "build/assets/OpenUtau Bridge.component" "$C"
codesign --force --deep --sign - "$C"
killall -9 AudioComponentRegistrar; sleep 4
auval -v aumu OuBg OuBr
```

---

## 已知的上游问题（本 fork 未修）

这两个是**发布包**的问题，不影响从源码构建：

1. **发布包里的 AU 缺 `AudioComponents` 键** → macOS 不注册该 AU → Logic 看不到。
   根因是打包时 `ditto` 拷了构建目录里**旧的 Info.plist**（二进制 Oct 9、plist Sep 26）。
2. **发布包里的 `OpenUtau.app` 签名不完整**（303 个文件只签了 24 个），
   导致 `install.sh` 自己的 `codesign --verify --strict` 必然失败，安装根本跑不起来。

详见本工作区 `ISSUES.md`。

---

## 下一步（尚未实现）

**把音符回传 OpenUtau** —— 这是最初的目标。通路已经验证，可以往上盖：

| 层 | 要做什么 |
|---|---|
| 协议 | `PROTOCOL.md §6.3` 已预留消息名（`midiNotes` / `recordMidi`），但**格式未定义** |
| AU 侧（C++） | 已有捕获；需要去重（Logic 每次播放/循环会重复送） + 量化 |
| relay | 转发新消息类型 |
| OpenUtau 侧（C#） | `DawManager` 接收 → 写回 USTX → 触发重渲染（**工作量最大**） |

**两个已知难点**：

1. **时序**：Logic 的 MIDI 是相对宿主的小节/拍，USTX 是绝对 tick + 自己的 tempo。
   协议明确把 tempo-map 双向同步列为 non-goal，所以两边速度不一致时音符会错位。
2. **去重**：播放时会反复送同样的音符，不能每次都往 USTX 写，否则会堆满重复音符。
   需要「按播放头位置去重」。

**歌词**：MIDI 音符表达不了歌词。已决定 DAW 侧默认用占位音节（如 "la"），回 OpenUtau 再填真词。

---

## 与上游同步

```sh
git fetch upstream
git rebase upstream/main     # 或 merge
```

因为改动集中在 3 个文件、且 ② 与上游架构意图有冲突，冲突面应该很小但**需要人工判断**。
