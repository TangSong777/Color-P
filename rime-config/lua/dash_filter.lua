-- 把占位符显示成真实符号：
--   DASH   -> "-"（编码 vvv）
--   USCORE -> "_"（编码 vvu）
--
-- 背景：Rime 的词条文件是 YAML，一行以 "-" 开头会被当成列表项，
-- 因此无法直接登记「编码为 -、文本为 -」的词条；"_" 虽然可以写在行首，
-- 但同样需要一个能被输入法识别的编码，于是统一采用「占位词条 + 显示替换」。
--
-- 只在候选文本里替换，不影响输入串与用户词库。

local REPLACEMENTS = {
  { "DASH", "-" },
  { "USCORE", "_" },
}

local function filter(input, env)
  for cand in input:iter() do
    local text = cand.text
    local hit = false
    for _, pair in ipairs(REPLACEMENTS) do
      if text:find(pair[1], 1, true) then
        text = (text:gsub(pair[1], pair[2]))
        hit = true
      end
    end
    if hit then
      yield(ShadowCandidate(cand, cand.type, text, cand.comment))
    else
      yield(cand)
    end
  end
end

return filter
