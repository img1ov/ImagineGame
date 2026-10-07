# ImagineGame 工程速查

## 工作约定
- 先核对本地源码、官方资料或案例；区分事实、资产名线索与待验证项。改代码前找到调用链、同类实现和所属模块，在原职责处扩展，避免重复状态与抽象。
- 新增 struct：指定目标 `.h` 就写入该文件；未指定时先调查位置，再请用户确定头文件。
- 仅在关键约束和生命周期边界写短注释；回复只给结论、证据、变更与必要风险。
- 未明确要求不编译；C++ 变更核对源码、引擎接口和构建依赖，完成后做静态链路与差异检查。不调查或修改 Content，除非用户要求。
- 先扫本文标题，只读相关小节；仅补长期有用的已核实事实，结构变化时清理过时内容。

## 方向与模块
- UE 5.8；Gameplay 核心机制用 C++，蓝图用于配置、装配、转发及明确保留的移动计算；暂不接脚本。项目专有类型用 `IMG`，通用类型遵循同目录命名；新增名称不得沿用参考项目名称或前缀。
- `Source/ImagineGame` 是共享运行时，`Source/ImagineEditor` 是编辑器模块；`Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime` 放射击玩法派生规则，依赖 ImagineGame。
- 新规则先核对所有者、权限、复制与生命周期；共享机制扩展现有核心职责，玩法规则放对应 Game Feature。

## 生命周期与 AI
- `AIMGGameMode` 选择 Experience，`UIMGExperienceManagerComponent` 加载；`UIMGAbilitySet` 授予能力、Effect 和 AttributeSet。`AIMGPlayerState` 持有 ASC/属性，`UIMGPawnExtensionComponent` 绑定 Pawn Avatar；`UIMGHeroComponent` 在 DataInitialized 阶段初始化 ASC 和本地输入。
- `UIMGBotCreationComponent` 在 Experience 加载后由服务端生成 Bot；重生分派只支持玩家控制器和 `AIMGPlayerBotController` 派生类。当前无 StateTree 运行时代码；依赖待实现时加到所属模块。
- `UIMGHealthComponent` 发送死亡事件，`UIMGGameplayAbility_Death` 控制开始/结束，`AIMGCharacter` 在结束后销毁；必须授予死亡能力。射击玩法的自动重生能力位于 `ShooterCoreRuntime/GameModes`。

## 角色与移动
- `UIMGHeroComponent` 处理 Move 输入、方向平滑和急转；`UIMGCharacterMovementComponent` 负责姿态、速度限制与预测。自动奔跑复用移动入口。

### Locomotion 的职责与接口
- `UIMGCharacterLocomotionComponent` 是本机 ASC/AttributeSet 与 CMC 之间的中间件，不承担复制、RPC 或移动预测；移动同步仍由角色/CMC 链路负责。
- C++ 负责引用、生命周期、属性订阅和 CMC 前置 Tick；组件蓝图负责监听 GA 意图 Tag、读取属性、计算并写入 CMC。不要把 Tag 规则搬进 C++，也不要重复维护意图或速度数据。
- `UIMGLocomotionSet` 保存七个可复制移动属性。属性初始化和 Buff 由用户通过 GE 配置；组件只读取 `CurrentValue`，不代管 GE 或回写属性。
- 组件在 GameplayReady 后绑定 ASC/Set，缓存 PawnExtension，接入 ASC 初始化/解除通知；每帧检查 Set 晚到或替换。退出/注销时清理订阅和 Tick 依赖，属性委托按句柄解绑。
- 蓝图入口：`OnLocomotionReady`、`OnLocomotionAttributeChanged`、`OnLocomotionUnavailable`、`UpdateLocomotionPreCMC`；通过 `GetLocomotionSet`、`GetAbilitySystemComponent`、`GetCharacterMovementComponent` 取引用，通过 `IsLocomotionReady` 查询绑定是否有效。
- Ready 表示 ASC/Set 已绑定；Unavailable 回调时 ASC/Set 引用已清空，CMC 仍可用于清理。最终移动参数在 PreCMC 回调计算、写入；音频 Foley/OnLanded 和 ABP 接口转发不属于此组件职责。

## 装备、背包与射击
- 装备、背包、快捷栏、远程追踪和武器生成器基类保持原职责；快捷栏与物品实例仅补跨模块导出。装备授予能力后，快捷栏再为装备实例关联物品。
- 通用开火、换弹、自动换弹能力位于 `ImagineGame/Weapons`；具体武器规则、快捷栏输入、丢弃和拾取派生逻辑位于 `ShooterCoreRuntime`。开火复用远程追踪，派生类处理 TargetData、服务端校验与弹药成本。
- `DropWeapon` 当前只移除快捷栏/背包物品，未生成地面拾取物；初始物品发放和死亡清理留给蓝图装配。校验阈值、自动装弹轮询及实机链路尚未按参考资产逐线验证。

## UI
- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged 消息和生命/死亡委托；装弹有开始/结束表现事件，自动重生有可配置消息 Tag。
