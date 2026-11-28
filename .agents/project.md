# 工程方向与编辑器

相关主题：[动画与 ControlRig](animation.md) · [Traversal 调试](traversal-debugging.md) · [GTA 参考](gta.md)。

## 工程方向与参考源码

- UE 5.8；Gameplay 核心用 C++，蓝图用于配置、装配、转发及明确保留的移动计算；暂不接脚本。
- 命名按职责区分：项目专有类型用 `IMG`；通用概念遵循 UE 类型前缀及同目录风格，不因定义在本工程就机械添加 `IMG`，例如步态枚举用 `EGait`，不用 `EIMGGait`。不沿用参考项目的专有名称或前缀，不为统一前缀批量重命名既有类型。
- `Source/ImagineGame` 为共享运行时，`Source/ImagineEditor` 为编辑器模块；`Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime` 放射击玩法派生规则并依赖 ImagineGame，共享层不反向依赖玩法。
- `ImagineGame` 与 `ImagineEditor` 按文件类型统一目录：`.h` 放 `Public`，`.cpp` 放 `Private`，配对文件保留相同相对路径；include 从所属模块 `Public` 根写起，不依赖 Private 同目录搜索或写 `Private/...`。内部算法和调试头也遵守此布局，跨模块导出仍按实际调用需求设置。
- 新规则先核对所有者、权限、复制与生命周期；共享机制扩展核心职责，玩法规则放对应 Game Feature。
- 跨工程 PA/PCA/物理材质对照、用户资料包、骨骼框架适配和统一脚本入口见 [PA/PCA 迁移流程](physics-asset-migration.md)；资产基线与Ragdoll运行时职责见 [Ragdoll](ragdoll.md)。
- Lyra 参考：`D:/Application/UnrealProjects/LyraStarterGame`（已核对本地源码）。核心源码保留本工程移动、Bot、Traversal 扩展；TopDownArena、LyraExtTool、自动化示例与发布脚本未迁入。文件对应不代表行为一致。
- GTA 参考当前位于 `C:/Application/gta5/src/dev_ng/game`，工程导航见 `C:/Application/gta5/AGENTS.md`；引擎侧 IK 在 `src/dev_ng/rage/base/src/crbody/iksolver.*`。该源码快照用于静态对照，发行版配置另行验证；本工程保留 UE/GAS/CMC 网络体系。
- UE 源码当前位于 `D:/Workspace/UnrealEngine/UE_5.8/Engine/Source`，`Engine/Build/Build.version` 为 5.8.4。GAS 在 `Engine/Plugins/Runtime/GameplayAbilities`，MotionWarping 在 `Engine/Plugins/Animation/MotionWarping`。当前工程有 Game/Editor Target 和编辑器 Validator/ContentValidationCommandlet；独立 Client/Server、渠道 Target 按打包需求设计。

## 编辑器资产搜索缓存诊断

- 已验证：`LogFileInfo` 来自引擎 `Engine/Plugins/Editor/AssetSearch/Source/Private/FileInfoDatabase.cpp`；`FFileInfoDatabase::Open` 以读写/可创建模式打开 `Saved/Search/FileInfo.db`，保存文件路径、修改时间与哈希。`AssetSearchManager.cpp::TryConnectToDatabase` 同时连接该库及 `AssetSearch.db`，失败后按至少30秒间隔重试；这属于编辑器资产搜索索引链。
- 排查入口：`LogSQLiteDatabase ... disk I/O error` 表示数据库打开阶段的I/O失败，仅凭该日志不能判定硬盘损坏或数据库损坏。先退出使用该工程的编辑器，再将 `Saved/Search` 整体改名备份，重启让搜索缓存重建；仍失败再核对目录写权限、其他进程占用及存储错误。不要在数据库打开期间单独移除日志文件，也不要为此删除整个Saved。
- 待验证：具体I/O根因须结合现场进程、文件访问和系统存储日志确认；缓存重建是否恢复须由实际重启结果判断。

## ImagineEditor 目录与接口

- Traversal 配置详情分组由 `{Public,Private}/Character/Traversal/IMGTraversalSettingsCustomization` 承接，在 ImagineEditor 模块启动/关闭时成对注册/注销四个配置结构体的 `IPropertyTypeCustomization`。UE 5.8 的 `FItemPropertyNode::InitChildNodes` 对普通结构体直接创建字段节点，不自动按 Category 建组；定制仅按字段 Category（支持 `|` 子组）组织原生 PropertyHandle 行，保留重置、多对象编辑和 EditCondition，不另存配置或重写运行时权限。配置职责及路径见 [Traversal](traversal.md#检测发起与选片)。
- ImagineEditor 遵循上面的文件类型布局，保留现有动画、验证、Traversal 等职责分类；不为头/源文件分离额外拆出 System/Style/Feedback/Rewind/Export 子目录。

| 目录（相对Source/ImagineEditor） | 职责 |
| --- | --- |
| `Public/*.h`、`Private/*.cpp` | 模块入口、ImagineEditorEngine、ImagineEditorStyle、ContextEffects Factory与AssetTypeActions。 |
| `Public/AnimationModifiers`、`Private/AnimationModifiers` | 动画修改器及FootstepUtils。 |
| `Public/Animation`、`Private/Animation` | 自定义 AnimGraph 节点的编辑器包装；运行时节点及 Proxy 归 ImagineGame/Animation。 |
| `Public/Validation`、`Private/Validation` | 资产验证器。 |
| `Public/Commandlets`、`Private/Commandlets` | ContentValidationCommandlet。 |
| `Private/Utilities` | 独立编辑器工具与控制台操作。 |
| `{Public,Private}/Character/Traversal` | Rewind轨道/回看接合及事件分类。 |
| `{Public,Private}/Character/Traversal/Animation` | Traversal 专用 AnimGraph 编辑器包装；运行时节点和接触算法在 ImagineGame 同路径。 |
| `{Public,Private}/Character/Traversal/Trace` | Trace分析存储及文本导出。 |
| `{Public,Private}/Character/Traversal/Widgets` | Details与Slate控件。 |

- 模块内头文件include从Public根写相对路径，不使用 `../` 穿越或依赖Private同目录头搜索。公开头直接包含的AssetTools/Slate/SlateCore/RewindDebuggerInterface/TraceServices列为Public依赖，TraceAnalysis及PropertyEditor实现依赖仍留Private。UBT自动发现子目录CPP，无需为搬迁增加IncludePaths。只改路径不改反射类型、模块名或 `/Script/ImagineEditor.*` 配置身份；生成头及UE_INLINE_GENERATED_CPP_BY_NAME继续使用原类型文件名。
- ImagineEditor 的加载类型继续为 `.uproject` 中的 Editor；因模块包含序列化到运行时 AnimBP 的 AnimGraph 编辑器节点，Build.cs 用 `OverridePackageType = PackageOverrideType.GameUncookedOnly` 标记脚本包。UBT 将覆盖类型传给 UHT，`EpicGames.UHT/Types/UhtModule.cs` 生成 PKG_UncookedOnly，避免纯 PKG_EditorOnly 包触发 `BlueprintEditorUtils.cpp::ValidateEditorOnlyNodes` 的运行时蓝图警告；该检查由 UK2Node 早期校验调用，不要求节点已连入执行链。模块仍不进入游戏运行时构建，运行时 FAnimNode 继续归 ImagineGame。此类 Build/UHT 包标记改动须正常重编译并重启编辑器验证，不能只依赖旧进程中的 Live Coding。
- 启动配置排查：两套清单的 BuildId 相同也不能证明 DLL 已同步。双击 `.uproject` 的普通编辑器启动加载 Development 产物 `UnrealEditor-ImagineGame.dll` / `UnrealEditor-ImagineEditor.dll`；VS 的 DebugGame Editor 加载带 `-Win64-DebugGame` 后缀的另一套产物，分别由对应 `.modules` 清单映射。只更新 DebugGame 不会同步 Development。新增节点在 VS 可见、普通启动缺失时，先对照 `Saved/Logs/ImagineGame.log` 的 Build Configuration、InternalLoadLibrary 路径及两套 DLL 时间；旧运行时 DLL 可导致 FStructProperty Unknown structure，旧编辑器 DLL 可导致节点类加载失败。此时应构建 ImagineGameEditor Win64 Development 并重启，不能将其误诊为类型重命名并添加 Redirect。加载缺失节点的会话中不要保存受影响 AnimBP，先恢复匹配二进制再回读资产；本地已确认过两套 DLL 时间不同引起的这一故障。
