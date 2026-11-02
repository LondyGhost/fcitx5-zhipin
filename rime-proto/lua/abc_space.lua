-- abc_space.lua — 智能ABC 门控处理器
--
-- 目标：打字时字母留在 Rime 的 context 里（preedit 正常显示），但不弹候选窗；
--       按一次空格把候选「放出来」，再按空格由 editor 的 space: confirm 上屏第一个。
--
-- 原理：用自定义开关 abc_reveal 作为「候选是否可见」的闸门。
--   本处理器只负责翻转开关；真正把候选藏起来的是 abc_hide.lua 这个 filter。
--   切换开关会触发 librime 重新翻译，因此候选窗会立刻出现/消失。
local function processor(key, env)
  local engine = env.engine
  local ctx = engine.context
  local repr = key:repr()

  -- 不在输入状态：确保闸门是关闭的，避免下次输入时直接看到候选
  if ctx.input == "" then
    if ctx:get_option("abc_reveal") then
      ctx:set_option("abc_reveal", false)
    end
    return 2  -- kNoop
  end

  if repr == "space" then
    if ctx:get_option("abc_reveal") then
      return 2  -- 已放出候选：交给 editor 的 space: confirm 上屏第一个
    end
    ctx:set_option("abc_reveal", true)  -- 放出候选（触发重译）
    return 1  -- kAccepted：这次空格只用于「出词」，不上屏
  end

  -- 字母 / 退格：回到「隐藏候选」状态。
  -- 注意只认字母：数字键是选词键，不能被这里清掉闸门。
  if repr == "BackSpace" or repr:match("^%a$") then
    if ctx:get_option("abc_reveal") then
      ctx:set_option("abc_reveal", false)
    end
    return 2
  end

  return 2
end

return processor
