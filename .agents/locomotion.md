# 角色输入与普通移动

相关主题：[Gameplay 框架](gameplay.md) · [Traversal](traversal.md) · [Ragdoll](ragdoll.md) · [镜头反馈](camera-shake.md)。

## 输入、GAS 与 Locomotion 职责

- `HeroComponent.AddMovementIntent` 统一处理玩家、自动奔跑和外部 AI 的世界空间方向，再提交 AddMovementInput。输入结束、取消自动奔跑、换 Controller 或 EndPlay 清理意图。原生 AI Path Following 独立；急转回调可能同步取消或替换请求，回调后须复核再提交输入。
- **步态选目标，GE 改目标值。** GA 管准入、互斥和意图释放，HeroLocomotion 用 Sprint/Walk、Aim/Strafe 四个 Tag 形成请求，再按输入方向和姿态选择实际 Gait。只有 Walk/Run/Sprint/Crouch 四项速度属于 `LocomotionSet`，便于各类 GE 共用速度入口，避免 GA 分散写 CMC。Set 构造默认 165/380/585/170，差异由 AbilitySet 初始化 GE 设置基础值。PlayerState 持有的 Set 随 ASC 保留，换 Pawn 读取已有 CurrentValue。
- Locomotion 汇聚属性与步态，统一计算并写入 CMC。基础加速度、地面摩擦、行走制动为组件配置，默认 800/2/800；Hero 按步态、速度、前侧后倍率和 0.3 秒落地窗口派生。Aim/Strafe 冲刺还须满足 50° 方向条件。Crouch 用独立速度属性，Crawl 沿 CMC 限速。组件配置和曲线在预测历史存在期间保持稳定。
- HeroLocomotion 的 `StrafeSpeedMapCurve` 在组件类默认值中配置为可选软引用。BeginPlay 在接合 CMC 前解析一次，未配置或加载失败时记 Warning 并使用前向倍率。权威端与自主代理使用一致的配置，EndPlay 解除移动绑定后释放曲线缓存。
- HeroLocomotion 的跑动相机表现独立于移动参数计算，实现在 `IMGHeroLocomotionComponent_Camera.cpp`，只调用 CameraComponent 的通用入口；扩展遵守下节「移动表现来源与扩展边界」，配置与本地来源生命周期见 [跑动 Shake](camera-shake.md)。
- **单向接合。** Locomotion 通过通用弱委托注册一个 CMC 提供者，CMC 只理解请求字节及 `IMGMovementSimulationContext.h` 中的通用上下文和参数，字段含义由提供者解释。Controller → PreCMC → CMC 的 Tick 依赖供本地输入与表现更新；蓝图覆写 PreCMC 须调用父实现。远端 RPC 按 Move 自带输入模拟，逐 Move 计算和重放不派发 PreCMC 蓝图事件。
- **动态 Set。** GameplayReady、当前 ASC/Avatar、真实 Set 与提供者同时有效才 Ready。等待 Set 时关闭 Tick，CMC 按原生移动。AbilitySet/Game Feature 走 ASC 的 `AddRuntimeAttributeSet` / `RemoveRuntimeAttributeSet`，客户端在 `PostRepNotifies` 后重检。下一次 PreCMC 接合并读取已施加初始化 GE 的值。移除或换绑释放旧代次、恢复接管前参数；物理子步中的 Set 变化等当前 Move 结束再释放。授予方先结束依赖 Set 的 GE。停用、注销、EndPlay 精确退订，旧提供者不能覆盖新提供者。换 CMC 须重新注册。

## 移动表现来源与扩展边界

- **当前职责归属：** `HeroLocomotionComponent` 可作为普通移动镜头表现的稳定来源，基于既有实际速度、步态、落地等事实，判定是否请求表现、幅度/频率及停止时机。基础 `LocomotionComponent` 保持移动接合、属性汇聚与参数计算职责，CMC 保持移动模拟职责；两者不增加镜头业务分支，不为表现另造移动事实或第二套移动权威。
- **来源判定与通用调度分开：** HeroLocomotion 只通过 CameraComponent 的统一入口提交、更新和停止本来源请求，并管理本方句柄。实例创建/复用、启动间隔、并发替换、释放与清理归 CameraComponent；模式只提供视图策略和效果接收倍率，Pattern/算法只消费明确参数。相机不反查 Locomotion、动画、武器或 GAS，不在每个混合模式重复建立来源驱动。
- **配置与运行时事实分开：** 效果形状、基础幅度/频率、曲线、包络及来源响应范围由配置提供；实际速度、当前状态及由此计算的强度/频率倍率在运行时提供。当前跑动参数在 HeroLocomotion 内联展开；有实际复用或独立编辑需求时再提取纯配置资产，不要求额外创建运行时实例蓝图。配置项须有实际消费者，运行时实例/历史/句柄不作为资源类型暴露，不为固定规则增加无效开关，不用配置覆盖事实所有权。
- **其他业务各自发起：** 走路、冲刺、游泳等由普通移动状态直接产生的反馈可继续从 HeroLocomotion 发起；开火、受击、爆炸及 Traversal 专属反馈由对应业务模块发起，共用相机入口。HeroLocomotion 不发展为所有镜头行为的总调度器；通用实例并发、替换、混合与释放规则不回迁到来源组件。
- **按实际复杂度提取：** 参数、曲线或预设增多时先整理配置，并保留独立实现文件隔开移动计算与表现；不按效果数量或文件行数机械拆组件。当移动表现形成独立状态机、多种表现协作、复杂来源规则或独立生命周期时，再提取专门的移动表现组件，由 HeroLocomotion 提供事实。提取后保持单一请求所有者、句柄和来源身份，旧驱动先释放再交接，禁止两边重复判定、触发或维护相同状态。
- **本地表现边界：** 移动产生的相机反馈仅消费本机事实，不写 ControlRotation、CMC 参数、SavedMove 或复制状态，不新增表现 RPC，不在逐 Move 参数计算、预测重放或动画工作线程发起相机请求。来源结束、取消、失效和切换须成对释放；具体本地视图过滤及清理合同见 [跑动 Shake](camera-shake.md)。GTA 用作执行链和参数语义参考，组件/模块归属按本工程单向依赖落位，不机械照搬其相机类内的业务判定。

## 逐 Move 预测与权威纠偏

- **把模拟输入钉在 Move 上。** 原生 CMC 已记录原生移动状态和输入；外部写 MaxWalkSpeed/MaxAcceleration 或启用子对象复制不会把值纳入历史。AttributeChanged 只更新下一次捕获的输入。ControlledCharacterMove 在缩放前捕获，SavedMove 保存请求、上下文、提供者代次和参数快照。Hero 上下文是四项速度与落地标志。参数或请求变化阻止跨状态合并，重放按历史输入及纠偏后的起点重算，保留非本组件所有的制动摩擦快照，不回写 GAS 或覆盖实时步态。
- **区分力度与加速度。** Packed Move 传 0–255 的输入力度和原生舍入方向，服务端及重放按当次加速度上限重建物理加速度，避免碰撞降速或合并回退后以旧上限解释力度。力度和请求、上下文变化参与 ImportantMove。权威端按自身 Set 重算，外推保留已接收的输入并采样当前属性。本地权威/AI 不生成客户端 SavedMove，模拟代理沿原生同步、只做本机表现。
- **指纹发现分歧，响应接合权威事实。** Packed Move 带本机 Revision/代次及输入、实际参数两份 32 位指纹，按字宽混合并借用 FNV 常量。两份都不同时可提前要求位置/速度纠偏，即使历史位置差很小；仅无关步态属性不同可随 ACK 更新上下文。指纹不授予客户端限速，也不代替原生位置验证。Packed Move/Response 两端须同版。
- 权威上下文在处理 Move 时留存，响应按 PendingAdjustment 时间戳回传。只在原生推进 ACK、对应 SavedMove 与代次/Revision 匹配、纠偏未被业务过滤时接收。逐属性本机变更标识允许新的合法 GAS 预测覆盖旧权威值；普通 OnRep 保留标识，避免复制乱序让晚到旧值撤销更新。停用/注销/换绑作废旧历史；Ragdoll 和 Traversal 维持各自接管及纠偏规则。
- **场景 Buff 的时序。** GE 的 Duration 不决定执行端。场景触发按施加目标的 Authority 裁定，避免把非复制触发器 Self 的本机 Role 当成服务端判断。客户端可在合法 GAS 预测窗口预测已知操作；服务端独知的生效/到期仍有信息延迟，可能产生首次纠偏。现有速度上限按 Move 切换；如需平滑模拟目标，两端须按 Move 时间推进并保存过渡状态。

## 自主代理表现与排查

- 拥有者纠偏时胶囊立即采用权威位置和速度，再重放 SavedMove。模拟代理插值参数不控制这条链。CMC 仅在本地自主代理的普通地面/下落移动中，以首个已接受纠偏前到重放后的**净位移**补偿网格。40 cm 内反向补偿重力平面分量并限制累计量，按 0.05 秒时间常数衰减。移动 Tick 前撤销、后应用；相机跟胶囊。瞬移、Root Motion、Ragdoll、网格更换和停用清理。
- Play As Client 即使未开 Network Emulation 也会纠偏。先用 `p.NetShowCorrections 1` 与 `IMG.Movement.NetDiagnostics 1` 对照历史误差、权威响应和 `[AutonomousMeshCorrection]` 的 PostReplayDelta/MeshOffset；历史位置差为零仍可能修正速度。级别 2 对照 Predict/Authority/Replay 的限速、力度、摩擦和制动。用 `IMG.Movement.AutonomousMeshSmoothing 0` 做表现对照。单机也急刹时先查超速制动，两端分歧时查 Move 输入与时序，不靠放宽误差掩盖。
- `LogIMGLocomotion` 默认输出 ASC/Set 等待、就绪和四项速度。权威端在 ASC 配对后缺 Set 或 Set 变更后失去 Set、Hero 在 BeginPlay 缺方向曲线、移动上下文或参数无效时直接记 Warning。客户端等待 Set 复制记 Log。用 `Log LogIMGLocomotion Verbose` 查看 GameplayReady、Set 变更排队和解绑。提供者冲突或 Packed RPC 未启用见 `LogIMGMovementNet`。
