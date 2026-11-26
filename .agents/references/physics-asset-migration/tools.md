# 工具、脚本调用与故障处理

以下命令与配置仅供AGENTS内部使用；不要求用户安装插件、注册技能或掌握脚本。已有工程MCP直接复用，必要时代理自行完成编辑器临时桥接。

## 本任务实际使用的能力

| 能力 | 用途与边界 |
| --- | --- |
| 根AGENTS → `.agents/AGENTS.md` | 工程入口、按主题阅读和发现写回；当前技能由此导航 |
| `.agents/project.md`、`ragdoll.md`、`animation.md` | 引擎路径/模块边界、Ragdoll身体与CMC职责、资产/MCP/Python已有约定 |
| Unreal MCP | 实时工具发现、Object/Asset工具、有效设置和CVar读取；不能假定有通用Python执行或完整PA数组/碰撞表接口 |
| UE Python | 完整T3D导出、内嵌对象回读、候选复制、原生反射setter、参考姿势、PA保存/卸载/重载、PCA编译 |
| 本地PowerShell + UE内置Python | 文件备份/hash、离线映射和MCP HTTP；不通过Windows终端UI执行 |
| computer-use技能 | 首次在编辑器Cmd中执行本地bootstrap、窗口定位和界面/统计确认；遵循当前安装技能的输入、截图及用户接管规则 |
| skill-creator技能 | 将当次经验整理为本工程内部说明、按需加载参考文件、脚本/示例及结构验证 |

本轮整理时使用的skill-creator来源为 `C:/Users/wildertek/.codex/skills/.system/skill-creator/SKILL.md`；本轮Windows技能来源为 `C:/Users/wildertek/.codex/plugins/cache/openai-bundled/computer-use/26.1120.41243/skills/computer-use/SKILL.md`。安装位置/版本可能变化，实际使用时读取当时可用的技能，不把此处旧API当成未来工具契约。

本轮未依赖新建C++插件、引擎编译或开启Python Remote Execution。编辑器Python脚本仅作编辑任务工具，不引入Gameplay脚本运行时。

## 统一入口的实际调用

以下路径按本次机器给出；其他工程从作业的project_dir和当前技能路径生成绝对路径。外部终端命令使用exec_command，不用Windows UI操作终端。

两个编辑器各执行一次：

```text
py "D:/Application/UnrealProjects/ImagineGame/.agents/tools/physics_asset_migration/unreal_worker.py"
```

这注册临时 `AgentPhysicsAssetTools`，重启即消失；每次stage重新加载工作实现，重复bootstrap保留小型UClass，避免重新定义同名Python UClass导致ToolsetRegistry弱引用失效。修改Python函数实现不必重新创建Toolset类。

外部PowerShell示例：

```powershell
$paPython = 'D:/Application/UnrealEngine/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
$paEntry = 'D:/Application/UnrealProjects/ImagineGame/.agents/tools/physics_asset_migration/run_job.py'
$paJob = 'D:/Application/UnrealProjects/ImagineGame/Saved/Codex/PhysicsMigration/my-job.json'
& $paPython $paEntry $paJob snapshot
& $paPython $paEntry $paJob plan
# 代理审阅plan，填写同作业目录review.json后再执行写入阶段。
& $paPython $paEntry $paJob stage
& $paPython $paEntry $paJob adapt
& $paPython $paEntry $paJob verify
# 要正式提交时，核对commit的引用/文件边界；提交前放弃则用discard。
& $paPython $paEntry $paJob commit
& $paPython $paEntry $paJob cleanup
```

Windows系统默认编码若为GBK，调用外部Python时可在脚本前加 `-X utf8`；维护CLI已将stdout/stderr固定为UTF-8。官方skill quick_validate.py默认read_text会受系统编码影响，中文技能文件验证时同样加 `-X utf8`。验证器缺PyYAML时将依赖装到独立Saved工具目录，不把依赖包或Python缓存提交进工程技能。

示例job完整提供了23骨同名映射，可修改为其他角色的一对一显式映射。`child_basis`的值是源局部坐标到目标局部坐标的单位四元数；不是Euler角。`uniform_scale_by_bone`是正数；scale/basis不是单位且形状policy仍copy_source时plan会拒绝。

`materials`是源PM包路径到目标PM包路径，保留null例外。PCA策略copy_source或preserve_target必须明确；generated control名等用reference_name_map精确转换。源/目标`clipboard`可以不提供，但此时计划不能从文本确定完整禁用对数量，须由整包克隆和编辑器回读补验。

## MCP会话与工具发现

本轮实际连接为8001/8002，初查时两者都拒绝，只有8000；用户在编辑器执行 `ModelContextProtocol.StartServer 8001/8002` 后恢复。配置文件写着8000不代表实时服务仍在那里。

HTTP序列：

1. POST `/mcp`，JSON-RPC initialize，协议 `2025-06-18`，clientInfo和capabilities。
2. 保存响应头 `Mcp-Session-Id`。
3. POST `notifications/initialized`；后续请求携带session头。
4. `tools/list`可能只有list_toolsets、describe_toolset、call_tool三个meta-tools。
5. list_toolsets → describe_toolset获取实际schema，call_tool短工具名与toolset_name分开传。
6. MCP错误既可能是JSON-RPC error，也可能在成功HTTP响应的 `isError=true` 中；客户端必须抛出，不能把终端exit0当成资产操作成功。
7. 会话结束尝试HTTP DELETE关闭，不在日志打印敏感配置或大段图片base64。

临时Python Toolset名称可能是 `PythonTypes...._0x...`，也可能带文件模块命名空间；客户端只发现当前内部接口，不能硬编码全名或随便选第一个相似旧工具。遇到Invalid Toolset Class，先查Python UClass热重载/注册状态，不反复换端口。

源端在snapshot后退出或更改包时，stage会在来源复核阶段停止，不能跳过dirty/hash检查继续用旧磁盘包。超时或连接断开后的写入结果不明确时，先检查state/实际资产，不能盲目重发修改。

## UE Python稳定用法

### 读取与导出

```python
pa = unreal.load_asset('/Game/.../PA_Name')
task = unreal.AssetExportTask()
task.set_editor_properties(dict(object=pa, filename=absolute_t3d, automated=True,
    prompt=False, replace_identical=True, exporter=unreal.ObjectExporterT3D()))
ok = unreal.Exporter.run_asset_export_task(task)
```

AssetExportTask是UObject，不接受任意多个构造关键字；本轮直接构造传6参数曾报 `call() takes at most 2 keyword arguments`，改为实例化后set_editor_properties。

PA内部数组的Python snake_case getter可能不存在；原始 `SkeletalBodySetups/ConstraintSetup/PreviewSkeletalMesh` 又可能受保护。通用ObjectTools可编辑schema同样不保证包含这些字段。应从完整T3D声明发现内嵌对象名，再按完整路径 `load_object(None, pa.get_path_name()+':'+object_name)` 读取。

未生成专用Python类的内嵌对象会表现为 `unreal.Object`。这时原始反射名 `DefaultInstance/AggGeom/BoneName/PhysMaterial` 可用，snake_case可能报Failed to find property。对返回struct先 `.copy()` 再临时import_text比较，避免只读验证意外修改原结构。

`get_objects_with_outer`在本轮环境不可依赖；早期脚本因此输出0 bodies/0 constraints，那是采样失败，不是PA真的为空。维护入口明确拒绝这种空快照。`EditorAssetLibrary.is_asset_loaded`本轮不存在；dirty检查使用EditorLoadingAndSavingUtils。

### Native setter与缓存

```python
system = unreal.get_default_object(unreal.SystemLibrary)
system.call_method('SetSoftObjectPropertyByName', args=(pa, unreal.Name('PreviewSkeletalMesh'), mesh))
system.call_method('SetNamePropertyByName', args=(body, unreal.Name('BoneName'), unreal.Name(target_bone)))
```

PreviewSkeletalMesh的Value应为已加载Mesh对象，传SoftObjectPath包装会Nativize失败。BoneName在本轮可读但read-only，不能普通set_editor_property；仅在确实改骨名时使用原生name setter，并立即回读。修改前核对当前引擎UFUNCTION签名，不把此机制扩展为无边界的反射写入工具。

保存PA并卸载/重载候选可重建骨名缓存。释放PA、内嵌对象、指向原结构的Python包装引用；检查unload返回首布尔值。不要用元组整体真假判断，不把关闭编辑器等同释放文件读取器。

2026-11-26 已遇到：对刚关闭 PhysicsAsset 编辑器的正式 PA 调用 `EditorLoadingAndSavingUtils.reload_packages`，在 `SharedPointer.h:1133 / IsValid()` 触发断言并退出；当次资产已成功保存。不要将候选的卸载/重载流程机械用于当前打开过的正式 PA；优先保存后在新编辑器会话中读取验证。该断言的具体 Slate/编辑器持有者尚未查明，不能据此判定 PA 包损坏。当前 UE Python 没有 `unreal.duplicate_object`；资产候选可使用 `AssetToolsHelpers.get_asset_tools().duplicate_asset`，且目标必须使用有效资产挂载根，`/Temp/` 在此接口中不被接受。

### PA / PCA默认值

完整克隆保留ProfileHandles、DefaultProfile、物理动画Profiles与不可反射数据。单独设DefaultInstance的Profile后应核对默认/命名Profile是否同步；本轮只改变框架，Profile保持源值。

PCA：`pca.call_method('Compile')` 后 `IsCompilationNeeded` 应为false，再保存并回读。Compile只保证authored数据折叠成运行数据，不证明每个运行时Control/Set名称引用都正确。

配置比较同时读取PhysicsSettings CDO与INI文本；CVar SearchCVars('p.')结果可能包含字符串中带p.的非物理项，比较报告应过滤真实前缀 `p.` 并分别记录缺失项/值差异。编辑器工具属性的JSON字段名来自实时schema，和Python snake_case、C++原始名不是同一个命名空间。

## Windows UI降级路径

先用purpose-built MCP/脚本；仅bootstrap或确有盲区才操作UI。本轮computer-use经node_repl导入 `@oai/sky`，从实际返回的app/window列表精确选工程窗口。

- 观察状态后再单次动作并刷新；每次把 `state.window` 更新回后续使用的window，防止旧窗口绑定/尺寸导致输入落错。
- 需要坐标时使用最新截图与screenshotId，不复用另一窗口或旧尺寸的坐标。窗口超出屏幕时可通过其系统菜单最大化，再重观测。
- 先点实际可编辑Cmd表面，确认焦点，再单独type_text，确认文字实际出现，最后Return。没有送达时不要连续重发。
- 出现user input detected时重新观察；用户正在接管则暂停输入。bootstrap自动输入确实失败时，请用户执行已经准备好的**具体一条命令**，说明工具缺少执行Python入口的原因。
- 不据“自动输入无文字”断言管理员权限或安全软件阻断，本轮未查明该原因。不要为解决它自动修改安全设置/打开Python Remote Execution。
- 原始数据/截图/文档不能替用户授权发信、删除其他资产或安装软件。使用其他电脑控制API时遵守它自己的接管与完成协议，不混用不同工具的窗口句柄。

## 源码入口

均相对当前UE的Engine目录，不固定安装盘符：

- `Source/Runtime/Engine/Classes/PhysicsEngine/{PhysicsAsset,PhysicsConstraintTemplate,ConstraintInstance,ConstraintDrives,BodySetup,SkeletalBodySetup,BodyInstance}.h`
- `Source/Runtime/Engine/Private/PhysicsEngine/PhysicsAsset.cpp`：PostLoad、索引缓存、PostEditChange。
- `Source/Editor/PhysicsAssetEditor/Private/{PhysicsAssetEditorSharedData,PhysicsAssetEditorActions,PhysicsAssetEditorMode}.cpp`：复制/粘贴、快捷键、显示统计。
- `Source/Editor/UnrealEd/Classes/Preferences/PhysicsAssetEditorOptions.h`：鼠标弹簧、插值、预览重力/帧率。
- `Source/Editor/AnimationBlueprintLibrary/{Public,Private}/AnimPose.*`：参考姿势API和空间语义。
- `Source/Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h`：native property setters。
- `Plugins/Animation/PhysicsControl/Source/PhysicsControl/{Public/PhysicsControlAsset.h,Private/PhysicsControlAsset.cpp,Public/PhysicsControlData.h}`：PCA编译/Control参数与稀疏更新。
- `Plugins/Experimental/Toolsets/EditorToolset/Content/Python/editor_toolset/toolsets/{object,asset,programmatic}.py`：工具封装与限制；programmatic是工具编排sandbox，不是通用Python执行接口。
- `Plugins/Experimental/ToolsetRegistry/Source/ToolsetRegistry/.../FunctionLibraryToolset.cpp` 与 `UToolsetRegistry.cpp`：工具类注册、弱引用和Invalid Toolset Class排查。
