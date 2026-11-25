# 迁移流程与单一脚本入口

全部配置、审阅和命令由AGENTS内部完成，不交给用户填写或执行。

## 作业文件和阶段

AGENTS从 [job.example.json](../../tools/physics_asset_migration/job.example.json) 复制到本次目标工程的Saved，修改为新的job_id、工程、资产、端口和映射。示例默认 `allow_target_writes=false`、保留目标项目PhysicsSettings，不会因照抄示例立即覆盖资产。

`scripts/run_job.py <job.json> <stage>` 是统一入口。MCP只连接localhost，编辑器内工作由 `unreal_worker.py` 完成；[工具操作](tools.md)说明一次性bootstrap。脚本不自动启动模拟，不执行C++编译或测试。

| stage | 输入 / 作用 | 写入范围 |
| --- | --- | --- |
| snapshot | 源/目标身份校验、PA/PCA/PM/参考姿势/物理与预览设置、p.*快照 | 仅作业Saved证据；不保存资产 |
| plan | 批量生成拓扑/BodySetup/材质/PCA/配置/CVar差异，检查映射和局部轴，计算目标框架与碰撞对 | comparison.json、plan.json，离线执行 |
| stage | 再核对源dirty/磁盘hash，备份目标，暂存源包并克隆候选PA/材质 | 本作业拥有的临时Content、新材质、Saved备份 |
| adapt | 根据已审阅计划更新候选骨名、材质例外、关节框架和选定形状变换 | 仅候选PA，随后保存/卸载/重载 |
| verify | 对实际候选或最终资产重新导出，比较BodyInstance/形状/Profile/框架/锚点、编译状态 | Saved回读报告 |
| commit | 核对候选hash、目标dirty/hash/referencers后替换PA，按策略改PCA及指定物理配置 | 正式目标PA/PCA及明确授权的Config |
| cleanup | 成功提交后按拥有记录和引用关系清理临时资产 | 仅无人引用的本次临时Content |
| discard | 正式替换开始前放弃候选，依拥有记录清理 | 不恢复或覆盖正式资产；保留Saved证据 |

没有把所有阶段连续执行的all命令。阶段边界是技术检查点，不代表每一步都要再次请求用户许可；现有任务授权覆盖的操作继续执行。

## 1. 身份、源材料和授权

先读工程约定及本技能。收集[资料包](input-packet.md)，从工具实际返回的项目路径与用户指定路径相互核实。8001/8002只是本轮端口，不能写死为所有项目身份。

明确哪个PA是源、哪个是目标、哪个PCA/运行组件实际消费目标PA。记录当前Mesh的PhysicsAsset默认引用、PCA的PhysicsAsset、组件PhysicsAssetOverride，不能只看资产编辑器标签。游戏逻辑中的覆盖需要沿实际蓝图/C++调用链核对，本脚本不自动改角色装配。

本轮源/目标PCA authored参数相同，差异只是目标仍引用旧PA；不要因任务提到PCA就随意修改已有正确控制参数。

## 2. 实时与磁盘备份

记录git状态和目标资产是否原本untracked。备份PA、PCA、将要修改的已有PM和DefaultEngine.ini；已有其他业务引用也要记录。原文件hash在提交前再次核对。

dirty资产有两种有效处理：用户明确保存其当前版本后再备份，或另存实时导出/候选并保留未保存内容。不能用旧磁盘备份假称已经保护了用户实时修改。通用脚本对源PA/PM和目标写入对象的dirty状态采取停止策略，由代理先解决具体内容。

Saved证据按 `Saved/Codex/PhysicsMigration/<job_id>/` 隔离。不要复用上次已staged/committed的job_id，不覆盖原始source/target快照。磁盘包的`.uexp/.ubulk`若存在也属于备份；自动正式替换目前拒绝带sidecar的PA包。

## 3. 批量对照清单

| 层 | 核对字段 / 关系 |
| --- | --- |
| 结构 | body骨名、独立形状数量/类型、约束两端、交叉约束、孤立体、碰撞禁用对和可碰撞数量 |
| BodySetup | AggGeom及内部变换/尺寸、PhysicsType、CollisionTraceFlag、骨名、边界/物理动画Profiles等完整序列化数据 |
| BodyInstance | MassOverride启用位和数值、MassScale、惯性/质心覆盖、线/角阻尼、CCD/全局DisableCCD、重力、通知、碰撞Profile/响应、求解迭代覆盖、睡眠/唤醒阈值和运动锁 |
| 材质 | BodySetup.PhysMaterial、BodyInstance.PhysMaterialOverride、null/默认材质、静/动摩擦、Combine及覆盖位、反弹、密度、RaiseMassToPower、睡眠阈值 |
| 关节 | Pos/PriAxis/SecAxis两框架、AngularRotationOffset、线/锥/扭转运动模式和限值、软限位/刚度/阻尼/反弹、驱动模式与各启用位/目标/MaxForce、AccelerationMode、投影、质量条件化、ParentDominates、ShockPropagation、可断裂/塑性、DefaultProfile/ProfileHandles |
| PA级 | SolverType、SolverSettings与旧SolverIterations的区别、Profiles名称列表、PreviewMesh、专服/Bounds/用户数据和CollisionDisableTable |
| PCA | 父/附加资产、LimbSetup、默认Controls/Modifiers、AdditionalSets、InitialUpdates、Profiles及每个稀疏更新的bEnable...掩码、编译数据是否过期 |
| 项目 | 真实PhysicsSettings CDO、INI层级、物理相关CVar、TickPhysicsAsync/子步进/最大dt/最大子步数、重力、World solver设置 |
| 预览 | GravityScale/Override、PhysicsBlend、UpdateJointsFromAnimation、FloorCollision、MaxFPS、鼠标弹簧和插值、动画/后处理启用状态 |

`ObjectExporterT3D`包含完整反射结构但使用省略默认值的文本形式；比较前要结合CDO/实际完整Struct.export_text回读。字段缺失不等于0或false。完整二进制克隆还能保留不可反射的碰撞表与未逐项暴露的数据。

## 4. 建立映射计划与技术审阅记录

按[映射规则](mapping.md)填写bone_map、child_basis、形状策略和PM路径。不要把骨架参考姿势差异当作轴向差异重复修正。plan会拒绝缺失/合并映射、非单位参考scale、未指定形状策略、需要旋转却仍选择不变几何等情况。

计划输出job/两快照hash、每个约束的新旧位置、轴向、源初始锚点误差、直接套用时的目标误差、映射后的禁用碰撞对与预期统计。逐项审阅后，在同一作业目录写 `review.json`：

```json
{
  "reviewed_by": "本次实际审阅者或代理",
  "reviewed_plan_sha256": "run_job plan输出的真实SHA256",
  "decisions": {
    "bone_map": true,
    "reference_pose": true,
    "geometry": true,
    "materials": true,
    "pca": true,
    "config": true
  },
  "source_anchor_warnings_reviewed": false,
  "notes": "写明非单位basis、形状拟合、PCA额外命名和配置差异的具体决定"
}
```

这是防止错用旧计划的技术记录，不是绕过用户授权的批准凭证。作业JSON的允许写入范围仍必须来自当前用户任务。改映射/端口/策略导致job hash变化后重建计划和审阅记录。

## 5. 候选资产

候选路径由脚本固定为 `/Game/__CodexPhysicsMigration/<job_id>/<目标PA文件名>`，与最终PA使用同一basename。源包暂存保留原包路径，因其内部PM引用依赖原路径；目标已存在同路径且hash不同时停止，不覆盖来源冲突资产。通用版本仅自动处理项目`/Game/`包和同引擎`/Engine/`材质，插件内容另做有依据的迁移。

从完整源PA复制，保留body顺序和全部约束/碰撞禁用表，再用映射修改骨名和框架。不能从旧目标PA局部粘贴后遗留多余交叉约束。保持源材质null例外，显式PM映射到目标PM，不把所有体统一赋同一个高摩擦材质。

snapshot还记录PA依赖；超出源Mesh、已知PM、Engine/Script范围的额外数据或插件依赖会要求专用适配，不能在缺失引用/类的情况下仅凭体数正确就宣告完整迁移。

候选修改基于原始source快照重建字段，可重复adapt而不累计旋转。保存并重载后再验证。部分阶段失败会留下state.json与候选，不能以“有文件”当作阶段完成。

## 6. 回读与落盘

verify重新读取真实资产：计数与对象集合、映射骨名、完整BodyInstance、形状、Profile、锚点与轴、PA solver、材质、PCA编译状态。复制保留CollisionDisableTable仍应使用编辑器统计/完整复制文本复核映射后的碰撞对；仅有预期组合公式不等于读到了实际表。

当前自动commit的明确范围：已有目标PA、干净、文件hash仍等于备份、没有referencer、PA包无sidecar、引擎版本相同。常见目标已被PCA/Mesh/蓝图引用时，脚本会拒绝文件替换；代理应设计编辑器内原位复制/受控引用替换及恢复步骤，不能删掉引用来满足脚本条件。

所有资产写入/清理阶段都要求先结束Play/Simulate-in-Editor；运行实例可能持有AssetRegistry里看不到的临时引用，不能只检查磁盘referencer就覆盖正在使用的包。

只关闭资产编辑器可能仍有UE读取器占用uasset。自动路径会释放Python对象引用，检查unload结果首布尔值后才复制候选。新包加载和约束数量检查失败则尝试先卸载失败包再恢复PA原件。恢复失败要报告路径和阶段，保留备份，不继续写PCA/Config。

PA、PCA、Config的提交分阶段记录 `replacing_pa/pa_committed/committed`。PCA或Config阶段失败不声称PA也自动回滚；依据原备份和state恢复。这类跨资产落盘不具有单一ScopedEditorTransaction的原子性。

PCA copy_source策略会转换已知引用字段，更新PhysicsAsset、Compile并回读；preserve_target只更新PA引用和编译。父/附加PCA、任意自定义额外控制字典键与生成控制名，必须提前完成明确映射。

PhysicsSettings自动路径只支持六个子步进/异步时间相关键，不覆盖CollisionProfile或整个配置。其他字段差异由代理明确处理。只有 `allow_project_physics_settings=true` 且政策copy_source时才写INI和CDO。当前运行World/PreviewScene可能已缓存旧设置，重开预览或重启后核对生效范围；CDO相等不等于所有既存Scene都已重建。

## 7. 清理、恢复与验收

- cleanup按state中的created列表和实际引用拓扑逐步删除本次临时资产，保留正式PA/PCA/PM。存在外部referencer时停止并列出，不强删。
- 提交前需要放弃候选使用discard。正式替换已开始后不允许discard伪装恢复，依据备份逐项处理。
- 恢复已有正式资产要先关闭对应编辑器、处理dirty与引用/文件占用，核对备份hash，恢复PA/PCA/Config的匹配组合并重新加载、Compile、回读。不要在持有失败包文件句柄时直接copy回去。
- 静态/回读检查通过后请用户按[验收表](diagnostics.md)做模拟。用户若反馈良好，记录实际范围；不要继续为“完全一样”无依据地改动已通过的参数。
- 主责文档保留最终资产基线和本技能链接，Saved保留本轮证据；源工程原资产不因目标任务被修改。
