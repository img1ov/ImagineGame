# Gameplay 框架、UI 与设置

相关主题：[角色移动](locomotion.md) · [装备](equipment.md) · [武器](weapons.md)。

## Experience 与 ASC 生命周期

- `AIMGGameMode` 选择 Experience，`UIMGExperienceManagerComponent` 加载；`UIMGAbilitySet` 授予能力、Effect、AttributeSet。`AIMGPlayerState` 持有 ASC/属性，`UIMGPawnExtensionComponent` 绑定 Pawn Avatar，`UIMGHeroComponent` 在 DataInitialized 初始化 ASC 和本地输入。
- Experience 优先级：URL Options → PIE 开发设置 `UIMGDeveloperSettings.ExperienceOverride` → 命令行 → World Settings → 专服/默认 Experience。
- Bundle：Standalone/Listen Server 加载两端，Dedicated Server 仅 Server，Client 仅 Client，PIE 同时加载两端。同一 Experience/ActionSets 的 GameFeature URL 去重后计数，保留跨 PIE World 请求计数。
- Experience 失败为本机终态：定义/Bundle/插件失败不广播 Loaded；HasExperienceLoadFailed/GetExperienceLoadError 提供诊断，加载屏保留失败原因，不自动重试、切图或新增蓝图失败引脚。各端仍独立加载，不复制失败裁决；同一 Manager 不支持运行中切换 Experience。
- 失败/EndPlay 共用退出链：先阻止后续激活并取消本机加载句柄/延迟计时器，再按实际进入的 Action 阶段逆序停用，等待原生 Deactivation Pauser 后 Unloading/Unregistering，最后释放已完成的插件请求。未完成插件由自己的晚回调释放；收尾不强持有 Manager/旧 World。Pauser 调用方必须在游戏线程执行完成委托，不能遗失。
- 跨 PIE 的未释放插件计数不在新一轮 BeginPIE 清空；共享输入映射 Action 的全局注册按使用者计数，最后一个使用者退出才注销。Bundle 仍由全局 AssetManager 持有，不按单个 World 强制卸载共享资产。
- 既有 WaitForExperienceReady 节点保留 OnReady 接口；加载失败、Manager EndPlay 或 World 退出时停止等待并解除委托/计时器及 GameInstance 保活，晚回调不再广播就绪。成功回调保持高/普通/低优先级，各回调之间复核退出状态。
- `AIMGCharacter` 实现 `IGameplayCueInterface`，角色蓝图可处理 Cue；独立 Notify 由 GameplayCueManager 的全局 CueSet 分发。
- 玩家/Bot Controller.OnUnPossess 仅在 ASC Avatar 和 PawnExt 仍匹配旧 Pawn 时调用 PawnExt.UninitializeAbilitySystem；其顺序为取消能力、清输入/Cue → 清 Avatar/ActorInfo → 广播解绑 → 清组件 ASC 引用。取消保留 SurvivesDeath 能力；缺少匹配 PawnExt 时保留原生清 Avatar 路径，旧 Pawn 不得解绑新 Pawn 的 ASC。
- `UIMGHealthComponent` 发死亡事件，已授予的 `UIMGGameplayAbility_Death` 控制开始/结束，Character 在结束后销毁；自动重生能力位于 `ShooterCoreRuntime/GameModes`。

## ASC 源码组织与共享接口

- IMGAbilitySystemComponent.cpp 保留基础生命周期、属性集合与通用服务；_Abilities.cpp 管能力查询/激活/组及取消，_Input.cpp 管输入采集/分发，_Montage.cpp 管复制播放前置门控。私有门控状态留现有公共头中的 FReplicatedMontageGateState，不引入业务类型反查或第二个 ASC。
- TryActivateGrantedAbilityByClass / ByInputTag 共用匹配入口，输出先清空；返回值表示 GAS 接受激活请求，包含同步结束或等待远端执行，不保证调用返回时仍有活动实例。取消/激活回调后核对 Avatar/输入代次；需要活动状态时另查询实例。
- Runtime AttributeSet 变更按对象身份序列检测，不只比较数量；先发布新身份缓存再广播，避免监听器同步增删重入。GetAbilityTargetData 查询先 Clear 输出，缓存缺失不保留调用方上一次结果。GA TagRequirement 临时容器改为局部，避免静态共享容器被重入污染。
- 本次不实施连招输入缓冲、RPC Batcher、同帧输入优先级或 Montage 段切换业务；共享任务的生命周期修正不代表连招系统已实现。Ragdoll 业务与待做见 [Ragdoll](ragdoll.md)。

## 能力输入绑定

- `FIMGInputAction` 只保存 InputTag/InputAction；`UIMGInputComponent::BindAbilityActions` 对有效 Action/Tag 按 IFE 的 LyraInputComponent 固定绑定 Started→Pressed、Completed→Released，不配置每项 PressedTriggerEvent 或 BindReleaseEvent。NativeAction 仍由调用方显式选择 TriggerEvent。
- 默认 PawnData 输入和附加 InputConfig 均经 Hero 的 Pressed/Released 转发当前 Avatar 匹配的 ASC，再由 PlayerController.PostProcessInput 调用 ASC.ProcessAbilityInput。GA.ActivationPolicy 决定 OnInputTriggered/WhileInputActive/OnSpawn；Started 在输入开始时转发一次 Pressed，Held 状态保持至 Released，避免持续 Triggered 被 WaitInputPress 当作下一次点按。
- Completed→ASC.AbilityInputTagReleased 移除 Held 并登记 Released；ProcessAbilityInput 更新 Spec.InputPressed，AbilitySpecInputReleased 发 InputReleased 通用事件，GA.WaitInputRelease 依赖该链及自身预测键向服务端传递释放。移除配置开关不能删掉释放转发。ProcessAbilityInput 固定本轮 Pressed/Released/Held 列表，能力回调中新排入的输入留给下一轮；ClearAbilityInput 或 Avatar 切换使旧轮次失效，禁止跨 ASC 共享临时激活列表或重入消费。ASC Avatar 切换、ClearActorInfo、EndPlay 清理输入。
- Hero 持有 InputConfig → 来源集合/绑定句柄，以及本机实际 InputComponent/Subsystem 的弱引用。Add/RemoveAdditionalInputConfig 显式接受 Source：同来源重复加入幂等，不同来源共享配置时只绑定一次，最后来源移除才解绑；默认 PawnData 配置以 Hero 自身为来源。GameFeature 只记录实际承接请求的 Hero，退出不依赖当前 Controller/LocalPlayer，重复 BindInputsNow 可重申请求。配置移除释放已无其他配置使用的 InputTag；Controller 更换、重绑定、EndPlay/OnUnregister 释放本方全部绑定及默认 IMC，不再 ClearAllMappings。GameplayReady 后通过既有初始化检查接合新的 InputComponent，不新增 Tick。
- Hero 临时 OnUnregister 后仍保留 InitState；已 BeginPlay 的 OnRegister 通过现有初始化检查恢复输入。解绑释放直接提交给 Avatar 仍匹配的 ASC，不经过会拒绝 IsBeingDestroyed 的普通输入回调，避免动态销毁 Hero 后残留 Held。
- 共用 FInputMappingContextAndPriority 声明归现有 `Input/IMGInputConfig.h`，Hero 与 GameFeature 都消费输入配置，Hero 不包含 Action 头；结构体仍在同模块且未改名，反射身份不变。bRegisterWithSettings 只决定设置注册，不控制运行时 AddMappingContext；默认 IMC 激活不要求 PawnData.InputConfig 非空。

- 通用 InputPressed/InputReleased 按活动实例及其 PredictionKey 分发，同一个 Spec/PredictionKey 只调用一次 GAS generic event；回调后再确认实例仍属该激活。InstancedPerExecution 不使用过时的 Spec 激活信息替代实例键。

## Montage 任务生命周期

- AbilityTask_PlayMontageAndWaitForEvent 提供 StartTimeSeconds，并按 AnimInstance + MontageInstanceId 管理混出/结束委托、禁用 RootMotion 栈与 Character 的根运动缩放。清理只能影响自己持有的实例，旧 Task 不解绑/停止后来播放；原 Avatar 丢失也须清本方状态。
- PlayMontage 及 GameplayEvent/动态委托可同步结束 Task 或切换 Avatar；启动返回后重新确认 Ability、ASC、当前 AnimInstance/播放实例与所有权，再绑定和应用状态。bFinishing/bDestroying 抑制重复广播，OnDestroy 成对退订事件/取消委托及根运动状态。
- Ragdoll GA 当前使用原生 UAbilityTask_PlayMontageAndWait，批准/迟到进度在 Ragdoll 自己的恢复实现处理；不能把共享自定义 Task 的修正误认为 Ragdoll 使用了另一条播放协议。

## 复制 Montage 前置条件

- 引擎 `GetRepAnimMontageInfo` 是 protected，外部组件不能直接读取。项目 ASC 的 `GetReplicatedMontagePosition(Montage, PlayInstanceId, OutPosition)` 提供只读位置查询：校验资产/实例、未停止、确实复制 Position 且未禁用位置纠正，以及位置有限且在资产范围内；失败输出 -1。该查询允许前置条件等待中的实例使用，不要求已在本机起播，也不公开整份原生复制状态。
- ASC 的 `RegisterReplicatedMontagePrerequisite(Avatar, Name, Delegate)` 返回注册句柄；委托须弱绑定 UObject，只接受当前 Avatar。相同对象/Name 重复注册复用句柄，不同步执行回调；注册后、前置数据变化后调用 `RetryReplicatedMontage`。检查返回 NotApplicable、Waiting、Ready 或 Discard，业务 Session/Sequence 由提供者自行核对，不复制第二套播放阶段。
- 所有适用条件 Ready 才放行；只有所属表现系统提供非负 LogicalStartTime，其他条件返回 -1。多个起点提供者或非法起点保留等待并诊断，不以注册顺序/优先级覆盖。非所属 Montage 必须返回 NotApplicable；沿用原生 GAS 的 Montage/PlayInstanceId、播放/停止与位置同步，不能据此前置门控认定自主代理预测/重放已满足。
- `UnregisterReplicatedMontagePrerequisite` 按句柄精确注销，默认丢弃本对象参与阻塞的待播放实例；只有明确交接前置职责时才传 false。Discard 仅抑制当前 Avatar/Montage/PlayInstanceId 的本机表现，不改变权威能力或移动阶段。新实例不继承旧丢弃；结束先到不创建已停止的新实例，已播放实例保留原生停止路径。
- 门控/Retry 只作用于非权威、非本地控制角色；重试合并至下一 Tick 并防重入，前置列表/快照使用有界内联存储，回调后复核注册版本、Avatar 代次和实例后才再访问原索引。Avatar 切换、ClearActorInfo、EndPlay 释放注册、起点缓存和计时器；ASC 临时注销暂停处理/释放计时器，保留弱注册并在重新注册后重试。失效提供者不能释放已放弃的实例。等待原因变化输出 Verbose，连续等待5秒输出一次 Warning并复核，不因超时自动放行；`Log LogIMGAbilitySystem Verbose` 查看 Avatar/Montage/Instance/Reason。

## 持久 Bot 与 PawnData

- `UIMGBotCreationComponent` 在 Experience 就绪后由服务端按 Controller → PlayerState → Pawn 生成 Bot，配置 BotControllerClass并保留分队/出生点规则。重生分派支持玩家控制器及 `AIMGPlayerBotController` 派生类；当前无 StateTree 运行时代码。
- `AIMGPlayerBotController` 开启 bWantsPlayerState，每个 Bot 独享 PlayerState/ASC；共享 PawnData 资产不共享运行状态。BotCreation 不另设 PawnData 配置。
- GameMode.GetPawnDataForController 按 PlayerState 已认领 → Experience 就绪时 Bot.GetPawnData → Experience.DefaultPawnData → AssetManager.DefaultPawnData 查询。全局回退沿用同步加载/保活接口，缺失返回空并记录日志。
- Bot.GetPawnData 为 BlueprintNativeEvent/BlueprintPure，默认空并回退默认配置；只同步、无副作用地返回选择，不授予能力或生成 Pawn。首次认领前保持稳定，选择依据须早于 Controller.BeginPlay、PlayerState 成员赋值、分队和 Possess 可用；空值不表示等待异步注入。
- PlayerState 在 Experience 回调中首次认领，只有服务端首次 SetPawnData 授予 AbilitySets；已加载时注册立即回调，普通 SpawnActor 返回后再覆盖已太晚。后续生成、出生点体型检查及重生复用已认领数据，切换不同 PawnData 须另行设计。
- GameMode 按 PawnData.PawnClass 生成，在 FinishSpawning 前写入 PawnExt；Pawn 仍须装配 PawnExt/Hero 等组件。初始化沿 Spawned → DataAvailable → DataInitialized → GameplayReady；Bot 只跳过玩家输入条件与绑定。
- 网络复用 PawnExt.PawnData、Pawn.PlayerState 与 GAS；客户端由 OnRep_PawnData、OnRep_PlayerState 等推进初始化，模拟代理不要求 AIController。晚注册用 RegisterAndCall；IMGAbilitiesReady 仅在服务端 SetPawnData 发送，本机 OnAbilitySystemInitialized 不保证能力/属性已复制齐全。
- 独立敌人 AI 可用 Pawn 自持 ASC 或其他体系。AIMGCharacterWithAbilities 虽自持 ASC，仍继承 PawnExt，自行 InitAbilityActorInfo 不会自动推进至 GameplayReady。

## UI 与设置

- `IMGNumberPopComponent_MeshText::LiveComponents` 保存受 GC 跟踪的 PoolMesh 键，回收时重新查 PooledComponentMap，不跨帧保存 Map 元素地址；按实际 ReleaseTime 回收并重设最早计时器，支持运行中调整寿命。MeshText/NiagaraText 在 EndPlay 销毁自行创建的表现组件，MeshText 同时释放计时器和池。Niagara 请求先验证 Style/Asset/World，并过滤远端 Controller；MeshText 校验寿命、材质指针和参数数组边界。
- `IMGPerformanceStatSubsystem` 的采样缓存为独立共享对象，Tracker 建立时预建全部 Stat 的环形缓存，GetCachedStatData 返回只读共享引用；Slate 只持弱引用并在绘制时 Pin，Map 扩容不影响地址，Tracker 释放后图表停止取样。Subsystem 的 Tracker 空值、UMG/Slate 未建立及图表无效 Y 上限均有守卫，绘制点数组由每个 Slate 实例复用。
- `UIMGPerfStatGraph` 在 UObject 控件侧保留颜色、Y 上限、缩放和采样弱引用，RebuildWidget 恢复到新 Slate；Slate 未建立时调用 setter 不丢失设置。父 `UIMGPerfStatWidgetBase` 保留最近一次 UpdateGraphData 的缩放，NativeConstruct 不覆盖蓝图 Construct 已指定的比例；NativeDestruct 清采样引用与 Subsystem 缓存，再交给原生/蓝图 Destruct。

- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged、生命/死亡委托；装弹提供开始/结束事件，自动重生提供可配置消息 Tag。
- GameSettings.UGameSettingDetailView 通过 HandleCurrentSettingChanged 订阅当前 Setting 的变化并刷新描述/动态详情；DLC Registry 定时变更沿同一路径，不另轮询。
- 切换/清空 Setting、NativeDestruct 时退订；NativeConstruct 保留当前选中项并重建详情/扩展。切换、清空、关闭、释放 Slate 均取消并清空扩展异步句柄；完成/装配回调复核当前 Setting，避免旧请求或重入回填过期控件。

## 交互与队伍边界

- `Public/Physics/IMGCollisionChannels.h` 当前将 Interaction 与 Traversal 同时映射为 `ECC_GameTraceChannel1`；`Config/DefaultEngine.ini` 将其配置为 Traversable、默认 Block。拆分交互通道须同步处理配置及交互目标响应，不能仅替换宏；既有资产/实例响应尚未核对。
- `AbilityTask_GrantNearbyInteraction` 以 Task 自身为授予能力的 SourceObject，按能力类缓存句柄；查询只增加，离开范围不撤销，OnDestroy 仅清扫描计时器。调整回收须先明确共享能力与授予来源的所有权。
- `IMGTeamSubsystem::CanCauseDamage` 先拒绝无效 Instigator/Target；允许自伤时只接受同一有效对象或同一非空 PlayerState。不同对象都没有 PlayerState 时继续走原队伍规则，不以 null == null 放行。

## 基础组件与 GameFeature 生命周期

- `IMGHealthComponent::InitializeWithAbilitySystem` 先验证 ASC 和 HealthSet，再提交两个成员及委托；缺 HealthSet 的失败不留下半初始化状态，后续可再次调用。初始化和清理的同步属性/Tag/业务回调后以绑定代次校验，避免旧调用继续访问已解绑或重建的 HealthSet。当前仍由既有装配链发起初始化，没有新增属性晚到轮询或自动重试。
- `GameFeatureAction_AddAbilities::AddToWorld` 的扩展委托保存原始 AbilitiesList 下标；ActorClass 空行不会压缩后续索引。现有同 Actor 多条授予配置的行为不因此改变。
- `GameFeatureAction_AddInputContextMapping` 在激活上下文中保存 Controller → 实际 EnhancedInputSubsystem 弱引用，重复 ExtensionAdded 不重复增加映射；BindInputsNow 先成对移除再重加。退出使用保存的 Subsystem，即使 Controller 已失去 LocalPlayer 或失效也可释放映射，不在未实际加入时误删。跨来源共享 IMC 仍遵循资产原生 RegistrationTrackingMode（默认 Untracked；CountRegistrations 才提供引擎引用计数）。
