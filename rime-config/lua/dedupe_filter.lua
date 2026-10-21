-- 候选去重：文本与注释都相同的候选只保留第一条。
--
-- 背景：英文白名单词同时存在于 custom_phrase（负责精确命中与前缀补全）
-- 和 mixed_tokens（负责参与造句），两条都会被翻译器产出，候选里就会出现
-- 「1. Markdown  2. Markdown」。
--
-- 只合并「文本 + 注释」完全一致的条目：
-- 同形异释（如「和 hé」与「和 hè」）注释不同，不会被误删。

local function filter(input, env)
  local seen = {}
  for cand in input:iter() do
    local key = (cand.text or "") .. "\0" .. (cand.comment or "")
    if not seen[key] then
      seen[key] = true
      yield(cand)
    end
  end
end

return filter
