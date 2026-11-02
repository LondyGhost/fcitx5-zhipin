-- abc_hide.lua — 候选闸门
--
-- abc_reveal 关闭时丢弃全部候选：菜单为空，前端就不弹候选窗，
-- 但 preedit（Context::input）不受影响，字母照常显示。
-- 打开时原样放行，候选顺序完全交给 rime-ice 原本的翻译器与滤镜。
local function filter(input, env)
  local ctx = env.engine.context
  if not ctx:get_option("abc_reveal") then
    return  -- 一个都不 yield
  end
  for cand in input:iter() do
    yield(cand)
  end
end

return filter
