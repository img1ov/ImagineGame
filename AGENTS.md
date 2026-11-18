# ImagineGame 工程约定与导航

## 工作约定

- **最高优先级｜沉淀每次探索的新发现。** 每次源码、配置、资产或工具调查收尾前，将新发现逐项归入本文件对应小节。已有条目直接补充或纠正，结构变化时清理过时内容。已验证事实、线索和待验证项分开写，保留可复核入口。调查过程与逐轮结果留在当次回复。
- 事实优先：先核对本地源码、官方资料或可复核案例，区分已验证事实、资产名线索和待验证项。改代码前找到调用链、同类实现与所属模块，在原职责处扩展，避免重复状态、分支和抽象。
- 新增 struct：用户指定目标 `.h` 时写入该文件；未指定时先调查合适位置，再请用户确定头文件。
- 保持单向有效依赖和模块独立性。仅在关键约束、不直观的生命周期与边界处写短注释；回复只给关键结论、证据、变更和必要风险。
- **业务判定归来源，通用调度与执行归消费者。** 来源使用本模块已有事实决定是否发起请求及运行时参数，通过统一入口消费下层机制；下层不反查来源业务类型。不因表现种类增加就建立总管理器，也不将各业务的判定集中到单个来源组件；先分离配置与实现，再按实际职责和生命周期决定是否提取独立组件。
- 未明确要求不编译；C++ 变更仍须核对引擎接口、访问权限、构建依赖、调用链和差异。不调查或修改 Content，除非用户要求。
- 不编写测试代码；运行测试由用户自行完成，发现问题后按反馈修复。交付前仍须完成必要的静态核对，不以此替代运行验证。
- 先扫本文标题，只读相关小节。工程导航、职责边界、接口合同、已确认限制、待办及必要调试用法统一归入 AGENTS。修复过程、编译/测试结果和逐轮审查记录留在当次回复。

### 收尾检查

- “全面检查、完整链路、收尾”须覆盖全部相关修改及受影响的上下游，沿配置消费、激活、执行/表现至结束清理追踪实际调用；增量复核应说明范围。
- 检查失败回滚、取消/中断、重复激活、对象失效与清理。涉及网络时分别追踪权威端、自主代理、模拟代理，核对预测与服务端重检、复制乱序、引用晚到及开始/结束先后关系。
- 核对配置是否实际生效、默认值与接口是否一致，临时状态是否成对恢复，委托、计时器、Tick 依赖和引用是否正确释放；发现问题在原职责内修复。
- 结论须说明实际检查范围和未验证项。差异格式检查、静态审查、编译和运行验证各有边界，不能相互替代；遵守用户对编译/测试的限制。

## 装备、背包与射击

### 组件、资产与授予

- 背包、装备、快捷栏、通用开火/换弹/自动换弹和远程追踪归 ImagineGame。具体武器资产、快捷栏输入、丢弃和原生拾取派生入口归 ShooterCore。PlayerController 装配 `UIMGInventoryManagerComponent`、`UIMGQuickBarComponent` 和 `UIMGWeaponStateComponent`，Pawn 装配 `UIMGEquipmentManagerComponent`。可在射击 Experience 或其 ActionSet 中用 Add Components 为对应类配置服务端与客户端组件。WeaponState 驱动当前远程武器的散布 Tick，并确认命中标记。
- 按 Lyra 实际资产组织：基础 InventoryManager、EquipmentManager、WeaponState 配置在根 GameFeatureData `/ShooterCore/ShooterCore` 的 Add Components；`/ShooterCore/Experiences/LAS_ShooterGame_StandardComponents` 添加 B_QuickBarComponent 等另一组组件，不包含前三者。本地 Lyra 两个资产的包内 Actions/组件类型与 Client/Server Bundle 引用已交叉核对；本工程同路径 ShooterCore GameFeatureData 当前未发现这三种组件或 AddComponents 引用。本工程推荐前三者归 ShooterCore GameFeatureData、QuickBar 归 IAS_ShooterGame_StandardComponents，各条 ClientComponent 与 ServerComponent 均勾选。此前将四者全放 ActionSet 是可行替代装配方案，不代表 Lyra 原样如此；同一组件不在两处重复配置。InventoryManager、EquipmentManager 当前未声明 BlueprintSpawnableComponent，不将角色/控制器蓝图直接 Add Component 作为统一装配路径。`AIMGPlayerState` 已创建 HealthSet 与 CombatSet，不在武器 AbilitySet 重复授予这两个属性集。
- `UIMGInventoryItemDefinition` 蓝图的 `EquippableItem.EquipmentDefinition` 指向 `UIMGEquipmentDefinition` 蓝图，`SetStats.InitialItemStats` 初始化每个物品实例的弹匣余量、备弹余量和弹匣容量。三个 Tag 由资产指定，各消费者使用同一组 Tag。准星和快捷栏图标分别配置 `ReticleConfig` 和 `QuickBarIcon` 片段。
- `EquipmentDefinition.InstanceType` 指向 `UIMGRangedWeaponInstance` 或其蓝图子类，`AbilitySetsToGrant` 引用武器 `UIMGAbilitySet`。`ActorsToSpawn` 配置枪体 Actor、`AttachSocket` 和 `AttachTransform`，角色装备挂到角色 Mesh。需要远端显示时由枪体 Actor 配置复制。
- 装备可见与掏枪动画是不同接点：`SpawnEquipmentActors` 已在 C++ 生成枪体并挂到 Character.GetMesh，`OnEquipped/OnUnequipped` 转发蓝图事件；IMGWeaponInstance 现提供 EquippedAnimSet 的晚就绪/外观变化动画层保底（见下节），但无 EquipMontage 自动播放链；掏枪与卸下 Montage 仍由现有装备蓝图处理，不能因动画层保底而认定整个蓝图表现已迁入 C++。
- Lyra `/ShooterCore/Weapons/B_WeaponInstance_Base` 包内确认有蓝图字段 WeaponEquipMontage、WeaponUnequipMontage、MeleeAttackMontage，以及 OnEquipped/OnUnequipped、LinkAnimClassLayers、PlayMontage 节点引用；这三个 Montage 字段不在本地 Lyra 的 WeaponInstance/RangedWeaponInstance C++ 中。本工程 IMGWeaponInstance 也无这些字段和自动装备动画执行，属共享蓝图表现尚未迁入的缺口；直接派生原生 IMGRangedWeaponInstance 的 Rifle 不会继承该蓝图字段，不能仅新增配置字段就认定链路补齐。
- 用户选择完整装备表现保留蓝图，C++ 仅增加下节的动画层晚就绪保底。已核对本工程 `/ShooterCore/Weapons/B_RangedWeaponInstance_Base` 文件及包内原生父类 IMGRangedWeaponInstance、装备/卸下事件、动画层链接与 Montage 播放引用；`/ShooterCore/Weapons/Rifle/B_WeaponInstance_Rifle` 的包内父类引用指向该蓝图，并存在 WeaponEquipMontage。后续配置沿这个实际名称继续，不再要求重复创建 B_WeaponInstance_Base；这里只确认静态引用，蓝图编译状态与运行行为尚未验证。
- `FIMGAnimLayerSelectionSet` 的 LayerRules 按顺序取首个 Layer 非空且 CosmeticTags 满足 RequiredTags 的条目，无匹配返回 DefaultLayer；它只选择 AnimInstance 类，不执行挂接或播放。角色动画采用何种分层/Overlay 架构须先确认，不能直接将任意持枪 AnimBP 当作可链接层。
- Rifle 资产命名与目录参考已核对的 Lyra 文件及包内引用：`/ShooterCore/Weapons/Rifle/{B_Rifle,B_WeaponInstance_Rifle,WID_Rifle,ID_Rifle,AbilitySet_ShooterRifle,GA_Weapon_Fire_Rifle_Auto,GA_Weapon_Reload_Rifle,GE_Damage_RifleAuto,WeaponPickupData_Rifle}`；WID_Rifle 是 EquipmentDefinition，ID_Rifle 才是 InventoryItemDefinition。`B_`、`GA_`、`GE_`、`WID_`、`ID_` 为用途前缀，保留；Lyra 专有名称替换为 IMG，`LAS_`（Lyra ActionSet）在本工程使用 `IAS_`。现有同名/相关资产不因此视为有效配置，不直接迁入 Lyra 蓝图逻辑。
- Lyra 的 WID_Rifle 包内有 AttachSocket 与 `weapon_r`；B_Rifle 引用 `/Game/Weapons/Rifle/Mesh/SK_Rifle`，WeaponPickupData_Rifle 引用同目录 `SM_Rifle` 与 ShooterCore 的 ID_Rifle。模型资源仍可在 `/Game/Weapons/Rifle`，玩法定义位于 ShooterCore；本工程实际骨架挂点及蓝图默认值须在编辑器核对。
- Lyra 的 `/ShooterCore/Experiences/LAS_ShooterGame_StandardComponents` 与 `LAS_ShooterGame_SharedInput` 分别装配附加组件和输入，本工程对应 `/ShooterCore/Experiences/IAS_ShooterGame_StandardComponents`、`IAS_ShooterGame_SharedInput`。StandardComponents 不能与根 `/ShooterCore/ShooterCore` GameFeatureData 的组件 Actions 混同，两者在不同入口激活。本工程已有 Experience 文件 `/ShooterCore/System/Experiences/B_ExperienceDef_ShooterCore`，可在其 ActionSets 引用这两项，并通过 GameFeaturesToEnable 请求 ShooterCore；存在性与类型已核对，当前具体配置仍需编辑器确认。
- 服务端发放沿 `AddItemDefinition` → `AddItemToSlot` → `SetActiveSlotIndex`，须在 Pawn ASC 初始化及装备组件就绪后执行；`FIMGEquipmentList::AddEntry` 找不到 ASC 时仍创建装备，不会事后补授能力。EquipmentManager 在当前 Pawn 的 ASC 上授予能力，Spec 的 SourceObject 为装备实例，QuickBar 随后将物品实例设为装备的 Instigator。开火/换弹用这两个引用读取武器和弹药，故由装备 AbilitySet 授予并保留 `InstancedPerActor`。切槽卸下旧装备并清除其能力、持续效果和枪体，弹药随仍在背包里的物品实例保留。Instant GE 已执行的属性修改不随 AbilitySet 收回；武器专属 BaseDamage 应用可移除的 Infinite GE，避免切枪累加。初始发放和死亡清理依赖蓝图装配。

### 换装组件与装备动画的动态装配边界

- 装配合同：Pawn/Character 本体通过蓝图组件或原生默认子对象预装一个 Pawn Parts，早于 Possess/初始装备查询；Controller Parts 可由 GF 动态添加，只在权威端决定和应用角色部件。不在 GF 再注入第二份 Pawn Parts。动态 Pawn Parts 晚到属于兼容保底，不保证重播首次掏枪 Montage；两组件头文件已用英文注明该边界及生命周期。
- 已实现：`IMGControllerComponent_CharacterParts` 保存实际承接句柄的 Pawn Parts 弱引用。已注册的预装组件在 BeginPlay 前也可承接部件，保留原 Possess 时序；缺组件时保留请求并 Warning，收到 `CharacterPartsReady` 后仅补未应用且未被 Cheat 抑制的条目。GF 添加 Controller 时 BeginPlay 追补既有 Possess；换 Pawn、组件替换/移除、Controller EndPlay 清理旧句柄与监听。部件请求可跨 Possess 保留，句柄不能跨 Pawn Parts 实例复用；取消请求后晚到事件不得重新添加。
- 已验证：Pawn Parts 默认复制，CharacterPartList 无自主/模拟代理过滤。UE `GameFrameworkComponentManager.cpp::CreateComponentOnInstance` 对复制组件只在 ROLE_Authority 创建，客户端等待网络复制；GF 的 Client 勾选不保证客户端 Possess/装备回调前存在组件。专服不生成外观 Actor；监听服/单机及客户端生成本机外观。
- 引擎接口：`GameFrameworkComponentManager.h` 中 `FComponentRequestHandle` 为 struct，前置声明须保持一致。共享句柄维持扩展监听，销毁句柄会同步发送 ExtensionRemoved；业务处理区分该事件与 ReceiverRemoved/PartsRemoved，不因解除自己的监听而清空他方外观。
- 生命周期合同：Pawn Parts 在 Super::BeginPlay 后发送 `CharacterPartsReady`，EndPlay 清部件前发送 `CharacterPartsRemoved`。Ready 只说明组件可用，不表示客户端部件 FastArray 已到齐。PostReplicatedAdd/Change/Remove 经 BroadcastChanged 更新 BodyMeshes，再发 OnCharacterPartsChanged；委托不回放历史，消费者先订阅并读取快照。PawnExtension.GameplayReady 不保证 Parts 就绪。UE Mesh.InitAnim 可能重建 LinkedInstances，OnAnimInitialized 后才适合补动画层。
- 武器保底：配置了 EquippedAnimSet 的 IMGWeaponInstance 在非专服 OnEquipped 进入蓝图前仅建立观察并安排下一 Tick（不即时 Link），同步 OnUnequipped 可取消本轮；正常蓝图仍优先执行装备/卸下事件、选层与 Montage。Parts 就绪/变更、Mesh.OnAnimInitialized 合并刷新；当前 DefaultLayer 或任一 LayerRule 的层若已由正常链链接则直接保留，不按本地 Tags 再覆盖。仅缺层或本方保底层待部件晚到更新时，按现有 PickBestAnimLayer 补 Link；补 Link 后复核当前监听、组件及主 AnimInstance，避免中断/重建后的旧回调回写。
- 表现所有权：C++ 保底不调用 UnlinkAnimClassLayers、不重播装备/卸下事件或 Montage、不重置热量与设备效果。层切换及卸下/死亡过渡继续归蓝图，LinkedInstance 生命周期由 Mesh 原生管理；C++ 只保留 Mesh/补层实例弱引用区分保底结果。卸下、Pawn ReceiverRemoved 或观察到死亡时取消待刷新并解绑；Parts 移除解除旧组件委托但不强拆当前表现，新组件就绪再读取快照。死亡与组件卸载不强制恢复持枪层；没有配置 EquippedAnimSet 的武器不启用该保底。
- Warning 入口为 LogIMG：Controller 有待应用部件却缺 Pawn Parts 时记录 Controller/Pawn 路径及装配提示；武器缺 Parts/Mesh/AnimInstance、无匹配层或 Link 失败时记录配置提示。同次缺失抑制重复告警，正常恢复后允许后续缺失再次告警。没有持续轮询、固定等待时长或新增复制状态。
- 已验证（用户提供的蓝图文本）：服务端初始发放沿 ReceivePossessed → Authority → WaitForExperienceReady.OnReady → WaitForInventoryReadiness → AddInitialInventory；Remote 未连接。宏内部条件未提供，不能由宏名推定 ASC/Parts 就绪，也不能保证客户端复制顺序。客户端装备表现由 Equipment FastArray.PostReplicatedAdd → OnEquipped 触发，Parts 与装备之间没有网络到达顺序保证。
- 独立上游边界：传统 FastArraySerializer 的 UpdateUnmappedObjects 用 PostReplicatedChange 通知引用晚映射，而本工程 EquipmentList.PostReplicatedChange 当前为空；若新增项的 Instance 首次为空，现有逻辑可能漏掉 OnEquipped。本次保底以既有 OnEquipped 已分派为前提，未扩改装备复制回调；当前复制路径与该场景是否触发仍待专项运行验证。
- 待验证：新增收紧后的 GF 两组件动态装配、Parts 移除重加、切枪/死亡过渡与潜在蓝图潜伏回调，仍须 PIE 验证；用户提供的装备事件截图不含宏内部，未修改或推断其内部过渡实现。基础动画仍须提供兼容骨架、层接口和有效 EquippedAnimSet；该保底不解决能力授予、弹药、输入或其他系统主动换层的通用优先级。

### 输入、开火与换弹

- `UIMGAbilitySet.GrantedGameplayAbilities[].InputTag` 与 `UIMGInputConfig.AbilityInputActions` 精确匹配，IMC 将按键映射到对应 Input Action。InputConfig 可由 PawnData 提供，或由射击 Game Feature 的 `Add Input Binds` 提供，IMC 由 Hero 的默认映射或 `Add Input Mapping` 装载。
- Hero 的 `InitializePlayerInput` 当前将 `AddMappingContext` 放在 `Mapping.bRegisterWithSettings` 分支内；使用 Hero.DefaultInputMappings 时该选项必须为 true（默认即 true），否则不会实际装载 IMC。给现有 PawnData.InputConfig 增加武器 AbilityInputActions，或复制完整配置再扩展，避免用仅含武器输入的配置替换现有移动/视角绑定。
- Lyra `/Game/Input/InputData_Hero` 的包内引用包含 Weapon.Fire/FireAuto/Reload；本工程同路径资产当前只发现移动、瞄准等输入 Tag，无 Weapon 输入。射击输入可放 `/ShooterCore/Input/Actions/InputData_ShooterGame_AddOns`，由 SharedInput 的 Add Input Binds 接入；IMC 放 `/ShooterCore/Input/Mappings/IMC_ShooterGame`，由同一 ActionSet 的 Add Input Mapping 装载，保留 PawnData 原 InputData_Hero。`/Game/Input/Actions/IA_Weapon_Fire`、`IA_Weapon_Fire_Auto`、`IA_Weapon_Reload` 两工程均有文件；存在性不等于字段已有效配置。Lyra 共享 GA 位于 `/Game/Weapons/GA_Weapon_Fire`、`GA_Weapon_ReloadMagazine`、`GA_Weapon_AutoReload`；本工程已有 C++ 实现，Rifle Fire/Reload 可直接派生对应原生类，不必复制共享蓝图执行图。配置本工程武器 Tag 的 AutoReload 蓝图可置 `/ShooterCore/Weapons/GA_Weapon_AutoReload`。
- 开火 GA 继承 `UIMGGameplayAbility_WeaponFire`，必填 `MagazineAmmoTag` 和 `DamageEffect`。`OnInputTriggered` 每次按下尝试一发，`WhileInputActive` 按住时在上一发 GA 结束后重新尝试。`FireIntervalSeconds` 默认 0.1 秒。开火 Montage 和 Fire/Impact Cue 可选。C++ 已在提交射击时由服务端扣一发弹匣余量，额外 InventoryItem Cost 会再次扣同一弹药。
- 换弹 GA 继承 `UIMGGameplayAbility_WeaponReload`，设置 `MagazineAmmoTag`、`SpareAmmoTag`、`MagazineSizeTag`。无 `ReloadMontage` 时以 `ReloadDurationSeconds` 完成，默认 1 秒。有 Montage 时由完成回调或指定的 `ReloadCompleteEventTag` 完成，先到者生效。提交后服务端按弹匣缺口与备弹取较小值转移弹药。需用 Notify 提前完成时，确保服务端也收到该事件。换弹持有 `Ability.Weapon.NoFiring`，开火 GA 的 `ActivationBlockedTags` 配置同一 Tag 才能覆盖普通按键激活，基类的 `SourceBlockedTags` 只检查传入的来源标签。
- 自动换弹 GA 继承 `UIMGGameplayAbility_AutoReload`，保留 `OnSpawn` / `LocalOnly`，与开火、换弹一起由装备授予，输入 Tag 留空。配置弹匣、备弹 Tag 和 `ReloadEventTag`，在换弹 GA 的 `Ability Triggers` 添加同一 Tag，触发源选 Gameplay Event。默认每 0.25 秒检查，距装备或上次开火至少 0.5 秒、空仓且有备弹时发送换弹请求。请求 Tag 与 Montage 完成 Tag 分开。
- 射击玩法配置 Tag 统一归 `Plugins/GameFeatures/ShooterCore/Config/Tags/ShooterCoreTags.ini`，使用 `[/Script/GameplayTags.GameplayTagsList]`，不放项目 `DefaultGameplayTags.ini`：`InputTag.Weapon.Fire/Reload`，物品栈 `Weapon.Stat.MagazineAmmo/SpareAmmo/MagazineSize`，自动换弹请求 `Event.Weapon.Reload`，可选 Montage 完成事件 `Event.Weapon.ReloadComplete`，以及 `Ability.Type.Action.WeaponFire/Reload`、`Ability.Type.Passive.AutoReload` 分类。通用移动等 Tag 保留项目配置，核心 C++ 的 native tag 保留原所有权。输入和分类名参考 Lyra；不照搬 Lyra 的 `Event.Movement.Reload` / `GameplayEvent.ReloadDone`，武器事件统一归 `Event.Weapon.*`，弹药命名使用通用 `Weapon.Stat.*`。两个事件消费者均读取可配置 FGameplayTag 属性，无硬编码名称；请求配置在 AutoReload.ReloadEventTag 与换弹 GA 的 Gameplay Event Trigger，完成事件配置在 ReloadCompleteEventTag 与发送端。登记不会自动绑定能力、事件或弹药。单枪装备与射击无需 UI、切槽 Tag 或单独 FireAuto 输入 Tag；全自动用同一 Fire 输入配 `WhileInputActive`。
- Game Feature 的 Tag ini 由引擎 `GameFeaturePluginStateMachine.cpp` 的 Registering.BeginState 经 `UGameplayTagsManager::AddTagIniSearchPath(PluginFolder/Config/Tags)` 自动导入；Unregistering 请求移除，实际卸载受 GameplayTagsSettings 控制。ShooterCore 须进入注册阶段才能依赖这些配置 Tag；通用武器 C++ 继续只消费资产传入的 FGameplayTag，不反向硬编码 ShooterCore 配置或在模块 StartupModule 重复注册。
- `Ability.Weapon.NoFiring`、`Status.Death` 和 `IMG.Weapon.SteadyAimingCamera` 已由 C++ native tag 注册，不重复写 ini。`UE_DEFINE_GAMEPLAY_TAG_STATIC` 的 static 仅限制 C++ 符号作用域，不限制 Tag 在资产选择器中使用；引擎入口为 `FNativeGameplayTag` 构造及 `UGameplayTagsManager::AddNativeGameplayTag`。现有瞄准输入为 `InputTag.Aim`，无需为单枪射击另加 Lyra 的 ADS 输入名。
- `UIMGGameplayAbility_WeaponFire::ActivateAbility` 显式调用 `UIMGGameplayAbility_FromEquipment::ActivateAbility`（实际落到 `UIMGGameplayAbility::ActivateAbility`），所以 `UIMGGameplayAbility_RangedWeapon::ActivateAbility` 的 TargetData 委托绑定和 `OnRangedWeaponTargetDataReady`（Lyra 蓝图出道口）不在开火 GA 链上；只有直接派生 `RangedWeapon` 并自行调用 `StartRangedWeaponTargeting` 的蓝图才走那条路。
- 开火 `CanActivateAbility` 同时要求 `MagazineAmmoTag`、`DamageEffect`、物品弹匣≥1 且距本实例 `LastShotTime` 达 `FireIntervalSeconds`；缺任一项按键静默失效（无 UI 提示）。装备能力必须由装备的 `AbilitySetsToGrant` 授予，放进 PawnData 会因 `Spec.SourceObject` 为空而永远无法激活。`UIMGGameplayAbility_DropWeapon` 为 ServerOnly，移除快捷栏与背包后不生成地面掉落实体（源码 TODO）。
- 快捷栏选槽的事件发送端不在 C++：`UIMGGameplayAbility_QuickBarSlots` 只等 `SelectSlotEventTag`/循环 Tag，槽号取 `EventMagnitude` 截断为整数；需要有 BP 发该 GameplayEvent，或直接调 `UIMGQuickBarComponent::SetActiveSlotIndex`（Server RPC）。装备实例的 `Instigator` 在 `FIMGEquipmentList::AddEntry` 之后才由 QuickBar 设置，故 OnSpawn 装备能力激活瞬间 `GetAssociatedItem()` 为空，只能依赖后续轮询。

### 命中、伤害与拾取

- 开火走 `CameraTowardsFocus` 的即时追踪。玩家从最终视点计算方向和起点，服务端校验 TargetData 的起点距离、射程、条数及遮挡。`BulletsPerCartridge` 是每次消耗一发弹药产生的射线数，`MaxDamageRange` 单位 cm，`BulletTraceSweepRadius` 默认为 0。散布用热量三条曲线和姿态倍率，ADS 倍率读取镜头模式 Tag `IMG.Weapon.SteadyAimingCamera` 的混合权重。`SpreadRecoveryCooldownDelay` 当前读取的 `LastFireTime` 未随开火更新，开火后延迟恢复仍需修复；本地 Lyra 的同名字段也只有初值与读取，并非本工程移植时遗漏。两边构造函数均已提供单发加热=1、每秒冷却=2的曲线默认键，不必把三条曲线全部自定义视为开火前提。
- 追踪与重检都使用 `IMG_TraceChannel_Weapon`（`ECC_GameTraceChannel2`）。项目 `Config/DefaultEngine.ini` 登记 Channel1=`Traversable`（保持默认 Block）、Channel2=`Weapon`（默认 Ignore），并提供 `AIMGCharacter` 请求的 `IMGPawnCapsule` / `IMGPawnMesh` Profile（均 QueryOnly、Pawn 类型）：胶囊对 Weapon Ignore，Mesh 对 Weapon Block。角色 Mesh/Physics Asset 仍须启用查询碰撞，既有蓝图/实例的碰撞覆盖须核对。配置改动后重启编辑器，用 `IMG.Weapon.DrawBulletTraceDuration 2` 对照射线与碰撞响应。
- 常规 `DamageEffect` 用 Instant GE 的 `UIMGDamageExecution`。执行捕获射手 `UIMGCombatSet.BaseDamage`（Source、Snapshot=true，属性 C++ 默认 0），再乘距离曲线、物理材质 Tag 倍率和 TeamSubsystem 裁决。Lyra 的 `/ShooterCore/Weapons/Rifle/GE_Damage_RifleAuto` 包内确认有 CalculationModifiers、CapturedAttribute、BaseDamage 与 LyraDamageExecution；本工程可在对应 GE 的 Executions[0].CalculationModifiers 对捕获的 Source BaseDamage 做 Override（首轮可配20），引擎 `FGameplayEffectCustomExecutionParameters` 的 scoped aggregator 会供 AttemptCalculateCapturedAttributeMagnitude 读取，不改射手持久属性，不必另授予伤害 GE。该值为本工程建议，未据此宣称 Lyra 的具体数值。也可由射手初始化 GE 或装备的可移除持续 GE 提供 BaseDamage，按所选模型配置，不重复叠加。目标需 ASC 和 HealthSet。首轮验收用异队目标，观察服务端生命与物品弹药。
- 场景拾取用 `AIMGShooterWeaponSpawner`，实例 `WeaponDefinition` 指向 `UIMGWeaponPickupDefinition`，填物品定义、展示 Mesh、正数冷却和 Pawn Overlap。基类 `AIMGWeaponSpawner` 的发放走蓝图 `GiveWeapon`。Shooter 派生类服务端发物品并放入空槽，默认选中该槽，`BlockAutoEquipTag` 可阻止切换。启用 `bRefillDuplicateWeaponAmmo` 时，按同一 `SpareAmmoTag` 将已有武器的备弹补至物品默认值。`UIMGGameplayAbility_QuickBarSlots` 监听选槽和前后切槽事件，选槽索引用 `EventMagnitude`，从 0 开始。`DropWeapon` 当前移除快捷栏及背包物品，地面掉落实体待接入。
- 伤害落链：`ApplyGameplayEffectToTarget` 经 `FGameplayAbilityTargetData_SingleTargetHit::GetActors` 向命中 Actor 的 ASC 应用；此路径由 target data 补齐 EffectContext 的 HitResult/Origin，`UIMGDamageExecution` 因此能取到物理材质与距离。命中挂接在 Pawn 上的子 Actor（枪体等）时 `HandleTargetData` 再按 `GetAttachParentActor` 给带 ASC 的父 Actor 补一次，但该分支新建 ActorArray，未保留 HitResult、也未设置 SourceLocation，不能认定物理材质与距离衰减正确。服务端仅对可追溯到 ASC 的目标重扫，用同一通道从客户端 TraceStart→TraceEnd 命中同一目标才通过；无 ASC 的场景命中不走此重扫。`MaxTraceOriginDistance` 默认 350 约束起点距 Avatar，`TraceRangeTolerance` 默认 350 叠加在 `MaxDamageRange` 上，条数上限为 `BulletsPerCartridge × MaxTargetDataPerBullet`(8)。
- Weapon 通道沿用 Lyra 的默认 Ignore 策略；`DefaultEngine.ini` 的 `+EditProfiles` 已给 `BlockAll/BlockAllDynamic/InvisibleWall/InvisibleWallDynamic` 显式设为 Weapon Block，引擎 `UCollisionProfile::LoadProfileConfig` / `FillProfileData` 负责合并通道、Profile 与 EditProfiles。其他自定义墙体/可受击物体若需挡弹，须显式配置 Weapon Block；不能仅由 ObjectType 推定响应。`IMG_TraceChannel_Weapon_Capsule/Multi`(Channel3/4) 目前只有头文件声明、无消费方，暂不登记；Channel5 的 aim assist 也未接入。
- 表现侧未接：`UIMGWeaponUserInterface::RebuildWidgetFromWeapon` 与 `UIMGInputComponent::AddInputMappings/RemoveInputMappings` 都是空实现，准星按 `ReticleWidgets` 组装和 IMC 额外装载都要自己接（IMC 走 Hero `DefaultInputMappings`、GameFeature `Add Input Mapping` 或设置注册）。
- 已有可选表现入口：WeaponFire 的 FireMontage、FireCueTag、ImpactCueTag，WeaponReload 的 ReloadMontage、K2_OnReloadStarted/K2_OnReloadEnded，WeaponUserInterface 的 OnWeaponChanged，物品的 ReticleConfig/QuickBarIcon。Fire Cue 当前 Location 取 Avatar 位置，并携带武器实例 SourceObject；后续枪口特效须自行定位枪口，不能把它当作已完成的枪口表现系统。纯逻辑验收可将 Montage/Cue 留空，并省略两个 UI Fragment。
- Lyra `/Game/Weapons/B_Weapon` 的用户导出 `D:/B_Weapon.T3D` 已核对：父类 Actor，Fire 组织独立 B_WeaponFire/B_WeaponImpacts/B_WeaponDecals 表现 Actor；TriggerFireAudio 管附着音频与触发参数，Add Fake Projectile Data 做补充表现追踪并填入命中数组，另有队伍配色和本地控制相关 CustomStencil。导出图未调用伤害 GE、能力提交或物品弹药扣除。ImagineGame/ShooterCore C++ 当前无等价完整枪体表现基类，FireCueTag/ImpactCueTag 也不会自动调用该蓝图 Fire；首轮 B_Rifle 可直接继承 Actor，持枪与射击逻辑不以迁入该表现基类为前提，后续按需接入。
- 以上装配合同只由源码与项目配置确定；不要依据现有 Content 里的武器相关资产推断配置，其中有无效资源，需要时按源码重建。

## 工程方向与参考源码

- UE 5.8；Gameplay 核心用 C++，蓝图用于配置、装配、转发及明确保留的移动计算；暂不接脚本。
- 命名按职责区分：项目专有类型用 `IMG`；通用概念遵循 UE 类型前缀及同目录风格，不因定义在本工程就机械添加 `IMG`，例如步态枚举用 `EGait`，不用 `EIMGGait`。不沿用参考项目的专有名称或前缀，不为统一前缀批量重命名既有类型。
- `Source/ImagineGame` 为共享运行时，`Source/ImagineEditor` 为编辑器模块；`Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime` 放射击玩法派生规则并依赖 ImagineGame，共享层不反向依赖玩法。
- 新规则先核对所有者、权限、复制与生命周期；共享机制扩展核心职责，玩法规则放对应 Game Feature。
- Lyra 参考：`D:/Application/UnrealProjects/LyraStarterGame`（已核对本地源码）。核心源码保留本工程移动、Bot、Traversal 扩展；TopDownArena、LyraExtTool、自动化示例与发布脚本未迁入。文件对应不代表行为一致。
- GTA 参考当前位于 `C:/Application/gta5/src/dev_ng/game`，工程导航见 `C:/Application/gta5/AGENTS.md`；原记录 `D:/Workspace/Learning/Sources/GTAV Source` 当前不存在。`D:/Application/GTA/GTAVSP/GTAV Source` 当前为空目录，不作为源码入口。仅作为本地源码快照对照，不代表所有发行版本启用的能力；保留 UE/GAS/CMC 网络体系。
- UE 源码当前位于 `D:/Application/UnrealEngine/UE_5.8/Engine/Source`，`Engine/Build/Build.version` 为 5.8.4；原记录 `D:/Workspace/UnrealEngine/UE_5.8` 当前不存在。GAS 在 `Engine/Plugins/Runtime/GameplayAbilities`，MotionWarping 在 `Engine/Plugins/Animation/MotionWarping`。当前工程有 Game/Editor Target 和编辑器 Validator/ContentValidationCommandlet；独立 Client/Server、渠道 Target 按打包需求设计。

### 编辑器资产搜索缓存诊断

- 已验证：`LogFileInfo` 来自引擎 `Engine/Plugins/Editor/AssetSearch/Source/Private/FileInfoDatabase.cpp`；`FFileInfoDatabase::Open` 以读写/可创建模式打开 `Saved/Search/FileInfo.db`，保存文件路径、修改时间与哈希。`AssetSearchManager.cpp::TryConnectToDatabase` 同时连接该库及 `AssetSearch.db`，失败后按至少30秒间隔重试；这属于编辑器资产搜索索引链。
- 排查入口：`LogSQLiteDatabase ... disk I/O error` 表示数据库打开阶段的I/O失败，仅凭该日志不能判定硬盘损坏或数据库损坏。先退出使用该工程的编辑器，再将 `Saved/Search` 整体改名备份，重启让搜索缓存重建；仍失败再核对目录写权限、其他进程占用及存储错误。不要在数据库打开期间单独移除日志文件，也不要为此删除整个Saved。
- 待验证：具体I/O根因须结合现场进程、文件访问和系统存储日志确认；缓存重建是否恢复须由实际重启结果判断。

### ImagineEditor 目录与接口

- 用户明确要求ImagineEditor按文件类型分离：所有 `.h` 放 `Public`，所有 `.cpp` 放 `Private`，配对文件使用两侧相同的相对目录。不因内部辅助用途把头文件留在Private，也不为这次分离额外拆出System/Style/Feedback/Rewind/Export子目录；已有动画、验证和Traversal分类保留。文件搬到Public不代表自动增加跨模块DLL导出API。

| 目录（相对Source/ImagineEditor） | 职责 |
| --- | --- |
| `Public/*.h`、`Private/*.cpp` | 模块入口、ImagineEditorEngine、ImagineEditorStyle、ContextEffects Factory与AssetTypeActions。 |
| `Public/AnimationModifiers`、`Private/AnimationModifiers` | 动画修改器及FootstepUtils。 |
| `Public/Animation`、`Private/Animation` | 自定义 AnimGraph 节点的编辑器包装；运行时节点及 Proxy 归 ImagineGame/Animation。 |
| `Public/Validation`、`Private/Validation` | 资产验证器。 |
| `Public/Commandlets`、`Private/Commandlets` | ContentValidationCommandlet。 |
| `Private/Utilities` | 独立编辑器工具与控制台操作。 |
| `{Public,Private}/Character/Traversal` | Rewind轨道/回看接合及事件分类。 |
| `{Public,Private}/Character/Traversal/Trace` | Trace分析存储及文本导出。 |
| `{Public,Private}/Character/Traversal/Widgets` | Details与Slate控件。 |

- 模块内头文件include从Public根写相对路径，不使用 `../` 穿越或依赖Private同目录头搜索。公开头直接包含的AssetTools/Slate/SlateCore/RewindDebuggerInterface/TraceServices列为Public依赖，TraceAnalysis及PropertyEditor实现依赖仍留Private。UBT自动发现子目录CPP，无需为搬迁增加IncludePaths。只改路径不改反射类型、模块名或 `/Script/ImagineEditor.*` 配置身份；生成头及UE_INLINE_GENERATED_CPP_BY_NAME继续使用原类型文件名。
- ImagineEditor 的加载类型继续为 `.uproject` 中的 Editor；因模块包含序列化到运行时 AnimBP 的 AnimGraph 编辑器节点，Build.cs 用 `OverridePackageType = PackageOverrideType.GameUncookedOnly` 标记脚本包。UBT 将覆盖类型传给 UHT，`EpicGames.UHT/Types/UhtModule.cs` 生成 PKG_UncookedOnly，避免纯 PKG_EditorOnly 包触发 `BlueprintEditorUtils.cpp::ValidateEditorOnlyNodes` 的运行时蓝图警告；该检查由 UK2Node 早期校验调用，不要求节点已连入执行链。模块仍不进入游戏运行时构建，运行时 FAnimNode 继续归 ImagineGame。此类 Build/UHT 包标记改动须正常重编译并重启编辑器验证，不能只依赖旧进程中的 Live Coding。
- 启动配置排查：双击 `.uproject` 的普通编辑器启动加载 Development 产物 `UnrealEditor-ImagineGame.dll` / `UnrealEditor-ImagineEditor.dll`；VS 的 DebugGame Editor 加载带 `-Win64-DebugGame` 后缀的另一套产物，分别由对应 `.modules` 清单映射。只更新 DebugGame 不会同步 Development。新增节点在 VS 可见、普通启动缺失时，先对照 `Saved/Logs/ImagineGame.log` 的 Build Configuration、InternalLoadLibrary 路径及两套 DLL 时间；旧运行时 DLL 可导致 FStructProperty Unknown structure，旧编辑器 DLL 可导致节点类加载失败。此时应构建 ImagineGameEditor Win64 Development 并重启，不能将其误诊为类型重命名并添加 Redirect。加载缺失节点的会话中不要保存受影响 AnimBP，先恢复匹配二进制再回读资产；本地已确认过两套 DLL 时间不同引起的这一故障。

### GTA 3C 源码导航与映射

- GTA 的 Player/AI 行为、Movement、Motion、局部动画分别有任务职责；入口为 `Peds/Ped.cpp::ProcessControl_Intelligence` → `PedMotionData.ProcessPreTaskTrees` → `PedIntelligence.Process` → `ProcessPostTaskTrees`。`scene/world/GameWorld.cpp::ProcessAfterAllMovement` 处理移动后任务与 PreCamera，PostCamera 另有回调。借鉴其意图、移动、表现和镜头的执行边界；本工程继续由 GAS 管能力、CMC 管移动与预测、Animation 管表现。
- 下表路径相对 GTA 的 `src/dev_ng/game`；ImagineGame 落点表示职责映射，不代表相应功能已经实现。

| 系统 | GTA 源码入口 | 可借鉴合同与 ImagineGame 落点 |
| --- | --- | --- |
| 输入与控制朝向 | `task/Default/TaskPlayer.cpp` 的 `CTaskPlayerOnFoot`、`CTaskMovePlayer`；`camera/CamInterface.cpp` | 玩家行为解释输入，MovePlayer 分派普通/Strafe/水中移动；控制 Heading/Pitch、瞄准 Frame 有独立查询。Hero 保留移动意图入口，镜头扩展须核对 ControlRotation 与最终画面的关系。 |
| 普通移动与步态 | `task/Motion/Locomotion/TaskMotionPed.*`、`TaskHumanLocomotion.*`；`task/Motion/TaskMotionBase.cpp::UpdateGaitReduction` | MotionPed 分派普通/瞄准/Strafe/潜行等；HumanLocomotion 有 IdleTurn、Start、Moving、Turn180、Stop、Reposition；GaitReduction 对照实际/动画速度及阻挡。规则扩展现有 Locomotion，启停/转向表现归 Animation，不复制真实速度/移动阶段。 |
| 情境镜头 | `camera/gameplay/GameplayDirector.*`、`follow/FollowPedCamera.*` | Director 消费角色、武器、掩体、水中等事实；FollowPed 有 Ragdoll/GetUp 与梯子对齐行为。扩展 CameraMode/选择规则，玩法提供镜头需求；镜头不取得移动状态所有权。 |
| 身体接触与 IK | `ik/IkRequest.h`、`IkManager.*`、`solvers/{LegSolver,ArmIkSolver,BodyLookSolver,TorsoReactSolver}.*` | 请求携带目标、混合参数，BodyLook 有优先级；LegSolver 有坡面/楼梯/动态支撑的脚与骨盆处理，TorsoReact 接收身体反应请求。映射 Animation/独立表现适配；目标失效、动作中断与优先级交接须释放本方请求。 |
| 受击与碰撞反应 | `task/Physics/TaskNM.*`、`TaskNMShot.*`；`task/Movement/TaskCollisionResponse.*`；`ik/solvers/TorsoReactSolver.*` | 局部身体反应、撞墙/避让与物理倒地有独立执行入口。伤害/反应规则由玩法或 GA 决策，表现适配执行；Ragdoll 移动层只消费跟随事实。恢复主链见 Ragdoll 小节的 `TaskGetUp` 对照。 |
| 掩体 | `task/Combat/Cover/TaskCover.*`；`camera/gameplay/GameplayDirector.cpp::UpdateFollowPedCoverSettings` | Seek/Enter/InCover/Exit、探头/盲射/转角及镜头协作；存在本地与 Clone 掩体信息消费。几何/接触机制可共享，射击交互规则归 ShooterCore；与武器、动画、镜头分别接合。 |
| 梯子与主动下沿 | `task/Movement/Climbing/{TaskGoToAndClimbLadder,TaskLadderClimb,DropDownDetector,TaskDropDown}.*` | 梯子区分接近、挂载、持续上下、滑降、阻挡和脱离；DropDown 独立检测与执行。持续交互需独立会话/移动合同，现有 Hurdle/Vault/Mantle 的一次 Montage 合同不能直接视为持续攀爬实现。 |
| AI 特殊路径动作 | `task/Movement/TaskNavBase.*`；`task/General/TaskBasic.*` 的 `CTaskComplexControlMovement` | 导航遇梯子时交接执行，成功恢复 FollowingPath，失败结束当前导航任务；LOD 可禁止 climb/drop/ladder。ImagineGame 可用导航链接适配现有 GA，须处理接近、接管、取消、失败重规划和路径恢复。 |
| 战斗翻滚 | `task/Motion/Locomotion/TaskCombatRoll.*` | Initial/RollLaunch/RollLand/Quit、方向与中止清理独立于 Vault。适合独立 GA，沿正常 Root Motion/CMC 执行，射击限制归玩法。 |
| 游泳与潜水 | `task/Motion/Locomotion/TaskInWater.*` | 水面与潜水分开 Motion task，有水位/净空、入水与游动过渡。MOVE_Swimming 只提供引擎移动基础；完整水中规则、表现与镜头需另行接合。 |
| 载体与上下车 | `task/Vehicle/{TaskGoToVehicleDoor,TaskEnterVehicle,TaskInVehicle,TaskExitVehicle}.*`；`task/Physics/TaskNM.cpp::OnMovingGround` | 门/座位与进入退出是交互执行链；移动地面有参照速度处理。Walking 复用 BasedMovement，Traversal/Ragdoll 的动态参照须扩展各自原合同；车辆规则归玩法模块。 |
| 场景交互 | `task/Scenario/Types/TaskUseScenario.*`、`task/Scenario/ScenarioPoint.*` | 接近起点、Enter、持续行为、Exit 与异常退出分段管理。可在现有 Interaction 上接交互位置/占用与执行；SmartObject 提供查询和预留，执行生命周期仍归调用方。 |
| 多角色预算 | `Peds/PedIntelligence/PedAILodManager.*`；`task/Physics/TaskNM.cpp` 的 Ragdoll Pool | AI、Motion、事件扫描、动画分时和布娃娃池分别有预算/优先级。ImagineGame 的 SignificanceManager 可作接点；降级须保留权威移动及关键动作完成/清理，不以统一停 Tick 代替。 |

- GTA 的梯子、DropDown 已有明确入口；持续悬挂/横移是否有可直接参考的完整实现仍须专项追踪。MoVE、NM、ClipSet、Clone 的实现合同依赖 RAGE；本工程按 UE 动画/物理及 GAS/CMC 合同重建对应能力，插件已启用不能作为项目链路完成的证据。

### GTA 玩法系统参考与职责

- 以下为本地 GTA 快照的源码导航与职责映射，不表示 ImagineGame 已具备对应玩法。共享事实/查询/资源合同可归 ImagineGame；具体反应策略、战斗、人口与警戒规则由对应 Game Feature 提供。现有 GAS、GameplayMessage、Team、Interaction、Equipment、Experience/GamePhase 保持原所有权。

| 系统 | GTA 源码入口（相对 `src/dev_ng/game`） | 可借鉴设计与本工程边界 |
| --- | --- | --- |
| 武器定义与近战/枪械配置 | `weapons/info/{WeaponInfo.h,WeaponAnimationsManager.psc}`；`weapons/{WeaponFactory.cpp,Weapon.cpp,WeaponEnums.h}`；`task/Weapons/Gun/TaskGun.*`、`task/Combat/TaskCombatMelee.*` | 快速静态核对：基础定义共用 CWeaponInfo，FireType 区分 Melee/InstantHit/DelayedHit/Projectile 等，GetIsMelee 依据 DamageType，GetIsGun 依据 Flag；工厂常规武器创建同一 CWeapon。CWeaponAnimations 同时含射击、近战 ClipSet 与 WeaponSwapData/WeaponSwapClipSetHash；执行由 Gun 与 Melee 任务分开，CWeapon::Fire 对 Melee 返回 false，不能将配置共用等同于攻击链共用。可借鉴共用装备/动画选择机制、分离攻击执行；不据此给本工程增设新的武器继承中间层。 |
| 事件响应与打断 | `Peds/PedIntelligence.cpp::Process`；`event/{EventGroup,EventHandler,EventResponseFactory}.*`；`event/decision/EventDecisionMaker.*` | 扫描、决策、最高优先级事件与任务执行分开；HandlerHistory 保留被临时事件替代的非临时事件。GameplayMessage 适合通知，AI 的有效期、优先级、记忆与恢复策略归 AI 适配/行为层，GA 负责执行。 |
| 世界刺激 | `event/{ShockingEvents,EventShocking}.*` | ShockingEventsManager 管事件添加、非脚本事件去重、寿命覆写及按区域/来源清理，供角色观察、逃离和反应。可为枪声、爆炸、尸体、危险区域提供空间事实；消费者自行决定行为，不让消息总线承担持续世界状态。 |
| 感知与阵营关系 | `Peds/PedIntelligence/PedPerception.*`；`Peds/{Relationships,RelationshipEnums}.*` | 感知区分中央/周边视野、距离和姿态等修饰；关系组独立于角色类型，支持 Respect/Like/Ignore/Dislike/Hate 等。`ComputeFOVVisibility` 主要检查视野和距离，不能作为完整 LOS 证明。扩展 Team/AI attitude 的统一查询，AI 敌意与 CanCauseDamage 的玩法政策分别明确。 |
| 群体战斗协调 | `task/Combat/{CombatDirector,CombatSituation,CombatOrders}.*` | Director 管成员、战况及协调开火；Situation 的 Initialize/Uninitialize 对称，Orders 提供 HoldFire、CoverFire、射速和射击间隔修饰。可由 ShooterCore 的服务端协调层向个体 AI 发约束，个体继续通过能力和导航执行。 |
| AI 射击节奏 | `weapons/FiringPattern.{h,cpp,psc}`；`Peds/PedIntelligence.cpp` 的 FiringPattern Pre/PostProcess | 共享 Info 存储开火前延迟、每轮连发数、发间/轮间间隔；每角色 CFiringPattern 保存执行状态。接在 AI 开火请求层，武器自身射速、弹药与成本仍由现有武器能力校验。 |
| 场景交互与多人同步 | `task/Scenario/{ScenarioPoint,Types/TaskUseScenario}.*`；`network/General/NetworkSynchronisedScene.*` | ScenarioPoint 有使用计数、位置和时间条件；UseScenario 分接近、Enter、持续行为、Exit。同步 Scene 明确参与角色/实体/镜头、起点、Phase、Rate 与开始/结束。可在 Interaction/GA 上扩展位置占用及同步执行；沿 UE 权威/复制体系重新定义会话，不沿用 GTA 控制权迁移协议。 |
| 伤害事实与多路反馈 | `weapons/WeaponDamage.*`；`event/EventDamage.*` | WeaponImpact 分派 Ped/Vehicle/Object、伤害事件、AI、VFX、Audio 与 Ragdoll 请求。复用 EffectContext/HitResult、DamageExecution、GameplayCue/ContextEffects，具体受击反应走独立适配；生命变化、物理冲量、AI 感知和表现各有消费者。 |
| 世界人口与资源预留 | `Core/game.cpp` 注册 `CPedPopulation::Process`；`Peds/{pedpopulation,PopCycle,PopZones}.*`；`streaming/populationstreaming.*`；`network/Objects/NetworkObjectPopulationMgr.*` | 区域/时段配置、可用模型、实体增删和网络名额预留分别管理。扩展场景生成/Encounter 时复用 Bot/PawnData 创建链，业务决定数量/时机，预算与可用资产先批准；Significance 负责更新成本，不能代替生成预算。 |
| 犯罪、警戒与调度 | `game/{Crime,Wanted,witness}.*`；`game/Dispatch/{IncidentManager,Incidents,DispatchManager,DispatchServices}.*` | 犯罪事实、目击/报告、Wanted 与事件调度分别有状态；Dispatch 提供服务开关与资源创建限制，并申请网络人口预留。可映射为独立警戒/增援 Game Feature，事件/目击事实先于升级与出兵决策。 |
| 玩法资源所有权 | `script/Handlers/{GameScriptHandler,GameScriptResources,GameScriptEntity}.*` | Handler 跟踪资源/任务实体，具体资源类型处理释放；实体带 Cleanup/NoLongerNeeded 与所有者信息。借鉴获取句柄、明确交接和对称释放，复用 AbilitySet GrantedHandles、GameFeature ContextData 等现有模式；本工程仍以 C++/GA 实现玩法，不引入脚本体系。 |

- 人口源码优先核对 `pedpopulation.*` 的 `CPedPopulation` 与 `Core/game.cpp` 的注册调用，不能仅凭旧 `Population.*` 的 `CPopulation` 判定当前运行链。`PopCycle::Update` 的快照实现按小时/区域切换配置，但 WeekSubdivision 固定为 Weekday，周末切换代码被注释；最终配置值仍须查实际数据。
- 数据与运行状态分开：EventDecisionMaker 的共享规则/可修改组件、FiringPatternInfo/每角色 FiringPattern 可作参考。映射到本工程时复用 DataAsset/PawnData/能力配置，角色会话、记忆、冷却与占用归运行实例；单帧 override 须按原消费时序清理，持久规则使用显式所有权。

## 初始化、能力与 AI

### Experience 与 ASC 生命周期

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

### 能力输入绑定

- `FIMGInputAction` 只保存 InputTag/InputAction；`UIMGInputComponent::BindAbilityActions` 对有效 Action/Tag 按 IFE 的 LyraInputComponent 固定绑定 Started→Pressed、Completed→Released，不配置每项 PressedTriggerEvent 或 BindReleaseEvent。NativeAction 仍由调用方显式选择 TriggerEvent。
- 默认 PawnData 输入和附加 InputConfig 均经 Hero 的 Pressed/Released 转发当前 Avatar 匹配的 ASC，再由 PlayerController.PostProcessInput 调用 ASC.ProcessAbilityInput。GA.ActivationPolicy 决定 OnInputTriggered/WhileInputActive/OnSpawn；Started 在输入开始时转发一次 Pressed，Held 状态保持至 Released，避免持续 Triggered 被 WaitInputPress 当作下一次点按。
- Completed→ASC.AbilityInputTagReleased 移除 Held 并登记 Released；ProcessAbilityInput 更新 Spec.InputPressed，AbilitySpecInputReleased 发 InputReleased 通用事件，GA.WaitInputRelease 依赖该链及自身预测键向服务端传递释放。移除配置开关不能删掉释放转发。ProcessAbilityInput 固定本轮 Pressed/Released/Held 列表，能力回调中新排入的输入留给下一轮；ClearAbilityInput 或 Avatar 切换使旧轮次失效，禁止跨 ASC 共享临时激活列表或重入消费。ASC Avatar 切换、ClearActorInfo、EndPlay 清理输入。输入解绑的绑定句柄与 ASC.ClearAbilityInput 保持原职责；额外输入配置的移除仍是现有未实现边界，不因固定绑定而视为已解决。

### 复制 Montage 前置条件

- ASC 的 `RegisterReplicatedMontagePrerequisite(Avatar, Name, Delegate)` 返回注册句柄；委托须弱绑定 UObject，只接受当前 Avatar。相同对象/Name 重复注册复用句柄，不同步执行回调；注册后、前置数据变化后调用 `RetryReplicatedMontage`。检查返回 NotApplicable、Waiting、Ready 或 Discard，业务 Session/Sequence 由提供者自行核对，不复制第二套播放阶段。
- 所有适用条件 Ready 才放行；只有所属表现系统提供非负 LogicalStartTime，其他条件返回 -1。多个起点提供者或非法起点保留等待并诊断，不以注册顺序/优先级覆盖。非所属 Montage 必须返回 NotApplicable；沿用原生 GAS 的 Montage/PlayInstanceId、播放/停止与位置同步，不能据此前置门控认定自主代理预测/重放已满足。
- `UnregisterReplicatedMontagePrerequisite` 按句柄精确注销，默认丢弃本对象参与阻塞的待播放实例；只有明确交接前置职责时才传 false。Discard 仅抑制当前 Avatar/Montage/PlayInstanceId 的本机表现，不改变权威能力或移动阶段。新实例不继承旧丢弃；结束先到不创建已停止的新实例，已播放实例保留原生停止路径。
- 门控/Retry 只作用于非权威、非本地控制角色；重试合并至下一 Tick 并防重入，回调后复核注册版本和实例。Avatar 切换、ClearActorInfo、EndPlay 释放注册、起点缓存和计时器；ASC 临时注销暂停处理/释放计时器，保留弱注册并在重新注册后重试。失效提供者不能释放已放弃的实例。等待原因变化输出 Verbose，连续等待5秒输出一次 Warning并复核，不因超时自动放行；`Log LogIMGAbilitySystem Verbose` 查看 Avatar/Montage/Instance/Reason。

### 持久 Bot 与 PawnData

- `UIMGBotCreationComponent` 在 Experience 就绪后由服务端按 Controller → PlayerState → Pawn 生成 Bot，配置 BotControllerClass并保留分队/出生点规则。重生分派支持玩家控制器及 `AIMGPlayerBotController` 派生类；当前无 StateTree 运行时代码。
- `AIMGPlayerBotController` 开启 bWantsPlayerState，每个 Bot 独享 PlayerState/ASC；共享 PawnData 资产不共享运行状态。BotCreation 不另设 PawnData 配置。
- GameMode.GetPawnDataForController 按 PlayerState 已认领 → Experience 就绪时 Bot.GetPawnData → Experience.DefaultPawnData → AssetManager.DefaultPawnData 查询。全局回退沿用同步加载/保活接口，缺失返回空并记录日志。
- Bot.GetPawnData 为 BlueprintNativeEvent/BlueprintPure，默认空并回退默认配置；只同步、无副作用地返回选择，不授予能力或生成 Pawn。首次认领前保持稳定，选择依据须早于 Controller.BeginPlay、PlayerState 成员赋值、分队和 Possess 可用；空值不表示等待异步注入。
- PlayerState 在 Experience 回调中首次认领，只有服务端首次 SetPawnData 授予 AbilitySets；已加载时注册立即回调，普通 SpawnActor 返回后再覆盖已太晚。后续生成、出生点体型检查及重生复用已认领数据，切换不同 PawnData 须另行设计。
- GameMode 按 PawnData.PawnClass 生成，在 FinishSpawning 前写入 PawnExt；Pawn 仍须装配 PawnExt/Hero 等组件。初始化沿 Spawned → DataAvailable → DataInitialized → GameplayReady；Bot 只跳过玩家输入条件与绑定。
- 网络复用 PawnExt.PawnData、Pawn.PlayerState 与 GAS；客户端由 OnRep_PawnData、OnRep_PlayerState 等推进初始化，模拟代理不要求 AIController。晚注册用 RegisterAndCall；IMGAbilitiesReady 仅在服务端 SetPawnData 发送，本机 OnAbilitySystemInitialized 不保证能力/属性已复制齐全。
- 独立敌人 AI 可用 Pawn 自持 ASC 或其他体系。AIMGCharacterWithAbilities 虽自持 ASC，仍继承 PawnExt，自行 InitAbilityActorInfo 不会自动推进至 GameplayReady。

## 角色与普通移动

### 输入、GAS 与 Locomotion 职责

- `HeroComponent.AddMovementIntent` 统一处理玩家、自动奔跑和外部 AI 的世界空间方向，再提交 AddMovementInput。输入结束、取消自动奔跑、换 Controller 或 EndPlay 清理意图。原生 AI Path Following 独立；急转回调可能同步取消或替换请求，回调后须复核再提交输入。
- **步态选目标，GE 改目标值。** GA 管准入、互斥和意图释放，HeroLocomotion 用 Sprint/Walk、Aim/Strafe 四个 Tag 形成请求，再按输入方向和姿态选择实际 Gait。只有 Walk/Run/Sprint/Crouch 四项速度属于 `LocomotionSet`，便于各类 GE 共用速度入口，避免 GA 分散写 CMC。Set 构造默认 165/380/585/170，差异由 AbilitySet 初始化 GE 设置基础值。PlayerState 持有的 Set 随 ASC 保留，换 Pawn 读取已有 CurrentValue。
- Locomotion 汇聚属性与步态，统一计算并写入 CMC。基础加速度、地面摩擦、行走制动为组件配置，默认 800/2/800；Hero 按步态、速度、前侧后倍率和 0.3 秒落地窗口派生。Aim/Strafe 冲刺还须满足 50° 方向条件。Crouch 用独立速度属性，Crawl 沿 CMC 限速。组件配置和曲线在预测历史存在期间保持稳定。
- HeroLocomotion 的 `StrafeSpeedMapCurve` 在组件类默认值中配置为可选软引用。BeginPlay 在接合 CMC 前解析一次，未配置或加载失败时记 Warning 并使用前向倍率。权威端与自主代理使用一致的配置，EndPlay 解除移动绑定后释放曲线缓存。
- HeroLocomotion 的跑动相机表现独立于移动参数计算，实现在 `IMGHeroLocomotionComponent_Camera.cpp`，只调用 CameraComponent 的通用入口；扩展遵守下节「移动表现来源与扩展边界」，配置与本地来源生命周期见「跑动 Shake、脚步与动画数据」。
- **单向接合。** Locomotion 通过通用弱委托注册一个 CMC 提供者，CMC 只理解请求字节及 `IMGMovementSimulationContext.h` 中的通用上下文和参数，字段含义由提供者解释。Controller → PreCMC → CMC 的 Tick 依赖供本地输入与表现更新；蓝图覆写 PreCMC 须调用父实现。远端 RPC 按 Move 自带输入模拟，逐 Move 计算和重放不派发 PreCMC 蓝图事件。
- **动态 Set。** GameplayReady、当前 ASC/Avatar、真实 Set 与提供者同时有效才 Ready。等待 Set 时关闭 Tick，CMC 按原生移动。AbilitySet/Game Feature 走 ASC 的 `AddRuntimeAttributeSet` / `RemoveRuntimeAttributeSet`，客户端在 `PostRepNotifies` 后重检。下一次 PreCMC 接合并读取已施加初始化 GE 的值。移除或换绑释放旧代次、恢复接管前参数；物理子步中的 Set 变化等当前 Move 结束再释放。授予方先结束依赖 Set 的 GE。停用、注销、EndPlay 精确退订，旧提供者不能覆盖新提供者。换 CMC 须重新注册。

### 移动表现来源与扩展边界

- **当前职责归属：** `HeroLocomotionComponent` 可作为普通移动镜头表现的稳定来源，基于既有实际速度、步态、落地等事实，判定是否请求表现、幅度/频率及停止时机。基础 `LocomotionComponent` 保持移动接合、属性汇聚与参数计算职责，CMC 保持移动模拟职责；两者不增加镜头业务分支，不为表现另造移动事实或第二套移动权威。
- **来源判定与通用调度分开：** HeroLocomotion 只通过 CameraComponent 的统一入口提交、更新和停止本来源请求，并管理本方句柄。实例创建/复用、启动间隔、并发替换、释放与清理归 CameraComponent；模式只提供视图策略和效果接收倍率，Pattern/算法只消费明确参数。相机不反查 Locomotion、动画、武器或 GAS，不在每个混合模式重复建立来源驱动。
- **配置与运行时事实分开：** 效果形状、基础幅度/频率、曲线、包络及来源响应范围由配置提供；实际速度、当前状态及由此计算的强度/频率倍率在运行时提供。当前跑动参数在 HeroLocomotion 内联展开；有实际复用或独立编辑需求时再提取纯配置资产，不要求额外创建运行时实例蓝图。配置项须有实际消费者，运行时实例/历史/句柄不作为资源类型暴露，不为固定规则增加无效开关，不用配置覆盖事实所有权。
- **其他业务各自发起：** 走路、冲刺、游泳等由普通移动状态直接产生的反馈可继续从 HeroLocomotion 发起；开火、受击、爆炸及 Traversal 专属反馈由对应业务模块发起，共用相机入口。HeroLocomotion 不发展为所有镜头行为的总调度器；通用实例并发、替换、混合与释放规则不回迁到来源组件。
- **按实际复杂度提取：** 参数、曲线或预设增多时先整理配置，并保留独立实现文件隔开移动计算与表现；不按效果数量或文件行数机械拆组件。当移动表现形成独立状态机、多种表现协作、复杂来源规则或独立生命周期时，再提取专门的移动表现组件，由 HeroLocomotion 提供事实。提取后保持单一请求所有者、句柄和来源身份，旧驱动先释放再交接，禁止两边重复判定、触发或维护相同状态。
- **本地表现边界：** 移动产生的相机反馈仅消费本机事实，不写 ControlRotation、CMC 参数、SavedMove 或复制状态，不新增表现 RPC，不在逐 Move 参数计算、预测重放或动画工作线程发起相机请求。来源结束、取消、失效和切换须成对释放；具体本地视图过滤及清理合同见「跑动 Shake、脚步与动画数据」。GTA 用作执行链和参数语义参考，组件/模块归属按本工程单向依赖落位，不机械照搬其相机类内的业务判定。

### 逐 Move 预测与权威纠偏

- **把模拟输入钉在 Move 上。** 原生 CMC 已记录原生移动状态和输入；外部写 MaxWalkSpeed/MaxAcceleration 或启用子对象复制不会把值纳入历史。AttributeChanged 只更新下一次捕获的输入。ControlledCharacterMove 在缩放前捕获，SavedMove 保存请求、上下文、提供者代次和参数快照。Hero 上下文是四项速度与落地标志。参数或请求变化阻止跨状态合并，重放按历史输入及纠偏后的起点重算，保留非本组件所有的制动摩擦快照，不回写 GAS 或覆盖实时步态。
- **区分力度与加速度。** Packed Move 传 0–255 的输入力度和原生舍入方向，服务端及重放按当次加速度上限重建物理加速度，避免碰撞降速或合并回退后以旧上限解释力度。力度和请求、上下文变化参与 ImportantMove。权威端按自身 Set 重算，外推保留已接收的输入并采样当前属性。本地权威/AI 不生成客户端 SavedMove，模拟代理沿原生同步、只做本机表现。
- **指纹发现分歧，响应接合权威事实。** Packed Move 带本机 Revision/代次及输入、实际参数两份 32 位指纹，按字宽混合并借用 FNV 常量。两份都不同时可提前要求位置/速度纠偏，即使历史位置差很小；仅无关步态属性不同可随 ACK 更新上下文。指纹不授予客户端限速，也不代替原生位置验证。Packed Move/Response 两端须同版。
- 权威上下文在处理 Move 时留存，响应按 PendingAdjustment 时间戳回传。只在原生推进 ACK、对应 SavedMove 与代次/Revision 匹配、纠偏未被业务过滤时接收。逐属性本机变更标识允许新的合法 GAS 预测覆盖旧权威值；普通 OnRep 保留标识，避免复制乱序让晚到旧值撤销更新。停用/注销/换绑作废旧历史；Ragdoll 和 Traversal 维持各自接管及纠偏规则。
- **场景 Buff 的时序。** GE 的 Duration 不决定执行端。场景触发按施加目标的 Authority 裁定，避免把非复制触发器 Self 的本机 Role 当成服务端判断。客户端可在合法 GAS 预测窗口预测已知操作；服务端独知的生效/到期仍有信息延迟，可能产生首次纠偏。现有速度上限按 Move 切换；如需平滑模拟目标，两端须按 Move 时间推进并保存过渡状态。

### 自主代理表现与排查

- 拥有者纠偏时胶囊立即采用权威位置和速度，再重放 SavedMove。模拟代理插值参数不控制这条链。CMC 仅在本地自主代理的普通地面/下落移动中，以首个已接受纠偏前到重放后的**净位移**补偿网格。40 cm 内反向补偿重力平面分量并限制累计量，按 0.05 秒时间常数衰减。移动 Tick 前撤销、后应用；相机跟胶囊。瞬移、Root Motion、Ragdoll、网格更换和停用清理。
- Play As Client 即使未开 Network Emulation 也会纠偏。先用 `p.NetShowCorrections 1` 与 `IMG.Movement.NetDiagnostics 1` 对照历史误差、权威响应和 `[AutonomousMeshCorrection]` 的 PostReplayDelta/MeshOffset；历史位置差为零仍可能修正速度。级别 2 对照 Predict/Authority/Replay 的限速、力度、摩擦和制动。用 `IMG.Movement.AutonomousMeshSmoothing 0` 做表现对照。单机也急刹时先查超速制动，两端分歧时查 Move 输入与时序，不靠放宽误差掩盖。
- `LogIMGLocomotion` 默认输出 ASC/Set 等待、就绪和四项速度。权威端在 ASC 配对后缺 Set 或 Set 变更后失去 Set、Hero 在 BeginPlay 缺方向曲线、移动上下文或参数无效时直接记 Warning。客户端等待 Set 复制记 Log。用 `Log LogIMGLocomotion Verbose` 查看 GameplayReady、Set 变更排队和解绑。提供者冲突或 Packed RPC 未启用见 `LogIMGMovementNet`。

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

## Traversal

### 职责与装配

- Traversal运行时及编辑器源码的注释统一使用英文，配置合同与示例保留原意；中文工程导航继续写在AGENTS中。
- 源码位于 `Source/ImagineGame/{Public,Private}/Character/Traversal`，属于 ImagineGame。以 UE 合同为准：GAS 管能力和 Montage 生命周期，CMC 管权威移动、预测与纠偏，MotionWarping 修改根运动；GTA 只对照职责与算法意图。

| 入口 | 职责 |
| --- | --- |
| `IMGTraversalDetector` | 无状态同步几何查询，产生候选和建议动作，不选择动画。 |
| `IMGGameplayAbility_Traversal` | 检测、CHT/PoseMatch 选片、校验/Commit、播放和退出。请求共用 `OnTargetDataReady → ResolveTraversalRequest → StartTraversalExecution`。 |
| `IMGTraversalComponent` | 配置、Target、临时碰撞忽略、Flying 接管/释放和复制快照。 |
| Private `IMGTraversalPlacement` | 实际播放路径、窗口链、落点、可选补偿和显式 Landing 校验。 |
| Private `IMGTraversalReplay.cpp` | 同一 Component 的逐 Move 上下文记录/恢复，无独立复制服务。 |
| Types / TargetData / Debug | 公共数据、客户端请求、私有观测采集、独立Trace与DrawDebugLibrary表现；编辑器轨道位于ImagineEditor同层级Character/Traversal。 |

- 保持来源组件 → Traversal → CMC 的源码依赖；CMC 只暴露通用委托/闭包接口，不包含 Traversal 类型。热插拔指组件与能力运行时接合/释放，不表示共享运行时可作为独立插件卸载。
- Pawn 装配 Traversal、MotionWarping 并由 ASC 授予 GA。`ActionSettings` 提供动作通用执行规则；`MontageSettings` 按 Action/Montage 整份覆盖，不合并字段。`WarpBindings/RequiredTargets` 管接触目标，Notify 管窗口，`Landing` 管落地合同。选片只返回一个 Montage；服务端本地/AI 选片缺少 PoseHistory 时须有后备，远端请求使用客户端选择并由服务端重检。

### 检测、发起与选片

- 每次激活只尝试一次，外部决定触发时机，`RequestSource` 与 `ActivationPolicy` 独立。`CanActivate` 作本地几何/发起预筛，激活后依次执行 `PreSelectTraversalAnimation`、`SelectTraversalAnimation`。Chooser 消费入场 MovementMode/Gait/Speed，必须早于 `BeginTraversal` 切 Flying。
- `SampleHandhold` 采样中心/左右顶部、宽度和楼梯轮廓，`MakeCandidate → ValidateCandidateDirection` 验证方向。方向回退复用本次公共采样，但从原始候选副本重算深度、后沿、净空和坡向；不跨 Detect 调用、帧或网络端缓存，不省略服务端重检。
- `InitiationSettings.bEnabled=false` 仅跳过发起限制。Manual 默认允许站立、范围150 cm；Automatic 默认允许 Falling，要求有效意图、朝向点积≥0.8、固定范围150 cm，不按速度二次缩距。速度缩放只影响搜索长度；`Candidate.Speed/RunningSpeedThreshold` 仅供建议/选片，不限制原生 Supports。
- 意图优先待消费输入，再取 CMC.Acceleration，不以 Velocity 替代。玩家使用 GA.RequestSource；服务端 AI 强制 AI 来源，复用 Manual 规则，无输入/加速度时可取 Moving 的 PathFollowing 当前段目标，无意图时可按手动站立规则向前尝试。
- 发起距离由当前 Actor 与前沿重新计算 XY；`Candidate.PlanarDistanceToLedge` 保留检测时的选片/诊断值。`ServerDistanceTolerance` 只放宽远端发起距离，不扩大服务端几何搜索。发起角度使用本次请求方向和重检前沿，不换成收包时朝向。
- PreSelect、Select、Commit 回调后复核原 ASC/Avatar/Component。Commit 后还须核对入场位姿、胶囊、Mesh/AnimInstance、BaseOffset 和参与表面的变换，防止旧落位结果应用到已变化的对象。候选默认时效 `MaxCandidateAge=0.25 s`。
- Supports 与推荐动作分开：Mantle 是登顶，要求前沿；Hurdle 是有后方支撑的翻越，要求前沿+后沿+`bHasBackFloor`；Vault 是没有确认后方支撑的翻越，要求前沿+后沿且 `!bHasBackFloor`。Mantle 可与其中一种翻越同时可选，由 CHT 决定覆盖与优先级；高度/深度/速度不在 Supports 中硬编码。`RecommendedAction` 仅作建议，Hurdle/Vault 各用自己的推荐高度限制，不因 Hurdle 超出建议高度改推有地面的 Vault。
- `bHasBackFloor=false` 表示探测范围内尚未确认可用后方支撑，不证明世界中不存在地面，也不能当作落点。`BackLedge` 在 `bHasBackLedge=false` 时仍保留最后顶面采样位置，坐标与 ObstacleDepth 都不能代替已确认后沿。空中 Height 随当前脚底升高而减小，需结合 StartTime 和选片核对。

### 手点、坡面与表面规则

- GroundLocation 只取 CMC.CurrentFloor 的有效可走支撑；空中或无有效支撑（含无碰撞支撑的 NavWalking）回退当前缩放胶囊底部，不读取 GetGroundInfo 的长距离下探。`bUseGroundPositionForHeight` 默认 false，开启后仍使用同一回退规则；坡面/脚下楼梯只消费有效支撑。
- 手点取前侧命中点上方的中心/左右实际顶部接触，不固定向内平移或投影回前侧平面。`UseRequestDirectionBelowHeight=80 cm` 以下优先请求方向，设0关闭；左右接触连线求交并插值高度，调整范围50 cm，失败再用原始手点/墙面法线重检。此近似不替代双手 IK。`MinFacingDot` 默认 Manual/AI=0.3、Automatic=0.8；HClear/VClear 是净空，不是动画轨迹。
- `AngularSurfaceTag` 默认 `Traversal.Angular`，读取命中组件或所属 Actor 的 FName 标签。请求与入面方向 dot>0.95 时，高障碍也可保留请求方向，失败回退法线。低障碍、棱面或 Automatic 要求双侧支撑；其余 Manual/AI 可追加外侧探针并重定中心，新中心组件须纳入障碍跟踪。标签不引入车辆滑行动作。
- 楼梯由 `StairSurfaceTag=Traversal.Stairs` 指定：前侧/顶部命中带标签，或站立时脚下带标签，启用轮廓过滤；未标记不保证识别。边缘跨度截止值默认22.5 cm、命中楼梯表面时30 cm，严格小于才拒绝，等于可过；跨度<40 cm且水平漂移>10 cm也拒绝。Span 不等于台阶高度，Height 默认相对脚底；独立顶部组件也须覆盖标签。
- `BlockedSurfaceTags` 拒绝全部来源；`AutomaticBlockedSurfaceTags` 仅拒绝 Automatic。Actor Tags 作用于全部组件，局部限制优先 Component Tags。例如 `Traversal.NoAuto` 必须显式加入自动禁用列表才生效；`Traversal.Stairs` 只是启用过滤，不禁止全部攀爬。自动深度限制、高位开放接近拒绝和楼梯下行方向限制只用于 Automatic，楼梯轮廓也用于 Manual/AI。
- 深度断层可能在曲面上形成后沿。`AutomaticMinDepthSamples` 是结束扫描前的最小扫描次数，不是连续顶面通过次数。`Candidate.GroundNormal` 是深度命中的平均法线，不能替代补偿实际命中的法线或可走判定。
- **平台可走坡度归 CMC。** BackFloor、补偿与落地检查使用 CMC.IsWalkable。通用上限为角色 Character Movement → Walking → Walkable Floor Angle（UE 5.8 原生约44.8°，以组件实际配置为准）；局部平台用碰撞组件 Physics → Walkable Slope Override。`MaxClearanceSlopeAngle=42.5°` 只控制净空探针方向。`MaxMovingSpeed=500 cm/s`、`MaxAngularSpeedDegrees=85°/s` 限制载体运动，检测及执行均复核。

### 播放路径与预测能力

- GA 以 `StartSection=NAME_None`、Selection.StartTime 启播。资产 `GetPlayLength()` 不代表本次末端；Placement 从 StartTime 所属 Section 沿资产 NextSectionName 解析有限路径，合并相邻段，将前跳/后跳映射到顺序播放时间轴。根运动、DistanceCurve 和窗口筛选只累计实际路径，显式窗口编号保留资产原始序号。循环、无效链或活动 Warp 跨非连续边界在 Commit 前拒绝。
- Motion Matching/PoseMatch返回的StartTime经Selection → TargetData.NetSerialize → ResolveTraversalRequest → PrepareTargets及PlayMontageAndWait原值传递；不按0起播，也不在服务端另选时间。ProjectPlacement的Cursor从PlaybackStart开始，窗内起播只Advance剩余区间；SkewWarp的有/无平移分支判定仍用完整窗口位移，与原生TotalRootMotionWithinWindow一致，不能误改成剩余位移判定。DistanceCurve的接触窗末→落地窗末距离是两锚点间距，不应减去StartTime；它与当前胶囊→落地的剩余位移不是同一量。
- 跳过起播前已结束及预计混出后才开始的窗口，窗内起播只算剩余区间；显式提前混出从末 Section 计算。逐段组合窗间根运动和各窗约束，IgnoreZAxis 沿用入窗高度，未约束旋转沿用动画。窗口可共用 Target，单窗/多窗共用预测与修正后复算；骨点参照和原生尾段按窗口缓存。
- 原生 `bSubtractRemainingRootMotion` 扣除的是 Window.EndTime → **资产全长**，实际后续播放位移才按 Section 路径累计。`ResolveWindowPose.NativeTail` 必须匹配原生扣除，不能统一替换为 SectionEnd。DistanceCurve 按路径分段累计，再分别处理接触/落地窗的原生扣余量参照偏移。
- Bone/Static/None 参照将 Target 换算成胶囊位置。有动画平移时按 BaseTranslationOffset 换算中心，关闭 WarpToFeetLocation 则清除该偏移的 Z；无平移时按脚底换算。Target 与胶囊中心不应直接比较。支撑求值只累计至所选接触窗口末端，不累计其后的走位；播放路径及预计混出时间仍用于筛选可到达窗口，不承担动作接触终点的含义。
- 自动补偿的模型支持完整、顺序、不重叠、未限速的原生 Skew 窗口，要求单位 Actor/Mesh 世界缩放、直立胶囊；旋转为 Default/Slerp，时间倍率在(0,1]。非 Warp Modifier 也保留范围，以空 Modifier 标记降级，防止漏算 Scale/自定义效果。超出模型范围时 Mantle、Landing=None 的 Hurdle 保留原目标执行；显式 Landing 仍要求可靠预测。重叠窗口当前不自动补偿，不能把此限制当作动画整体准入条件。`URootMotionModifier_SkewWarp::MaxSpeedClampRatio` 是 protected 且无公开 getter，Placement 用 `FindFProperty` 反射读取（0 表示不限速）；反射取不到时该窗按不可预测处理，Mantle/Landing=None 降级保留原目标，显式 Landing 直接拒绝，故引擎升级后需复核该字段名与语义。

- 已验证重叠执行语义：UE `MotionWarpingComponent.cpp::ProcessRootMotionPreConvertToWorld` 按 Modifiers 数组顺序处理全部 Active Modifier，并不禁止窗口重叠；`RootMotionModifier_SkewWarp.cpp::ProcessRootMotion` 的平移/旋转分支会重新计算并写入对应分量，不能把重叠理解为两个独立终点修正相加。IMG `PrepareTargets` 的 `bProjectSequence` 当前对整个剩余可达窗口链判定；任一受检窗口重叠即关闭本次预测，即使冲突在所选接触终点之后。首尾严格相接（前窗 End <= 后窗 Start）允许；同 Target 或仅旋转/平移职责分离的重叠也未获特殊放行。
- 重叠诊断已接通：`IMGGameplayAbility_Traversal.cpp::PrepareWarpTargets` 调用 Placement 时，Verbose 日志或 `IMGTraversalDebug::IsObserving` 均开启详细诊断；`PlacementPredictionUnavailable`/`PlacementPredictionFallback` 的原因含 `Overlap`、`OverlapWith`、六位小数时间、Target 和轴设置，进入 Rewind Execution Details 与原始导出。当前只描述与前一受检窗口的冲突，不是全部重叠对清单；无观测且未开 Verbose 时，普通可选补偿不构造完整原因。
- 设计建议（尚未实现）：维持普通补偿保留原目标、显式 Landing 预测失败拒绝的边界，以现有诊断辅助手调窗口。若扩展模型，优先研究按所选求值终点裁剪依赖链，及职责独立重叠的严格可预测条件；不可直接忽略重叠、自动截短 Notify 或跳过显式 Landing 校验。重叠资产的实际表现、Modifier 激活顺序与帧步长影响仍需运行确认。

### 高度补偿与显式落地

- 当前补偿实现只调整执行 Target.Z，不改检测锚点、XY、旋转，也不在播放中程序化移动胶囊。反推关系为 `TargetZ = SafeCenterZ - CenterOffsetZ`，允许上下修正；支撑层位容差与修正幅度分开。Mantle 在最后可到达平移窗末检查登顶；Hurdle 有显式 Landing 时用所选落地窗，否则仅在有 BackFloor 平移窗时用最后一个此类窗。Vault 不做支撑补偿；Hurdle 仅绑定沿点且 Landing=None 时保留动画跨越轨迹。Mantle 的资产窗口合同见下节；仅抓沿即结束、其后仍靠根运动登顶的资产不满足当前窗末支撑补偿模型，不能将其跳过补偿解释为重叠或碰撞算法必然错误。
- 普通 Mantle 的补偿是可选修正。平面支撑、完整胶囊扫掠、CMC可走性、归属、层位、边缘容差、重叠及全窗口链复算通过才写回；失败恢复/保留原目标，不追加名义终点贴地硬门槛，全 IgnoreZ 同样处理。`CompensatedPlacementCenter` 只缓存所选求值终点上校正成功的安全中心。降级不证明最终站稳；几何、表面、绑定、RequiredTargets 和有限路径校验仍生效。
- 补偿排查入口：`PlacementEvaluation` 已出现说明接触终点投影已通过；随后 `PlacementPlane` 的 Hit=1 只证明线探针命中。`IsPlacementSupport` 仍要求载体/障碍归属、顶层高度（Mantle）或后方落差（Hurdle）、有效阻挡、CMC.IsWalkable 和表面规则全部通过，否则以 `No usable plane support on the selected layer` 跳过，尚未进入完整胶囊扫掠与 Target.Z 写回。该日志当前不逐项输出失败布尔值或实际可走阈值，不能只凭 Normal 推定具体坡度配置；检测摘要 GroundSlopeAngle 是采样均值，不代表此命中面坡度。若最后平移窗仍对应抓沿阶段，求值点也在该窗末，不会自动等到后续登顶姿态或累计窗后根运动。
- Mantle 始终限定障碍顶层，`Landing.Mode` 必须为 None，不允许实际消费 BackFloor 目标；Vault 同样禁止显式 Landing 和实际消费 BackFloor。未消费的默认 BackFloor binding 可保留。顶层取 `TopSupportHeightRange ± DepthHeightTolerance`，范围来自中心、两侧、首个断层前连续顶面和后沿细化。仅比较组件不足以区分同一车身上的车顶与低层斜面。
- 支撑参照取求值接触窗的几何绑定，修正 Target 则取该窗及之前最后控制 Z 的窗口，二者可以不同。Mantle 接受该接触载体/障碍集的顶层；Hurdle 仅接受接触 BackFloor 载体、位于后沿朝向的后方半空间且落差在 `0..MaxDrop` 的支撑，不借前沿载体或障碍顶面代替后方地面。Hurdle 探高范围由胶囊、接触锚点距离及 CMC/组件可走坡度推导。安全中心保留 CMC 地面间隙，改后复算截至同一接触终点的窗口链，防止同名 Target 影响前窗。向下修正保留独立支撑碰撞，向上修正通过复核后才可加入临时忽略。
- 显式 Landing 仅适用 Hurdle，需 BackFloor 绑定和预计混出前完成的平移窗；None 可保留动画自带落地，不自动补设窗口。补偿、修正后复算、净空/支撑/直线路径与预览胶囊统一在选中Landing窗口末端求值，即使它是最后平移窗也不累计窗后Montage根运动。只选该落地窗及之前最后控制Z的窗口修正Target；之后的高度窗不能反过来补偿先前落点。NativeTail的bSubtractRemainingRootMotion参照仍按引擎资产全长扣除，不能因截断求值而删掉该扣除。较早Landing不让后窗覆盖共享同名Target。CapsuleClearance 只加校验余量；预测、后沿后方可走落点、落差、完整胶囊、实际根位置的后方支撑及可选直线路径不通过即拒绝。普通补偿的“保留原目标”不豁免此合同。结果按载体局部 Target 复制。

### Mantle 窗口制作与优化边界

- 用户确认采用现有窗末求值合同：让 Root 完成上抬、到达 attach 所代表的平台落位阶段后结束最后一个平移 Warp 窗口；不把抓沿后仍需登顶的中间姿态作为站立检查点。此处不是要求胶囊中心与 attach 世界坐标重合，仍须按 Bone/Static Provider、Mesh BaseOffset 与胶囊尺寸换算；也不要求覆盖后续走路尾段或自动选择 Root 最高点。
- UE `RootMotionModifier.cpp` 的 Bone Provider 在 Window.EndTime 采样 Root 相对骨点的 Transform；改变 EndTime 会改变参照关系，窗口变长不等于逐帧手部 IK。资产作者须保证 attach 在所选窗末仍是有效的平台参照，实际接触与运动表现由资产运行验收。
- 当前选定方案保留原生 MotionWarping 与既有 Target.Z 可选修正，不新增独立补偿 ANS、后段 Modifier、时间字段或复制计划。两者若都按作者指定区间修正同一根运动，仅拆出独立标记不构成算法能力提升。后续优化聚焦原模型的计算、适用范围和诊断；短 Warp 加窗后登顶预测仅在出现明确资产需求后另行设计。
- Target.Z 的非零修正会改变接触 Target，相同名称的相关前窗也受影响；即使支撑/完整复算通过，也不能称为未偏移原几何手点的绝对对齐。若要求原手点严格不变，应独立评估该约束与最终站立是否兼容，不通过强行拉长窗口或新增标记声称两者同时保证。
- 待优化诊断：区分无需修正、窗末尚无可用顶面、修正后校验失败、窗口重叠/模型不支持及实际执行偏差；保留原目标降级与显式 Hurdle Landing 的不同合同。不将补偿量非零当作动作成功的必要条件。

### 执行与三端网络

- LocalPredicted 顺序：校验 → Commit → 获取 Target/碰撞忽略 → Flying → 启动 Montage → 提交 TargetData/发布快照。本地即时播放，不等待权威落点。服务端重检来源、方向/意图、几何、Action、时间/倍率、Warp 合同和可选 AllowedMontages，不接受客户端几何；请求不携带检测位姿/时间戳，不回溯世界。TargetData 的 Source/Action 分别按 2bit 序列化，扩展枚举须同步核对 `IMGTraversalTargetData.cpp::NetSerialize`。两端数据结构及配置须一致。
- 启动前只发一次取消；Commit 后重入或播放失败共用退出链。激活/请求栈退出前禁止复用同一 GA 实例；处理时保留 TargetData 句柄，防止同步回调消费 GAS 缓存后悬空。远端等待 TargetData 默认2秒，开始后复用计时器，以默认0.05秒检查目标。
- `WhileInputActive` 在 Spec 不活动时持续尝试。已激活尝试的 RetryInterval 默认0.25秒，**只从本地 EndAbility 计时**；自主代理另等匹配 Avatar/Spec/预测键的权威终止，晚通知只释放门、不重计间隔。等待门/间隔先于 GAS 常规准入和几何扫描判断。ASC.AbilityNetworkTermination 透传可靠 ClientEnd/Cancel/ActivateFailed，含本地已结束实例；远端权威退出须发送终止。

| 角色 | 执行合同 |
| --- | --- |
| 权威端 | 独立重检并计算 Target，复制执行快照；生命周期沿 GAS，移动沿 CMC。 |
| 自主代理 | 本地先执行；仅活动且 Avatar/Spec/预测键/Action/Montage/StartTime 匹配时回填权威 Target/障碍。引用未齐继续预测并在活动 Tick 重试；保留本机 PreviousWarpTargets，版本变化后 GA 重建表面复核快照。晚到/结束快照不启动或结束本地会话。 |
| 模拟代理 | 不选片、不重检几何；按 Montage/PlayInstanceId 等待快照引用、Target、碰撞忽略和 Flying。结束先到时保留已播放实例至退出（含混出），未播放终态实例丢弃，晚引用不得复活。恢复 Target 使用权威历史，非 Flying 到达后先恢复碰撞再查支撑。 |

- Component 按独立句柄注册 ASC Montage 前置条件。ReplicatedMontages 非空时，选片/复制归属仅接受其列表及 MontageSettings，仍可复用 ActionSettings；空列表保留目标名推断以兼容装配，多 Warp 系统应配置明确列表。同实例引用补齐不重复拆建，结束快照保留 Montage/PlayInstanceId。
- `CMC.ClientCorrectionFilter` 仅处理 Packed MoveResponse 中仍匹配 SavedMove 的纠偏。GA 只在本方活动且 Flying 时过滤启动前已记录的待确认 Move；保留原生 ACK/缓存释放，不应用该次位置/模式/根运动，超出时间戳重置安全窗口则放行。启动后的纠偏和权威拒绝仍有效，不能据过滤推断落位已被权威消费。
- UE 的 ASC.PrePredictionActivation 和 GameplayAbility.PreActivate 已有激活前 FlushServerMoves 路径；沿用它们，不额外发送或增加确认等待。

### 重放、生命周期与退出

- 原生 SavedMove 保存原始动画根运动，MotionWarpingCharacterAdapter 在 bClientUpdating 时再次处理。Traversal 经 CMC 通用 MovementReplayCapture 保留逐 Move 的 setup/restore 闭包；在原生 OnPreUpdate（发现窗口后、更新 Modifier 前）记录世界 Target、SwapState 和本方碰撞忽略，记录/重放不合并 Move。
- 重放临时安装历史上下文，以原生 Modifier 副本计算并屏蔽重复 Notify gameplay 回调，随后恢复当前会话。副本仅首次重放创建，后续重置状态复用，FreeMove/Clear 释放历史引用；无额外 Tick/保活计时器。历史 Flying/退出边界可重放，不强制接管外部 Custom/物理模式。
- 权威终止/取消/拒绝按 Avatar/Spec/预测键退休对应上下文；组件停用、注销、销毁、ASC解绑或换绑同样使其失效。旧 Move 仍可能进入 PrepMoveFor，失效分支须清除属于该历史 Montage 的 RootMotionParams，不能只提前返回。闭包弱引用组件；不承诺回滚世界碰撞、动态载体或自定义 Modifier 外部副作用。
- 正常完成以 PlayMontageAndWait.OnCompleted（含 BlendOut）为边界。有限动作要求开启 AutoBlendOut 且时间有限，否则 Commit 前拒绝。站稳动作应在站稳后混出；预测落点、Warp 窗口结束或途中接触不提前切 Walking，也不增加独立落地阶段复制。
- 完成、中断、取消、死亡、失效或 ASC解绑均幂等释放 Target、碰撞忽略、Tick依赖、委托和计时器。恢复碰撞后，仅存活且仍 Flying 时按实际支撑切 Walking/Falling；Ragdoll 等接管的新模式不得覆盖，退出无需等待 Montage 自然结束。
- BeginPlay/Activate/重新注册接合 MotionWarping、PawnExt/ASC 和 CMC。Deactivate/OnUnregister/EndPlay 释放接合，期间 IsTraversalAvailable=false，晚快照不重启。拒绝无根运动、非正/非有限 RateScale 和无效检测通道；PawnExt 解绑顺序见 ASC 生命周期。

### 性能与诊断

- 按频率与查询类型计量：检测含分层球扫、顶部/两侧、深度、后沿细化和净空，不是固定几条射线。默认最多6层搜索、5次深度采样、4步细化；一次 Detect 最多尝试12组边缘候选，提前退出/方向回退改变查询数。成功发起通常本地 CanActivate/Activate 各检一次，服务端另检一次，模拟代理不检。
- 按住且无表面时，预筛失败不会激活/EndAbility，故 RetryInterval 不限制这条逐输入帧扫描路径。补偿在已选片的启动准备阶段每次算一次，失败尝试也可能计算；执行中不重算整链，但活动 Tick/OnPreUpdate 刷新目标、定时复核表面、自主代理逐 Move 记录历史，纠偏时才重放。
- 测量须分开探测、选片/骨骼采样、执行、历史记录/重放及 Debug。关闭绘图/日志后量本体；源码计数不等于实测耗时，多客户端同进程 PIE 总成本不等于单角色预算。
- 调试通道、快捷键和录制合同见下节。几何显示开关仅作用于表现；记录与绘图独立，文字仅进Rewind Details。探针球/胶囊显示真实尺寸，点标记大小不再缩放查询体积。记录的 ElapsedMs 包含当次 Debug 开销，只可比较相同采集配置；ExplicitQueries 计项目显式发起的碰撞查询，不包含 CMC.FindFloor 等引擎内部查询。
- 源码诊断用 `Log LogIMGTraversal Verbose`、`Log LogIMGAbilitySystem Verbose`，结束恢复 Log；窗口原生位移用 `a.MotionWarping.Debug 1`。按角色/连接、Spec、激活 PredKey 关联，事件含 Loc/Velocity/MoveMode。LocalRequestPrepared 与 ServerDetectionRejected 对照起点和意图；同键两端各一条不等于重复执行，不同键/键号跳号不能证明输入重复或丢包。

| 事件/标记 | 应如何解释 |
| --- | --- |
| WarpTargetsRejected，Local=1/Auth=0/Started=0/Sent=0 | 本地启动前拒绝；Sent 是 Traversal TargetData，不是 GAS 激活 RPC，不能直接归因延迟/回滚。 |
| PlacementPredictionFallback / PlacementCompensationSkipped | Verbose 正常降级，保留原目标；Plane/CapsuleSupport 提供失败细节，不按 Warning 刷屏。 |
| Overlap / OverlapWith | 同一播放路径内的窗口时间重叠。比较两窗六位小数起止、目标及 Translation/IgnoreZ/Rotation，不代表重复输入。 |
| ServerDetectionRejected / NoSurface | 请求已到服务端，当前几何检测失败；需查两端位置/时序/碰撞。Reliable RPC 也不能证明底层无丢包，须另测重传/延迟。 |
| AuthorityEndedPredictedTraversal | 同键 GAS 权威终止先于本地完成，不是 CMC 重放或预测错误的单独证据。PredictionRejected 才表示激活拒绝。 |
| Safe capsule / PlacementReprojectionRejected | 仅证明模型支撑/复算结果；缺少拒绝日志不证明进入过复算。全链复算只在预测可用且高度修正非零时执行。 |

### 调试工具与记录合同

#### 日常入口与数据边界

- 非Shipping/Test：PIE后直接在Rewind Debugger开始录制，复现后暂停/停止，选角色的Traversal轨道回看。运行时注册Rewind Runtime Extension，开始/停止录制自动切换IMGTraversal Channel；无采集开关，无需GameplayDebugger。显示设置不影响正在进行的录制，回看不重新检测世界或修改游戏对象。

| 入口 | 默认值与行为 |
| --- | --- |
| Traversal轨道 | 父轨道看概览/关键事件；Requests / GA、Detection、Execution、Network / Replay各看本领域Details。隐藏轨道停止回看绘制，不停止采集。 |
| `IMG.Traversal.DebugDraw 0/1` | 默认0，全局几何show/hide，现场与Rewind共用。开关不写入配置，每次进程启动使用默认值；不影响Rewind采集。无等级、阶段掩码和世界长文字。 |
| `IMG.Traversal.DebugGraph 0/1` | 默认0，四框显示实际平面速度、Montage资产归一化位置、最近一次检测耗时/查询次数；第五框横跨两列，对比原始Montage根运动高度与执行胶囊高度。现场取本地Pawn，回看取当前显示轨道；不造Contact/EnableWarping曲线。 |
| Copy frame JSON | 复制当前完整帧，含所有轨道、候选、事件、图元；不受搜索或分页影响。 |
| Export visible range | 停止录制后，将该角色与时间轴可见范围重叠的全部已分析帧导出为JSONL；边界保留完整帧和同时间戳的多次提交。客户端/权威端分别导出。 |
| Last export | 完整路径可选中复制；剪贴板图标复制路径，文件夹图标定位文件。失败保留上次成功路径；文件已移走则提示重新导出。 |

- 调试文字只进Details；常规Warning/Verbose仍用 `LogIMGTraversal` 写Output Log。旧Visual Logger桥接、Traversal GameplayDebugger Category、F6..F10、采集/等级/阶段/角色过滤、残留时长及两个DDCvar.Traversal配置均已移除，无兼容别名。旧Visual Logging录制需重新录制，不能追溯删除或转换。
- 本地分析优先读取 `Saved/Debug/Traversal` 最新JSONL的metadata与全部帧，再按ActorId/Role/NetMode、Spec/PredKey、Attempt、Sequence和Montage实例关联。先核对Dropped、范围是否包含开始和结束、是否只有客户端记录；有最后检测摘要不等于该帧重新检测。单端记录不能替代服务端重检，跨进程录制时间也不能直接当作同一时钟。
- Warp排查先区分目标接触点、接触窗末胶囊、完整播放路径终点和实际结束胶囊。Mantle 的 TopContactWindow、Hurdle 的 LandingWindow/BackFloorWindow 都以所选窗末为口径，不套用 PlaybackPath.PlacementTime/动画全长。完整路径终点含窗后根运动，不能冒充落地点；提前中断也不能反证或证明完整路径XY正确。Landing clearance胶囊另含校验余量。PlacementEvaluation/LandingEvaluation记录StartTime、求值终点、参考/修正Target、窗口时间、IncludeTail和中心；PlacementWindow记录首次投影的实际入窗时间、剩余平移、原生尾段扣除、WarpPoint/RootOffset与轴选项。旧记录可能仍用 PlaybackEnd/IncludeTail=1，须按记录版本和事件内容解释，不能套用新算法。
- MontageStopped后的Montage_GetPosition可能为0，不能据此认为动画回到了0。应取最后活动帧与事件时序，实际播放时长须减去 Selection.StartTime。新事件 `MontageInterrupted` 对应任务 OnInterrupted（Montage 中断或 GA 取消转发），`MontageTaskCancelled` 对应 OnCancelled（启动失败或 ExternalCancel），记录可观察实例位置/ID、当前活动 Montage、AnimInstance 与结束标志；实例已销毁时位置为 -1。Rewind 继续识别旧合并事件 MontageInterruptedOrCancelled。原生 CancelAbility 先广播取消再 EndAbility，本项目回调可能先 K2_EndAbility，因此 End 日志 Cancelled=0 不能单独排除 GAS 取消或判定中断来源。只有本端 JSONL 时不以晚到权威结束快照倒推最初停止原因，也不以复制 Montage 重试排除自主代理的 GAS 取消。

#### 模块、采集与版本

- 运行时根目录为 `ImagineGame/{Public,Private}/Character/Traversal`，编辑器按同层级放在 `ImagineEditor/{Public,Private}/Character/Traversal`，头文件/实现按Public/Private分离，Game不依赖ImagineEditor。

| 源码入口 | 所有权/合同 |
| --- | --- |
| Public IMGTraversalDebugTypes | Frame/Event/Shape/Candidate只读、可反射/序列化，无UObject引用。 |
| Private IMGTraversalDebug | 同步弱引用Scope、Actor/Attempt/帧采集；不取得玩法状态所有权。 |
| Private IMGTraversalDebugTrace | 版本化Trace、FObjectTrace身份与录制时间换算、Runtime Extension。 |
| Private IMGTraversalDebugPresentation | DrawDebugLibrary几何/Graph，拥有本方LineBatch批次。 |
| Editor Trace/IMGTraversalTraceProvider | Analyzer、AnalysisSession内不可变帧与查询。 |
| Editor IMGTraversalRewindDebugger / RewindData | 轨道、时间轴、回看与统一事件分类。 |
| Editor Widgets/SIMGTraversalDetails / IMGTraversalDetailsCustomization / IMGTraversalDetailWidgets | 导出工具栏、原生Details分类、记录展开、只读复制和资产操作。 |
| Editor Trace/IMGTraversalTraceExport | 文本转换、范围快照和文件写入。 |

- Game仅接RewindDebuggerRuntimeInterface、TraceLog及DrawDebugLibrary；Editor接RewindDebuggerInterface、TraceServices/TraceAnalysis、PropertyEditor和Json/JsonUtilities/ApplicationCore。Shipping/Test不采集绘制，仅保留数据类型/无操作接口。
- 当前原生事件 `IMGTraversal/FrameV4`；V3在V2后追加MontageAssetPath/AbilityClassPath，V4再追加bHasHeightSample、HeightCapture、HeightReferenceZ、MontageRootHeight、WarpedCapsuleHeight。Analyzer分别读取V2/V3/V4，旧版本显式置高度不可用，不伪造零曲线。旧录制缺路径仅显示名称，不扫描Content或按同名猜资产；新增路径/高度需新录制。后续序列化变更须升级事件版本。
- Frame/Event时间统一换算为对象World的录制ElapsedTime，执行区间用实际DeltaSeconds；Provider在Session读锁下访问，时间查询用 `Algo/BinarySearch.h::UpperBoundBy`，不将旧帧延续到无样本空档。
- Events包括预筛、实际激活、检测/选片/落点、降级/拒绝、开始/完成/中断、复制等待/回填与Replay，含Phase/Stage、Attempt及已知身份。预筛不复用旧激活预测键，GA在ActiveState建立前显式提供Spec/Pred。Replay保留历史Spec/Pred/Sequence/Montage/Move时间戳；网络等待按原因/实例去重，新录制重置去重。
- 同帧多次Detect各保留Attempt、候选和几何，不让最后一次覆盖预筛；阶段只作分类，详细数据默认采集。当前帧预算：512事件、4096新图元、32候选，另带入最多8个当前执行的准备胶囊；超出累加Dropped。运行时缓存最多64个Actor，淘汰前提交待写帧。
- RecordingStarted可能来自任意线程，只更新Channel与原子代次；Actor采集在游戏线程。停止录制尝试提交末帧，最终边界仍取决于原生Trace连接/帧时序。录制代次清缓存/去重；Actor/Scope弱引用，组件/ASC解绑、World清理、模块退出释放缓存与委托；Provider随Session释放。

#### 原生Details与复制、资产操作

- 使用PropertyEditor的分类、搜索、列宽和记录展开；保留按轨道分工。Detection看最近检测/逐Attempt候选与决策，Execution看Montage/移动/执行候选/补偿，Network / Replay看身份与网络事件。Details不展开Geometry图元列表，不显示普通Search/Intersections/Output阶段的通用Detail绘图文字及Event.Stage实现分类；Warning、决策、失败和具名补偿诊断保留，底层图元/原始事件仍供视口回放及完整导出。向量、Transform、数组逐层展开；浮点最多显示3位小数，Role/NetMode显示名称，Trace默认折叠。事件/候选每页32条记录，Previous/Next访问全部；页码保留并按记录数钳制，搜索只作用于当前页，导出不分页。
- 不整块禁用Details、不改运行时UPROPERTY权限。默认属性隐藏，由标准DetailWidgetRow与只读文本/超链接展示；FStructOnScope只供布局类型，值来自不可变帧。旧固定高度列表/自制双列已移除。字段值可选中Ctrl+C；右键Copy full recorded value不受显示舍入影响，字段名右键还可复制内部名、名称与值、显示文本。记录头Copy为该事件/候选完整JSON，图元通过完整帧导出访问。父轨道Request与Detection共用的Attempt/Source/Direction只在Request显示一次，Detection子轨道仍自带这些上下文；事件与候选自身的同名字段属于独立时间/Attempt，不能按显示去重删除。
- 标量float/double完整值用9/17位有效数字，结构/数组递归保留精度；字符串保留原名/路径。输入控件IsReadOnly，Paste明确禁用。`FDetailWidgetRow::IsCopyPasteBound`要求Copy/Paste均绑定；值控件通过ContextMenuExtender提供完整值复制，避免自身文本菜单遮蔽。过滤后为空的Events/Candidates不创建分类，避免空记录占位。
- `FDetailGroup::HeaderRow()`每次调用都会新建并替换头部，不能当getter反复调用。每组只取一次引用，再设置标题、摘要、复制和Tooltip；事件名放折叠标题，时间/Attempt/Phase放摘要，展开不重复Name字段。
- SHyperlink的下划线覆盖控件实际分配宽度；资产链接置于AutoWidth子槽，避免Details值列的最小/填充宽度将短名称下划线撑长，默认列宽和其它值控件不受影响。
- Montage按完整对象路径打开；GA类路径取ActiveState.AbilitySpec对应Spec.Ability.GetClass，蓝图GA打开ClassGeneratedBy，原生类走FSourceCodeNavigation。资产只在顶部Traversal分类显示一次；同一行提供名称/完整路径复制、名称点击打开、Tooltip完整路径与Content Browser定位，搜索包含名称及路径。Trace不再重复资产路径/打开按钮，只保留采样元数据；原始Frame/JSON仍保留全部路径。/Script类不提供伪资产定位。运行时Actor/Component路径仅供复制。仅点击时TryLoad，刷新不加载资产；缺失/失败提示，不保活录制World对象。
- Height Comparison仅在父轨道/Execution展示本帧的参考世界Z、MontageRootHeight与WarpedCapsuleHeight，可复制完整精度；无有效采样显示状态。Trace的HeightCapture标识独立执行，不用Attempt/Sequence临时变化连接不同起点。JSON的height_basis说明单位、起点及Warp/CMC含义。
- UE 5.8 `String/FormatStringSan.h::TCheckedFormatStringPrivate`以consteval接字面量数组，Printf首参不能三目选择不同长度格式串；精度选择放在调用分支，每个Printf直接传TEXT字面量。

#### 文本导出与精度

- 输出 `Saved/Debug/Traversal/Traversal_<ActorId>_<UTC>_<Guid>.jsonl`：UTF-8，首行版本2元数据（角色/世界、范围、帧数、枚举含义、时间基准），后续每行完整Frame。单帧复制为格式化JSON。只导出已分析快照，不生成未录制内容，也不替代utrace。
- 64位整数用十进制字符串，避免Cycle/ActorId/Frame/Attempt经过double丢精度。结构体强制递归为JSON对象，避免原生math ExportTextItem先转成精度受限字符串；旧版本1文本仍可读。转换与行级复制共用RecordToJson/RecordObject，不改变原生Trace格式。
- Session锁内仅复制不可变帧引用，JSON转换、进度/取消和磁盘I/O均在解锁后。无Session/无数据/写入失败显示原因；唯一文件名且不覆盖，取消/失败清理部分文件，清理失败给出路径。

#### 图元、Graph与排查边界

- 线用像素细线，点标记最多3cm，探针球/胶囊显示真实尺寸。World后端写普通World/Foreground LineBatcher，暂停回放不能依赖游戏Tick回收；本方图元带独立BatchID，每次现场Tick/回看Update先ClearBatch后画当前帧。隐藏、无帧、切世界、清录制、模块退出也清本方批次，不Flush其他系统；现场暂停不清回看批次。新增会产生Mesh的图元须扩展批次归属。
- 准备胶囊：橙色为补偿前；绿色仅在支撑/重叠/全链复算通过后；已计算但被拒绝的修正位置为红色；无有效预测不伪造胶囊。Purpose区分补偿前后和显式Landing净空，同Attempt活动帧持续带入，结束/新准备/解绑后停止沿用。位置对应动作所选接触窗口末端，Landing=None 也不累计窗后走位；橙色不是 Warp Target 或强制结束位置。位置是准备时世界坐标，不代表动态载体后续预测；模拟代理无本机准备胶囊。补偿为0时前后重合。
- Graph最多240个不同帧的样本，现场与回看均将同一Frame的多次提交合并为最后一个Graph样本，原Trace全部提交不变。回看仅在Graph打开且停止模拟/录制后构建历史；按游标最近5秒采样、剔除窗口外边界帧，不读取游标之后数据，并独立缓存GraphRevision/GraphTime，关闭时释放历史。检测耗时含采集开销，Queries只计项目显式查询，不含CMC.FindFloor等内部查询；后续帧保持最近检测值，不代表每帧重检。Montage进度为资产位置/资产长度，不是Section路径进度。
- 高度框比较同一起点的世界Z增量（cm），橙线MontageRootHeight从本次Selection.StartTime沿PrepareTargets已验证的有限Section路径提取未Warp根运动，以入场Actor/Mesh参照组合；绿线WarpedCapsuleHeight读取同一时刻实际胶囊/Actor高度，包含MotionWarping、碰撞、移动载体及CMC纠偏，不等于单独Target.Z补偿量。原补偿算法只预测所选终点，不据此插值伪造整条“纯高度补偿前后”轨迹。
- 起点/路径只在准备成功且已观测时缓存，实际ExecutionStarted才启用；缓存使用弱Montage引用，随Actor观测释放。录制中途接入且缺起点、模拟代理无本机准备、无效/未起播/已结束或位置不在已验证路径时标不可用。逐帧根运动提取只在录制或Graph开启时进行；仅开几何Draw不提取高度曲线。开启Graph即可在本地采集，无需开始录制；录制开启时即使Graph关闭仍采集。该提取属于Debug开销，不计为纯Traversal检测成本。
- 高度采样必须有实际Montage实例；活动实例已从ActiveMontagesMap移除但Traversal尚在混出时，可使用同资产的原生RootMotionMontageInstance位置，避免Montage_GetPosition的停止后0值伪造回零尖峰。准备成功会退休旧高度起点；仅同帧Prepare→ExecutionStarted接合新起点，未观测的新动作不能继承上一次同名Montage的参考位置。
- 高度框只画最近一次有效HeightCapture，跨执行、缺失样本和采样空档断开；原始与执行曲线共用纵轴，可显示负值，单点画点，无数据只显示状态。不从旧录制或资产名反推缺失样本；History与回看GraphFrames同时保留高度/身份字段。
- 全部Graph曲线共用PlotCurve的采样空档/单点处理，横轴固定为游标前[-5,0]秒；Rewind显式传ScrubTime，空档保留历史并标注History only，当前指标显示n/a，历史高度值标(last)，不把旧值延伸为当前帧。Graph角色优先级为选中的父/子轨道→当前根对象→其它可见调试对象，较低优先级不能覆盖已选角色。无当前Frame不重画旧几何；换会话、隐藏及退出仍清理本方批次。
- Graph使用DrawDebugLibrary的空间World Drawer：运行时在OnWorldPostActorTick采样后直接绘制，Rewind在Extension.Update选定显示轨道后绘制；不再注册Canvas/DebugDrawService回调，也不使用CanvasString。运行时取本地PlayerCameraManager视点，回看优先取同World的当前LevelEditor视口，回退同World本地玩家镜头。图框位于视点前方，按FOV/宽高比布置，旋转后的Y/Z分别对应横纵轴，bDepthTest=false；轴、曲线、图例和字形线全部经WithOwnedBatch归入同一Traversal批次，隐藏/更新/清会话沿既有Clear回收，不另建文字残留或录制通道。一个样本即可显示轴与当前值，无样本显示等待提示；DebugDraw=0仍可独立显示Graph。
- 用户提供的DebugDraw_MultiLineGraph蓝图导出使用MakeVisualLoggerDebugDrawer、旋转后Offset、DrawDebugGraphAxes/Line/Legend，属于空间图形；本工程采用其位置/旋转、轴/曲线/图例和关闭深度测试的用法，但用World Drawer直接绘制，不恢复旧VisualLogger文字通道。原Canvas后端也是库支持的接口；本轮已改用空间路径，旧后端未实际显示的具体运行时断点不能据源码差异断言。
- Traversal业务CPP只通过IMGTraversalDebug提交观测，空间绘制集中于DebugPresentation；不保留直接DrawDebugHelpers、UE_VLOG、屏幕文字、GameplayDebugger入口或等级判断。执行Target不再逐帧生成重复文字事件，目标位置/朝向留在图元中；AddedIgnores数量仅在ExecutionAcquired/Releasing记录。Live图元去重与准备胶囊缓存有各自用途，保留当前帧去重和生命周期清理。旧Frame序列化字段即使当前表现不消费也不能随清理删除，须遵守版本合同。
- RewindDebuggerTrack.Update只更新当前节点，父轨道必须显式Child.Update，参照GameplayInsights/Private/MontagesTrack.cpp。子轨道Name稳定且独立；时间轴与Details共用事件分类，时间轴省略Detail/ExecutionState/ReplayCaptured/Restored/PlaybackSection/Path及PlacementWindow/PlacementEvaluation/LandingEvaluation等明细标记，子轨道Details仍保留。Session/Provider缺失时递归清时间轴/选帧/Details。
- 参考入口：PoseSearch的RecordingStarted/Stopped、Trace Module/TrackCreator/Details；GameplayInsights/Private/SMontageView.cpp的资产链接。Visual Logging文本会被引擎画回视口，不再作本系统主通道。GTA的ClimbDebug、PedDebugVisualiser、DebugDrawStore负责空间绘制，debug/Debug.cpp::RenderProfiler独立调用GetRageProfileRenderer().Draw绘制EKG；仅借鉴职责，不依赖RAGE。
- 验证边界：已核对源码接口和单向依赖；三端行为、远程会话、录制开销与全部UI交互仍须运行验收。具体录制只证明相应角色/时间段，编译或静态检查不替代表现验证。不主动调查或修改Content。

### GTA 对照与已知边界

- GTA 路径相对 `src/dev_ng/game`：检测/手点 `task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`，发起 `task/Default/TaskPlayer.cpp`，Jump/Vault 分流 `task/Movement/Jumping/TaskJump.*`，执行/清理 `task/Movement/Climbing/TaskVault.*`。GTA Clone 同步不等同 UE 的 CMC Replay。
- CTaskVault 按手点、DropPoint、GroundToRootOffset 和动画相位求目标位移，`CalcTotalAnimatedDisplacement`、`SetDesiredVelocityAdjustmentRange` 分别处理动画位移/调整区间。`ClimbDetector::ValidateHandHoldAndDetermineVolume` 调 `CTaskVault::IsHandholdSupported` 校验出口，不能把几何通过等同动画可落位；IMG 分层落实为选片后绑定、可选补偿和显式 Landing 合同。
- 已验证 GTA 位移与接触分别处理：`TaskVault.cpp::CalcTotalAnimatedDisplacement` 从 StartScalePhase 提取至相位1的 mover 位移并按活动双 Clip 权重混合；`ScaleDesiredVelocity` 将 TargetDist 与该总位移之差按各轴调整区间添加到动画速度，`ProcessPhysics` 在 Climb/Slide 消费。`StateClimb_OnUpdate` 在混入或超过初始调整区间时重算剩余位移，并可把后续调整延长到相位1；因此不是只对齐手点后永久冻结后段位移。`ProcessHandIK/UpdateHandIK` 由动画信号启停，向 Arm IK 提交绝对或载体相对目标；`ComputeIsOnGround` 另查实际支撑。这些是本地源码事实，不代表该快照所有资产的配置或发行版实际行为，也不将其整体视为精确接触/必定落位证明。
- 标记职责映射已核对：GTA `StateClimb_OnEnter` 直接读取 Clip 的 `AdjustAnimVel.GetStart/GetEnd`，传给 `SetDesiredVelocityAdjustmentRange`；它是 Climb 的位移调整区间，不特指手点对齐后的第二补偿阶段。UE 的 `UAnimNotifyState_MotionWarping` 本身就是区间标记，`OnBecomeRelevant` 以相同 StartTime/EndTime 创建 Modifier，`MotionWarpingComponent::ProcessRootMotionPreConvertToWorld` 逐帧消费。因此二者在“动画提供允许调整根运动的时间区间、执行端向目标收敛”这一职责上对应，不能据 GTA 使用标记便要求 IMG 再新增独立补偿 ANS。源码只证明标记数据消费，不证明 GTA 编辑器的具体制作操作。
- 数学实现不能视为等同：GTA 以至 Clip 相位1的总动画位移计算 DistanceDiff，将各轴残差按调整区间相位覆盖量加到 DesiredVelocity，并可能动态延长区间；UE SkewWarp 默认从 PreviousPosition 提取到 Window.EndTime 的剩余根运动，缩放/剪切本帧根运动，Bone Provider 另做窗末 Root/骨点目标换算，`bSubtractRemainingRootMotion` 是额外尾段扣除选项。故 GTA 调整标记末端不必等于其目标位移的求值末端，而 IMG Mantle 当前明确以最后平移窗末作支撑求值。保留职责映射，不据此承诺同轨迹、同终点或同碰撞结果；项目准备阶段的 Target.Z 支撑修正也须与原生逐帧 Warp 区分。
- `ms_VaultExitConditions` 的 eVEC_Slide 属于独立滑行动作且禁自动 Vault，IMG 推荐表只保留对应普通 Vault 的六条。27.5 cm深度球、55 cm步距、20 cm断层等保持既有映射，不以调阈值替代执行语义。GTA 多层搜索/坡面后筛、MBR发起范围、JumpVault步态门槛及 AutoVault摇杆/受阻/冷却可作对照；纯自动落地要求与 IMG 默认允许空中属于策略差异。
- 当前预测假设资产默认有限 Section 链正常推进。运行时改链、混合权重、限速/轴耦合和 CMC 碰撞截断会改变实际落位；抓沿下移受阻后，后段会从实际高度继续根运动，预计算不能修复此执行偏差。结束时查实际支撑，不补做运行时轨迹或第二次 Warp；当前用户选择按 Mantle 窗口制作合同保留原生执行，不引入独立后段修正。
- 局部 Target 跟随不等于完整动态平台支持。BasedMovement/动态 Bone、世界回滚、异步检测、持续悬挂/Shimmy/梯子另行设计；沿用 UE/GAS/CMC，不移植 RAGE 的附着、Capsule、手动位移或网络协议。
- **待运行验收：** 各 Action、窗内起播/跨 Section、上下补偿、失败/中断、专服/Listen Server 的自主与模拟代理、引用晚到及延迟/丢包纠偏。源码合同、静态检查及三客户端 PIE 总帧数均不能替代资产表现与联机验收。

## 镜头

### 调用链与职责

- Hero.DetermineCameraMode 选择能力覆盖或 PawnData.DefaultCameraMode，CameraComponent 回退 Default；CameraModeStack 更新/混合，CameraComponent 输出视图。TPS 顺序为 Pawn 标准视点 → 相对视点过渡 → 位置 Lag/骨盆补偿 → Framing 基础枢轴/轨道/可选焦点 → 碰撞请求；Component 在栈混合及单帧FOV偏移后统一推进/叠加 Shake，再防穿墙，再修正离轴后的 LookAt，最后按实际朝向扫描近裁面。旋转误差使用本模式 FOV、UE 投影及分屏尺寸，不按栈混合或后续特效重算；无本地目标 Controller/视口时按相机 AspectRatio 回退。
- `Camera/IMGCameraDamping.h/.cpp` 为项目相机阻尼库，合并标量/FVector 弹簧核、`FIMGCameraLocationDampingState` 的位置 Lag 与既有 `FIMGCameraRotationDamping*` 类型；保留反射类型名及 CameraMode 属性名/默认值。`IMGCameraShake.h/.cpp` 独立提供振荡/包络。各 CameraMode 按需持有本方数值状态；位置、旋转、骨盆及不同 Mode 不共享历史。阻尼依赖从 Mode 指向算法库，Shake 依赖从 Component 经原生适配指向算法库，数值层不查 World、角色、动画、Controller、GAS 或视口，不自行 Tick/复制。新增效果的可复用运算/数值状态扩展相机算法层；模式的采样/构图归 Mode，通用 Shake 的来源/调度/数值边界见「Shake 调度与旋转阻尼职责」。不向 ABP、CMC 或 Mesh 写回。
- View.ControlRotation 保留原始期望朝向，阻尼/Shake 只改表现。武器和 AimAssistInputModifier 仍取最终玩家视点，因而受镜头表现影响。
- 栈首索引0是当前目标模式，`GetBlendInfo` 返回其 Tag/权重，供武器 ADS 散布消费；无栈、停用或未注册时返回空 Tag/0。模式缓存复用实例；抽象/过时类由 Component 回退 Default。Stack.ResetInterpolation递增StackRevision，保证退出回调中重置活跃模式时当前样本也会失效。采样或混合回调中发生停用、结构变化或显式重置时丢弃该次未完成样本，模式移出先提交数组变化再通知退出，避免回调重入破坏遍历。
- 输出覆盖边界：这里的“混合后碰撞”指本工程 CameraModeStack。UE `PlayerCameraManager.cpp::UpdateViewTarget` 在 Component 返回后应用原生 CameraModifier，`DoUpdateCamera` 随后还可混合不同 ViewTarget Actor；任意原生 Modifier/跨 Actor 转场的最终路径不在本求解范围。需要防穿墙的本工程位移/旋转/FOV效果须在 Component.ResolveCollision 前完成；Shake 已集中到 Component，当前职责见「Shake 调度与旋转阻尼职责」。不能将当前实现描述为所有原生相机输出的统一末端约束。

### 碰撞探测与模块边界

- 已实现：`Camera/IMGCameraCollision.h/.cpp` 是独立碰撞 helper，配置、输入、请求、结果与数值历史均定义在用户确认的该头文件。实现只依赖 UE 世界/物理查询与 IMGCameraDamping，不包含 Character、Controller、Traversal、GAS、动画、车辆或水体实现；无全局可写查询暂存、独立 Tick、委托监听、计时器或复制状态。Damping/Shake 继续只接收数值与显式时间，不反向依赖碰撞或世界。
- 实际链：TPS 采样标准 Pawn 视点、位置 Lag、骨盆补偿和朝向后只产生 CollisionRequest；ModeStack 混合未碰撞视图与可用起点事实，CameraComponent 在栈混合、单帧 FOV 偏移及通用 Shake 后调用 Solve 一次，再运行近裁面检查并输出 FMinimalViewInfo。距离、半径、回补与自动轨道历史只由该 Component 持有，切模式保留同一目标/锚点的历史；混合期间冻结新增自动绕转，使用目标碰撞策略，碰撞来源完全混出后才解除约束。不同目标/锚点不混合起点，按目标请求重新求解；混合资格按弱引用索引/序列号身份判定，不将两个失效引用当作同一目标。
- 起点合同：TPS 提供锚点当前根位置 Origin、投影进内缩胶囊的标准视点 Fallback、构图后的 OrbitPivot；探测半径限制在目标胶囊半径减 TargetRadiusMargin 内。胶囊内缩只提供几何候选，Solve 先查询世界有效性，必要时以有限 MTD 修正，失败再尝试经重查的历史 Root 或从有效期望镜头向 Origin 反扫；每次恢复限制距离和迭代次数。随后分别约束 Origin→Fallback、Fallback→理想 Root，危险侧立即收约束、恢复侧用弹簧。深穿透或无可用几何解不保证成功，结果明确报告 safe-root/safe-camera，不能把0距离当作脱离墙体。
- 主求解：Root→期望镜头 Sphere Sweep 生成遮挡距离（启用缓入时为软目标，关闭时直接限位）；可选弧形搜索按命中法线、上一帧角速度构成的四元数轴或镜头移动方向选轴，比较上一帧偏好距离，再用轨道间 Capsule Overlap 及原轨道重扫寻找可保留距离；路径净空/重扫采用相同递减半径。自动绕转的增量于下一帧生效，不写 ControlRotation，主动转镜/混合期间不继续自动调整；开关关闭会清本方自动偏移。硬阻挡立即限位，回拉按距离/比例误差与速度阈值决定弹簧恢复或静止暂停。镜头附近使用无方向短 Capsule Overlap，分步找清空起点并以更小半径重扫；然后 PushBeyond、限幅/平滑半径回补及较小 CameraBodyRadius 最终复核。
- 距离历史合同：恢复判定使用 PushBeyond/半径回补之前的阻尼距离及比例，距离弹簧基线承接 PushBeyond 之后、半径回补之前的距离。没有实际推出需求且没有推出历史时，PushBeyond 弹簧必须直接重置为本次基线，不得以旧距离抵消普通收近；只有实际推出后的退回才用释放弹簧，关掉 MaxPushBeyondDistance 立即清该行为。推出后复扫沿用当前Clipping半径和遮挡过滤（立即模式为略减的广域半径，缓入模式为本体半径）；不能将缓入模式描述成再次强制广域限位。PushBeyondClearance与普通Clearance分别配置，外侧求解及倍率目标均受MaxPushBeyondDistance约束，最终净空单独复核。回补半径增加量不超过本次向内移动量乘 PullBackGrowthFraction，默认1遵循参考的单调不向外规则，允许几何上的短暂保持；可调至0.5以保留至少一半的净向内移动。较小非零回补立即收缩，归零时弹簧释放；关闭回补立即清该状态。两类距离不能合并。
- 过滤合同：默认 ECC_Camera、简单碰撞，Pawn ObjectType 仅在遮挡查询中忽略；最终净空按组件实际 Camera 响应执行。Actor 或 Component 的 `IgnoreCameraOcclusion` 只排除遮挡，`IgnoreCameraCollision` 排除全部查询；Tag 不会替组件打开查询碰撞或改通道响应。命中可过滤 blocker 后有界重查同一段，预算耗尽保留阻挡；不再按前方 CameraBlockingVolume 类型自动放行。没有通用小物件尺寸筛选、角色近身免检区或自动继承 MoveIgnore 列表。
- 通用接合：TPS 从 Controller、视图目标和防穿目标的 IIMGCameraAssistInterface 收集请求，接通既有 `GetIgnoredActorsForCameraPentration`（全阶段允许穿透），新增 `GetIgnoredComponentsForCameraOcclusion`（仅遮挡）与 `GetActorsToPushCameraBeyond`（径向忽略但要求最终镜头在其外）。PushBeyond 按 Actor 组件碰撞反扫求外侧距离、增加安全距离并平滑退回，再检查其他几何及最终净空；受数量/距离上限约束，空间冲突或预算不足时不能保证所有外侧要求同时满足，safe-camera 仍单独复核。目标及其挂接 Actor 默认加入普通忽略，可关闭 bIgnoreTargetAttachments。各请求引用均为弱引用，无持久障碍忽略表。
- 重置/输出合同：目标、锚点、Controller 或查询通道/复杂度变化、CameraCut、显式 Mode/Component.ResetInterpolation、组件注销及无碰撞视图回退会清本方输出历史；仅 Mode 激活重置其本地 Lag/旋转/骨盆，不清同目标切镜的输出碰撞历史。异常时间或长帧重置数值历史后仍执行当前几何约束，零步长不推进弹簧。通知在当前输出完成后发送，仍通过既有 OnCameraPenetratingTarget 隐藏路径，ReportPenetrationPercent 默认0即关闭。XR 沿原生相机输出并清本方历史，不在该分支执行新增碰撞。
- 近裁面：按最终 FOV、UE 投影与本地分屏尺寸得到半视角切线，允许局部视图 PerspectiveNearClipPlane 覆盖，不修改全局 GNearClippingPlane。采用包络 Box 快检及有限保守视锥切片，无法原样等同 RAGE 变截面 Quad；原生 Camera 通道/忽略策略仍决定可检测几何，最低近裁距也不保证处于几何内时画面可恢复。无本地目标 Controller/视口时沿现有规则用组件 AspectRatio 回退。
- 配置替换：旧 PenetrationAvoidanceFeelers、bDoPredictiveAvoidance、PenetrationBlendIn/OutTime、CollisionPushOutDistance 与模式内 BlockedPct/DebugActors 不再参与实现，改由 TPS.CollisionSettings 配置；bPreventPenetration 与 ReportPenetrationPercent 保留。未读取或修改 Content，不能认定派生蓝图旧字段覆盖已迁移或资产已重新编译。

### 碰撞配置与诊断

| 配置组（TPS → CollisionSettings） | 原生默认与含义 |
| --- | --- |
| Probe / Root | ProbeRadius=24 cm，TargetRadiusMargin=7.5 cm，Clearance=2 cm（编辑器最小0.01 cm，与运行限幅一致），连续检测半径差0.1 cm；半径恢复k=2.5、阻尼比0.7。RootToPivotBlend=0，默认用无 Lag 的 Fallback；Root 恢复k=2.5、阻尼比0.7。初始穿透最多4轮、100 cm。 |
| Recovery | 距离恢复k=10、阻尼比0.8、距离误差0.5 cm；静止暂停默认开启，Root 与期望镜头速度阈值均10 cm/s；暂停状态不受弹簧强度影响。该组只控制碰撞恢复，不控制进入/退出瞄准时的构图混合。 |
| Occlusion Search | SweepToAvoidPopIn 与 MoveTowardsClearLineOfSight 默认均关闭；启用后搜索6次、路径最多6次，最大角分别90°/60°，移动搜索最低1 cm/s，沿上一帧绕转轴继续搜索的最低角速度0.1°/s；绕转k=20、阻尼比0.4，保持 push-in 行为默认开启。角度均为度，弹簧k不是Hz。 |
| Clipping / Push Beyond | 镜头本体半径4 cm、短段20 cm、分步4次；安全半径回补默认开启，增长比例PullBackGrowthFraction=1，释放k=15、阻尼比0.8。PushBeyond 最多4个 Actor、合计16次组件 Sweep、1000 cm，距离倍率1.35、外侧余量PushBeyondClearance=5 cm，退回k=5、阻尼比0.8，仅消费显式请求及其有效退出历史。 |
| Query / Near Clip | Camera 通道、TraceComplex=false、遮挡忽略Pawn、同段最多8次过滤重查；近裁面扫描默认开启，1–10 cm、4片，空旷处包络快检通过即结束。各数值在实际消费前限幅并处理非有限值。 |

- 遮挡收近合同：OcclusionPushInBlendTime 默认0.1秒，采用已核对的原实现比例缓入 `Alpha=Clamp(DeltaTime/Time,0,1)`，不是新增弹簧系数，也不是保证在Time秒内完成。广域探头只给软目标，另用当前Root→期望镜头的小球Sweep给主轴硬限位，二者沿用遮挡过滤；忽略遮挡的对象仍由本体短段/最终净空处理，不因新增长段检测恢复其遮挡作用。软模式短段改用实际CameraBodyRadius，并清半径回补，避免随后以广域半径或回补抵消缓入。只在软目标与硬限位之间平滑，首帧/重置、突现硬阻挡、本体/MTD强制约束仍可直接限位。Probe与Body无半径差时没有可用缓入带。
- 策略来源与配置：向内0.1秒来自仓库HEAD原TPS.PenetrationBlendInTime默认及SoftBlockedPct比例插值分支；本工程将该软/硬分离原则接到现有广域探头/小球本体分层，没有恢复旧feelers或兼容字段，没有延长射线或额外预测距离。它是按用户收近手感要求增加的项目策略，不冒充GTA原样算法。OcclusionPushInBlendTime=0关闭该策略，回到参考的遮挡立即限位/半径回补路径；DistanceRecoverySpring仍只管向外恢复。启用时bPullBackByProbeRadius不参与执行，编辑器也禁用该项。
- 回拉弹簧消费：`IMGCameraCollision.cpp::Solve` 在允许距离增大且绝对距离/比例均满足恢复判定时，通过 ConstrainedSpring→IMGCameraDamping.AdvanceSpring 消费 DistanceRecoverySpring/Damping；启用OcclusionPushInBlendTime时，遮挡软目标变小按比例缓入，主轴小球硬限位/本体约束仍立即执行；该值为0时遮挡直接限位。bPauseRecoveryWhenStationary=true 可在Root与期望镜头都静止时暂停本轮恢复，继续移动才解锁，并非固定延时；showdebug camera 的 recovering/paused 可区分。Spring=0表示直达而非更慢，最后不足DistanceRecoveryTolerance的尾差可直接收敛；首帧/显式重置也不承接旧弹簧。移除构图曲线不会删除或关闭该碰撞恢复链。
- 收近的参考边界：GTA `Collision.cpp::Update` 明确用 `isPullBackDesired ? OrbitDistanceDamping.SpringConstant : 0`，普通遮挡收近不是通用缓入弹簧；平顺策略分布在弧形候选/连通路径、Root/半径平滑、回补与实际推出后的释放。不能为了“收近更顺”默认另造提前延长射线/向内弹簧。仓库HEAD原TPS的 PenetrationBlendInTime 只平滑 SoftBlockedPct，超过 HardBlockedPct 仍直接限位；它提供与当前行为不同的软探针策略入口，但不能据静态分支断言用户现场卡顿来自哪一项。参考搜索开关schema默认均false，缺少具体镜头实例数据时不擅自称其常开。
- `showdebug camera` 按当前实际 ViewTarget 显示有效 Root/镜头、初始重叠、实际半径、缩距比、近裁面、暂停恢复、PushBeyond 预算耗尽、物理查询/过滤次数及最多16个命中 Component 路径；同时短时画 Origin（黄）、Root（青）、期望连线及最终镜头本体球；近裁面值为-1表示沿用全局值。CameraComponent.bDrawCollision 可持续显示，默认关闭。查询计数包含 World Sweep/Overlap、恢复 MTD 与 PushBeyond 的组件 Sweep；候选和重查在受阻时增加预算。
- 查询成本合同：查询参数按本次上下文及过滤用途懒构建、Overlap 数组在本次上下文复用，常见请求/命中弱引用使用内联存储。默认无阻挡、无历史回补、非零Fallback/轨道时为6次物理查询；相同遮挡过滤下大球Sweep净空可证明同段小球净空，启用缓入也不重复扫，只有大球命中后才追加本体小球硬限位Sweep；RootToPivotBlend=0 不额外扫描零长度段，搜索开关关闭不执行候选循环，关闭近裁面扫描不计算其投影。受阻时每条 Sweep 最多初查+8次过滤重查，路径最多6次，短段分步最多4次，MTD 默认每处最多4轮/每轮16组件（MaxPenetrationComponentTests可配），PushBeyond 最多16组件 Sweep，近裁面包络后最多4片。这些是算法/查询预算，单次引擎 Overlap 的返回数量及场景复杂度不由预算限制，不能等同固定耗时。
- Unreal Insights CPU 热点为 `IMGCameraView`、`IMGCameraCollisionSolve`、`IMGCameraNearClip`，物理查询统计名为 `IMGCameraCollision`；需同场景、相同镜头输入和配置对照耗时/分配/查询次数。默认预算与 GTA 接近不表示跨引擎毫秒数、最坏帧或实际手感等价。
- 调试绘制：`IMG.Camera.Debug 1` 显示基础枢轴（蓝）、轨道枢轴（黄）、碰撞起点（橙）、安全Root（青）、期望镜头（白）与最终镜头/视线（绿）；`IMG.Camera.Debug 2` 再显示分阶段 Sphere Sweep/Capsule Overlap/近裁 Box、命中法线和世界空间距离流水。红为阻挡、青为净空、银为过滤命中；`IMG.Camera.DebugDuration` 默认0只画当前帧，最多2秒；`IMG.Camera.Debug 0` 关闭。`bDrawCollision` 与 showdebug camera 的短时绘制只相当于基础档。详细查询证据默认每次最多128条，IMG.Camera.DebugMaxQueries可调1–256；未开启时不填充/分配查询证据数组，不新增物理查询；`ENABLE_DRAW_DEBUG` 外不收集/绘制。查询证据属于当前结果，重置/注销一并释放，没有全局线条清空或持续Tick。
- 距离诊断：showdebug camera 与详细世界标签区分 Desired→Occlusion→Spring→Clipping→PushBeyond＋Radius→Final，同时列出主轴hard限位距离，另列 blocked/clipping/recovering/paused、安全状态与过滤数量，用于区别“探测晚命中”“根点恢复”“静止暂停”“推出/回补抵消”和最终 MTD。MTD 的重叠球有绘制，MTD 内部逐组件求解不是独立 World Sweep。静态算法修正不能证明具体截图中的运行根因，仍需这些阶段数据复核。
- 待运行验证：实际资产响应及手感，以及贴墙/薄栏杆/低顶、起点穿入、快速转镜、跨模式混合、暂停/长帧/CameraCut、忽略对象后方真实阻挡、PushBeyond 冲突及自主/模拟目标网络纠偏。其他玩法模块尚未接入新接口，不能据通用相机实现认定 Traversal/车辆/水体等策略已迁完。

### GTA 相机碰撞参考与迁入候选

- 瞄准参数边界：`CameraMetadata.psc::camCollisionMetadataOrbitDistanceDamping` 的共享 schema 默认k=10、阻尼比0.8；ThirdPersonAim/PedAim 源码未找到对该碰撞恢复参数的专用覆盖，实际碰撞对象仍由 `BaseCamera.cpp` 按 `m_CollisionRef` 选择，不能据 schema 默认值断言瞄准模式最终为10。`CameraFactory.cpp::Init` 从 CAMERA_METADATA_FILE 的 PSO及可选meta补丁加载，开发原始入口为 `export/data/metadata/cameras.pso.meta`；当前源码树未找到对应相机实例数据，实际瞄准 CollisionRef/覆盖值待该数据验证。第三人称瞄准 schema 另有 FineAimBlend k=100/阻尼0.95、WeaponZoomFactor k=300/阻尼1，分别为精瞄混合和武器缩放，不是碰撞回弹。
- 以下路径相对本地 GTA `src/dev_ng/game`，仅确认源码与元数据默认值，不代表发行版或具体镜头配置启用了全部分支。主链为 `camera/gameplay/ThirdPersonCamera.cpp::UpdateCollision` → 半径限制/平滑 → `UpdateCollisionOrigin` → `UpdateCollisionRootPosition`（含 `ComputeCollisionFallBackPosition`）→ `camera/helpers/Collision.cpp::camCollision::Update`。Origin、Fallback、Pivot 与实际碰撞 Root 分开，分段定向 Capsule 检查能到达的位置；向危险侧立即收约束，恢复侧弹簧平滑。自定义 Origin 分支仍假定调用方提供安全位置，不能视为任意深穿透的通用解算器。
- 已验证的 GTA 近身边界：主 SweptSphere 从上游修正的 CollisionRootPosition 开始，没有角色周围固定距离的世界碰撞免检区，也不先越过胶囊外表面。minOrbitDistanceToTest=2*collisionTestRadius 只用于轨道间路径搜索，g_MinSafeOrbitDistanceAfterCollision=0.01 是求解后的数值下限，ShouldPullBackByCapsuleRadius 是碰撞后回补，均非近身免检规则。touching 忽略受实体类型与临时 no-collision 策略限制。IMG 按用户选择保持这一边界，不增加通用近身盲区。
- 已验证：`Collision.cpp::ComputeDesiredOrbitDistanceAndOrientationDelta` 首先 SweptSphere 得到安全缩距退路，再按首命中法线、上一帧避让角速度或相机移动方向选择弧形搜索轴；搜索邻近轨道，按距上一帧偏好轨道距离的误差选候选，再以 Capsule 检查候选轨道之间的连通路径，并在原轨道重扫可保留距离。它不只是增加几条 feeler，也不保证绕障总能成功；元数据默认搜索6次、路径检查最多6次，两项启用开关 `ShouldMoveTowardsLos/ShouldSweepToAvoidPopIn` 默认均 false。静止、持续 pop-in、玩家主动转镜及 Hint 等情况会限制自动绕转。
- 已验证：GTA 将遮挡与镜头本体穿模分开。遮挡层可忽略 `POLYFLAG_NO_CAM_COLLISION`、可选碎片及特定实体；`AvoidClipping` 使用更完整的类型和短 Capsule 复核镜头附近，分步 Sphere 找清空起点后再 SweptSphere 约束，不能把“不驱动遮挡缩距”理解为“镜头可停在物体内”。`POLYFLAG_NO_CAM_COLLISION_ALLOW_CLIPPING` 才连本体检测也忽略。被指定 Ignore/PushBeyond 的角色、车辆等另有反向定向检测求外侧安全距离，并复核其他障碍；不是全局忽略附近实体，也未发现该 helper 按物体小就自动忽略的通用规则。
- 已验证：`ComputeDesiredOrbitDistanceDampingBehaviour` 同时比较绝对距离与距离比例的小误差，阻挡收近仍可立即执行；恢复距离用弹簧，角色与期望相机都静止时可暂停回拉，继续移动后解除，减少边缘反复伸缩。连续检测按物理容差逐次略减半径以避免擦边数值震荡；这不是任意缩小保护范围。`UpdateBuoyancy` 在碰撞后处理水面，偏移后可再做遮挡检测。半径回补分支对 near-clip quad Sweep 仍留 TODO，该限制只属于 `camCollision` 的回补实现；GTA 另有实际使用的独立 `NearClipScanner`，见下节。
- 已验证：`camCollision::Update` 在 AvoidClipping 之后执行 PushBeyond，再覆盖距离弹簧基线，随后限制回补增长不超过本帧向内距离并平滑/应用回补；末尾分别记录推出前阻尼距离/比例与最终比例。`AvoidClipping` 的首检是无方向 Capsule，并非从相机朝 Root 的有方向 Sweep。`m_MinOrientationSpeedToMaintainDirection` 默认0.1°/s，满足阈值时从上一帧 heading/pitch 弹簧速度的四元数取轴。IMG 保留这一顺序与历史区分，不将全部值复用成一个 OrbitDistance。
- 已验证：`camera/gameplay/follow/FollowPedCamera.cpp::ComputeMaxSafeCollisionRadiusForAttachParent` 对 Vault/Climb 立即采用更小半径上限，退出混出，源码明确针对 Ped capsule 可能穿入碰撞的问题；元数据 `MaxCollisionTestRadiusForVault` 默认0.15。`UpdateAttachParentMatrix` 在 Vault 中混向动画 root，不能误写成 Vault 的最终 CollisionOrigin 直接改用 root：`ComputeFallBackForCollisionOrigin` 的 root 替换只在 Ragdoll/GetUp 等对应行为启用。`TaskVault.cpp` 的部分非附着 Climb 路径也会阶段性 DisableCollision，故 GTA 并非始终依靠角色胶囊绝不穿透。
- 迁入范围：IMG 已重建通用碰撞、候选搜索、混合输出与近裁面接合；保留标准 Pawn 视点，不因 GTA 用动画 root 就改跟骨骼。未迁入 GTA 的 Traversal、Ragdoll、梯子、车辆、水体、Hint 或对象工厂策略，各玩法需通过自身所有权接入通用接口。原生配置已给默认值，实际手感与资产碰撞响应仍待运行验证。

### GTA 相机 helper 分层与迁入评估

- 已验证：GTA 的 `camera/helpers` 已拆出 `DampedSpring`、`Oscillator`、`Envelope`、`Interpolator`、`FrameShaker`、`Collision`、`LookAtDampingHelper`、`LookAheadHelper`、`CatchUpHelper`、`SpringMount`、`NearClipScanner` 等。它是共享实现分层，不等同纯数值库：DampedSpring/Oscillator 内部取全局 fwTimer，Envelope 消费时间模式，Collision/NearClipScanner 直接查询物理世界及角色/车辆/水体，部分 helper 还直接读 GameplayDirector。当前 IMG 的 Damping/Shake 显式接收 DeltaTime 且无世界/玩法依赖，这个数值边界继续保留；已有弹簧/包络/振荡不重复迁入第二份。
- 已验证：`camera/base/BaseCamera.cpp` 按 `CameraMetadata.psc::camBaseCameraMetadata.m_CollisionRef` 通过 camFactory 创建可选 camCollision，析构释放，PreUpdate 清除本帧忽略列表和 override。`Collision.h` 的轨道距离/回拉/朝向弹簧与历史位置属于 helper 实例；物理 ShapeTest、过滤 callback 用的忽略数组和开关另有静态共享暂存。迁入 UE 时须按相机实例保留历史，查询上下文按次隔离，不能直接复制这些静态可写暂存并用于并发/异步查询。RAGE 工厂、对象池、元数据哈希与 Regd 引用身份不迁入现有 CameraModeStack。
- 已验证：GTA 的安全 Origin/Fallback/Root、跟随者半径限制仍主要由 `gameplay/ThirdPersonCamera` 计算，Vault/Ragdoll/梯子等事实及策略由 FollowPed/GameplayDirector 消费；独立 Collision helper 负责候选搜索、约束、恢复及查询。文件拆分不表示全部职责都已数值化，也不能只迁 Collision.cpp 就视为整条相机碰撞链已迁完。
- 已验证：`GameplayDirector.cpp` 的第三人称镜头插值分支对源/目标普通碰撞调用 Bypass，并让 `helpers/ThirdPersonFrameInterpolator.cpp::UpdateCollision` 处理插值结果；它从目标安全 root 检查混合 root 的可达位置，再调用同一 Collision helper，首次承接源镜头的距离/回拉阻尼状态，强制本帧不自动绕转。该分支避免各模式求解后直接混合安全位置，也避免同帧重复推进碰撞阻尼；不代表所有类型的镜头切换都有统一碰撞保证。本工程已将混合后碰撞与历史所有权接到 CameraComponent 的单次输出求解。
- 已验证：GTA `CameraManager.cpp → FramePropagator::Propagate → NearClipScanner::Update` 在最终输出阶段按开关执行近裁面检查。`ComputeMaxSafeNearClipUsingQuadTest` 按 FOV/宽高比生成近段视锥，用 `phShapeScalingSweptQuad` 检查可用距离，初始相交回退最小近裁距，另有水面与特定视角例外；结果修改 Frame.NearClip，不移动相机，不能替代镜头位置防穿墙。UE 5.8 `CameraTypes.h::FMinimalViewInfo.PerspectiveNearClipPlane` 有每视图入口，`CameraStackTypes.cpp` 的投影计算消费该值；IMG 当前由 CameraComponent 在最终输出阶段直接设置该每视图值。标准 FCollisionShape 仅 Line/Box/Sphere/Capsule，GTA 变截面 Quad 检测不能直接换名使用，若采用分段 Box/多探针等近似须明确覆盖和性能边界。
- 已验证的其他参考：LookAheadHelper 按目标前向速度、包围半径、正反向配置算偏移，并以弹簧平滑倍率，已找到 CinematicCamMan/Mounted 等消费者，不认定普通 FollowPed 统一使用；SpringMount 按宿主挂点加速度驱动俯仰/侧倾，已找到 CinematicMounted 消费者；CatchUpHelper 在 ThirdPerson 接合旧画面与当前轨道，按时间或移动距离消退，CameraCut/无效来源可终止。它们分别是构图、车辆惯性和切镜策略，均不是解决当前 Traversal 碰撞的先决条件。
- 已确认迁入差异：UE Sphere Sweep/胶囊 Overlap 与保守 Box 切片重建检测意图，不保证与 RAGE 的 ShapeTest、polyhedral/BVH、逐面材质标志或变截面 Quad 数值等价。近裁面继续采用本工程 Camera 响应/显式忽略合同，而 GTA NearClipScanner 另有 visible ped、物理类型和逐面过滤。IMG 在世界起点无效时加入有限 MTD/已验证候选恢复，PushBeyond 外侧以小球/净空限幅而非直接复制实体 bound 半径扣减，最终小球约束半径回补；GTA 搜索6次及路径最多6次只是候选默认预算，实际成本另含起点、本体、实体和过滤检查，性能仍须运行观察。Camera 源码注释只描述本工程职责/算法/边界，参考项目和移植说明集中在 AGENTS。

### 轨道构图与 GTA 对照

- 已验证：Lyra `LyraCameraMode_ThirdPerson.cpp::UpdateView` 为 Pivot＋Rotation×PitchCurveXYZ，View.Rotation直接取PivotRotation；本工程原轨道沿用该式，换阻尼/碰撞 helper 不等于换了轨道模型。GTA `ThirdPersonCamera.cpp::Update` 则分别维护 BasePivot、OrbitPivot、OrbitDistance、BaseFront/OrbitFront，先 `UpdatePivotPosition` 再 `cameraPosition=Pivot-OrbitFront*Distance`，碰撞后 `UpdateLookAt/UpdateRoll`。`UpdatePivotPosition` 仍用完整轨道矩阵旋转 camera-relative 的肩位/高度，因此“GTA只转Yaw”不成立；固定枢轴/距离/偏移且无附加策略时，拆式与原整向量旋转可代数等价，不能只改变量名就宣称改善。
- 已实现：用户确认的 `Camera/IMGCameraFraming.h/.cpp` 为纯数值构图库，仅依赖 Core，无 World/Character/Controller/GAS、查询、Tick或持久状态。TPS 采样标准视点和目标Rotation；BasePivotLocalOffset 乘完整目标矩阵，BasePivotHeight 默认沿世界Up、bUseTargetUpForPivotHeight可改目标Up；OrbitPivot 再沿 camera-right肩位及camera-up高度偏移，镜头沿轨道Forward退 OrbitDistance。基础高度、camera-relative高度和距离职责独立，`OrbitPitchOffset` 只改表现轨道不写控制朝向。采用 GTA 的职责与坐标空间语义，同时保留本工程标准 Pawn 视点输入。
- 构图库与配置入口：FIMGCameraFramingSettings仍是数值库输入，不再作为TPS的整组可编辑属性暴露。TPS只从TargetOffset曲线生成OrbitDistance=-X、ShoulderOffset=Y、CameraVerticalOffset=Z；BasePivotLocalOffset/BasePivotHeight/OrbitPitchOffset为0，不叠加第二份偏移。无显式焦点、Roll为0且值在安全范围时，构图等式为Anchor+OrbitRotation.RotateVector(X,Y,Z)。OrbitDistance允许0及负值，保证X为0或正值不被强制改成后退1cm；数值库仍限制距离±10000cm、肩位/高度±1000cm。PostCollisionLookAtWeight=0.625、MinFocusDistance=50cm、OffAxisTolerance=1cm作为TPS高级策略单独配置；不再暴露第二组距离、肩位、支点高度或轨道倾角字段。
- Offset配置合同：恢复同名同类型TargetOffsetCurve（UCurveVector）、bUseRuntimeFloatCurves（默认false）、TargetOffsetX/Y/Z（FRuntimeFloatCurve）。输入为限幅/旋转阻尼后的轨道Pitch（度），输出为完整相机局部XYZ（cm），不是叠加调整量；负X在后方、正Y朝camera-right、正Z朝camera-up。关闭Runtime开关且Vector资产有效时只用该资产，否则回退XYZ曲线。已有CurveVector可重新指定到TargetOffsetCurve，不转换键值；未检查Content，不能保证蓝图此前丢失的属性引用自动恢复。四条AdjustmentCurve、bUseFramingCurves及其叠加分支已移除；碰撞、阻尼、模式混合及可选焦点算法保留。
- 默认Offset：TPS内联XYZ在-70°/0°/45°均为(-300,30,0)cm，Cubic/Auto切线；普通Default覆盖为(-300,0,-50)cm。普通模式的-50现在沿camera-up，符合完整Offset语义，不再是额外世界Up高度；修改Pitch范围不自动重排曲线键。Vector资产缺失时仍可用该内联预设；无效/空内联数据或非有限输出回退通道安全预设(-300,30,0)，Vector非有限结果回退整向量安全预设。此缺资产保底有别于Lyra空资产留在Pivot的原行为。
- 运行时预览合同：偏移字段均EditDefaultsOnly；EvaluateFramingSettings每次从当前模式类CDO读取开关、Vector资产及XYZ曲线，避免缓存模式实例键值未同步。Vector直接GetVectorValue；Runtime各通道按引擎CurveFloat.cpp.GetRichCurveConst规则优先用ExternalCurve（UCurveFloat），清空后回到内联键。编辑当前相机蓝图Class Defaults或绑定的曲线资产，不对运行实例曲线单独覆盖；不缓存RichCurve指针，不新增Tick/委托/计时器或复制状态。资产由反射属性持有，更换/清空下一次采样读取当前来源。编辑器/PIE预览仍须实际验证，源码路径不等于运行验收。
- 曲线能力边界：GTA常规ThirdPerson/ThirdPersonAim/PedAim主链未发现Lyra式Pitch→XYZ资源曲线消费；本工程仅以该入口复用用户已有偏移资产，不宣称其为GTA原有配置。GTA的ComputeOrbitRelativePivotOffset使用包围盒高度/宽度和Pitch计算越顶高度，ComputeDesiredOrbitDistanceLimits结合包围盒、FootRoom与FOV求距离；这些自适应规则仍未接入，单按Pitch采样不能跨体型/FOV/环境自动等效。动画IK曲线/Shake包络属其他职责。FIMGPenetrationAvoidanceFeeler反射头仍已删除，不因恢复偏移字段恢复旧碰撞分支。
- 最终朝向：未指定焦点且只沿原Root→镜头轴收近时保持原朝向；离轴位移超过可配OffAxisTolerance（默认1 cm）时，根据Root/OrbitPivot与前后距离比例构造LookAt，再以PostCollisionLookAtWeight修正当前表现朝向。可选 `GetFramingFocusPoint` 蓝图事件逐次提供显式世界焦点，默认关闭，无隐式锁敌/寻敌射线；距BasePivot或镜头不足MinFocusDistance（默认50 cm）或无效的焦点回退自由视图；有效焦点先按asin(肩位/距离)、asin(高度/距离)补偿轨道偏角，再求最终朝向，输入控制朝向不被覆盖。焦点启用权重随Mode混合，碰撞后纠偏保留本次视觉偏移、移除与显式焦点冲突的碰撞自动绕转；最终Pitch仍限幅，ControlRotation不回写修正值。Frame几何随ModeView混合，Component只在碰撞后修正一次、再扫描NearClip；模式更新中回调失效沿既有修订号丢弃整个样本。
- 已验证的 GTA 扩展：`UpdateBasePivotPosition` 分开宿主局部锚点/世界或宿主Up高度；`ComputeDesiredOrbitDistanceLimits` 结合包围盒、基础FOV和FootRoom形成距离范围；`ComputeOrbitRelativePivotOffset` 可按当前Pitch增加越过包围盒的高度（schema BlendLevel默认0）；`ThirdPersonAimCamera::ComputeOrbitOrientation` 在锁定分支补偿肩位/高度造成的目标偏角，`ComputeBaseLookAtFront` 混合自由瞄准与目标方向，`ComputeLookAroundOrientationOffsets` 按FOV缩放输入。并非每个第三人称镜头统一套用 LookAtDampingHelper，它的确认消费者仍主要是Hint/CinematicMounted。
- 边界：未迁入GTA的包围盒FootRoom自动取景、宿主自动跟随/回中、目标锁定、鼠标/手柄输入系统、切镜首帧射线重对焦或GameplayDirector。`ReorientCameraToPreviousAimDirection` 仅在满足首帧/Ped等条件时沿前次瞄准方向进行最长50 m的武器类型LineTest，不是所有镜头每帧都自动找焦点。需要这些玩法时由其所有者采样并通过配置/焦点入口接合，不让纯构图库反查其他模块。
- GTA Debug 入口：`ThirdPersonCamera.cpp::DebugRender` 在选中镜头时画宿主轴、BaseAttach/BasePivot/Pivot/CollisionOrigin/Root/LookAt；`Collision.cpp` 的性能页分别记录阻尼距离、推出增量、最终距离与期望/实际回补。本工程用对应枢轴绘制、阶段距离及有界查询证据替代其BANK/DebugScene/Profiler依赖；本次未接其对象工厂或全局共享查询暂存。

### 标准视点与姿态高度

- `IMGCameraMode::GetPivotLocation` 对所有 Pawn 统一调用 `GetPawnViewLocation()`，非 Pawn 回退 Actor 位置。UE `Pawn.cpp` 的默认实现为 `GetActorLocation() + FVector(0,0,BaseEyeHeight)`；`GetActorEyesViewPoint` 同样消费该入口。相机不再抵消站立/当前胶囊半高差，不读取蹲伏/匍匐配置或姿态枚举。
- 高度配置由 Character 持有：`BaseEyeHeight` 类默认值为站立眼高，`CrouchedEyeHeight` 为蹲伏眼高，`CrawledEyeHeight` 为匍匐眼高，均相对当前碰撞中心；胶囊 `CrouchedHalfHeight/CrawlHalfHeight` 另属 CMC 碰撞几何。原生蹲伏回调调用虚函数 `RecalculateBaseEyeHeight`；`AIMGCharacter` 覆盖该入口，仅在当前实际姿态为 Crawl 时采用 CrawledEyeHeight，其余交回 UE。匍匐开始/结束回调也走此入口，Controller.AcknowledgePossession 等引擎重算不会直接清掉仍有效的匍匐眼高。
- 姿态高度变化与胶囊中心移动仍共同形成标准视点；TPS.UpdateViewpoint先平滑标准视点相对当前移动参考点的偏移，再进入整体位置Lag。胶囊缩放造成的中心位移不再直接跳进镜头；实际根位移跟随参考点，关闭整体Lag也保留视点过渡。TPS 的 `CrouchOffsetBlendMultiplier`、姿态偏移/混合状态及更新函数已删除，不留兼容字段或重定向。骨盆曲线补偿只处理 FootPlacement 已施加的 IK 位移，独立于姿态眼高。
- Controller 是玩家视图查询入口，但不是姿态高度的存储者。UE `PlayerController.cpp::GetPlayerViewPoint` 在相机缓存有效时返回 PlayerCameraManager 的最终视图；不能在 CameraMode 内以该输出代替 Pawn 视点输入。CameraComponent 仍同步 ControlRotation，TPS 对 Controller 的读取用于切镜重置、视口投影与碰撞辅助，不用于决定姿态高度。
- Lyra 本地对照：`Source/LyraGame/Camera/LyraCameraMode.cpp::GetPivotLocation` 抵消胶囊半高变化；`LyraCameraMode_ThirdPerson.cpp::UpdateForTarget` 显式检查 IsCrouched，并平滑 CDO.CrouchedEyeHeight−CDO.BaseEyeHeight。该相机链没有 Crawl 分支。本工程按用户选择统一标准视点，不保留这一套站立基准与专用蹲伏补偿。
- 待验证：标准视点方案下的实际构图、快速 Stand/Crouch/Crawl 互切、低顶阻挡与网络纠偏表现须 PIE 验证；相较旧方案，相机枢轴会包含实际胶囊中心下降，不能按旧方案的站立基准解释眼高。未调查或迁移 Content 配置。

### 配置与蓝图合同

- TPS 原生ViewPitchMin/Max=-70°/45°（对应参考第三人称schema，可由蓝图覆盖）；普通原生 `IMGCameraMode_Default` 默认 FOV=80°、内联Offset=(-300,0,-50) cm，BlendTime=0.25秒、EaseInOut、指数2。进入某模式使用该目标模式的 BlendTime，退出瞄准回普通模式使用普通模式的 BlendTime；不存在独立的“瞄准退出时长”字段。派生蓝图的显式覆盖须由用户同步；不全局修改 UIMGCameraMode 的0.5秒基础默认值。
- 相机资产边界：Camera源码不按路径加载、自动修复或重新指定资产引用；Default仅提供数值曲线键，不构造/保存资产。DefaultEngine.ini已移除IMGCameraMode_ThirdPerson旧SocketHeightCompensation→Pelvis字段的10条PropertyRedirects（部分目标早已失效），相机偏移及姿态字段无兼容重定向。曲线、PawnData、Ability相机类由用户在蓝图明确指定；其他模块的CoreRedirects/引擎碰撞ProfileRedirects保留，不读取或修改Content。
- 用户以蓝图拓展TPS瞄准模式，不增加原生瞄准类。构图入口为Third Person.TargetOffsetCurve；使用已有CurveVector时关闭Use Runtime Float Curves并指定资产，XYZ沿相机局部轴解释，不再另配FramingSettings。也可开启Runtime开关编辑TargetOffsetX/Y/Z。CameraTypeTag配IMG.Weapon.SteadyAimingCamera；进入/退出由既有能力SetCameraMode/ClearCameraMode驱动，普通PawnData默认模式不指向瞄准蓝图。未写入资产或验证实际构图。
- GTA 瞄准 FOV 依据：`ThirdPersonAimCamera.cpp::ComputeDesiredFov` 消费 WeaponInfo.CameraFov 与精瞄倍率，`weapons/info/weaponinfo.psc` 的 CameraFov schema 默认45°、精瞄倍率1；`camera/viewports/Viewport.cpp` 将 Frame.Fov 传至 `rage/base/src/grcore/viewport.cpp::grcViewport::Perspective(float fovyDegrees,...)`，明确为垂直视角。UE 当前 FieldOfView 为水平角，16:9时45°垂直对应约72.73°水平（2*atan(tan(45°/2)*16/9)），不直接填45°；本工程建议73°为取整后的近似基线。实际武器/镜头实例可覆盖这些schema默认，肩位、距离和0.20/0.25秒过渡是本工程配置建议，不宣称为GTA实例原值；GTA的武器缩放k=300与精瞄混合k=100不能直接填入本工程以秒计的BlendTime。
- 位置 Lag 默认启用，使用 GTA 风格弹簧，XYZ 共用强度/阻尼。保留蓝图属性 `CameraLocationLagSpeed`（默认10），作为 sqrt(k) 的响应强度，实际 k=Speed²；不是 Hz，也不保证与旧 VInterpTo 相同的跟随曲线。新增 `CameraLagDampingRatio` 默认1、范围0至10，欠阻尼允许超调；Speed=0直达目标。`CameraLagMaxDistance` 仍以世界 cm 限制枢轴误差，0不限制，触及硬限距时清速度。
- Lag 数值状态/子步/限距封装在 `FIMGCameraLocationDampingState`，TPS 仅传入采样、现有配置与本帧直达请求。子步保留 `bUseCameraLagSubstepping` / `CameraLagMaxTimeStep`（默认开启、1/60秒、运行时最小1/200秒），从上一帧真实期望枢轴插值到本帧期望枢轴，均分时间推进弹簧，不从滞后输出构造目标轨迹。停用、重激活、CameraCut、目标/控制器变化清理位置/速度及目标采样历史；关闭、非法配置/时间或超出子步预算直达有效目标并清速度，无效目标清历史并返回上次有效滞后位置（未初始化时为零）。
- 视点过渡配置位于Third Person|Viewpoint：ViewpointSpringConstant=20（单位质量k，0直达）、ViewpointDampingRatio=1；复用FIMGCameraLocationDampingState和AdvanceSpring数值核，无额外Tick/复制/计时器。Character的Movement.UpdatedComponent为当前胶囊且bCrouchMaintainsBaseLocation为true时，Reference为沿Movement.GetGravityDirection偏移半高的胶囊底部；其余使用Root中心。UE的该标志默认Walking为true、其他模式false，本工程ResizeForCrawlTransition也读取它。参考规则/重力轴切换时用上一帧半高调用LocationDampingState.Rebase换算位置/前次目标，保留速度，不让参考系变更伪装成视点跳变；最终目标仍是GetPawnViewLocation，不回写角色眼高、不检查Crouch/Crawl Tag。状态归当前Mode，根组件更换、首次激活、停用/重激活、目标/Controller变化、CameraCut/显式Reset及异常时间清历史；站蹲/匍匐切换本身不触发Reset。当前期望参考点与相对偏移同时采样，连续反向切换承接当前过渡；碰撞几何仍可优先强制约束。k=20参考CameraMetadata.psc.m_FollowPedCrouchingBlendSpringConstant的schema量级，1沿用DampedSpring默认阻尼；本地未找到该schema字段实际消费，且本工程为标准视点适配，不宣称GTA姿态实现或发行版实例参数等同。showdebug camera显示Viewpoint误差与配置。
- Rotation Damping 默认关闭。旋转按 Yaw/Pitch 两轴执行目标角速度平滑 → 积分推进朝向 → 限制角度误差 → 弹簧收拢，继续使用 UE `FMath::SpringDamper` 两级公式；频率为 Hz，Roll 透传，MaxErrorFraction 限制相对各轴半 FOV 的误差，并在角度弹簧后再次限幅。保留最短角度路径、轴旁路和既有重置合同。
- 旋转优先沿现有结构调参，不因位置 Lag 的表现改善或同属算法库而统一替换弹簧核。UE 的临界/过阻尼分支保留完整速度状态，GTA 快照保留慢指数模式，两者启停/拖尾响应不同；k=(2πf)² 只对应单位质量下的频率/系数关系，不能保证参数或手感兼容。替换数值核须有明确响应需求，不能据结构相似认定表现一致。
- TPS 的 `Third Person|Pelvis Offset` 默认关闭，原生曲线名默认 `LegIK_PelvisDeltaZ`、骨骼 `pelvis`、SpringConstant=10、DampingRatio=1；资产输出及派生相机配置须同名，修改原生默认值不会重命名资产曲线。`UpdatePelvisOffset` 从目标 ACharacter 主 Mesh 的 `GetCurveValue(Name,0,Value)` 读取已求值的组件曲线（含 PostProcess/URO 混合），只消费世界 cm 的已加权高度差，不读取骨骼绝对高度、不再乘 IK 权重；骨骼名用于校验对应骨架。曲线缺失以0为目标渐退；关闭、无效来源/配置/数值立即清理，切换目标、Mesh/骨架、主 AnimInstance、曲线/骨骼名及 CameraCut 后首样本直达目标。相机只读已有姿势，不强制动画求值，不新增 Tick/复制或移动依赖。
- Lag/骨盆平滑共用 `IMGCameraDamping::AdvanceSpring`，按 GTA `camera/helpers/DampedSpring.cpp` 的单位质量 k/阻尼比公式：欠阻尼携带速度；临界/过阻尼沿用该快照只保留慢指数模式的近似，阻尼比1时响应约为 exp(-sqrt(k)·dt)，不是 UE 的 Hz 参数或完整临界阻尼解。k=0直达目标；已初始化时零步长不推进。骨盆来源校验/曲线采样与缓存留在 TPS，补偿同号加在位置 Lag 之后，并进入现有防穿墙；`showdebug camera` 显示 Lag 误差/速度/强度/阻尼及骨盆曲线/骨骼名与当前 cm 偏移。
- CameraShake 由独立定义和 CameraComponent 统一执行，模式仅配置接收倍率，入口与配置见下节；不存在 ThirdPerson 的总开关/单套振荡配置或逐帧 Active 查询。

### Shake 调度与旋转阻尼职责

- 已实现分层：IMGCameraShake为无World/Actor查询的数值配置、振荡/包络与历史；IMGCameraShakeDefinition是纯配置DataAsset及有效性校验，可复用资产或由来源持有内联配置的临时副本；IMGCameraShakePattern适配数值与UE视图，配置由Prepare复制，Root不再开放第二份配置；IMGCameraShakeInstance为Transient/NotBlueprintable播放句柄，仅持有配置强引用、来源身份与运行状态；IMGCameraComponent_Shake负责统一入口、调度与清理。来源/Component单向依赖配置、实例、Pattern、数值层，Camera禁止包含或查询Weapon/GAS/ShooterCore。持续实例只监听通用来源Actor（或组件Owner）的OnEndPlay，创建失败、替换、结束均对称解绑；无新增Tick、计时器或RPC。
- 已实现数据合同：Definition.Settings.Components及Hero内联Pattern是有序振荡列表，支持同轴多项叠加；各项配Component、Amplitude/Frequency/Phase/Waveform/Decay/DCOffset及独立包络。World/Camera位置用cm，Camera XYZ为前/右/上，Pitch/Yaw/Roll/FOV用degree，Zoom无量纲。通用分量默认Pitch、幅度/DC=0、频率1Hz、随机初始相位Phase=-1、Sine、Decay=1；通用列表为空，跑动来源另提供非零默认分量。数值配置/枚举不开放蓝图读写，运行时倍率经统一API提交。
- 已实现算法：总体包络与各分量包络相乘，支持 PreDelay→Attack→Decay→Hold→Release；有 Decay 时 Attack 到1再线性到 Sustain，无 Decay 时直接 Attack 到 Sustain。默认 PreDelay/Attack/Decay/Release=0秒、Sustain=1（允许0至10）、Hold=1秒、Attack=Linear、Release=Exponential，对照 GTA CameraMetadata.psc schema；总体包络默认启用，分量包络默认关闭。曲线覆盖三角函数 SlowIn/Out/InOut、复合 VerySlow、EaseIn/Out、二至五次及圆形 easing，Exponential Attack 为 1-exp(-7t)、Release 为 exp(-7t)。DampedSine 保留以 1/Frequency 驱动第二相位的周期衰减，不改成按总时间单向衰减。
- 已实现时间/恢复：Start 初始化所有分量并使用运行时种子；AutoStart 保留振荡相位，仅恢复总体 Release 或刷新总体有限 Hold；提前 Release 从当前包络值退出；Sustain>1时从Release恢复会接回当前线性Decay段，不将现有幅度截回Attack峰值；Release=0 或未配置总体包络时立即完成停止，同帧重读不再回放旧样本。零时长阶段在零步长也可完成。数值层按显式 DeltaTime 消费剩余阶段时间，长正帧采用闭式相位推进，不再沿用旧 Shake 的32/120秒清零；异常时间/配置清本实例。首次取景按请求以来的 World 时间补算（至少本次 DeltaTime），避免从未取景的组件在很久以后重放过期短效果；随后 Pattern 每个 GFrameCounter 只推进一次，重复视图查询复用分量样本再应用当前倍率；Scrub 使用同一种子从起点求值，不宣称支持 GTA 回放倒放。释放期间频率按原倍率与1的插值恢复，未照搬 GTA 快照中可能在1两侧交替的频率回写。
- 统一入口：PlayCameraShake(Definition, AmplitudeScale, FrequencyScale)为有限单次；StartCameraShake(Definition, SourceOwner, AmplitudeScale, FrequencyScale)复用同来源/同配置身份的持续实例；启用总体包络时由MakeContinuous统一设置Hold=-1，关闭包络的配置保持有效、Stop直接结束；AtLocation提供空间单次。入口不再接受Instance子类或TSubclassOf，不做旧类入口兼容。Update/Stop与按来源/全体停止消费返回的播放句柄。持续来源注销/已观察到的EndPlay或Actor.EndPlay开始释放，旧代次不能Update恢复；单次与瞬时GA生存期独立。原生PlayerCameraManager.StartCameraShake不准备本Pattern，会Warning并拒绝。实例强持有Definition，Pattern持有本次数值副本；来源临时配置受UPROPERTY持有，不能每帧重建改变身份。
- 已实现调度：同一Definition对象身份共享MaxInstances（默认1）、MinTimeBetweenInstances（默认0秒）与Overflow；默认ReplaceOldest，ReplaceWeakest按幅度×空间倍率×释放比例比较，可拒绝较弱请求。最小间隔按成功创建时间计算，短实例结束后仍生效；时间表用TObjectKey保持索引/序列号身份而不强持有配置，创建时清失效项，StopAll(true)/目标切换/停用/注销/结束清表；同源持续实例更新/恢复不重复消费启动间隔。配置有效且新实例初始化成功后才提交替换；结束/强停沿原生StopShake/Teardown并释放组件数组引用。不另建Manager/Subsystem，不在原生CameraModifier重复注册。
- 已实现空间链：PlayCameraShakeAtLocation 提供固定世界来源与强度，使用未摇晃视图重新计算 SlowIn(SlowInOut(distanceRatio)) 衰减及远离目标时的 sqrt(IdealTargetDistance/Distance) 补偿；InnerRadius=500cm、OuterRadius=20000cm、IdealTargetDistance=500cm，对照 GTA Explosion schema 的5/200/5米。空间字段只在空间入口校验。通用定义默认仍为1个/替换最旧；需要 GTA 爆炸式策略时该定义配 MaxInstances=2、ReplaceWeakest。室内、武器精度和事件强度修饰由来源决定，本层不读取建筑/武器系统。
- 模式与清理合同：各 Mode 仅配置 CameraShakeScale（默认1）及精确 ShakeCategory 倍率，FIMGCameraModeView 在混合前计算有效类别倍率，栈按实际权重混合，缺类别用该模式全局倍率。模式激活/退出不重建共享实例，倍率0仍推进其寿命；目标/Controller更换（按弱引用索引/序列号身份比较，涵盖 stale→空）、显式 Component.ResetInterpolation/CameraCut、组件停用/注销/EndPlay、XR/无效视图回退会清理相应效果，连续未求值后恢复丢弃旧实例并保留本帧新请求。BeginPlay、普通激活和首个视图更新只重置模式/碰撞历史，不误删此前或激活通知中提交的新效果；Activate(bReset=true) 在通知前清旧效果。
- 已接开枪来源：IMGRangedWeaponInstance的Weapon Config|Camera配FireCameraShake纯配置资产（UIMGCameraShakeDefinition）、幅度/频率倍率，默认空/1/1，均为EditDefaultsOnly，不开放蓝图读写。C++ getter实际传入PlayCameraShake；WeaponFire在TargetData校验、Commit成功且能力/武器有效后，仅本地PlayerController提交一次。监听服AI、远端确认/模拟代理不重播，无振荡复制，GA结束不停止有限反馈。Shake不写ControlRotation，但武器后续GetTargetingTransform读取最终PlayerViewPoint。
- 弱引用身份合同：UE WeakObjectPtr.h 的普通相等运算会把两个 Get() 均为空的引用视为相等，包括 stale 与显式空；不可据此判断对象身份未变。Controller 销毁中/失效时先归一为空；相机 Shake 上下文、TPS 目标/Controller历史和输出碰撞目标/锚点/Controller历史使用 HasSameIndexAndSerialNumber，按来源停止亦使用该身份比较，确保对象失效后的清理/重置不会漏掉。引擎接口已核对 IsXRHeadTrackedCamera() const、Actor.OnEndPlay 签名及 UCameraShakeBase Stop/Teardown 合同。
- 配置用法：Instance不是配置类型，禁止以其蓝图子类/原生类作资源入口。Hero跑动直接展开现有FIMGCameraShakeSettings；需要跨来源复用时创建UIMGCameraShakeDefinition DataAsset，在Settings配振荡/包络，Scheduling配并发，Spatial仅由空间事件消费。Mode只配接收倍率。旧Instance子类/RootShakePattern配置入口与旧TPS bEnableShake/GetShakeInput/模式内ShakeState均无兼容或重定向；未修改Content，既有类引用/旧节点须按新纯配置接口重配。
- GTA 可复核入口：`camera/gameplay/follow/FollowPedCamera.cpp::UpdateRunningShake` 为运动事实→强度/频率→实例；`weapons/Weapon.cpp → camera/CamInterface.cpp::RegisterWeaponFire → base/BaseDirector.cpp → gameplay/GameplayDirector.cpp::RegisterWeaponFireLocal → base/BaseCamera.cpp::Shake/BaseUpdate` 为开枪事件/调度；`weapons/ExplosionInst.cpp → CamInterface::ShakeCameraExplosion → GameplayDirector::RegisterExplosion/PostUpdate` 为独立空间事件列表；`helpers/{Oscillator,Envelope,FrameShaker}` 与 `rage/base/src/math/amath.h` 为算法。GTA 第三人称插值在 `helpers/ThirdPersonFrameInterpolator::UpdateFrameShake` 将 Shake 放在插值/碰撞后，本工程按现有几何合同在 Component 碰撞前叠加。
- 边界/待验证：未迁入 GTA 动画 Shake、DOF/运动模糊/手柄震动、独立相机时钟/倒放或 Director。FOV 分量使用 UE 水平角；Zoom 按当前 AspectRatio 换算垂直角再除倍率、转回水平角，不保证各种视口轴约束下与 GTA 数据一比一。调度属于当前目标的 CameraComponent，跨 ViewTarget 持久效果没有自动交接。跑动来源已接HeroLocomotion（见下节）；落地/受击/爆炸来源尚未接入，未迁移其资产。模式混合、预测拒绝后的短暂视觉反馈、强弱替换、来源失效与构图表现仍需用户 PIE 验证；不以静态核对替代编译/运行。

### 跑动 Shake、脚步与动画数据

- **当前普通TPS范围：** 本阶段移动镜头以GTA FollowPed.UpdateRunningShake为基准：走路不启用当前跑动通道，跑步与冲刺共用一套Pattern，平滑移动强度r后连续插值幅度及额外频率。HeroLocomotion配置界面按共享Pattern、Run幅度倍率、Sprint幅度倍率/额外频率、Response刚度分组；对应内部MinAmplitude、MaxAmplitude、MaxFrequencyScale与RunRatioSpringConstant。本阶段不预建三套步态Pattern或走路/闲置头摆，不增加第二套步态决策。额外频率0.25表示最大1.25倍，基础跑步频率为Pattern频率乘1，不暴露恒为1的字段；来源幅度/频率倍率不改变FOV/Zoom，模式继续只管接收倍率。项目仍采用已记录的实际速度适配，不将UE的Gait或cm/s冒充GTA MBR。
- **保留移动表现扩展边界：** 当前关闭Walk仅为跑动通道规则，不是框架对走路镜头的永久限制。未来Walk及其他移动表现按实际需求在HeroLocomotion来源侧增加响应或独立通道，形状相同可复用Pattern，确需不同形状再分配置；CameraComponent/Pattern/数值层不增加EGait或具体移动业务分支，现有统一入口继续消费配置与幅度/频率倍率。各通道独立持有句柄，普通通道切换只停止本通道，按来源全清用于整体取消/失效/退出，避免新增Walk后被Run的局部结束误停；复用同一持续通道时保持配置身份及相位，不每帧或每次步态变化重建实例。不为尚未接入的场景预留空选择器、无消费者参数或额外管理器。
- 已验证其他GTA链路（不并入普通TPS）：ThirdPersonPedAssistedAimCamera在Update中分别调用RunningShakeSettings/WalkingShakeSettings的UpdateMovementShake；CameraMetadata.psc的ShouldApplyCinematicShootingBehaviour默认false。ShouldScaleWithWalk/Run/Sprint按优先选择1/2/3作为MBR归一化参照，并非互斥步态准入，两组可同时参与；其Min/MaxAmplitude schema默认0.5/1，另乘目标/电影化状态的activity倍率。FirstPersonShooterCamera.UpdateFrameShakers则读取相对头骨姿态，经逐轴弹簧后按Idle/Walk/Run/Sprint的BlendLevel缩放；RelativeAttachOrientationSettings schema默认0.02/0.125/0.15/0.5、Aiming=0，另有Pitch/Roll/Heading倍率。这些是头骨姿态权重，不是普通TPS正弦幅度预设，不能直接照抄到cm/degree振荡分量。
- 已验证缩放边界：ThirdPersonPedAimCamera.UpdateFrameShakers将传入幅度乘ShakeAmplitudeScaleFactor（schema默认1）；本工程对应相机Mode的接收倍率。FPS瞄准开始另调用Shake(ZoomInShakeRef)，属于一次性转场事件；AssistedAim的PivotScalingSettings.MaxZoomFactor影响另一条构图平滑链，不能当作跑动Shake频率/幅度配置。本次不增加瞄准、武器或FOV逻辑到HeroLocomotion。
- 已验证 GTA 普通第三人称：`camera/gameplay/follow/FollowPedCamera.cpp::UpdateRunningShake` 消费 MotionData.CurrentMoveBlendRatio 的模长和实体实际速度，不读取 Foot/Contact 动画标记。`Peds/PedMotionData.cpp` 的 Run/Sprint MBR 边界为1.5/2.5；实际速度另须达到源码中的1.5阈值，且排除游泳、降落伞、跳跃、空中与下落。MBR在[1.5,2.5]时强度比r=0，超过2.5时r=Clamp((MBR-2.5)/0.75,0,1)。MBR是移动混合语义，不能直接当成UE的cm/s。
- 已验证 GTA 参数消费：ShakeRef选择定义；Amplitude=Lerp(MinAmplitude,MaxAmplitude,r)，FrequencyScale=1+r*MaxFrequencyScale，后者字段是额外增长量。r由camDampedSpring平滑，RunningShakeSettings schema的RunRatioSpringConstant默认5、阻尼比默认1；首次更新或CameraCut直达目标。`helpers/DampedSpring.cpp` 在该阻尼比下实际采用exp(-sqrt(k)*dt)响应，本工程`IMGCameraDamping::AdvanceSpring`有对应数值路径；它不是5 Hz，也不等同于FInterpTo速度5。无效移动进入Release，GTA还在释放时将来源振幅拉向MinAmplitude，本工程通用Stop保持最后请求振幅并由包络退出，频率向1恢复，尚无跑动专属释放振幅修饰。
- 已验证数据边界：`camera/system/CameraMetadata.psc::camFollowPedCameraMetadataRunningShakeSettings` 的ShakeRef为空、Min/MaxAmplitude与MaxFrequencyScale初值均0，不能把schema初值当作已启用的跑动效果。FrameShaker调试列表中的FOLLOW_RUN_SHAKE只证明名称存在；本次未定位其实际振荡分量资源及FollowPed实例参数，不宣称任何建议起调值是GTA发行预设。GTA相机局部X/Y/Z为右/前/上，对应本工程CameraLocationY/X/Z；位置资源从米换到cm，旋转从radian换到degree并按旋转语义核对轴与符号，频率Hz和初始相位cycles可直接对应，不照抄轴下标。
- 已实现来源：`IMGHeroLocomotionComponent_Camera.cpp` 在Locomotion Ready订阅Character.OnCharacterMovementUpdated，释放连接时对称解除；移动后读取重力平面实际速度，跳过bClientUpdating预测重放，用World帧时间推进r，同一GFrameCounter最多推进一次平滑。只接受本地PlayerController控制且当前ViewTarget就是本角色的视图，过滤专服/远端/AI；目前不自动为观战的远端目标驱动。以HeroLocomotion组件本身为SourceOwner，只调用CameraComponent.Start/Update/Stop及来源清理，Camera、Mode、算法不反查移动/GAS。不注册额外Tick、计时器、复制状态或RPC，已有PreCMC Tick只补清理，不在CalculateMovementParameters/重放中发送请求。
- 已实现速度映射：r=Clamp((Speed-RunSpeed)/(SprintSpeed-RunSpeed),0,1)，参考速度复用LocomotionSet及已有方向映射/RunDirectionScale/SprintDirectionScale；冲刺参考不大于跑速时r=0。普通站立地面跑动、非Walk且无RootMotion/Ragdoll恢复时另用固定150cm/s物理门槛（对照GTA 1.5m/s），不开放MinSpeed配置；该速度映射是本工程适配，不等同GTA MBR曲线。首次/重进/失效实例直达r，持续用AdvanceSpring(k,1)平滑，Update失败统一Start恢复，同源同配置释放中保留振荡相位。
- 配置入口为HeroLocomotion的Locomotion|Running Camera Shake：RunningCameraShakePattern内联数值配置，替代运行时类选择器，无需创建Shake蓝图。默认上下0.5cm/2.8Hz/相位0、Pitch 0.12度/2.8Hz/相位0.25、Roll 0.08度/1.4Hz/相位0，Sine/DC=0，总体Attack=0.15s、Hold=-1、Release=0.2s；Min/MaxAmplitude=0.6/1、额外FrequencyScale=0.25、SpringConstant=5。仅类默认配置，无蓝图读写；开关和振荡/包络均由配置消费，持续播放只接管总体Hold，不覆盖bUseEnvelope；范围对照GTA schema，启用要求0<Min<Max，Min=0或Max<=Min作为正常关闭，不记非法配置告警；起调振荡/幅度不是已验证GTA发行预设。跑动固定分类为来源注册的native tag IMG.CameraShake.Running，可由Mode的类别接收倍率独立调节；首次本地消费时建立临时Definition并UPROPERTY持有，连接释放时清缓存，固定本轮配置身份；非法配置在速度门槛之前给出一次具体Warning，避免被未达速条件遮住。
- 来源生命周期：停跑、腾空、蹲伏/匍匐、RootMotion或特殊移动Stop(false)，无效期间不Update；释放中保留弱句柄供重进复用。失去本地当前视图、死亡、CMC停用/停Tick、换相机、Locomotion不可用及EndPlay清本来源全部实例并复位。已有组件Tick覆盖移动通知消失时的清理；基类停用/注销/Set移除沿OnLocomotionUnavailable解绑并释放临时配置，随后Ready建立新配置身份；切镜使旧句柄失效后由有效移动通知重建。新修改仍待用户编译/PIE验证；用户已有ImagineGame_2.log确认Ready与AutonomousProxy非法空定义拒绝执行，只证明该现场来源链到达校验，不代表新修改已运行。
- 已验证动画分支：GTA `animation/EventTags.h`有Foot/Left/Right/Heel及FindEventPhase，HumanLocomotion用左右脚标记相位选择停止动画；`TaskHumanLocomotion::VerifyClip`的标记遍历是校验入口，不是普通TPS相机驱动。`Peds/PedFootstepHelper.cpp`通过骨骼脚位置/速度、地面查询及逐脚重置门槛触发脚步音效/VFX，是独立分支；`FirstPersonShooterCamera.cpp::UpdateFrameShakers`另有头骨姿态驱动，不混称TPS跑动Shake。
- 已验证本工程表现接点：`AnimNotify_IMGContextEffects`把AnimMotionEffect广播给Actor及所有实现接口的组件，双处转发会重复；`IMGContextEffectsLibrary`当前只加载SoundBase/NiagaraSystem，不会执行CameraShake类。需要脚步冲击时在公共运动事件处由单一消费者调用相机入口，不以声音/VFX加载或生成成功为前提。FootContactCurves默认contact_l/r是独立的0..1落脚状态，不能保证任意时刻恰好一只脚为1；Footstep Modifier另产出的Phase默认在左+1/右-1间线性插值，是往返波形而非单调0..1周期相位。当前Shake运行时只接幅度/频率倍率，Phase仅为起始配置；真正按动画相位锁定需后续通用采样接口，不能仅连接Contact就宣称同步。涉及相机对象的请求在游戏线程提交，不从动画工作线程求值中调用。

### 重置、调试与参考

- CameraComponent.ResetInterpolation/NotifyCameraCut 向所有在栈模式转发，下次完整视图更新消费，回调中的新请求留给下一次。组件 Activate/Deactivate、注册/注销与 BeginPlay/EndPlay 对称管理栈及输出状态，重复激活不重复通知；EndPlay 解绑模式选择，退出/注销回调不得重新激活。组件未注册、停用、目标失效、栈为空或 XR 时回退原生视图并清碰撞/FOV历史；连续引擎帧中断后再次采样先重置模式数值历史，避免未渲染的旧历史恢复。单帧FOV偏移按 GFrameCounter 归属和消费，跨帧未消费的值不累积。TPS 激活、目标/控制器变化或可观察到的 CameraCut 重置模式本地历史；输出碰撞按独立 Component 生命周期清理并查询当前几何；没有通知的瞬移须显式重置。
- 选择与外部接口：Hero.EndPlay 仅解除仍属于本 Hero 的选择委托并清本方能力覆盖；能力正常结束/取消沿既有 EndAbility→ClearCameraMode→Hero 的 SpecHandle 所有权检查清理，设置空模式同样只清该句柄的覆盖，不得清后来的其他能力覆盖。UI Camera 仅弱引用保存最近一次显式目标请求并转发原生 SetViewTarget/转场，没有持续优先级锁、空轮询或全局 HUD 调试订阅。相机不新增 RPC/复制状态；权威、自主/模拟目标的视图均读取本机当前 Pawn/Controller/几何，纠偏、远端引用晚到及实际构图仍需运行验证。
- 零步长不推进弹簧/振荡或求导，但仍应用轴旁路及 FOV/偏移上限；零时长包络阶段可立即完成。超过32/120秒时旋转清历史并直达目标；Shake 按长正帧推进包络/相位，不再用该阈值停止；无效输入/配置分别回退原朝向或零 Shake。位置 Lag 开启子步时最多32步，超出预算直达目标并清速度；零步长保留弹簧状态但刷新目标采样并应用限距。
- `showdebug camera` 在 CameraComponent 下查看 Shake 实例数、类型/分类、包络、幅度/频率倍率及单次/持续/释放状态。
- GTA 路径相对 `camera/`：`gameplay/follow/FollowPedCamera.cpp::UpdateRunningShake` 对照运动驱动；`helpers/{FrameShaker,Oscillator,Envelope}` 对照数值层；`base/BaseCamera.cpp` 对照 PostEffectFrame；`gameplay/GameplayDirector.cpp::RegisterExplosion` 复用同类 Shaker。`gameplay/aim/FirstPersonShooterCamera.cpp::UpdateFrameShakers` 的 FPS 头骨跟随尚未迁入。
- 旋转参考 `helpers/LookAtDampingHelper.cpp::Update`：角速度/角度两级弹簧与当前结构相近，已确认有 HintHelper 及 Cinematic Mounted/Tracking 调用，不能视为普通第三人称镜头统一采用的算法。`gameplay/follow/FollowCamera.cpp::ComputeOrbitOrientation` 的跟随及 PullAround 另有速度、输入与配置分支；GTA 镜头表现须沿实际消费者追踪，不能仅凭 `DampedSpring::UpdateAngular` 判定整条旋转链。

### Montage 稀疏曲线与 Slot 求值

- 社区证据：[ALS-Refactored Discussion #681](https://github.com/Sixze/ALS-Refactored/discussions/681)。发帖者 jomath229 在 2026-08-02 提出 `MONTAGE_WEIGHT` 元数据曲线配 `LerpToValid`，08-06 明确确认该版在 Blend In/Out 时不正确，改用自定义 AnimNode 调用自定义 AnimInstanceProxy 中重写的 Slot 曲线求值；11-10 又展示以 control curves 按组交还 Overlay 控制权。帖子未公开最终完整代码，也未声明修改 Engine；这是使用者实现报告，不是维护者已经合入的功能。
- 社区实现的 Graph 接法边界：08-02 原型代码有 SourcePose/CurvesPose 两个输入，读取已求值曲线中的元数据权重；08-06 最终版仅说明自定义节点调用 Proxy 曲线求值，未公开足以核对其引脚、是否替代原生 Slot 或放在 Slot 后的最终实现。本工程的“原生 Slot 后增加双输入曲线节点、按 SlotName 读取快照”是项目设计，不宣称与作者最终接线完全一致；其最终是否纳入轨道 Sequence 曲线也未由公开代码确认。
- ALS 主分支本次核对提交 `b754d6f0f2bb03741d301f8fb88077ebfe561e17`：`Source/ALS/Private/Nodes/AlsAnimNode_CurvesBlend.cpp` 仍只提供 Accumulate/Lerp/Combine 等通用合并，未实现按 Montage 快照求值的稀疏覆盖；`AlsAnimationInstanceProxy.cpp` 仅转发 PostUpdate。不可将帖子原型或现有 Combine/Override 模式视为完整的加权稀疏 Montage 功能。
- 本地 UE 5.8 链路：`Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_Slot.cpp::Evaluate_AnyThread` → `Runtime/Engine/Private/Animation/AnimInstanceProxy.cpp::SlotEvaluatePose` → `AnimationRuntime.cpp::BlendCurves`。原生 Slot 从轨道提取姿势/曲线，再用 Montage 自身 `EvaluateCurveData` 的曲线 Combine 覆盖同名项，最后将曲线整体按姿势权重混合；源权重是 Slot 级的，不是每条曲线的存在性权重。仅基础侧存在的曲线因而仍会被乘源权重。Slot 满权重时不求值 Source；`bAlwaysUpdateSourcePose` 只保证 Update，不能使其在该路径 Evaluate。
- 稀疏覆盖合同：Montage 未声明的曲线保留当帧基础值；声明且值为0仍有效；声明的曲线按实际混合权重插值。`Runtime/Engine/Public/Animation/AnimCurveTypes.h::LerpToValid` 对双侧存在项 Lerp，对单侧存在项直接保留该侧值，所以基础侧缺失、Montage 侧存在时不会自动按 Alpha 从0淡入；若采用缺省0的合同，须显式补基线或逐项按存在性与权重计算。已混过的 Slot 输出不能作为未加权的 Montage 曲线再乘一次权重。
- UE 5.8 项目层可行性已静态核对：`Animation/AnimInstanceProxy.h::GetMontageEvaluationData()` 为 `protected`、`ENGINE_API` 的导出接口，派生 Proxy 可调用；底层数组是 private，不直接访问。`Animation/AnimMontageEvaluationState.h` 提供 Montage 弱引用、位置、DeltaTimeRecord 和 `BlendInfo.GetBlendedValue()`；`UAnimSequenceBase::EvaluateCurveData`、`UAnimMontage::IsValidSlot/GetAnimationData` 均有导出接口。`UAnimInstance::CreateAnimInstanceProxy/DestroyAnimInstanceProxy` 可覆盖，无需修改 Engine。原生 `SlotEvaluatePose` 非 virtual，不能只在派生 Proxy 中写同名函数就替换原生节点调用。
- 项目实现：ImagineGame 的 `Animation/IMGAnimInstanceProxy.h/.cpp` 提供无跨帧缓存的 `FIMGAnimInstanceProxy`，`IMGAnimNode_SparseMontageCurves.h/.cpp` 提供运行时节点；ImagineEditor 的 `Animation/IMGAnimGraphNode_SparseMontageCurves.h/.cpp` 提供 `Animation|Montage` 菜单中的 `Sparse Montage Curves` 节点。编辑器公开依赖 AnimGraph/BlueprintGraph，原生 Slot 类型的实现依赖为私有 AnimGraphRuntime；运行时模块不新增编辑器依赖。
- `UIMGAnimInstance::CreateAnimInstanceProxy` 创建 FIMGAnimInstanceProxy，由引擎原有 DestroyAnimInstanceProxy 经虚析构删除；后续 C++ 派生类若覆盖工厂须返回此类型或其派生类型。节点每次求值检查所属实例类型，经 UIMGAnimInstance 的非蓝图 C++ 求值接口访问 Proxy；不支持的父类在蓝图编译时报告错误，运行时回退基础曲线并报告求值警告。节点及 Proxy 不持有 Montage/快照/曲线容器的跨帧引用，不新增计时器、委托、复制或清理状态；原生 Uninitialize 会清空快照。基础与输出曲线容器须不同，接口仅用于 AnimGraph 求值。
- 具体曲线合同：节点保留 `Pose` 的全部骨骼及自定义属性，将整份输出曲线重建为 `BaseCurves` 加稀疏覆盖；“声明”仅认可 Montage 自身 Float Curve，不从轨道 AnimSequence/Composite 导入覆盖曲线。Proxy 从空 FBlendedCurve 配当前 RequiredBones 的曲线过滤器调用 Montage 批量 EvaluateCurveData，按实际元素枚举，缺失基础值默认为0，显式0仍参与加权，标志合并保留。无快照、失效引用、无匹配 Slot 或零权重均保留当前基础曲线；非有限位置/权重/曲线值不参与覆盖。若以后也认可轨道曲线，须另按 FAnimTrack 的 Segment 时间映射提取，不能把当前实现误认为已包含轨道曲线。
- 同槽多 Montage 逐曲线使用 `Base + Σ w_i × (Value_i − Base)`，仅纳入声明该曲线的项；参与该 Slot 的所有有效 Montage 总权重超过1时统一归一化，未声明曲线的项把其份额留给 Base。停止但仍有混出权重的实例继续求值，不以 bIsPlaying/bIsActive 排除。Additive 轨道上的 Montage 自身曲线仍按绝对覆盖处理，包括值为0的 IK/Layer 控制；BlendProfile 不改变这里采用的标量 Montage 权重，不将逐骨骼权重套到曲线。Inertialization 放在本节点之后时仍会惯性处理输出，其最终值/姿势不保证等于显式权重公式。
- Graph 合同：Overlay → Save Cached Pose；两个 Use Cached Pose 分别接原生 Slot.Source 和本节点 BaseCurves，Slot 输出接本节点 Pose，SlotName 默认 DefaultSlot 且须匹配，节点输出再接 Inertialization/IK。本节点先更新 Pose，再以零贡献/零 Root Motion 权重的 inactive 上下文更新 BaseCurves；SaveCachedPose::PostGraphUpdate 选择最高权重上下文，原生路径仍主导正常更新，Slot 满权重跳过 Source 时缓存也继续更新，并在 Evaluate 独立提取当前基础曲线。不可用两套独立动画播放器代替同一个共享缓存。
- SlotName 只填 Slot 本名，不加 GroupName 前缀；例如编辑器显示 DefaultGroup.DefaultSlot 时填 DefaultSlot。`AnimMontage.cpp::IsValidSlot` 直接比较 SlotAnimTracks[].SlotName，不解析 Group.Slot 字符串；本节点沿用此匹配。
- Linked Layer 所属 AnimBP 也须派生 UIMGAnimInstance；需要主实例 Montage 时配置 `Use Main Instance Montage Evaluation Data`，引擎 getter 按此设置返回主实例快照，不强制修改该设置。节点不注册新的 Slot，不推进 Montage、不重复提取 Root Motion 或发通知，实际原生 Slot 保留这些职责；网络沿既有 Montage 播放/复制链消费本端快照，无额外曲线复制。
- 编辑器检查父类、双输入连接、非空 SlotName；目标骨架缺 Slot 时提示 Warning，直接连接的原生 Slot 名称不匹配时报告 Error。中间缓存/其他节点隔开的复杂 Graph 无法据此保证 Slot 对应正确；同一组控制曲线应有明确的覆盖入口，后续普通 Slot/曲线节点仍可能再次混合。待运行验收包括淡入/满权重/混出/取消/连续重播、多 Montage、显式0/缺失曲线、LOD 曲线过滤、Linked Layer 主实例数据、Inertialization、联机本端 Montage 同步与打包。
- 节点可用性：编辑器包装通过 IsActionFilteredOut 按菜单中的全部 Blueprint/Graph 上下文过滤，IsCompatibleWithGraph 同时限制 AnimationGraphSchema 和拥有 UIMGAnimInstance 派生父类的 AnimBP；UEdGraphNode::CanPasteHere 默认调用该兼容检查，因此普通 AnimInstance 图不能直接粘贴该节点。共享父类条件用于菜单、粘贴和编译校验；重设父类后遗留节点仍受编译校验保护。无上下文的动作数据库枚举不作为父类有效性的保证，实际目标图须通过兼容检查。
- `BlueprintGraph/Public/BlueprintActionFilter.h` 的 FBlueprintActionFilter 实际定义为 class，前向声明须使用 class；不能因 F 前缀推断为 struct，MSVC 的 C4099 在当前构建配置下会阻止编译。
- 编译提示来源：`AnimBlueprintExtension_Base.cpp::ProcessPosePins/ProcessNonPosePins` 的 `was visible but ignored` 与 `AnimGraphNode_Base.cpp` 的 `Node is experimental` 均调用 MessageLog.Note，不直接增加 NumWarnings。前者表示某个可见输入 Pin 未生成链接/属性处理，需按具体 Pin 及所属节点判断影响；后者由节点类的 Experimental 标记生成。不能只凭这两类 Note 推断“successful, but with 1 Warning(s)”的实际警告正文，也不为消除提示而屏蔽引擎校验。

### FootPlacement ControlRig 与镜头曲线

- 资产：`Content/Characters/Heroes/Mannequin/Rigs/CR_Biped_FootPlacement.uasset`，对象路径 `/Game/Characters/Heroes/Mannequin/Rigs/CR_Biped_FootPlacement.CR_Biped_FootPlacement`。用户维护既有 Graph 对齐和注释框；编辑前读取实时图与坐标，不用旧 T3D 覆盖当前资产。`D:/CR_Biped_FootPlacement.T3D` 仅为调查时的导出快照。
- 主图为 EditorAsset 的 `RigVMModel`（编辑器显示 Rig Graph），同时包含 Construction 与 Forwards Solve。Forwards Solve 原入口为 `RigUnit_BeginExecution` → EarlyOut Branch；主 Sequence 按顺序执行 Reset/动画目标缓存、`UpdateRootDamper`、地面查询/平滑、植脚、腿/骨盆控制、腿 IK/Post Solve，再缓存世界变换和调试绘制。
- 骨盆主链：`Update Leg Controls` 左/右 → `Update Pelvis Control` → `RigUnit_SetRelativeTransformForItem_1`（Child=pelvis，Parent=pelvis_ctrl）→ `Solve Leg IK` 左/右 → `Update Foot Post Solve` 左/右。`Update Pelvis Control` 的 `Pelvis Offset` 已经腿长约束和平滑，按世界方向保存再逆旋转用于落位；它不等于包含 RootDamper 的完整骨盆高度差。
- 镜头输出接在主图，不修改上述函数内部。`CameraPelvisCaptureComment` 框在 Forwards Solve/EarlyOut 前，以 Current pelvis Global Transform → Translation → To World(Location) 保存 `CameraPelvisBeforeWS`，再清零曲线。`CameraPelvisOutputComment` 框接在右脚 `Update Foot Post Solve_1` 后，读取同次求解后的 pelvis 世界位置，以 After−Before 的 Z 写 `FootIK_PelvisDeltaZ`；包含 RootDamper 和骨盆修正，单位 UE cm、上正下负。Rig Global 不是 World；To World 的位置版本包含组件变换与缩放。
- 曲线通过 Rig Hierarchy 中的 Curve 和 `Set Curve Value` 输出，不从 Mesh Socket 读取上一完成姿势来充当求解前采样。Rig 内不额外乘 ABP 的 ControlRig Alpha；外部 Pose/曲线混合仍须核对。ABP 同名曲线应以上游 `Modify Curve`（Blend、Alpha=1、Value=0）提供零基线，避免旁路继承旧值；TPS 已提供曲线消费者，但 Rig 输出本身不证明 ABP 基线、动画 Tick/曲线过滤和当前派生相机配置均已接通。
- `FootIK_PelvisDeltaZ` 仅供相机消费，不驱动腿部 IK、骨盆或 ControlRig 权重；曲线值不能单独证明最终姿势采用了完整 Rig 输出或可见脚底已贴地。

### FootPlacement 地面查询、Contact 与 Montage 合同

- `ABP_Mannequin_Base` 主姿势顺序为 `DefaultSlot` → `Inertialization` → `OffsetRootBone` → `Procedural_Feet` → `Procedural_Hand` → `PoseHistory` → 输出。`Procedural_Feet` 的 ControlRig 节点引用本资产并传入姿势/曲线，输出再经 `BlendListByEnum(E_MovementModeForAnim)`；其输入包含 `ControlRigIK`、`BasicIK` 和 `PreProceduralPose`。Rig 执行与最终输出权重分开，不从枚举名推断具体旁路规则。
- `Procedural_Feet` 将 `AllowFootPinning` 返回值接到 `DoRaycast`，并非 `Enable Foot Pinning`；`AllowSlopeWarping` 同时驱动 `Enable Slope Warping`、`WorldZDamperEnabled`，两个查询均按 MovementMode 选择。Rig 持续执行不等于每帧执行查询：主图先查 `DoRaycast`，再查 `Enable Slope Warping`；两者为真才依次执行左右 `Do Scene Query`。DoRaycast 为假不更新原始 floor；前者为真、后者为假则将左右 collision 恢复 Initial Transform。之后独立执行左右 `Update Floor Control`。
- 动画目标链：主 Sequence.C 在 RootDamper 前用 `CalculateTargetAnimatedToeTransformGlobal` 缓存左右 `animated_toe_target_*_null` 和 `animated_pelvis_null`，D 更新 RootDamper，E 查询地面，F 平滑 floor。`UseIKBoneTargets` 默认 true：Toe 相对 Foot × 外部 IK Foot 相对 Foot × Foot Global，等效将 Toe 的局部偏移应用到 `ik_foot_l/r`；false 时直接取当前 `ball_l/r` Global。须区分动画 IK 骨骼与变形脚骨，不能只凭其中一套骨骼推断另一套的目标。
- `Do Scene Query` 先缓存 Animated Foot Target Current Global。第一段球扫从 Rig 空间 `(0,0,HeightOffset)` 横向扫至 `(Target.X,Target.Y,HeightOffset)`，检测脚前墙面；HeightOffset 当前局部默认50 cm。命中且 `Adjust Feet Targets For Collision`（默认 true）时只修正动画目标 XY，保留动画 Z。第二段从第一段命中回退位置或横扫终点向下扫 `2 × HeightOffset`，默认约 `Z=+50 → -50`，半径5 cm；检测高度未使用动画脚目标 Z，不能视为覆盖完整攀爬/落差范围。坐标为 Rig Global，经引擎查询节点转换到 World，不能把这些值当世界绝对高度。
- 第二段只在 bHit=true 时写 `floor_l_collision` / `floor_r_collision` 的位置与旋转；未命中分支没有清零、失效标记或重建 floor，保留上次值。高度悬空须区分未命中留下旧值、平滑和实际姿势权重，不能用 Contact 曲线解释所有位置问题。当前节点为 `Sphere Trace By Trace Channel`，TraceTypeQuery1；对应引擎 `Units/Collision/RigUnit_WorldCollision.cpp` 按执行次数和输入缓存，同次 Solve 可复用，跨 Solve 重新检查；不能仅因 Start/End 不变就认定射线跨帧不刷新。节点忽略 OwningActor（或缺少 Actor 时忽略 OwningComponent）。
- **Contact 控制地面法线混合，不控制地面命中位置或查询启停**：主图 `GetCurveValue_2/3` 读取 `contact_l/r` 传入各自 `Do Scene Query.Contact`；函数将传入的世界 `Ground Normal` 转到 Rig 空间，再 `Interpolate(A=GroundNormal,B=HitNormal,T=Contact)`，由 Z 轴到该结果的旋转驱动 collision。Contact=0 使用传入法线，=1 使用该脚命中法线；即使射线已命中斜坡，Contact=0 仍可能保持与坡面不同的朝向。最终还受 SlopeWarpingAlpha、坡度限制和平滑影响。
- `contact_l/r` 按左右脚实际接触窗口配置：离地趋0，接触时平滑趋1；最终效果以 Montage 混合后的求值曲线为准，不把单个源动画或旧快照中的关键帧状态当作当前运行值。
- `DisableLegIK` 与 Contact 分责：ABP ControlRig Alpha 选 Curve，暴露引脚名为 `DisableLegIK`，`AlphaScaleBiasClamp` 的 Scale=-1、Bias=1，禁用曲线为1会抑制整体 Rig 修正；Contact 仅控制对应机制，不能代替整体权重。
- `Update Floor Control` 消费原始 collision：先按 `GroundMovementWorldDelta` 更新世界历史，平滑 `SlopeWarpingAlpha` 向启用状态的0/1，再在世界空间平滑位置/旋转、转回 Rig 空间并混合到零平移/单位旋转，写 `smoothed_floor_*_ctrl` 并保存左右世界历史。`Floor Smoothing Time` 当前默认0.05秒；不是 Montage 结束事件触发更新。`DebugDrawGroundCollision` 两个深绿色长方框画的是这些 smoothed 控制器，不能作为原始射线是否命中、是否更新的直接证据。
- Hierarchy：`floor_*_collision` 为无父级 Control；`animated_toe_target_*_null` 为无父级 Null，其 ctrl 是子级；`smoothed_floor_*_null` 挂在 root 下，smoothed ctrl 挂在对应 null 下；`ik_foot_l/r` 挂在 `ik_foot_root` 下，后者挂在 root 下。RootDamper 修改 root，动画目标缓存与 floor/腿部求解需按各自时序和空间区分；静态图和资产默认值不替代运行时 bHit、Contact、GroundNormal 或最终 Alpha。

### 动画曲线 Modifier 写入边界

- 工程自定义的五个 Animation Modifier（`AddAnimationCurves`、`RemoveAnimationCurves`、`RemoveAnimationNotifiers`、`FootContactCurves`、`Footstep`）统一为一次性，均继承 `UIMGOneShotAnimationModifier`。正常 Apply 后移除源实例及应用记录，保留生成/删除结果；重新生成须重新添加。FootContact/Footstep 的参数或写入失败会提前返回，不安排自动移除。基类 Revert 不修改动画数据，不能用它恢复一次性写入前的内容。
- 已核对源码：`ImagineEditor/AnimationModifiers/IMGAnimationModifier_FootContactCurves` 的 Apply 仅经 `Footstep::ReplaceFloatCurve` 重建配置的左右 Float Curve，默认 `contact_l/r`。该类不直接写 `DisableLegIK`，但名称可编辑，实际写入边界以实例配置为准。`IMGAnimationModifier_FootstepUtils.cpp` 的删除最终按名称/类型调用 Controller，不清空其他曲线。
- UE 5.8 的批量 `Add Animation Modifier(s)` 窗口并非仅执行新添加项：`Editor/AnimationModifiers/Private/SAnimationModifierContentBrowserWindow.cpp::OnApply` 将新实例加入后，遍历动画已有的全部 Modifier 并 Apply。单个 Modifier 重复 Apply 还会先执行其上次应用实例的 Revert，见 `AnimationModifier.cpp::ApplyToAnimationSequence`。只重算 Contact 时，已存在实例应从动画的 Animation Modifiers 页选中该项单独 Apply，避免批量添加窗口或 Apply All 重跑其他项。
- `UIMGAnimationModifier_AddAnimationCurves` 的 `bOverrideExistingCurves` 默认 true，匹配名称会删除整条旧 Float Curve，再写时间0的默认值；false 时保留已有同名曲线。旧 `IMGAnimationModifier_ApplyAnimationCurves` 由 `Config/DefaultEngine.ini` 的 ClassRedirect 映射到该类；遗留实例是否仍挂在资产上须实际回读，不能只凭资产字符串判定。
- 一次性清理在 Core Ticker 延后执行，等待 `FApplyModifiersScope` 与编辑器当前事务结束，避免修改引擎正在遍历的 Modifier 数组或在模态 Apply 对话框中提前清理。每个回调保存实际源实例的强引用、动画与本次应用副本的弱引用，不用模板复制时可能重复的 GUID 合并跨资产清理；回滚销毁/后续 Apply 替换应用副本或动画失效时跳过。清理范围内建立 `FApplyModifiersScope`，避免清应用记录时 PostEditChange 再触发其他 Modifier；回调完成后释放引用。`InstanceId` 只在目标动画/骨架的实例列表中匹配引擎应用副本与源实例。
- 排查线索与待验证项：若批量添加 Contact 后其他曲线被重置，先核对动画已有 Modifier、Add 曲线项的 Override/DefaultValue 及 Contact 的输出名称。资产二进制出现旧 Add/Apply 类名只证明存在序列化名称线索，不证明当前活动实例或曲线原值；具体受影响资产须回读实际 Modifier 配置和原始关键帧，不能据此自动恢复为猜测值。

### FootPlacement 骨盆目标、约束与重置

- `Update Pelvis Control` 本身不查询 Montage、Contact、移动模式、SlopeWarpingAlpha 或独立 Pelvis Enable/Alpha；它在左右 `Update Leg Controls` 后直接执行。整体 IK 有效仍不证明上游 `DoRaycast` / `Enable Slope Warping` 有效；Contact 经地面法线和可选植脚间接影响腿/骨盆，不是骨盆专用启用曲线，缺少 Contact 不能单独解释全部骨盆高度问题。
- 腿目标链：`Update Leg Controls` 从 `smoothed_floor_*_ctrl` 取平面点与 Z 轴法线，先混合动画 Toe/Pin 目标，再经 `AdjustPositionToPlane` 写 `toe_*_ctrl`，随后由 Toe 到 Foot 的动画关系、AllowHeelLift 和坡度混合求 `foot_*_ctrl`。`AdjustPositionToPlane` 沿 `(0,0,-1)` 与调整后的地面平面求交，再加回 `Animated Height Offset` 的 Z；它消费 `animated_slope_angle_ctrl`（来源为 ik_foot_root 旋转）扣除动画已表达的坡度。骨盆消费这些修正后的 Toe/Foot 控制器，不直接读取原始 bHit。
- 左右骨盆候选分别为 `AnimatedPelvisPosition - AnimatedToeTargetPosition + ToeControlPosition`；XY 取两候选平均，**Z 取两候选 Maximum**，不是平均/Minimum，也不按 Contact 筛选支撑脚。左右一侧要求下降、另一侧保持原高时，初始 Z 保留较高一侧；是否再下降由后续腿长约束决定，不能据此直接判定骨盆函数没执行，也不能未核对支撑策略就将 Maximum 改 Minimum。
- `Clamp Pelvis Offset` 先处理左腿，再处理右腿，保持当前 Pelvis 到 UpperLeg 的相对偏移，把候选 UpperLeg 到修正后 Foot Control 的向量限制在最大长度内。最大长度取 `Max(预计算 LegLength × Hyper Extension Limit Factor, 当前动画 Foot 相对 UpperLeg 的距离)`；因当前动画已伸直而保留较大长度属现有保护策略。Hyper Extension Limit Factor 默认0.98，不能认定始终以0.98倍腿长强制放松。动画腿部弯曲/伸展变化会改变约束结果，不要求 Montage 结束事件。
- 两腿约束后减去当前 pelvis Global Position，按组件世界旋转转换成世界方向偏移，`Damp(Vector)` 使用 `Pelvis Offset` 历史和 `Pelvis Smoothing Time`（默认0.3秒）平滑；再逆旋转回 Rig 空间，加到当前 pelvis 位置写 `pelvis_null`，旋转沿用当前 pelvis。主图将 pelvis 以单位相对变换贴到 `pelvis_ctrl`（Weight=1、传播子级），随后解腿 IK。该平滑每次有效求解执行，不是等满0.3秒或等 Montage 结束才开始。
- `Has Teleported || PendingReset || ForceReset` 在当次目标缓存之前执行 `Reset`，清 `Pelvis Offset`、植脚状态/权重、RootDamper 速度，重建 floor/世界历史并清 PendingReset/ForceReset；该帧后续仍走正常求解。反复外部置位可能反复清掉平滑历史，须与纯粹目标不变区分。EarlyOut 要求 FootPinning、SlopeWarping、WorldZDamper 都关闭且对应 Alpha/PinWeight 接近0，并且不用外部 IK 目标或两脚 IK/动画位置已一致；EarlyOut 设置 PendingReset，恢复求解时重置，不是 Montage 状态门控。
- 后续 `Solve Leg IK` 求解 thigh/calf/foot，`Update Foot Post Solve` 修正脚部，不再写 pelvis；Post Solve 的最小 Toe 高度来自动画相关高度，不是无条件把可见鞋底夹到地面。
- RootDamper 独立于 `Pelvis Offset`：它在骨盆目标缓存后修改 root，消费 WorldZDamperEnabled、世界位移、GroundNormal 与自身历史；RootDamperSmoothingTime 默认0.15秒。动画输入姿势、原始 collision、smoothed floor、腿部目标、骨盆约束和平滑、最终输出混合分别核对，不能将其中一项代替完整姿势结果。

### UE 5.8 ControlRig 编辑工具与接口限制

- 本地 MCP 服务：`ModelContextProtocol.StartServer 8000`，端点 `http://127.0.0.1:8000/mcp`；`ModelContextProtocol.RefreshTools` 刷新目录。`EditorToolset` 提供资产/属性工具，`AnimationAssistantToolset` 提供 CR 工具。先 `list_toolsets`/`describe_toolset` 核对实际加载；`call_tool` 的 tool_name 用短名，toolset_name 单独传。
- 当前资产是 `ControlRigRuntimeAsset`；内置 `animation_toolset.toolsets.controlrig.ControlRigTools` 的参数仍要求旧 `ControlRigBlueprint`，会拒绝该资产。不要强转或改资产类型。UE Python 正确入口：`rig=unreal.load_asset(path)` → `ed=rig.get_editor_asset()`（ControlRigEditorAsset）→ `g=ed.get_default_model()` → `c=ed.get_controller(g)`；HierarchyController 用 `ed.get_hierarchy_controller()`。
- **结构类型变量的 AddMemberVariable 必须传完整类型路径**：`ed.add_member_variable(name, '/Script/CoreUObject.Vector', False, False, '(X=0,Y=0,Z=0)')`。此接口将字符串按 CPPTypePath 解析，裸 `FVector` 会使 CPPTypeObject 为空并触发 RigVMExternalVariable 断言，不能靠 Python try/except 恢复。反之，`c.add_variable_node` 分别接收 `FVector` 和 `unreal.load_object(None, '/Script/CoreUObject.Vector')`；两种签名不能混用。修改前核对本地源码、方法 docstring 和 `ed.generate_python_commands()`。
- 实际引脚名以 `node.get_pins()` 为准：当前 `RigUnit_BeginExecution`、`RigUnit_SetCurveValue` 等 RigUnit 使用 `ExecutePin`；Variable Setter 和函数引用使用 `ExecuteContext`。连线用 `c.add_link(source_path,target_path)`，对返回值做检查；浮点类型、Getter/Setter 和子引脚也先回读，不能从节点标题推测接口。
- 编辑前检查 dirty 状态，并备份磁盘资产、实时 `c.export_nodes_to_text(...)`、节点坐标/连线和 `ed.generate_python_commands()`。分步创建变量/Hierarchy Curve、创建节点、设置默认值、接线、`ed.recompile_vm()`；使用事务/UndoBracket，批量操作期间暂停自动 VM 重编译并在 finally 恢复。回读检查只移除预期旧连线、保留既有节点/用户修改；仅保存目标资产。
- 需要通用 Python API 而 MCP 工具签名不兼容时，可使用引擎自带 `PythonScriptPlugin/Content/Python/remote_execution.py` 的本机入口；先核对绑定为 `127.0.0.1`、匹配 `project_root/project_name`，记录并恢复 Remote Execution 原开关。该入口用于编辑资产，不修改 C++ 或工具插件源码。独立验证可用 `ed.create_rig_vm_host()`，执行 Construction/Forwards Solve，避免直接改运行角色或资产默认配置。
- 引擎目录以「工程方向与参考源码」为准。引擎 Python 为 `Engine/Binaries/ThirdParty/Python3/Win64/python.exe`，系统 `python.exe` 可能只是不可用的 WindowsApps 别名。本机接口发现不到节点时区分沙箱网络可见性与编辑器设置，不能据此反复改配置；Remote Execution 属于安全相关设置，调查资产本身不自动授权开启，须遵守审批并在授权使用后恢复原值。
- 只读 Rig 图可用 `ed.get_all_models()` 覆盖主图、函数库及聚合子图；节点取 `get_all_pins_recursively()`，引脚取 `get_pin_path/get_cpp_type/get_default_value/get_linked_source_pins/get_linked_target_pins`，坐标用 `node.get_position()`，默认配置用 `ed.get_asset_variables()`。调查快照位于 `Saved/Codex/FootPlacement/live_rig.json`，仅作导航/证据；修改前仍按现有要求回读实时目标图，不用快照覆盖资产。
- 已有角色 Rig 的只读查询：`unreal.ControlRig.find_control_rigs(actor, unreal.ControlRig)`，再用 `rig.get_hierarchy()`、`get_variable_as_string(name)`；Outer=None 会返回空，不表示没有实例。`RigElementKey` 构造使用 `type=unreal.RigElementType.CONTROL, name='...'` 等关键字，不能按 name/type 的位置顺序传参。动画只读接口为 `unreal.AnimationLibrary.get_float_keys(seq,name)` 和 `get_bone_pose_for_time(seq,bone,time,extract_root_motion)`，当前不是 `AnimationBlueprintLibrary`；前者返回两组 UE Array，须转 list 才能看到值。没有运行实例时不将资产默认值当作运行时数据，不为调查自行启动 PIE 或构造验证运行。
- 需要明确骨骼采样空间时，可用 `unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,time,unreal.AnimPoseEvaluationOptions(...))` 取得 AnimPose，再 `get_bone_pose(pose,bone,space=...)`；后者默认 `AnimPoseSpaces.LOCAL`，须区分局部、组件与去 Root Motion 后的高度，不能直接把局部 Z 当脚底离地高度。`Saved/Codex/FootPlacement/live_pelvis.json` 与 live_rig.json 同样只作导航，不覆盖实时资产。

## UI 与设置

- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged、生命/死亡委托；装弹提供开始/结束事件，自动重生提供可配置消息 Tag。
- GameSettings.UGameSettingDetailView 通过 HandleCurrentSettingChanged 订阅当前 Setting 的变化并刷新描述/动态详情；DLC Registry 定时变更沿同一路径，不另轮询。
- 切换/清空 Setting、NativeDestruct 时退订；NativeConstruct 保留当前选中项并重建详情/扩展。切换、清空、关闭、释放 Slate 均取消并清空扩展异步句柄；完成/装配回调复核当前 Setting，避免旧请求或重入回填过期控件。
