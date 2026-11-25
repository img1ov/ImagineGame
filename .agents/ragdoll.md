# Ragdoll 移动与 GA 接入

相关主题：[普通移动](locomotion.md) · [Gameplay 框架](gameplay.md) · [镜头](camera.md)。

## Ragdoll 移动基底

### 分层与入口

- 源码位于 `Source/ImagineGame/{Public,Private}/Character/Ragdoll`，包含 IMGRagdollComponent、IMGCustomMovementMode_Ragdoll、IMGRagdollTypes 三组文件，属于 ImagineGame。
- 依赖方向：GA → 可选协调组件 → CMC，由 CMC 分派自身 Mode。Component 管请求、只读采样、事件和绑定清理；CMC 管单一移动快照、装配及网络；Mode 管胶囊跟随、地面抬升、限制、碰撞/朝向接管和恢复落位。AIMGCharacter 不默认持有协调组件，CMC/Mode 不查找或引用它。
- 胶囊不模拟物理。协调组件不操作 Mesh 物理、碰撞、附着、快照、AnimInstance 或 Montage；身体物理与动画由独立适配层负责。移动层不依赖 GA/MM/PAC，不为其增加 Mover、PhysicsControl、Chooser/PoseSearch 或 GAS 依赖。
- 权威入口：`StartRagdoll` → `BeginRecovery(Session)` → `CompleteRecovery(Session)`。选片可先 `QueryRecovery(Session, Transform, Issue, BodyLocalOffset)`，再 `BeginRecoveryAt(Session, Transform, BodyLocalOffset)`；调用者显式完成/中断，无自动起身计时器。
- 无协调组件时直接使用 CMC.StartRagdollMovement、RecoverRagdollMovement/RecoverRagdollMovementAt、CompleteRagdollRecovery、StopRagdollMovement。持续 SubmitRagdollFollowTarget 提交世界空间 Transform、正反面、有效 InputSource、原 Session；Submit/Clear 均要求 Session，同一来源未 Clear 前其他来源不能覆盖。
- OnRagdollStarted、OnRecoveryStarted、OnRagdollEnded 携带 Session，CMC 提供 OnRagdollMovementUpdated。Phase/Session/Revision 只承担移动配置、跟随会话与恢复输入锁，不承担动画或 GA 业务状态机。

### 身体参照与跟随

- 默认位置为已模拟 `pelvis` 的骨骼世界原点，按骨骼索引读取；不是 Mesh 原点、胶囊中心或刚体质心。`spine_05` 辅助平面朝向与正反面，FaceUpAxis 为该骨骼胸口向外的局部轴，默认 Y；缺失朝向骨骼不停止 pelvis 跟随，朝向回退角色朝向、正反面默认 false。
- 无有效骨骼变换或模拟 pelvis 时返回当前胶囊目标；此回退不证明身体跟随生效。可覆写 GetRagdollFollowTarget，返回 false 清本组件输入。PelvisBone、OrientationBone、FaceUpAxis 由组件复制，骨骼名与轴须按资产配置。
- 协调器在 TG_PostPhysics、CMC 和缓存 Mesh 后采样；UE EndPhysics 包含骨骼物理混合及其并行完成任务，常规 PrePhysics CMC 下一次移动消费缓存，存在正常采样延迟。默认读最终混合姿势，权重小于1时不等于纯物理体变换；身体适配负责根部混合、附着和世界空间一致性，或覆写采样。
- 击飞链：权威 Start/开始事件 → 身体适配启用模拟并施加初速度/冲量 → 采样 → CMC 输入 → Mode 跟随。AdditionalVelocity 只合入 InitialVelocity，Move 不替身体施加冲量或模拟重力；仅 Start 不会产生击飞。无 Controller 遵循 bRunPhysicsWithNoController。
- 跟随不做完整胶囊扫掠，身体提供者负责接触；空中跟随目标，近地面按当前胶囊尺寸抬升。默认 MaxFollowSpeed=4000 cm/s、MaxTargetDistance=500 cm，bValidateTargetPath 开启旧中心到跟随位置的保守阻挡线检查，使用进入前胶囊响应。
- 目标失效、超距或阻挡清速度并报告原因，保留会话由上层处置；该检查不能证明完整身体碰撞或反作弊。高速击飞须按玩法配置限制。
- GetRagdollBodyMotion 为可覆写只读入口，返回模拟 pelvis 线速度 cm/s、角速度 rad/s；无有效身体返回 false并清零。身体稳定阈值/持续时长归 GA；MaxRecoverySpeed 只限制胶囊速度。

### 恢复几何与安全退出

- QueryRagdollRecovery 只查询，禁止重放和模拟代理执行。RecoverRagdollMovementAt 由权威复核 Session、有效且未过期的缓存目标、最终变换及量化后落位；拒绝前不改变阶段、输入或碰撞，BeginRecovery 复用该链。
- 原位优先，再搜邻域：默认半径100 cm、2圈、每圈8点，上限4圈/16点，半径0仅原位。检查可走支撑、IsWithinEdgeTolerance、当前完整胶囊、范围、平面约束及通路；下扫从低处渐进抬升，FloorClearance 限于2 cm至 CMC.MAX_FLOOR_DIST-0.1 cm。继承 CMC 移动忽略对象，使用进入前胶囊响应；无查询碰撞的胶囊拒绝开始。
- 恢复检查“采样身体点 → 匹配身体点 → 最终胶囊中心”两段 LOS；起点胶囊无重叠时另做完整胶囊通路扫掠，初始重叠时允许沿身体通路作有界局部脱离。
- BodyLocalOffset 为匹配的跟随点在最终单位缩放胶囊空间中的位置，默认零；须有限且不超过 MaxTargetDistance，由查询、批准及量化复核共同消费。它只作几何输入，不引入复制状态/动画依赖，也不证明完整起身动画手脚空间或深穿透脱困。
- 恢复批准后退出 Custom Mode，清 RequestedVelocity，限制普通移动输入、速度、跳跃、主动姿态切换及 Controller 朝向，允许正常 CMC Root Motion；恢复事件等待 Walking/落位就绪。
- 普通 AbortRagdoll(Session)/StopRagdollMovement 找不到安全出口返回 false并保留会话；成功经同一快照传退出落位，空中退出保留速度。缺少有效身体目标时只允许从无重叠胶囊沿完整胶囊通路退出，普通恢复仍拒绝缺失/过期/错会话/超距目标。
- AbortRagdoll(Session, true) 按 Unavailable 强制结束；无安全位置则 MOVE_None/禁碰撞并报告 ExitBlocked。所有者须安排救援、重生或明确恢复模式/碰撞，不自动 Walking；死亡和外部模式接管不被普通退出覆盖。

### 网络与装配生命周期

- 开始、恢复、完成由服务端批准；自主代理批准后预测跟随，服务端复核 Session、有限数值、距离、步长速度及恢复几何。AI/无自主代理需服务端身体或自定义目标；模拟代理只消费权威胶囊位置、速度、朝向和模式，不以本机身体反向驱动。
- 量化目标、朝向、正反面、Ready/Missing/Expired 和 Session 随 SavedMove/Packed MoveData 保存，非零 Session 的采样不合并；重放只消费历史输入，不采样身体或按当前时间判过期。坐标遵循 UE 零原点约定；远端自主代理禁用 Packed Movement RPC 时拒绝开始。
- MaxTargetAge 默认0.25秒；实时 GetRagdollMovementInput 复核采样/收包时效，远端静默在既有 CMC Tick 清速度并刷新复制。时间仅是本地缓存，查询不读取新姿势，不新增权威状态。
- CMC 属性与 MoveResponse 共用一个权威快照，Revision 拒绝旧/重复版本；配置随快照传输，不依赖客户端 CDO 一致。不回滚/复制逐骨骼物理，序列化布局变化要求各端同版。
- 快照与模式可乱序：结束通知等待退出 Ragdoll，恢复/立即完成或中断保留批准落位；自主代理即使只收到带落位的结束快照也补应用一次。同会话事件去重，旧会话回调不能影响新会话。
- ServerMoveHandleClientError 在原生应答生成时缓存时间戳/落位会话，MoveResponse 的 bRagdollPlacementReflected 只表达相关性；客户端仅在同会话 ACK/纠偏被接受、LastAckedMove 推进时标记落位已消费。忽略纠偏、关闭误差检查的 GoodMove 不作位置证明，避免生成前/打包后的错位及旧锚点回拉。
- Character.OnRep_ReplicatedMovement、CMC.SimulatedTick 仅在 Ragdoll 接管胶囊通道，旧 Ragdoll 模式应答也禁用表现 Root Motion 纠偏替代；退出恢复原生路径。Movement.Mode.Custom.Ragdoll 沿角色 Tag 映射。
- 服务端动态创建/注册可复制组件，客户端等待复制；同角色只绑定一个协调器。FindRagdollComponent 返回已注册、活动且已绑定 CMC 的组件，晚添加/重新激活/注册从当前快照补齐。
- 卸载协调器先退订、移除精确缓存的 Tick 依赖，保留目标供自己发起的会话安全退出，再按原 Session 清自己的来源；不终止其他调用者会话。替换/独立重新注册 CMC 后须重新激活或注册协调器，不用 Tick 发现新 CMC。
- CMC.Deactivate/OnUnregister/EndPlay 释放模式资源、输入与应答缓存，清理回调期间禁止 Start；晚快照可缓存，结束生命周期后不重新接管朝向。事件在移动模式和角色更新后发送，回调重入后复核对象/会话/模式；MOVE_None 保留死亡禁碰撞，Traversal 清理不覆盖后来的 Ragdoll/None。

### GTA 映射、调试与后续边界

GTA 主链为 `task/Physics/TaskNM.{h,cpp}`、`TaskRageRagdoll.cpp`、`TaskBlendFromNM.cpp` → `task/Movement/TaskGetUp.cpp`；保留其分层思路，网络合同映射到 UE。

| GTA 参照 | 本工程归属 |
| --- | --- |
| TaskNM.cpp:2808 身体线/角速度、连续稳定时长 | GA 等待稳定；Component 只提供事实 |
| TaskGetUp.cpp:1583 姿态匹配 | MM/Pose Matching 选择适配 |
| TaskGetUp.cpp:3944/4166/4193/4203 落位、邻域、两段 LOS | Mode 恢复几何与权威最终复核 |
| TaskGetUp.cpp:1641/4487/4494/4652 受阻等待、重试、爬行、救援 | 上层业务，不放进移动计时器 |
| TaskGetUp.cpp:3491 Finish_OnUpdate 克隆等待所有者 | Session/Revision、模式就绪与结束通知；保留 CMC 网络协议 |

- 身体参照：`peds/Ped.cpp`、`physics/gtaInst.cpp`；网络参照：`network/Objects/Prediction/NetBlenderPed.cpp`；调试参照：`ai/debug/types/RagdollDebugInfo.cpp`。
- 尚待独立实施：GA 业务、PAC/PhysicsControl 启停、MM/动画适配、进入预测与拒绝回滚、身体整体网络纠偏、速度/关节/姿态异常约束、深穿透脱困、载体相对事实/恢复锚点及跨角色预算/降级。GTA 的 FixStuckInGeometry、UpdateRagdollBlending、OnMovingGround、池化/LOD 按相应职责参考，不能据此断言 Chaos/资产没有同类能力。
- GetRagdollMovementIssue 是本机诊断。`IMG.Ragdoll.DebugDraw`：0关、1跟随/失败目标、2增加候选胶囊/支撑/两段通路；青点为采样点，黄点为匹配点。`IMG.Ragdoll.DebugDuration` 默认2秒，仅影响显式查询，跟随单帧；绘图受 ENABLE_DRAW_DEBUG 保护，CVar 为 Cheat并默认关闭。
- `Log LogIMG Verbose` 查看 [Ragdoll] 状态、原因及 ExitRejected，按角色/Role/Session/Revision 对照，结束用 `Log LogIMG Log` 恢复。

## PA / PhysicsControl 资产基线

完整复用流程、用户资料包、不同骨架映射公式、统一脚本入口、工具限制及本轮全部参数/原脚本归档见 [PA/PCA 迁移流程](physics-asset-migration.md)。下次迁移由AGENTS先读该入口，不直接重跑Saved中的历史硬编码脚本。

- 2026-11-25 已从 `D:/Application/UnrealProjects/GameAnimationSanple/Content/Characters/UEFN_Mannequin/Rigs/PA_UEFN_Mannequin.uasset` 移植到 `Content/Characters/Heroes/Mannequin/Rigs/PA_Mannequin_PC.uasset`。目标为 Manny 预览 Mesh；原 `PA_Mannequin` 不作为此次修改对象。目标现为23刚体、19胶囊+4盒、22约束、185对可碰撞组合（68对禁用），已在编辑器回读确认。迁移前目标为22刚体、23约束（3交叉约束）、4对可碰撞组合。
- 源/目标骨架骨名对应，但参考姿势和骨长不同，不能直接复制全部关节坐标。此次保留源碰撞形状、BodyInstance、Constraint Profile/驱动/软限位/投影等参数；保留子体 `Pos1/PriAxis1/SecAxis1`，按 Manny 参考姿势重新对齐父体 `Pos2`，旋转父体 `PriAxis2/SecAxis2`，保持源参考姿势下两约束框架的相对角度。直接套用源坐标在 Manny 上的最大锚点偏差约9.43cm；不得用增加阻尼掩盖该几何错误。骨架参考入口为 `AnimPoseExtensions.get_reference_pose(mesh.skeleton)` 的 LOCAL/WORLD 姿势；后续换 Mesh 仍须重新核对参考姿势及形状适配。
- 新物理材质 `Rigs/PM_Mannequin_PC` 复制源 `PM_UEFN_Mannequin`：Friction=1.75、StaticFriction=1、FrictionCombine=Multiply并覆盖、Restitution=0、RestitutionCombine=Multiply并覆盖、SleepLinearVelocityThreshold=1、SleepAngularVelocityThreshold=0.05、SleepCounterThreshold=4。依源资产保留 `neck_02/head` 的无显式材质状态，其余21体引用新材质。源 `neck_02` 的 MassInKgOverride 数字为默认100但 **bOverrideMass=false**，不能将该数字加进覆盖质量总和或解释为实际100kg；此次按源设置保留。
- `PCA_Mannequin` 与源 `PCA_SandboxCharacter` 的 authored setup、集合及 `kinematic/physicalAnimation/ragdoll` Profiles 原本一致；此次将其 PhysicsAsset 从旧 `PA_Mannequin` 改为 `PA_Mannequin_PC`。回读 `IsCompilationNeeded=false`，不以只比较 authored 字段代替编译数据检查。PA 的 SolverSettings 同源为 Position/Velocity/Projection=6/1/1、CullDistance=3、UseLinearJointSolver=true、UseManifolds=false，SolverType=World；引擎 `PhysicsAsset.h` 明确这些 SolverSettings 的数值用于 RBAN，不应据此断言它们控制 World ragdoll 的全部迭代设置。
- `Config/DefaultEngine.ini` 的 `[/Script/Engine.PhysicsSettings]` 同源显式配置：`bTickPhysicsAsync=False`、`bSubstepping=True`、`MaxSubstepDeltaTime=0.016667`、`MaxSubsteps=16`、`bSubsteppingAsync=False`、`AsyncFixedTimeStepSize=0.016667`。迁移前目标 Substepping=false、MaxSubsteps=6、AsyncFixedTimeStepSize约1/30秒；该配置影响全工程物理。两边完整 PhysicsSettings 回读一致，1208个 `p.*` CVar 同名同值；不是只比 DefaultEngine.ini 文本。
- 编辑器鼠标弹簧与插值是 `UPhysicsAssetEditorOptions` 的预览设置，不是 PA 关节驱动或 PCA Control 参数。本次两边本来一致：线阻尼/刚度200/750、角阻尼/刚度500/1500、InterpolationSpeed=50；重力倍率1、无GravityOverride、PhysicsBlend=1、UpdateJointsFromAnimation=false、MaxFPS=-1。源码入口 `Engine/Source/Editor/UnrealEd/Classes/Preferences/PhysicsAssetEditorOptions.h`。比较动态表现仍需一致的预览初姿、帧率、地面碰撞、拖拽/冲量及 PCA Profile。
- 本轮已完成参数/依赖/编译状态回读与编辑器结构确认；用户实际模拟反馈“效果很好”。这不等于完整游戏运行链、全部冲量/姿态/帧率或网络场景验收。原始资产和配置备份在 `Saved/Codex/PhysicsParity/backup`，导出、适配说明与回读证据在同级JSON/T3D；Saved只作当次证据，不成为运行时依赖。临时迁移Content已清理；最终PA仅依赖 Manny预览Mesh与新物理材质，没有 UEFN工程或临时目录依赖。

### 运行时调用时序（PhysicsControl 组件）

- 四条刷屏 warning 同源，全部来自 `UPhysicsControlComponent`（引擎 `Engine/Plugins/Animation/PhysicsControl/Source/PhysicsControl/Private/PhysicsControlComponent.cpp`，5.8 已从 Experimental 移到 Animation）。`No physics asset in skeletal mesh`（无 owner 后缀）只可能出自 `CreateControlsAndBodyModifiersFromLimbBones` 的 line 3191；该函数在 `SkeletalMeshComponent->GetPhysicsAsset()` 为空时直接 return false，`CreateControlsAndBodyModifiersFromPhysicsControlAsset` 见失败即整链 return（line 3232-3246），因此控件与 BodyModifier 数为 0，PCA 的 InitialControlAndModifierUpdates 也不会应用。
- `WorldSpace`/`ParentSpace` 是 `GetControlTypeName()`（line 39-50）自动命名的集合，`All` 由 `FPhysicsControlNameRecords::AddControl` 的默认 `bAddToAll=true` 自动加入（`PhysicsControlNameRecords.cpp` line 18-27）；三者只在控件/修饰器成功创建后才存在。`ExpandName`（`PhysicsControlNameRecords.cpp` line 147）命中集合才展开，否则原样当控件名查 → `ApplyControlAndModifierUpdates`（line 3330-3414）报 `Failed to find control/modifier with name ...`。即"找不到集合"等价于"本次调用时一个控件都没建出来"，不是名字拼错。
- 本机 `Saved/Logs/ImagineGame.log`（2026-11-25）对照：13:35-13:36 批次含 `WorldSpace_Feet` 与 `Feet` 修饰器，对应 PCA 的 `PhysicalAnimation`；13:38 起只剩 `WorldSpace`/`ParentSpace`×2/`All`，对应 `Ragdoll`（与上方 PCA 快照 1/2/1 一致）。每批都是"No physics asset"同帧紧跟 ApplyControlAndModifierUpdates，说明调用方没有用 Create 的返回值兜住就调 InvokeControlProfile。
- 触发时机在 `B_ExperienceDef_ShooterCore` 加载后的 Pawn 生成阶段，紧跟其后同一毫秒报 `Controller Parts ... has no available PawnComponent_CharacterParts ... waiting for CharacterPartsReady`（`IMGControllerComponent_CharacterParts.cpp` line 161）。身体网格要等 `UIMGPawnComponent_CharacterParts::BroadcastChanged` 里的 `SetSkeletalMesh` / `SetPhysicsAsset(ForcedPhysicsAsset)`（line 345-359）才带 PA，此时 `GetPhysicsAsset()` 仍为空，故不是 PCA 或 PA 资产问题，而是运行时顺序：先建控件失败，再套 profile。
- 修法与就绪信号：`NAME_CharacterPartsReady`（`IMGPawnComponent_CharacterParts.cpp` line 254 的 GameFrameworkComponentExtensionEvent）或 `OnCharacterPartsChanged` 之后再 Create，Create 返回 true 才 Invoke；换 body style 或 `ForcedPhysicsAsset` 变更后须先 Reset/销毁再重建控件。`bWarnAboutInvalidNames`（`PhysicsControlComponent.h` line 1706-1710）只静音不修复。调用点位于蓝图/AnimBP，本仓 C++ 无该组件引用（`Source/` 全量无 `PhysicsControl` 命中），具体是哪个 BP 的哪两个节点未验证，需在编辑器确认。

### 编辑器工具边界

- 本轮会话 MCP 端点为 ImagineGame `http://127.0.0.1:8001/mcp`、GameAnimationSanple `http://127.0.0.1:8002/mcp`。端口/编辑器状态会变，操作前先验证工程身份。HTTP需先 initialize并保存 `Mcp-Session-Id`，再通知 initialized；不带session直接调用tools会400。内置客户端配置曾指向8000，不能据配置文件认定实时端口。
- 内置 ObjectTools 不提供 PA 刚体数组和非反射 `CollisionDisableTable` 的完整编辑。`ObjectExporterT3D` 可导出刚体/约束结构，但不能代表完整碰撞禁用表；整资产复制保留该表，再逐项适配。`PhysicsAssetEditorSharedData.cpp::CopySelectedBodiesAndConstraintsToClipboard/PasteBodiesAndConstraintsFromClipboard` 会按骨名输出/恢复 `PhysicsAssetCollisionPair`；粘贴先启用所有粘贴体之间碰撞，再按文本禁用，但不会自动删除目标多余约束，不能只粘贴一次就认为拓扑一致。
- Python读取未生成专用包装的内嵌UObject时须用原始反射名，如 `load_object(None, pa_path+':SkeletalBodySetup_18').get_editor_property('DefaultInstance')`；snake_case 可能找不到字段。PA的 `SkeletalBodySetups/ConstraintSetup/PreviewSkeletalMesh` 是受保护字段，不应假定通用getter可读。此次预览Mesh经原生反射 `get_default_object(SystemLibrary).call_method('SetSoftObjectPropertyByName', args=(pa, Name('PreviewSkeletalMesh'), mesh))` 设置；Value传加载的Mesh对象，不能传SoftObjectPath包装。修改后须用完整导出和最终依赖回读确认。
- 已加载uasset可能仍被UE文件读取器占用；仅关闭资产编辑器不保证释放。此次目标无referencer且无dirty修改，备份后关闭编辑器、释放Python资产引用并 `EditorLoadingAndSavingUtils.unload_packages`，再替换同名包、加载并保存。任何后续迁移须重新确认引用/dirty状态及备份，不能直接覆盖已加载或有未保存修改的文件。会话内的 CodexPhysicsParityTools 是此次任务临时Python Toolset，不是工程插件或通用运行时接口，重启即消失；不依赖开启Python远程执行。

## Ragdoll GA 接入规划（尚未实现）

### 会话、激活与身体事实

- 一个派生 `UIMGGameplayAbility` 的 InstancedPerActor GA 持有“进入 → 等待稳定 → 选起身 → 恢复执行 → 完成/取消”。首版 ServerInitiated，客户端请求复用 GAS 能力/事件/TargetData；未来 LocalPredicted 须成对实现拒绝/回滚。业务步骤只作本地流程，不复制第二套移动 Phase/Session，不以 GAS 预测键替代 Revision。
- GA 调用协调组件/CMC，选择适配调用 MM，身体适配独立消费同 Session 事件控制可选 PAC。普通移动 Tag 留在 GAS/玩法配置；通用合同可放共享层，伤害、车辆/水域/平台、爬行和救援规则归具体玩法。
- 激活校验 Avatar、ASC 绑定、存活/可打断规则与协调器可用性，完成授予/事件/互斥配置后 Commit；Start 前绑定事件，成功后认领实际新 Session并处理同步回调。失败只清本次资源；有成本时明确 Commit 后 Start 失败的补偿策略，不接管他人会话。
- 首版建议 Independent 配合 Tag 阻塞/取消；Exclusive_Blocking 只阻塞其他独占能力，不能替代通用规则。保持可取消，死亡/PawnExt 解绑可结束它，不默认 SurvivesDeath；尸体物理需显式交接。重复受击可更新当前最短等待/提交冲量，恢复再受击先结束旧执行，再开新会话。
- 业务最短倒地时间之后，GetRagdollBodyMotion=true且线/角速度均达标才累计连续稳定时长；超限、采样失败、Avatar/Session 变化清零。用 GA 管理的 AbilityTask/有界周期与最大等待时间，EndAbility 清理；目标过期后的胶囊零速度或失败采样零输出不代表身体稳定。
- 专服无身体模拟时不能证明身体稳定；须选择服务端轻量模拟、自定义只读事实，或明确的客户端建议＋服务端规则/几何重检降级。实施时定义数据来源、时效、误差与信任范围，不用胶囊速度替代，不让 Movement 依赖 PAC。

### 选片、网络与恢复执行

- MM/Pose Matching 输出 Montage、有限正倍率、起始时间、最终胶囊变换及与采样点一致的 BodyLocalOffset。先 QueryRecovery 取几何参考，再 BeginRecoveryAt 提交实际匹配；异步加载/选择后重检会话与身体位置，失配重选或延期。
- 静态落位不证明完整动画空间；持续 Root Motion与手脚空间由动画合同和正常 CMC 扫掠验证。客户端选片限定 Spec/Activation/Avatar/Session，复用 GAS TargetData/取消通道；服务端按允许资产、时长、倍率/起点及受信匹配点配置重建/重检，不直接信任客户端位置/任意偏移。专服无 PoseHistory/资产未就绪时须有后备选择或明确拒绝。
- 同 Session OnRecoveryStarted 且 Walking/落位就绪后，权威和自主代理按同一批准合同各自启动 GAS PlayMontageAndWait并检查成功；绑定完成/中断/取消，复用 GAS Montage复制和 Root Motion。OnBlendOut 不提前完成；仅权威 OnCompleted 调 CompleteRecovery(Session)，中断用 CompleteRecovery(Session, true)，拥有者本地结束仍等权威生命周期。
- UE 5.8 OnRep_ReplicatedAnimMontage 不为本地控制的自主代理播放；批准选片、时间和 Session 须经 GAS/表现合同传给拥有者。模拟代理不选片/判稳定/提交目标，按 Session 与 Montage/PlayInstanceId 关联等待模式/引用，乱序补齐一次，结束先到不得重启旧表现；该关联不成为第二套移动阶段。
- 后续动画适配可独立注册 ASC 复制 Montage 前置条件，按自己的 Session 与 Montage/PlayInstanceId 判断，不查询 Traversal 或覆盖其他提供者。公共合同见“复制 Montage 前置条件”；不向 RagdollComponent/CMC 引入 ASC/Traversal 依赖。

### 失败、结束与验收范围

- 批准前几何拒绝保留 Simulating，按原因有界重试：缺失/过期先恢复数据，受阻等环境，动画/加载失败释放候选。批准后同步回调可能结束会话/改模式，须读最终 Session/Phase，不能凭 BeginRecoveryAt=false 判断仍在 Simulating。
- 超时后的继续倒地、爬行、救援/重生由玩法决定；交接前 GA 仍持有会话，不得丢弃一次 Abort=false 后直接结束。
- EndAbility 遵循 IsEndAbilityValid、ScopeLockCount、bIsAbilityEnding；先屏蔽重入，清本次任务/计时/加载句柄、TargetData/取消委托及组件/ASC 订阅，再按原 Avatar/缓存组件/Session 清理。正常 CompleteRecovery；异常 Recovering 用 CompleteRecovery(..., true)，Simulating 用 AbortRagdoll(Session, true)；组件失效可由原 Avatar 缓存 CMC.StopRagdollMovement(Unavailable, Session) 兜底。
- 只终止自己的 Montage/身体适配实例，旧回调同时匹配 Activation/Task 实例和 Session，不影响新会话/Pawn。强制结束成功只代表会话结束，ExitBlocked/MOVE_None 仍须交玩法救援。
- 待验收：进入/Commit/动画失败、连续受击与恢复再受击、坡面/台阶/墙角/低顶/初始穿透、来源缺失/过期、死亡/取消/ASC解绑/卸载、生命周期乱序/合并、引用晚到、自主纠偏重放、模拟代理；包括应答生成前后恢复/完成、GoodMove ACK、忽略纠偏和晚快照。按单机 → Listen/Dedicated → 延迟/丢包 → 多角色预算推进。
- 车辆/移动平台未来由来源提供相对事实，扩展原输入/SavedMove/快照并重检；Walking 继续复用 BasedMovement，不预埋玩法类型判断或直接 Attach 绕过预测。
