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

## 能力输入绑定

- `FIMGInputAction` 只保存 InputTag/InputAction；`UIMGInputComponent::BindAbilityActions` 对有效 Action/Tag 按 IFE 的 LyraInputComponent 固定绑定 Started→Pressed、Completed→Released，不配置每项 PressedTriggerEvent 或 BindReleaseEvent。NativeAction 仍由调用方显式选择 TriggerEvent。
- 默认 PawnData 输入和附加 InputConfig 均经 Hero 的 Pressed/Released 转发当前 Avatar 匹配的 ASC，再由 PlayerController.PostProcessInput 调用 ASC.ProcessAbilityInput。GA.ActivationPolicy 决定 OnInputTriggered/WhileInputActive/OnSpawn；Started 在输入开始时转发一次 Pressed，Held 状态保持至 Released，避免持续 Triggered 被 WaitInputPress 当作下一次点按。
- Completed→ASC.AbilityInputTagReleased 移除 Held 并登记 Released；ProcessAbilityInput 更新 Spec.InputPressed，AbilitySpecInputReleased 发 InputReleased 通用事件，GA.WaitInputRelease 依赖该链及自身预测键向服务端传递释放。移除配置开关不能删掉释放转发。ProcessAbilityInput 固定本轮 Pressed/Released/Held 列表，能力回调中新排入的输入留给下一轮；ClearAbilityInput 或 Avatar 切换使旧轮次失效，禁止跨 ASC 共享临时激活列表或重入消费。ASC Avatar 切换、ClearActorInfo、EndPlay 清理输入。输入解绑的绑定句柄与 ASC.ClearAbilityInput 保持原职责；额外输入配置的移除仍是现有未实现边界，不因固定绑定而视为已解决。

## 复制 Montage 前置条件

- ASC 的 `RegisterReplicatedMontagePrerequisite(Avatar, Name, Delegate)` 返回注册句柄；委托须弱绑定 UObject，只接受当前 Avatar。相同对象/Name 重复注册复用句柄，不同步执行回调；注册后、前置数据变化后调用 `RetryReplicatedMontage`。检查返回 NotApplicable、Waiting、Ready 或 Discard，业务 Session/Sequence 由提供者自行核对，不复制第二套播放阶段。
- 所有适用条件 Ready 才放行；只有所属表现系统提供非负 LogicalStartTime，其他条件返回 -1。多个起点提供者或非法起点保留等待并诊断，不以注册顺序/优先级覆盖。非所属 Montage 必须返回 NotApplicable；沿用原生 GAS 的 Montage/PlayInstanceId、播放/停止与位置同步，不能据此前置门控认定自主代理预测/重放已满足。
- `UnregisterReplicatedMontagePrerequisite` 按句柄精确注销，默认丢弃本对象参与阻塞的待播放实例；只有明确交接前置职责时才传 false。Discard 仅抑制当前 Avatar/Montage/PlayInstanceId 的本机表现，不改变权威能力或移动阶段。新实例不继承旧丢弃；结束先到不创建已停止的新实例，已播放实例保留原生停止路径。
- 门控/Retry 只作用于非权威、非本地控制角色；重试合并至下一 Tick 并防重入，回调后复核注册版本和实例。Avatar 切换、ClearActorInfo、EndPlay 释放注册、起点缓存和计时器；ASC 临时注销暂停处理/释放计时器，保留弱注册并在重新注册后重试。失效提供者不能释放已放弃的实例。等待原因变化输出 Verbose，连续等待5秒输出一次 Warning并复核，不因超时自动放行；`Log LogIMGAbilitySystem Verbose` 查看 Avatar/Montage/Instance/Reason。

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

- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged、生命/死亡委托；装弹提供开始/结束事件，自动重生提供可配置消息 Tag。
- GameSettings.UGameSettingDetailView 通过 HandleCurrentSettingChanged 订阅当前 Setting 的变化并刷新描述/动态详情；DLC Registry 定时变更沿同一路径，不另轮询。
- 切换/清空 Setting、NativeDestruct 时退订；NativeConstruct 保留当前选中项并重建详情/扩展。切换、清空、关闭、释放 Slate 均取消并清空扩展异步句柄；完成/装配回调复核当前 Setting，避免旧请求或重入回填过期控件。
